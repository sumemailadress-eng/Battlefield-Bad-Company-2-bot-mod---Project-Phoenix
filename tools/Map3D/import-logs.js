// Project Phoenix: fills in the flags, M-COMs, spawns and vehicles of maps the bots scanned before 1.7.0 (when nothing but
// the bot log recorded them), so the 3D map can show them.
//   node import-logs.js <navcacheFolder> <folder with reports> [more folders...]
// Every runtime.log found under the folders (any depth) is read. For each map key (bc2v2_...), the log section where the
// bots played it gives the level name, the flags (names, positions, starting owners), every Rush M-COM (all stages: the
// game lists every stage's arm/defuse points at the start of the round), the spawn points and the vehicle spawners.
// <key>.objectives and <key>.vehicles are written only where they don't exist yet (or have no flags / M-COMs) - files the
// bots wrote themselves always win. Nothing else is touched.
'use strict';
const fs = require('fs'), path = require('path');

function findLogs(dir, out, depth) {
    let ents = []; try { ents = fs.readdirSync(dir, { withFileTypes: true }); } catch (e) { return; }
    for (const e of ents) {
        const p = path.join(dir, e.name);
        if (e.isDirectory()) { if (depth < 6 && !/^(navcache|PhoenixNavcache|source|node_modules|cache)$/i.test(e.name)) findLogs(p, out, depth + 1); }
        else if (/^runtime\.log$/i.test(e.name)) out.push(p);
    }
}
const num = s => +s;
const d2 = (a, b) => Math.hypot(a[0] - b[0], a[2] - b[2]);

// one log section per level: from a "level:" line to the next one
function parseLog(text, sections) {
    let cur = null;
    const start = () => { cur = { level: '', name: '', key: '', flags: null, scanFlags: null, inter: [], spawns: new Map(), startSpawns: null, vehs: [] }; sections.push(cur); };
    for (const line of text.split(/\r?\n/)) {
        let m;
        if ((m = line.match(/\] level: (Levels\/\S+)(?: - (.+))?$/i))) { start(); cur.level = m[1]; cur.name = (m[2] || '').trim(); continue; }
        if (/\] level: level object /.test(line)) { start(); continue; }
        if (!cur) start();
        if ((m = line.match(/nav: (?:loaded cached grid|building grid) (bc2v2_[a-z0-9_]+)/))) { if (!cur.key) cur.key = m[1]; continue; }
        if ((m = line.match(/\] flags: memory scan .* (\d+) active capture point/))) { cur.scanFlags = []; if (+m[1] === 0) cur.scanFlags = null; continue; }
        if ((m = line.match(/\] flags:\s+([A-P]) '([^']*)' at \(([-\d.]+), ([-\d.]+), ([-\d.]+)\) radius ([\d.]+) owner (\d+)(.*)$/))) {
            if (!cur.scanFlags) cur.scanFlags = [];
            cur.scanFlags.push({ name: m[2], pos: [num(m[3]), num(m[4]), num(m[5])], r: num(m[6]), owner: num(m[7]), locked: /locked/.test(m[8]) });
            if (!cur.flags || cur.scanFlags.length >= cur.flags.length) cur.flags = cur.scanFlags;   // (the first full list keeps the starting owners)
            continue;
        }
        if ((m = line.match(/objectives:\s+interaction 0x[0-9a-f]+ at \(([-\d.]+), ([-\d.]+), ([-\d.]+)\) team (-?\d+) hold [\d.]+ s type (\d+)/))) {
            const t = +m[5]; if (t === 2 || t === 3) cur.inter.push({ pos: [num(m[1]), num(m[2]), num(m[3])], team: +m[4], type: t });
            if (!cur.startSpawns) cur.startSpawns = [...cur.spawns.values()];   // (the round's first spawns: the stage-1 bases)
            continue;
        }
        if ((m = line.match(/objectives: a human \(team \d+\) is at interaction 0x[0-9a-f]+ type (\d+) team (-?\d+) at \(([-\d.]+), ([-\d.]+), ([-\d.]+)\)/))) {
            const t = +m[1]; if (t === 2 || t === 3) cur.inter.push({ pos: [num(m[3]), num(m[4]), num(m[5])], team: +m[2], type: t });
            continue;
        }
        if ((m = line.match(/spawns: #\d+ id \d+ team (\d+) (\w+) at \(([-\d.]+), ([-\d.]+), ([-\d.]+)\)/))) {
            const p = [num(m[3]), num(m[4]), num(m[5])], k = p.join(',');
            if (!cur.spawns.has(k)) cur.spawns.set(k, { team: +m[1], pos: p, enabled: m[2] === 'enabled' }); continue;
        }
        if ((m = line.match(/vehicles:\s+(\S+)\s+([\w/]+)\s+team (\d+) at \(([-\d.]+), ([-\d.]+), ([-\d.]+)\)/))) {
            const v = { name: m[1], kind: m[2], team: +m[3], pos: [num(m[4]), num(m[5]), num(m[6])] };
            if (!cur.vehs.some(o => o.name === v.name && d2(o.pos, v.pos) < 2)) cur.vehs.push(v);
        }
    }
}

// Rush M-COMs: one per arm/defuse spot; attackers = the team the arm points are for; stage = rank from the attackers' spawns
function mcoms(sec) {
    const spots = [];
    for (const i of sec.inter) {
        let s = spots.find(o => d2(o.pos, i.pos) < 4 && Math.abs(o.pos[1] - i.pos[1]) < 3);
        if (!s) { s = { pos: i.pos, armTeam: 0, defTeam: 0 }; spots.push(s); }
        if (i.type === 2 && i.team >= 1 && i.team <= 2) s.armTeam = i.team;
        if (i.type === 3 && i.team >= 1 && i.team <= 2) s.defTeam = i.team;
    }
    if (!spots.length) return [];
    let att = (spots.find(s => s.armTeam) || {}).armTeam || (3 - ((spots.find(s => s.defTeam) || {}).defTeam || 2));
    // the attackers' first base: their spawns that were enabled when the round started (Rush moves both teams' spawns along)
    const all = sec.startSpawns && sec.startSpawns.length ? sec.startSpawns : [...sec.spawns.values()];
    let base = all.filter(s => s.team === att && s.enabled); if (!base.length) base = all.filter(s => s.team === att);
    const ref = base.length ? [base.reduce((a, s) => a + s.pos[0], 0) / base.length, 0, base.reduce((a, s) => a + s.pos[2], 0) / base.length] : null;
    spots.forEach((s, k) => { s.d = ref ? d2(s.pos, ref) : k; });
    spots.sort((a, b) => a.d - b.d);
    const per = /SR$/i.test(sec.level) ? 1 : 2;
    return spots.map((s, k) => ({ pos: s.pos, owner: 3 - att, stage: Math.floor(k / per) + 1, name: 'Stage ' + (Math.floor(k / per) + 1) + (per > 1 ? ' ' + 'AB'[k % per] : '') }));
}

function score(sec) { return (sec.flags ? sec.flags.length * 10 : 0) + sec.inter.length * 5 + sec.spawns.size + sec.vehs.length + (sec.level ? 3 : 0); }

function main() {
    const [nav, ...dirs] = process.argv.slice(2);
    if (!nav || !dirs.length) { console.log('usage: node import-logs.js <navcacheFolder> <folder with reports> [...]'); process.exit(1); }
    importLogs(nav, dirs, console.log);
}
function importLogs(nav, dirs, say) {
    const logs = []; for (const d of dirs) findLogs(d, logs, 0);
    const sections = [];
    for (const f of logs) { try { parseLog(fs.readFileSync(f, 'latin1'), sections); } catch (e) {} }
    // best section per key; the level name may come from any section of that key
    const best = new Map(), names = new Map();
    for (const s of sections) {
        if (!s.key) continue;
        if (s.level && !names.has(s.key)) names.set(s.key, { level: s.level, name: s.name });
        const b = best.get(s.key); if (!b || score(s) > score(b)) best.set(s.key, s);
    }
    // flags and M-COMs stand where they stand whatever key a version gave the map: the best list per level fills the others
    const byLevel = new Map();
    for (const s of sections) {
        const lv = (s.level || (names.get(s.key) || {}).level || '').toUpperCase(); if (!lv) continue;
        const e = byLevel.get(lv) || { flags: null, mc: [] };
        if (s.flags && (!e.flags || s.flags.length > e.flags.length)) e.flags = s.flags;
        if (s.inter.length) { const mc = mcoms(Object.assign({}, s, { level: lv })); if (mc.length > e.mc.length) e.mc = mc; }
        byLevel.set(lv, e);
    }
    let wrote = 0, kept = 0;
    for (const [key, s] of best) {
        if (!fs.existsSync(path.join(nav, key + '.nav'))) continue;   // (a map that isn't in this navcache)
        const objPath = path.join(nav, key + '.objectives');
        let old = ''; try { old = fs.readFileSync(objPath, 'latin1'); } catch (e) {}
        const nm = names.get(key) || { level: s.level, name: s.name };
        const rush = /GR$|SR$|^Levels\/MP_00[2468]$|^Levels\/NAM_MP_\d+R$/i.test(nm.level || '') || (!s.flags && s.inter.length);
        const lvl = byLevel.get((nm.level || '').toUpperCase()) || { flags: null, mc: [] };
        let mc = rush ? mcoms(s) : []; if (rush && lvl.mc.length > mc.length) mc = lvl.mc;
        let flags = rush ? [] : (s.flags || []); if (!rush && lvl.flags && lvl.flags.length > flags.length) flags = lvl.flags;
        if (/^(flag|mcom) /m.test(old)) { kept++; continue; }
        if (!flags.length && !mc.length && !s.spawns.size) continue;
        let t = `# Project Phoenix: ${key} - from the bot logs (import-logs.js). flag <letter> x y z owner name | mcom <letter> x y z owner stage name | spawn team x y z\n`;
        t += `level ${nm.level || ''}\nname ${nm.name || ''}\n`;
        flags.forEach((f, i) => { t += `flag ${String.fromCharCode(65 + i)} ${f.pos[0].toFixed(1)} ${f.pos[1].toFixed(1)} ${f.pos[2].toFixed(1)} ${f.owner} ${f.name}${f.locked ? ' (base)' : ''}\n`; });
        mc.forEach((c, i) => { t += `mcom ${String.fromCharCode(65 + i)} ${c.pos[0].toFixed(1)} ${c.pos[1].toFixed(1)} ${c.pos[2].toFixed(1)} ${c.owner} ${c.stage} ${c.name}\n`; });
        for (const sp of s.spawns.values()) t += `spawn ${sp.team} ${sp.pos[0].toFixed(1)} ${sp.pos[1].toFixed(1)} ${sp.pos[2].toFixed(1)}\n`;
        fs.writeFileSync(objPath, t);
        const vPath = path.join(nav, key + '.vehicles');
        if (s.vehs.length && !fs.existsSync(vPath))
            fs.writeFileSync(vPath, s.vehs.filter(v => !/fixed/.test(v.kind)).map(v => `veh ${v.name} ${v.pos[0]} ${v.pos[1]} ${v.pos[2]} 0 ${v.team}`).join('\n') + '\n');
        wrote++;
        say(`${key}: ${nm.name || nm.level || '(no level name in the log)'} - ${flags.length} flag(s), ${mc.length} M-COM(s), ${s.spawns.size} spawn(s), ${s.vehs.length} vehicle(s)`);
    }
    say(`${logs.length} log(s) read; ${wrote} map(s) filled in, ${kept} already had their objectives.`);
    return wrote;
}
if (require.main === module) main();
module.exports = { parseLog, mcoms, importLogs };
