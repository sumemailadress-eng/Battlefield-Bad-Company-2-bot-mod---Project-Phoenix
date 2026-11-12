// Project Phoenix 3D map (the ThugBot / FloodBots 3D map, adapted to Phoenix's BC2 files): a local web page that shows a
// map's raycast scan - the "lidar" survey the bots make of every walkable surface on the BC2 server (up to 6 floors per
// cell, walls and ledges worked out from the blocked links between cells) - as a 3D point cloud, with the routes the
// bots follow drawn on top. Routes can be drawn, moved and deleted here and saved into the files Phoenix reads; Phoenix
// re-reads them within 5 s, even mid-round. Separate tool: it never touches the bot DLL or the running server.
//   node server.js [navcacheFolder] [port]      then open http://localhost:8644/
// The navcache folder (1.6.9+: shared by every Phoenix install) is, unless given: NavcacheDir= in phoenix.ini, else
// <BC2 server folder>\PhoenixNavcache (the server folder from Phoenix-Launcher.ini / launcher.ini), else <Phoenix>\navcache.
// Files (in the navcache folder):
//   <map>.nav          the scan (read only)
//   <map>.froutes      "route <from> <to> <len> <n> x,y,z ..." foot routes: made by the bots between spawns and objectives,
//                      and the ones you draw (Foot route). Bots join the route that ends at their objective and walk it.
//   <map>.vroutes      "route ax ay az bx by bz n x y z ..." vehicle routes (Vehicle route). Drivers drive one to its end.
//   <map>.routes       vehicle lines drawn in LearnViewer (1.6.x) - shown with the vehicle routes, moved into .vroutes on save
//   <map>.objectives   flags / M-COMs / spawns, written by the bots (read only)
//   <map>.vehicles     where vehicles stand at round start, written by the bots (read only)
//   <map>.roads        road lines read from the level data (read only)
//   live.txt           the map being played
// Every save first copies the old files to navcache\backup_3d\.
const http = require('http'), fs = require('fs'), path = require('path');
const { findOverhead } = require('./overhead.js');   // bridges and beams

const VERSION = '1.7.3';   // (the launcher restarts an older 3D map server it finds running)
const SERVER = path.join(__dirname, '..', '..');   // (the Phoenix folder: tools\Map3D is inside it)
const PORT = +(process.argv[3] || 8644);
function iniValue(file, key) {
    try { const m = fs.readFileSync(file, 'latin1').match(new RegExp('^\\s*' + key + '\\s*=\\s*([^;\\r\\n]*)', 'mi')); return m ? m[1].trim() : ''; } catch (e) { return ''; }
}
function findNav() {
    if (process.argv[2]) return path.resolve(process.argv[2]);
    const own = iniValue(path.join(SERVER, 'phoenix.ini'), 'NavcacheDir'); if (own) return own;
    let dir = iniValue(path.join(SERVER, 'Phoenix-Launcher.ini'), 'ServerDir');
    if (!dir) { const sp = iniValue(path.join(SERVER, 'launcher.ini'), 'ServerPath'); if (sp) dir = path.win32.dirname(sp); }
    if (dir && fs.existsSync(path.join(dir, 'PhoenixNavcache'))) return path.join(dir, 'PhoenixNavcache');
    return path.join(SERVER, 'navcache');
}
const NAV = findNav(), LEARN = NAV, ROUTES = NAV, REPORT = NAV;
const BACKUP = path.join(NAV, 'backup_3d'), CACHE = path.join(__dirname, 'cache');
const L = 6, WALKABLE = 1, WATER = 2;   // (Phoenix: 6 surfaces per cell)
const DIRS = [[1, 0], [1, 1], [0, 1], [-1, 1], [-1, 0], [-1, -1], [0, -1], [1, -1]];
const FEATURES = { veh: true, exits: false, learn: false };   // (Phoenix: no water-exit file; its .learn is its own format)
const LIMITS = { learnRoutes: 160, learnPts: 240, vRoutes: 160, vPts: 256, pRoutes: 400, pPts: 250 };   // (Phoenix: 400 foot routes, 256 points each)

const safeMap = m => /^[A-Za-z0-9_]+$/.test(m || '') ? m : null;
const navFile = m => path.join(NAV, m + '.nav');

// ---- the scan -> point cloud. kinds: 0 floor, 1 water, 2 wall / blocked edge
function buildCloud(map) {
    const file = navFile(map);
    const st = fs.statSync(file);
    const cacheFile = path.join(CACHE, map + '.cloud6');   // (2: + slope per point, 3: walls down from roof edges, 4: + floor surface triangles)
    try { const c = fs.statSync(cacheFile); if (c.mtimeMs > st.mtimeMs) return fs.readFileSync(cacheFile); } catch (e) {}
    const buf = fs.readFileSync(file);
    const magic = buf.readUInt32LE(0);
    const x0 = buf.readFloatLE(36), z0 = buf.readFloatLE(40), cell = buf.readFloatLE(44);
    const w = buf.readInt32LE(48), h = buf.readInt32LE(52), layers = buf.readInt32LE(56);
    if ((magic !== 0x3556414e && magic !== 0x3456414e) || layers !== L) throw new Error('not a Project Phoenix nav grid: ' + file);
    const n = w * h * L, HO = 60;
    const height = new Float32Array(buf.buffer.slice(buf.byteOffset + HO, buf.byteOffset + HO + n * 4));
    const flags = buf.subarray(HO + n * 4, HO + n * 5), edges = buf.subarray(HO + n * 5, HO + n * 6);
    // (the tester 2026-11-06: "bridges show no opening on the bottom") an edge with a drop drew a wall down to the floor below: right for
    // a building's face, but it closed bridges off. Bridges and beams get no wall down.
    const isOver = new Uint8Array(n); for (const it of findOverhead(file).items) for (const i of it.nodes) isOver[i] = 1;
    const pos = [], kind = [], slope = []; const nodeIdx = new Int32Array(n).fill(-1);   // (floor node -> its point, for the surface)
    let ymin = Infinity, ymax = -Infinity;
    const put = (x, y, z, k, s = 0) => { pos.push(x, y, z); kind.push(k); slope.push(s); };
    for (let cz = 0; cz < h; cz++) for (let cx = 0; cx < w; cx++) {
        const c = cz * w + cx, wx = x0 + cx * cell, wz = z0 + cz * cell;
        for (let l = 0; l < L; l++) {
            const node = c * L + l;
            if (!(flags[node] & WALKABLE)) continue;
            const y = height[node]; if (!isFinite(y)) continue;
            if (y < ymin) ymin = y; if (y > ymax) ymax = y;
            // slope: the steepest walkable link to a straight neighbour, in degrees
            let steep = 0;
            for (let d = 0; d < 8; d += 2) {
                if (!(edges[node] & (1 << d))) continue;
                const nx = cx + DIRS[d][0], nz = cz + DIRS[d][1]; if (nx < 0 || nz < 0 || nx >= w || nz >= h) continue;
                const nc = (nz * w + nx) * L; let best = null;
                for (let k = 0; k < L; k++) if (flags[nc + k] & WALKABLE) { const dh = Math.abs(height[nc + k] - y); if (best === null || dh < best) best = dh; }
                if (best !== null && best > steep) steep = best;
            }
            nodeIdx[node] = kind.length;
            put(wx, y, wz, flags[node] & WATER ? 1 : 0, Math.min(90, Math.round(Math.atan(steep / cell) * 180 / Math.PI)));
            if (flags[node] & WATER) continue;
            const wall = [null, null, null, null, null, null, null, null];
            for (let d = 0; d < 8; d += 2) {   // straight directions only
                if (edges[node] & (1 << d)) continue;
                const nx = cx + DIRS[d][0], nz = cz + DIRS[d][1];
                // wall from lo to hi (metres above this floor): 2 m by default, up to a higher neighbour floor, and at an
                // edge with a drop (a roof, a balcony, a cliff) down to the floor below - so tower facades show
                let lo = 0.4, hi = 2;
                if (nx >= 0 && nz >= 0 && nx < w && nz < h) {
                    const nc = (nz * w + nx) * L; let best = null;
                    for (let k = 0; k < L; k++) if (flags[nc + k] & WALKABLE) { const dh = height[nc + k] - y; if (best === null || Math.abs(dh) < Math.abs(best)) best = dh; }
                    if (best !== null && best < -1) {
                        if (best > -2.5 || isOver[node]) continue;   // (a step down, or the side of a bridge: no wall)
                        let bottom = y + best;   // (the nearest neighbour floor is the highest one below)
                        for (let k = 0; k < L; k++) if (k !== l && (flags[c * L + k] & WALKABLE) && height[c * L + k] < y - 1 && height[c * L + k] > bottom) bottom = height[c * L + k];   // (only down to this cell's own next floor: each storey draws its own stretch)
                        lo = Math.max(bottom - y, -400); hi = -0.2;
                    } else if (best !== null && best > 1) hi = Math.min(best, 400);
                }
                wall[d] = [lo, hi];
            }
            // two blocked sides that meet at a corner (one step of a wall running diagonally across the grid): one diagonal
            // stretch from the middle of the one side to the middle of the other, so diagonal walls come out straight
            const column = (x, z, lo, hi) => { const step = Math.max(0.6, (hi - lo) / 60); for (let yy = lo; yy <= hi + 0.01; yy += step) put(x, y + yy, z, 2, 90); };
            for (let d = 0; d < 8; d += 2) {
                const e = (d + 2) % 8; if (!wall[d] || !wall[e]) continue;
                const ax = wx + DIRS[d][0] * cell / 2, az = wz + DIRS[d][1] * cell / 2, bx = wx + DIRS[e][0] * cell / 2, bz = wz + DIRS[e][1] * cell / 2;
                const lo = Math.min(wall[d][0], wall[e][0]), hi = Math.max(wall[d][1], wall[e][1]);
                for (const u of [0, 0.5, 1]) column(ax + (bx - ax) * u, az + (bz - az) * u, lo, hi);
                wall[d] = wall[e] = null;
            }
            for (let d = 0; d < 8; d += 2) {
                if (!wall[d]) continue;
                const ex = wx + DIRS[d][0] * cell / 2, ez = wz + DIRS[d][1] * cell / 2, px = -DIRS[d][1] * cell * 0.25, pz = DIRS[d][0] * cell * 0.25;
                for (let s = -1; s <= 1; s += 2) column(ex + px * s, ez + pz * s, wall[d][0], wall[d][1]);
            }
        }
    }
    // floor surface: each node joined to its linked neighbours at +x, +z and the diagonal into triangles, so ramps and
    // stairs show as slopes (points only show where the surface is)
    const idx = [];
    const near = (cx2, cz2, y, maxDh) => { if (cx2 >= w || cz2 >= h) return -1; const nc = (cz2 * w + cx2) * L; let bestK = -1, bd = maxDh; for (let k = 0; k < L; k++) if (nodeIdx[nc + k] >= 0) { const dh = Math.abs(height[nc + k] - y); if (dh < bd) { bd = dh; bestK = nc + k; } } return bestK; };
    for (let cz = 0; cz < h - 1; cz++) for (let cx = 0; cx < w - 1; cx++) for (let l = 0; l < L; l++) {
        const a = (cz * w + cx) * L + l; if (nodeIdx[a] < 0) continue; const y = height[a];
        const B = edges[a] & 1 || flags[a] & WATER ? near(cx + 1, cz, y, 2.5) : -1, C = edges[a] & 4 || flags[a] & WATER ? near(cx, cz + 1, y, 2.5) : -1, D = near(cx + 1, cz + 1, y, 3.5);
        const A = nodeIdx[a], b = B >= 0 ? nodeIdx[B] : -1, c = C >= 0 ? nodeIdx[C] : -1, d = D >= 0 ? nodeIdx[D] : -1;
        if (b >= 0 && c >= 0 && d >= 0) idx.push(A, b, d, A, d, c);
        else if (b >= 0 && d >= 0) idx.push(A, b, d);
        else if (c >= 0 && d >= 0) idx.push(A, d, c);
        else if (b >= 0 && c >= 0) idx.push(A, b, c);
    }
    const meta = { map, x0, z0, cell, w, h, count: kind.length, tris: idx.length, ymin, ymax, file: path.basename(file) };
    const j = Buffer.from(JSON.stringify(meta)); const pad = (4 - ((4 + j.length) % 4)) % 4;
    const head = Buffer.alloc(4); head.writeUInt32LE(j.length + pad);
    const out = Buffer.concat([head, j, Buffer.alloc(pad, 32), Buffer.from(new Float32Array(pos).buffer), Buffer.from(new Uint8Array(kind).buffer), Buffer.from(new Uint8Array(slope).buffer), Buffer.alloc((4 - (kind.length * 2) % 4) % 4), Buffer.from(new Uint32Array(idx).buffer)]);
    try { fs.mkdirSync(CACHE, { recursive: true }); fs.writeFileSync(cacheFile, out); } catch (e) {}
    return out;
}

// ---- route files
const read = f => { try { return fs.readFileSync(f, 'utf8'); } catch (e) { return null; } };
const exitsFile = m => path.join(LEARN, m + '.exits');
const vehiclesFile = m => path.join(LEARN, m + '.vehicles');   // where vehicles stood at round start, written by the bots: "veh <name> x y z yaw <rounds seen>" (read only)
const learnFile = m => path.join(LEARN, m + '.3dlearn'), vFile = m => path.join(LEARN, m + '.vroutes'), pFile = m => path.join(ROUTES, m + '.froutes'), lvFile = m => path.join(LEARN, m + '.routes');
// Map names. The bots name a map in <map>.objectives (1.7.0); older maps are named from the first line of their .learn
// file ("... learned on Levels/MP_006 - Arica Harbor (key)") or the .froutes header. A <map>.name file (one line) wins. The mode comes from the level name
// the way Phoenix reads it: ..CQ Conquest, ..GR Rush, ..SR Squad Rush, ..SDM Squad Deathmatch; a plain MP_00x level is
// Conquest when its number is odd (MP_001 Panama Canal, MP_005 Atacama) and Rush when even (MP_002 Valparaiso, MP_006 Arica).
const BC2_MAPS = [['NAM_MP_002', 'Vantage Point'], ['NAM_MP_003', 'Hill 137'], ['NAM_MP_005', 'Cao Son Temple'], ['NAM_MP_006', 'Phu Bai Valley'],
    ['NAM_MP_007', 'Operation Hastings'], ['MP_SP_002', 'Cold War'], ['MP_SP_005', 'Heavy Metal'], ['BC1_OASIS', 'Oasis'], ['BC1_HARVEST', 'Harvest Day'],
    ['MP_001', 'Panama Canal'], ['MP_002', 'Valparaiso'], ['MP_003', 'Laguna Alta'], ['MP_004', 'Isla Inocentes'], ['MP_005', 'Atacama Desert'],
    ['MP_006', 'Arica Harbor'], ['MP_007', 'White Pass'], ['MP_008', 'Nelson Bay'], ['MP_009', 'Laguna Presa'], ['MP_012', 'Port Valdez']];
function bc2Mode(code) {
    const u = code.toUpperCase();
    if (/SDM$/.test(u)) return 'Squad Deathmatch';
    if (/SR$/.test(u)) return 'Squad Rush';
    if (/GR$/.test(u) || /^NAM_.*\dR$/.test(u)) return 'Rush';   // (Vietnam: NAM_MP_002R)
    if (/CQ$/.test(u)) return 'Conquest';
    const d = u.match(/(\d)$/); return d ? (+d[1] % 2 ? 'Conquest' : 'Rush') : '';
}
function mapLabel(map) {
    const own = (read(path.join(NAV, map + '.name')) || '').split(/\r?\n/)[0].trim();   // (a one-line name you can write yourself)
    if (own) return own;
    let level = '', name = '';
    const ob = read(path.join(NAV, map + '.objectives'));
    if (ob) for (const l of ob.split(/\r?\n/)) { if (l.startsWith('level ')) level = l.slice(6).trim(); else if (l.startsWith('name ')) name = l.slice(5).trim(); }
    if (!level) {
        const lt = read(path.join(NAV, map + '.learn')); const m = lt && lt.match(/learned on (.*?) \(/);
        if (m) { const p = m[1].split(' - '); if (/^levels\//i.test(p[0])) { level = p[0]; name = name || p.slice(1).join(' - '); } }
    }
    if (!level && !name) {
        const ft = read(pFile(map)); const m = ft && ft.match(/foot routes for \S+ \(([^)]*)\)/);
        if (m) { if (/^levels\//i.test(m[1])) level = m[1]; else name = m[1]; }
    }
    const code = level.replace(/^levels\//i, '');
    if (!name && code) { const hit = BC2_MAPS.find(([c]) => code.toUpperCase().startsWith(c)); if (hit) name = hit[1]; }
    if (!name && !code) return '';
    const mode = code ? bc2Mode(code) : '';
    return (name || code) + (mode ? ' - ' + mode : '') + (code ? '  [' + code + ']' : '');
}
function reportData(map) {   // Phoenix: <map>.objectives (flags / M-COMs / spawns, written by the bots)
    const t = read(path.join(NAV, map + '.objectives')); const flags = [], spawns = []; let name = '';
    if (t) for (const l of t.split(/\r?\n/)) {
        const v = l.trim().split(/\s+/);
        if (v[0] === 'flag' && v.length >= 5) flags.push({ x: +v[2], y: +v[3], z: +v[4], r: 10, owner: +v[5] || 0, mcom: false, letter: v[1], name: v.slice(6).join(' ') });
        else if (v[0] === 'mcom' && v.length >= 5) flags.push({ x: +v[2], y: +v[3], z: +v[4], r: 3, owner: +v[5] || 0, mcom: true, letter: v[1], stage: +v[6] || 0, name: v.slice(7).join(' ') });
        else if (v[0] === 'spawn' && v.length >= 5) spawns.push(`spawn ${v[1]} ${v[2]} ${v[3]} ${v[4]}`);
        else if (v[0] === 'name') name = l.trim().slice(5);
    }
    return { flags, spawns, name };
}
function loadRoutes(map) {
    const out = { learn: [], veh: [], planned: [], spawnLines: [], files: {} };
    const lt = read(learnFile(map));
    if (lt) for (const l of lt.split(/\r?\n/)) {
        if (!l.startsWith('route\t')) continue;
        const f = l.split('\t'); if (f.length < 4) continue;
        const [uses, made] = f[2].split(' ').map(Number);
        const pts = f[3].trim().split(/\s+/).map(p => p.split(',').map(Number)).filter(p => p.length === 3 && p.every(isFinite));
        if (pts.length >= 2) out.learn.push({ kind: f[1], uses: uses || 1, made: made || 0, pts });
    }
    const vt = read(vFile(map));
    if (vt) for (const l of vt.split(/\r?\n/)) {
        if (!l.startsWith('route ')) continue;
        const v = l.trim().split(/\s+/); const fb = v[v.length - 1] === 'fb'; const n = +v[7]; const pts = [];
        for (let q = 0; q < n; q++) { const p = [+v[8 + q * 3], +v[9 + q * 3], +v[10 + q * 3]]; if (p.every(isFinite)) pts.push(p); }
        if (pts.length >= 2) out.veh.push({ fb, pts });
    }
    const pt = read(pFile(map));
    if (pt) for (const l of pt.split(/\r?\n/)) {
        if (l.startsWith('spawn ')) { out.spawnLines.push(l); continue; }
        if (!l.startsWith('route ')) continue;
        const v = l.trim().split(/\s+/); const pts = v.slice(5).map(p => p.split(',').map(Number)).filter(p => p.length === 3 && p.every(isFinite));
        if (pts.length >= 2) out.planned.push({ from: v[1], to: v[2], len: +v[3], pts });
    }
    const lv = read(lvFile(map));   // LearnViewer's drawn vehicle lines: "route", "x z" / "x y z" per line, "end"
    if (lv) { let cur = null; for (const l of lv.split(/\r?\n/)) { const t = l.trim(); if (/^route/i.test(t)) { cur = []; continue; } if (/^end/i.test(t)) { if (cur && cur.length >= 2) out.veh.push({ lv: true, pts: cur }); cur = null; continue; }
        const v = t.split(/\s+/).map(Number); if (cur && v.length >= 2 && v.every(isFinite)) cur.push(v.length >= 3 ? [v[0], v[1], v[2]] : [v[0], NaN, v[1]]); } }
    if (out.veh.some(r => r.pts.some(p => !isFinite(p[1])))) {   // (LearnViewer lines carry no heights: the scan's top floor there)
        const buf = fs.readFileSync(navFile(map)), x0 = buf.readFloatLE(36), z0 = buf.readFloatLE(40), cell = buf.readFloatLE(44), w = buf.readInt32LE(48), h = buf.readInt32LE(52), n = w * h * L;
        const height = new Float32Array(buf.buffer.slice(buf.byteOffset + 60, buf.byteOffset + 60 + n * 4)), flags = buf.subarray(60 + n * 4, 60 + n * 5);
        for (const r of out.veh) for (const p of r.pts) if (!isFinite(p[1])) { const cx = Math.round((p[0] - x0) / cell), cz = Math.round((p[2] - z0) / cell); p[1] = 0;
            if (cx >= 0 && cz >= 0 && cx < w && cz < h) for (let k = 0; k < L; k++) { const node = (cz * w + cx) * L + k; if (flags[node] & WALKABLE) { p[1] = height[node]; break; } } }
    }
    out.spawnLines = reportData(map).spawns;
    const et = read(exitsFile(map));
    out.exits = et ? et.split(/\r?\n/).map(l => l.trim().split(/\s+/).map(Number)).filter(p => p.length === 3 && p.every(isFinite)) : [];
    const vs = read(vehiclesFile(map));
    out.vehicles = vs ? vs.split(/\r?\n/).map(l => l.trim().split(/\s+/)).filter(v => v[0] === 'veh' && v.length >= 7).map(v => ({ name: v[1], p: [+v[2], +v[3], +v[4]], yaw: +v[5], seen: +v[6] })).filter(v => v.p.every(isFinite)) : [];
    out.files = { learn: !!lt, veh: !!vt, planned: !!pt, exits: !!et };
    return out;
}
const f1 = v => (Math.round(v * 10) / 10).toFixed(1);
const chunks = (pts, max) => { if (pts.length <= max) return [pts]; const o = []; for (let i = 0; i < pts.length - 1; i += max - 1) o.push(pts.slice(i, i + max)); return o.filter(c => c.length >= 2); };
const plen = pts => { let s = 0; for (let i = 1; i < pts.length; i++) s += Math.hypot(pts[i][0] - pts[i - 1][0], pts[i][2] - pts[i - 1][2]); return s; };
const stamp = () => new Date().toISOString().replace(/[-:]/g, '').replace('T', '_').slice(0, 15);
function backup(file) { if (!fs.existsSync(file)) return; fs.mkdirSync(BACKUP, { recursive: true }); fs.copyFileSync(file, path.join(BACKUP, path.basename(file) + '.' + stamp())); }
function saveRoutes(map, body) {
    const done = [];
    const clean = r => (r.pts || []).map(p => p.map(Number)).filter(p => p.length === 3 && p.every(isFinite));
    if (Array.isArray(body.learn)) {
        const file = learnFile(map); const old = read(file);
        let keep = old ? old.split(/\r?\n/).filter(l => !l.startsWith('route\t')) : [`AltBotMod learned\t1\t${map}_TurfWarLarge0`];
        while (keep.length && keep[keep.length - 1] === '') keep.pop();
        const now = Math.floor(Date.now() / 1000); let n = 0;
        for (const r of body.learn) for (const c of chunks(clean(r), LIMITS.learnPts)) {
            if (c.length < 2) continue;
            const kind = ['foot', 'ground', 'boat'].includes(r.kind) ? r.kind : 'foot';
            keep.push(`route\t${kind}\t${Math.max(1, r.uses | 0 || 10)} ${r.made | 0 || now}\t` + c.map(p => p.map(f1).join(',')).join(' ')); n++;
        }
        backup(file); fs.writeFileSync(file, keep.join('\n') + '\n'); done.push(`${n} learned route(s)`);
    }
    if (Array.isArray(body.veh)) {
        const file = vFile(map); const lines = [];
        for (const r of body.veh) for (const c of chunks(clean(r), LIMITS.vPts)) {
            if (c.length < 2) continue; const a = c[0], b = c[c.length - 1];
            lines.push(`route ${a.map(f1).join(' ')} ${b.map(f1).join(' ')} ${c.length} ` + c.map(p => p.map(f1).join(' ')).join(' ') + (r.fb ? ' fb' : ''));
        }
        backup(file); fs.writeFileSync(file, lines.join('\n') + (lines.length ? '\n' : '')); done.push(`${lines.length} vehicle route(s)`);
        const lvf = lvFile(map); if (fs.existsSync(lvf)) { backup(lvf); fs.renameSync(lvf, lvf + '.moved-to-vroutes'); }   // (now in .vroutes)
    }
    if (Array.isArray(body.planned)) {
        const file = pFile(map); const old = read(file) || '';
        const head = old.split(/\r?\n/).filter(l => l.startsWith('#') || l.startsWith('spawn '));
        if (!head.length || !head[0].startsWith('#')) head.unshift(`# Project Phoenix foot routes for ${map}: route <from> <to> <length m> <points> x,y,z ... - edit them in the 3D map`);
        let n = 0;
        for (const r of body.planned) {
            const from = String(r.from || '').replace(/\s/g, ''), to = String(r.to || '').replace(/\s/g, '');
            if (!from || !to) continue;
            const c = clean(r).slice(0, LIMITS.pPts); if (c.length < 2) continue;
            head.push(`route ${from} ${to} ${(isFinite(r.len) && r.len > 0 && c.length === (r.pts || []).length ? r.len : plen(c)).toFixed(0)} ${c.length} ` + c.map(p => p.map(f1).join(',')).join(' ')); n++;
        }
        backup(file); fs.writeFileSync(file, head.join('\n') + '\n'); done.push(`${n} spawn/flag route(s)`);
    }
    if (Array.isArray(body.exits)) {   // ways out of the water: "x y z" per line, 64 at most (pool.inc)
        const file = exitsFile(map); const ex = body.exits.map(p => (p || []).map(Number)).filter(p => p.length === 3 && p.every(isFinite)).slice(0, 64);
        backup(file); fs.writeFileSync(file, ex.map(p => p.map(f1).join(' ')).join('\n') + (ex.length ? '\n' : '')); done.push(`${ex.length} water exit(s)`);
    }
    return done;
}
function liveMap() {   // navcache\live.txt, rewritten every 30 s by the bots
    try { const f = path.join(NAV, 'live.txt'); const st = fs.statSync(f); const m = (read(f) || '').trim(); return m && Date.now() - st.mtimeMs < 3 * 60 * 1000 ? m : null; } catch (e) { return null; }
}

const overheadData = map => findOverhead(navFile(map)).items.map(({ nodes, ...it }) => it);

// ---- where the roads are (the tester 2026-11-06), from what the bots already learned, as binary: [scan count, trail count] as Uint32, then
// x y z floats. <map>.roadscan = grid nodes on a road material ("w h x0 z0 cell", then one node per line; only for this grid);
// <map>.roads = lines of "x y z x y z ..." that vehicles were driven along.
function roadData(map) {   // Phoenix: <map>.roads = "road <used> <kind> <name>", "x y z" per point, "end" (the level's own road lines)
    const scan = [], trail = [];
    const rd = read(path.join(NAV, map + '.roads'));
    if (rd) { let cur = null, used = false;
        const flush = () => { if (!cur || !used) return; for (let k = 0; k + 1 < cur.length; k++) { const a = cur[k], b = cur[k + 1], d = Math.hypot(b[0] - a[0], b[2] - a[2]), steps = Math.min(200, Math.max(1, Math.ceil(d / 2)));
            for (let q = 0; q < steps; q++) { const f = q / steps; trail.push(a[0] + (b[0] - a[0]) * f, a[1] + (b[1] - a[1]) * f, a[2] + (b[2] - a[2]) * f); } } };
        for (const l of rd.split(/\r?\n/)) { const t = l.trim(); if (!t || t[0] === '#') continue;
            if (/^road\s/.test(t)) { flush(); cur = []; used = t.split(/\s+/)[1] === '1'; continue; }
            if (/^end/.test(t)) { flush(); cur = null; continue; }
            const v = t.split(/\s+/).map(Number); if (cur && v.length >= 3 && v.every(isFinite)) cur.push(v); }
        flush(); }
    const head = Buffer.from(new Uint32Array([scan.length / 3, trail.length / 3]).buffer);
    return Buffer.concat([head, Buffer.from(new Float32Array(scan).buffer), Buffer.from(new Float32Array(trail).buffer)]);
}

// ---- web server
const send = (res, code, type, body) => { res.writeHead(code, { 'Content-Type': type, 'Cache-Control': 'no-store' }); res.end(body); };
const server = http.createServer((req, res) => {
    const u = new URL(req.url, 'http://localhost'); const map = safeMap(u.searchParams.get('map'));
    try {
        if (u.pathname === '/' || u.pathname === '/index.html') return send(res, 200, 'text/html; charset=utf-8', fs.readFileSync(path.join(__dirname, 'index.html')));
        if (u.pathname === '/api/maps') {   // every scanned map in navcache, named from its .objectives file
            const maps = fs.readdirSync(NAV).filter(f => f.endsWith('.nav')).map(f => f.slice(0, -4)).filter(safeMap).sort();
            const names = {}; for (const m of maps) { const n = mapLabel(m); if (n) names[m] = n; }
            return send(res, 200, 'application/json', JSON.stringify({ version: VERSION, server: NAV, maps, names, live: liveMap(), limits: LIMITS, features: FEATURES }));
        }
        // the map's own top-down image, read from this PC's game files (textures\\<map>.jpg + .json: re\\scripts\\fb_minimap.py)
        if (u.pathname === '/api/texinfo' && map) { return send(res, 200, 'application/json', 'null'); }
        if (u.pathname === '/api/teximg' && map) return send(res, 200, 'image/jpeg', fs.readFileSync(path.join(__dirname, 'textures', map + '.jpg')));
        if (u.pathname === '/api/roads' && map) return send(res, 200, 'application/octet-stream', roadData(map));
        if (u.pathname === '/api/playarea' && map) {   // (none for BC2 yet: <map>.playarea "area <mode> n x z ...")
    const modes = {}; const txt = read(path.join(LEARN, map + '.playarea'));
    if (txt) txt.split(/\r?\n/).forEach(l => { const w = l.trim().split(/\s+/); if (w[0] !== 'area' || w.length < 3) return; const n = +w[2], pts = []; for (let k = 0; k < n; k++) pts.push([+w[3 + k * 2], +w[4 + k * 2]]); (modes[w[1]] = modes[w[1]] || []).push(pts); });
    return send(res, 200, 'application/json', JSON.stringify(modes));
}
if (u.pathname === '/api/overhead' && map) return send(res, 200, 'application/json', JSON.stringify(overheadData(map)));
        if (u.pathname === '/api/cloud' && map) return send(res, 200, 'application/octet-stream', buildCloud(map));
        if (u.pathname === '/api/routes' && map && req.method === 'GET') return send(res, 200, 'application/json', JSON.stringify({ ...loadRoutes(map), ...reportData(map), live: liveMap() === map }));
        if (u.pathname === '/api/routes' && map && req.method === 'POST') {
            let b = ''; req.on('data', d => { b += d; if (b.length > 50e6) req.destroy(); });
            req.on('end', () => { try { const done = saveRoutes(map, JSON.parse(b)); console.log(new Date().toLocaleTimeString(), map, 'saved:', done.join(', ')); send(res, 200, 'application/json', JSON.stringify({ ok: true, done })); } catch (e) { send(res, 500, 'application/json', JSON.stringify({ ok: false, error: String(e.message || e) })); } });
            return;
        }
        if (u.pathname === '/api/ply' && map) {   // the scan as a PLY point cloud (for CloudCompare / Blender / MeshLab)
            const c = buildCloud(map); const jl = c.readUInt32LE(0); const meta = JSON.parse(c.subarray(4, 4 + jl).toString());
            const pos = new Float32Array(c.buffer.slice(c.byteOffset + 4 + jl, c.byteOffset + 4 + jl + meta.count * 12)); const kind = c.subarray(4 + jl + meta.count * 12);
            const col = [[150, 160, 150], [40, 120, 230], [230, 120, 60]];
            const head = `ply\nformat binary_little_endian 1.0\ncomment Project Phoenix scan of ${map}\nelement vertex ${meta.count}\nproperty float x\nproperty float y\nproperty float z\nproperty uchar red\nproperty uchar green\nproperty uchar blue\nend_header\n`;
            const body = Buffer.alloc(meta.count * 15);
            for (let i = 0; i < meta.count; i++) { body.writeFloatLE(pos[i * 3], i * 15); body.writeFloatLE(pos[i * 3 + 2], i * 15 + 4); body.writeFloatLE(pos[i * 3 + 1], i * 15 + 8); const k = col[kind[i]] || col[0]; body[i * 15 + 12] = k[0]; body[i * 15 + 13] = k[1]; body[i * 15 + 14] = k[2]; }
            res.writeHead(200, { 'Content-Type': 'application/octet-stream', 'Content-Disposition': `attachment; filename="${map}.ply"` });
            return res.end(Buffer.concat([Buffer.from(head), body]));
        }
        send(res, 404, 'text/plain', 'not found');
    } catch (e) { console.error(e); send(res, 500, 'text/plain', String(e.message || e)); }
});
module.exports = { roadData, buildCloud, loadRoutes, reportData, overheadData, liveMap, LIMITS };
// 1.7.1: maps the bots scanned before 1.7.0 get their flags, M-COMs, spawns and vehicles from the bot logs - this install's
// reports and those of the other Project Phoenix folders next to it (files the bots wrote themselves are never replaced)
function importOldLogs() {
    try {
        const dirs = [SERVER], up = path.dirname(SERVER);
        for (const e of fs.readdirSync(up, { withFileTypes: true })) if (e.isDirectory() && /phoenix/i.test(e.name) && path.join(up, e.name) !== SERVER) dirs.push(path.join(up, e.name));
        require('./import-logs.js').importLogs(NAV, dirs, t => console.log('logs: ' + t));
    } catch (e) { console.log('logs: ' + e.message); }
}
if (require.main === module) { importOldLogs(); server.listen(PORT, '127.0.0.1', () => console.log(`Project Phoenix 3D map: http://localhost:${PORT}/  (folder ${NAV})`)); }
