// Bridges and elevated beams, worked out from a map's nav grid: a walkable surface with another surface (floor or water) at
// least 2.2 m straight below it is "over" something. Joined surfaces are one piece, and a piece's shape says what it is:
//   beam    a strip up to 3 cells wide, long for its width
//   bridge  up to 40 m wide and at least twice as long as it is wide, or anything that spans water
// and you must be able to walk or sail out from under it on 40%+ of its sides (a building's lower floor is walled in).
// Wide pieces are left out: those are roofs and the upper floors of buildings. It is a guess from shape, so it can miss
// things and it can name something else as a bridge. "low" = under 4.5 m clear above what is below (a car
// or tank can hit it). Nothing here touches the bots: the 3D map only draws it.
const fs = require('fs');
const L = 6, WALKABLE = 1, WATER = 2, MIN_GAP = 2.2, LOW_GAP = 4.5;   // (Phoenix: 6 surfaces per cell)
const DIRS = [[1, 0], [-1, 0], [0, 1], [0, -1], [1, 1], [1, -1], [-1, 1], [-1, -1]];

function findOverhead(file) {
    const buf = fs.readFileSync(file);
    const x0 = buf.readFloatLE(36), z0 = buf.readFloatLE(40), cell = buf.readFloatLE(44);
    const w = buf.readInt32LE(48), h = buf.readInt32LE(52), layers = buf.readInt32LE(56);
    if (layers !== L) throw new Error('unexpected grid: ' + file);
    const n = w * h * L, HO = 60;
    const height = new Float32Array(buf.buffer.slice(buf.byteOffset + HO, buf.byteOffset + HO + n * 4));
    const flags = buf.subarray(HO + n * 4, HO + n * 5), edges = buf.subarray(HO + n * 5, HO + n * 6);
    const SIDE = [[0, 1, 0], [2, 0, 1], [4, -1, 0], [6, 0, -1]];   // (edge bit, dx, dz: as in the grid)
    const used = (i) => (flags[i] & WALKABLE) && isFinite(height[i]);
    // the node over something, and how much room there is under it
    const gapOf = new Float32Array(n);
    let over = 0;
    for (let c = 0; c < w * h; c++) {
        const b = c * L;
        for (let a = 0; a < L; a++) {
            const i = b + a; if (!used(i) || (flags[i] & WATER)) continue;
            let best = Infinity;
            for (let k = 0; k < L; k++) { const j = b + k; if (k === a || !used(j)) continue; const g = height[i] - height[j]; if (g >= MIN_GAP && g < best) best = g; }
            if (best !== Infinity) { gapOf[i] = best; over++; }
        }
    }
    // joined pieces: neighbours within 1.5 m of height
    const seen = new Uint8Array(n), pieces = [];
    const stack = [];
    for (let s = 0; s < n; s++) {
        if (!gapOf[s] || seen[s]) continue;
        const nodes = []; stack.length = 0; stack.push(s); seen[s] = 1;
        while (stack.length) {
            const i = stack.pop(); nodes.push(i);
            const c = Math.floor(i / L), cz = Math.floor(c / w), cx = c - cz * w;
            for (const [dx, dz] of DIRS) {
                const nx = cx + dx, nz = cz + dz; if (nx < 0 || nz < 0 || nx >= w || nz >= h) continue;
                const b = (nz * w + nx) * L;
                for (let k = 0; k < L; k++) { const j = b + k; if (gapOf[j] && !seen[j] && Math.abs(height[j] - height[i]) < 1.5) { seen[j] = 1; stack.push(j); } }
            }
        }
        if (nodes.length >= 4) pieces.push(nodes);
    }
    // shape: peel the outline off until nothing is left; the number of rounds is half the width
    const out = [];
    for (const nodes of pieces) {
        const cells = new Set(nodes.map(i => Math.floor(i / L)));
        let minX = Infinity, maxX = -Infinity, minZ = Infinity, maxZ = -Infinity;
        for (const c of cells) { const z = Math.floor(c / w), x = c - z * w; if (x < minX) minX = x; if (x > maxX) maxX = x; if (z < minZ) minZ = z; if (z > maxZ) maxZ = z; }
        let cur = cells, peel = 0;
        while (cur.size && peel < 200) {
            const next = new Set();
            for (const c of cur) { const z = Math.floor(c / w), x = c - z * w; if (x > 0 && x < w - 1 && cur.has(c - 1) && cur.has(c + 1) && cur.has(c - w) && cur.has(c + w)) next.add(c); }
            cur = next; peel++;
        }
        const widthCells = Math.max(1, peel * 2 - 1), width = widthCells * cell;
        const length = Math.hypot(maxX - minX + 1, maxZ - minZ + 1) * cell;
        let overWater = 0, low = 0, ySum = 0;
        for (const i of nodes) {
            const b = Math.floor(i / L) * L; ySum += height[i];
            for (let k = 0; k < L; k++) { const j = b + k; if (j !== i && used(j) && (flags[j] & WATER) && height[j] < height[i]) { overWater++; break; } }
            if (gapOf[i] < LOW_GAP) low++;
        }
        const waterShare = overWater / nodes.length;
        // the way out from underneath: for each outer side of the piece, is the floor under it linked on past the side?
        let sides = 0, openSides = 0;
        for (const i of nodes) {
            const c = Math.floor(i / L), z = Math.floor(c / w), x = c - z * w; let j = -1, jh = -Infinity;
            for (let k = 0; k < L; k++) { const q = c * L + k; if (q !== i && used(q) && height[q] <= height[i] - MIN_GAP && height[q] > jh) { jh = height[q]; j = q; } }
            if (j < 0) continue;
            for (const [bit, dx, dz] of SIDE) {
                const nx = x + dx, nz = z + dz; if (nx < 0 || nz < 0 || nx >= w || nz >= h) continue;
                if (cells.has(nz * w + nx)) continue;
                sides++; if ((flags[j] & WATER) || (edges[j] & (1 << bit))) openSides++;
            }
        }
        const openShare = sides ? openSides / sides : 0;
        let kind = null;
        if (widthCells <= 3 && length >= Math.max(6, width * 2)) kind = 'beam';
        else if (width <= 40 && length >= Math.max(12, width * 2)) kind = 'bridge';
        else if (waterShare > 0.5 && width <= 60 && length >= 15) kind = 'bridge';
        if (!kind || openShare < 0.4) continue;
        const pts = [];
        for (const i of nodes) { const c = Math.floor(i / L), z = Math.floor(c / w), x = c - z * w; pts.push(+(x0 + x * cell).toFixed(1), +height[i].toFixed(1), +(z0 + z * cell).toFixed(1), gapOf[i] < LOW_GAP ? 1 : 0); }
        out.push({ nodes, openShare: +openShare.toFixed(2), kind, width: +width.toFixed(1), length: +length.toFixed(0), y: +(ySum / nodes.length).toFixed(1), lowShare: +(low / nodes.length).toFixed(2), waterShare: +waterShare.toFixed(2), pts });
    }
    out.sort((a, b) => b.pts.length - a.pts.length);
    return { cell, nodesOver: over, pieces: pieces.length, items: out.slice(0, 400), n };
}
module.exports = { findOverhead };
if (require.main === module) {
    const r = findOverhead(process.argv[2]);
    const c = { beam: 0, bridge: 0 }; r.items.forEach(i => c[i.kind]++);
    console.log(`cell ${r.cell} m, ${r.nodesOver} nodes over something, ${r.pieces} pieces, kept ${r.items.length}:`, c);
    for (const i of r.items.slice(0, 14)) console.log(i.kind.padEnd(7), 'w', i.width, 'len', i.length, 'y', i.y, 'nodes', i.pts.length / 4, 'low', i.lowShare, 'water', i.waterShare, 'open', i.openShare, 'at', i.pts[0], i.pts[2]);
}
