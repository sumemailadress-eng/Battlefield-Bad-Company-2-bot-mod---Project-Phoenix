# BC2 server engine notes (Phoenix 0.7.0)

Server: `Frost.Game.Main_Win32_Final.exe`, SHA-256 `CA8169FA03A29E02AF1BF8DB8770AB0E68F13A95B010C52ECA23733068298347`
(build ROMEPC851434). All addresses are preferred-image VAs (base 0x400000), rebased at runtime.
Found by disassembly plus the executable's own assertion strings and reflection tables.

## Server context `0x19C4CC0` (vtable `0x170E120`, getter `0x613900`)
- `+0x04` physics ray caster (`this` for the ray query)
- `+0x30` ServerPlayerManager (roster `+0x58/+0x5C`, max players `+4`, id table `+0x6C`)
- `+0x34` level object → `+0x78` → `+4` TeamInfo, `+8` spawn manager

## AI players die for good
`ServerSoldierEntity::onKilled` (`0x7F57D0`): `player->setControllable(0)`, then **if the player is AI
(`+0xD84`)**: `leaveGame` (`0x64C130`) and `ServerPlayerManager::deletePlayer` (`0x62DE20`). So an AI player
lives exactly one soldier life; Phoenix creates a new player per life. This is why the 0.5 bot "lost identity"
when it was killed.

## Player creation (as in 0.5)
allocate `0x64CDF0` → setTeam `0x614460` → setAI `0x5F97E0` → EntryInput ctor `0x724D10` + attach `0x5F9110`
→ register `0x63A660` → ready `0x614E10` → selectUnlocks `0x645290` → selectKit `0x625670(soldierResId, pairs, flags)`
→ selectedSpawn `0x6257A0(spawnResId)`.
- Spawn id = resource id of the spawn entity's data (`0x70DF10`). Spawn manager list `+8/+0xC`; spawn entity
  team `+0x2C`, enabled `byte +0x26 bit 3`. **Never pass 0**: the engine's default pick asserts on an empty team list.
- `leaveGame` asserts the player is in game, then `releaseLevelDependencies` (`0x6452D0`): detaches the soldier,
  clears `+0xD24` (soldier data) and `+0xD68` (ready). Players survive level changes this way.

## Kits
TeamInfo `[team*4]` = TeamEntityData; `+0x88` TeamData; TeamData `+0x20` Kits (reference array object:
count `+0xC`, elements `+0x10`) of SoldierAsset; `SoldierAsset+0x50` = SoldierEntityData (vtable `0x1859B78`).
`SoldierEntityData+0x1A0` WeaponUnlocks (struct array: begin `+8`, end `+0xC`, 12-byte
`SoldierWeaponUnlockData {WeaponSlot +0, Weapon +4, IsBundledWithSelectable +8, IsSelectable +9}`).
selectKit only uses the resource ids of the pairs. Asset name: `Asset+0xC` string {begin,end,cap}.

## Input and aiming
- AI soldiers are driven by their EntryInput: `ServerControllableEntity::prePhysicsUpdateEntry` (`0x7BF600`)
  copies the player's input for AI players (humans are client-predicted).
- EntryInput (0x1B0): two 0xD0 states, current `+0x1A0`, previous `+0x1A4`. Native AI pattern
  (`dice::ai::PlayerInput`, `0x663B10..0x663C10`): setLevel `0x71D290`, setDeltaTime `0x710100`,
  `EntryInput::push(state)` `0x724E30` (swaps cur/prev, copies, updates hold timers).
- **Input tick**: state `+0xB8` must hold the current simulation frame (`[[context+0xC]]`, setter `0x710690`).
  With a stale tick the soldier keeps aim/fire/stance but throttle and strafe are ignored (0.6 bug).
- Aim: `ServerSoldierEntity::setAiming(yaw, pitch)` `0x7D0700` (asserts `0 <= yaw <= 2*pi`).
  State yaw `+0xA8`, pitch `+0xAC`. Verified on 3,094 captured samples: soldier heading `atan2(fx, fz) = -yaw`.
- Captured human movement: throttle +1 ≈ 3.5 m/s forward, sprint ≈ 6.3 m/s, strafe +1 moves towards −right.

## Soldier movement step (why AI soldiers stood still)
- `ServerControllableEntity::prePhysicsUpdateEntry` (`0x7BF600`) gives AI players' input only to soldier vt+0x120
  (`0x7DD800`: weapons, aim, stance, animation). Walking is elsewhere.
- Soldier vt+0x110 (`0x7BD1E0`, entry index): input = entry player `+0xC38`; vt+0x11C (`0x7DD770`, movement input →
  `0x7CF9D0` builds the character-physics input: throttle × state `+0xB0` × speed × soldier `+0x128`, strafe, jump,
  pose, sprint); then `0xD60950(physics = soldier+0x140, dt)`; then vt+0x120 and `0x804C10(entry)`.
- The only caller of vt+0x110 is the network input handler (`~0x772166`), once per client input frame.
- Character physics `+0xDE` = stepped by input (set from `Physics.SingleStepCharacter`, `0x7CF980`); the physics world
  (`0xD61F60`) skips those. So without a client nothing walks the AI soldier.
- Live 0.6.2: `+0xDE` = 0 on bot soldiers, yet nothing stepped them (the world step `0xD61F60` only runs when
  `[[world+0xA230]+0xB0] == 0`). vt+0x110 itself calls `0xD60950` unconditionally, so 0.6.3 always steps.
- Live 0.6.3: physics input block (+0x120) held the bot's throttle/strafe, step ran, still no motion. Flags dc..df = 1 1 0 0.
  `ServerSoldierEntity::setPlayer` (`0x7F29C0`) calls `0xD34650(isAI)` (+0xDC) and
  `dice::physics::CharacterEntity::setAI` (`0xD50E00`, +0xDD; forces +0xDE = 0; asserts on true->false).
  In the step (`0xD60B69`) an AI character replaces throttle/strafe with values projected from the Havok proxy's
  velocity (proxy = phys+0x40, velocity at proxy+0x10) - velocity-driven, as for single-player AI. AI characters also
  get 0 step-up height (`0xD34530`). 0.6.4 writes +0xDD = 0, +0xDE = 1 (only if phys+0xB8 AI rigid body == 0) and steps;
  fallback: AI mode + writing proxy velocity.
- Spawn assert "AI already spawned from the spawnPoint" (`0x876AA2`, ServerSoldierSpawnEntity.cpp 601):
  its "Ignore Always" byte is `0x19CF48B`; Phoenix sets it after checking the code bytes.

## Weapon unlocks
- `SoldierWeaponUnlockData` (12 bytes): WeaponSlot +0, Weapon +4 (SoldierWeaponAsset), IsBundledWithSelectable +8, IsSelectable +9.
- Soldier weapon setup (`~0x7EE080`): non-selectable, non-bundled entries are given per slot (last one wins) if
  `0x7FDE20(player, [weapon+0x64])` passes; then each selected unlock (player list +0xD28, filled by selectKit from
  pair resource ids) that equals an entry's Weapon is given in that slot, plus bundled entries with the same key name.
- `0x7FDE20`: key null or key+0x3C == 0 -> allowed; else bit key+0x4C of the player's unlock bitset
  (+0x168: words +0x16C, bits +0x174, word count +0x178). `ServerPlayer::setUnlocks` 0x5F96A0 ORs into it.
  AI players have the bitset allocated but empty; Phoenix 0.7 sets all bits in place.
- Resource tables: id->object hash at resources+0 (buckets +4, count +8, node {id, object, next}); object->id at +0x20.
- `ServerPlayer::setSquadId` 0x61C0A0 (player+0xAC, 24 = none).

## Rays
`physics ray query` `0x741510`, thiscall on context `+4`:
`(ident, mode=1, from, to, hits[], maxHits, 0x20000000, mask, exclude=NULL)` → hit count; hit = 0x40 bytes
(position `+0`, normal `+0x10`, entity `+0x30`). Masks from the AI's collideLineWithWorld (`0x65A050`):
kind 1 = `0x7E`, kind 2 = `0x38`. `0x7E` hits terrain but misses many buildings and decks; `0x38` sees
static geometry. Phoenix casts both and keeps the nearest hit.

## Capture points
`ServerCapturePointEntity` (vtable `0x17572E8`, 0xB4 bytes): data `+0xC` (CapturePointEntityData), owner team
`+0x7C`, progress `+0x98`, capturing team `+0xA8`, active `byte +0x26 bit 3`, world transform via `0x70DAF0`.
Data: radius `+0x11C` (0 on the shipped conquest flags; Phoenix uses 15 m), name `+0x124`, location `+0xCC`, index `+0xA4`, only-takeable-by `+0x84`, locked `+0x13A`.
Phoenix finds the entities by scanning committed memory for the vtable on a worker thread and validates them
on the simulation thread.

## Spawn point AI claim
A spawn entity keeps a weak reference to the last soldier it spawned at `+0x9C` (holder − 4 = soldier,
soldier `+0x240` = player). Spawning another AI there while it is set asserts
`AI already spawned from the spawnPoint` (ServerSoldierSpawnEntity.cpp 601). Phoenix clears it for dead/AI
claims and adds a 1.6 s per-spawn cooldown.

## Reflection
Type records in the `typeinfo` section, field tables in `fieldinf` (`{name, flags, type, elemType, offset, 0}`,
0x18 bytes), registered by `0x55D490(static, super, ctor, fields)`. `reflection.json` in this folder is the dump
(963 types).

## Character state (0.7.1)
- `[[phys+0x44]+0x10]` = character state, names at `0x1925480`: 0 OnGround, 1 Jumping, 2 InAir, 3 Climbing,
  4 Falling, 5 Parachute, 6 Swimming. Getters `0xD33E80` (+0x60), `0xD33E90` (+0x64), `0xD33F20` ([+0x44]+0x3C).
- Live 0.7.0: pinned bots showed proxy velocity y -0.63 with flags dc..df 1 0 1 0 and every input ignored for
  minutes. 0.7.1 logs the state when a bot is pinned and frees it by switching to AI mode for 0.45 s and writing a
  proxy velocity of 4.5 m/s away from the obstacle with +4 m/s up; after 30 s it is redeployed (leaveGame).

## Vehicles (0.7.1)
- `ServerVehicleEntity` primary vtable `0x17475C0` (getType `0x7B4A30` -> `0x209E480`). Data at `+0xC`
  (VehicleEntityData: MaxPlayersInVehicle `+0x20C`, NameSid `+0x210`). Entries vector `+0x90/+0x94`,
  entry `+0xE8` = occupant player. vt+0xF8 = entry count, vt+0x104 `0x7D1E20` = isAllowedToEnter(player, idx)
  (thiscall, ret 8; checks `0x7BFA20` then flags), `0x7B4720(player)` = first allowed entry.
- `ServerPlayer::enterVehicle` `0x64B800` thiscall(player, vehicle, entryIdx), ret 8 (kicks AI occupants for humans).
  Leaving: `0x645080(player, 0)` - what ChangeVehicle (16) does inside a vehicle (player update `0x653A3B`).
  ChangeEntry (17) -> `0x63A2D0`, SelectEntry 18+ -> `0x63A450`. On foot, holding ChangeVehicle runs
  `ServerPlayer::entityInteract` `0x64BBA0` on the interaction target `player+0xC98`.
- Player controlled entity `+0xC6C` (vehicle while inside); the soldier stays at `+0xC3C`; entry `+0xC5C` (0 on foot).
- Vehicles take the player's input in `prePhysicsUpdateEntry` `0x7BF600` (AI input is forwarded; no network step).
  Which action steers (Yaw/Strafe/Roll) and its sign are measured by the bots' driving test and logged.
- Phoenix finds vehicles with the same memory scan as the flags (both vtables in one pass, every 20 s).

## Live 0.7.1 findings
- The 0.6.3 double-step guard (bot speed > 6.5 m/s on foot for 45 ticks -> stop Phoenix's character step) fired
  from a bot carried by a vehicle (12.9 m/s). With the step off, bots stood "in air" (state 2, velocity 0) and the
  unpin hop (+4 m/s up) lifted them steadily - "walking in the sky". The step is required on this server; 0.7.2
  only logs speed.
- Vehicle names (NameSid, char*): 'Abrams', 'Bradley', 'BMD-3', 'HMG XM312', 'F18' (1 seat, airstrike), 'dot';
  several unnamed (2 and 4 seats at the RU base). Driving test: Throttle moves all three tanks/APCs, model forward
  +z, Yaw steers with +1 turning towards -heading.

## 0.8.0 notes
- Selected unlocks: player+0xD28 intrusive list {next, prev} of 12-byte nodes {next, prev, asset}; selectKit (0x625670)
  rebuilds it from (category, resourceId) pairs via resource_by_id, nodes from the EASTL allocator 0x432870
  (cdecl: size, "EASTL", 0, 0, allocator.h, 0xE9), refcounted assets (vt[0] addRef, vt[1] release); the clear
  0x619B80 releases and frees with 0x41B560. Phoenix appends weapon assets that have no resource id the same way.
- With every unlock bit set the soldier weapon setup gives the LAST unlocked non-selectable entry per slot
  (USAS-12 for primaries) unless a selected unlock overrides it.
- Players on the network: ServerPlayerManager ctor (0x62D9F0) makes manager+0x70, one 0x1C ServerPlayerManagerPlayer
  ghost (vtable 0x1710330, ghostWrite 0x5F99E0) per player id; registerPlayer (0x63A660) stores the player at +0x14
  for AI too, copies the name to player+0xC2C and marks the player ghost (player+0xC0C) dirty bit 2 (name).
  ServerPlayer::ghostWrite 0x624AA0 writes id, name, team, flags. isAI = player+0xD84 (setAI 0x5F97E0).
- CapturePoint attacker (+0xA8) stays 0 while a neutral flag is being captured; the progress (+0x98) moves.
- Level names: Levels/MP_001 Panama Canal, MP_003 Laguna Alta, MP_005 Atacama Desert, MP_006CQ Arica Harbor,
  MP_007 White Pass, MP_008CQ Nelson Bay, MP_009CQ Laguna Presa, MP_012CQ Port Valdez, BC1_OASIS_CQ Oasis,
  BC1_HARVEST_DAY_CQ Harvest Day, MP_SP_005CQ Heavy Metal, NAM_MP_002CQ Vantage Point, NAM_MP_003CQ Hill 137,
  NAM_MP_005CQ Cao Son Temple, NAM_MP_006CQ Phu Bai Valley, NAM_MP_007CQ Operation Hastings. Level archives
  (dist/linux/levels/*/level-00.fbrb: 'FbRB' + BE header length + gzip header + gzip body) hold no AI nav data.

## 0.8.1 notes
- Soldier weapon setup (0x7EDFD0): loop 1 gives non-selectable, non-bundled kit entries per slot if the unlock key
  [[entry.Weapon+0x50]+0x64] passes 0x7FDE20 (last wins); loop 2 walks the soldier's selected list (+0x2C8, from
  player+0xD28) and compares each listed object with entry.Weapon+0x50 (SoldierWeaponData) - so the list holds weapon
  DATA objects, not SoldierWeaponAssets.
- Kit slots: 0 primary, 1 sidearm, 2 selectable gadget (40 mm GL / rocket / defibrillator / mortar strike, plus AT mine
  choices), 5 fixed gadget (ammo / repair tool / medkit / C4 "M112"?). DynamicGadget1 (35) = slot 5 (live: engineer
  drill, recon C4). Slot 2 assumed on GrenadeLauncher (33).
- Online identity: player+0x18 (setOnlineId 0x5F94E0) and player+0xB0 {u64 id; char name[0x81]} (0x5F9580, marks the
  player ghost dirty bit 8; ServerPlayer::ghostWrite sends it with 0x6A1E50). The client connection (0x768EE1) sets
  both, plus +0x140 (0x5F95E0), +0xB98/+0xD98 (0x30 bytes each).

## 0.9.0 notes
- Live 0.8.1: weapons from the selected list now applied (M60, SVU, Carl Gustaf, UMP...). Kit gadget slots by name:
  US Assault slot 2 = XM320 variants (40 mm / shotgun / smoke, non-selectable bundles); Demolition 2* = Carl Gustaf,
  M136, AT mine, 5 = PowerTool; Support 2* = Defibrillator, 5 = MedKit; Recon 2* = MortarStrike, 5 = MotionSensor.
  Scoreboard still empty with the online identity set - client-side filter (BFBC2Game.exe is a stripped release
  build: no class/assert strings).
- Arica Harbor (MP_006CQ) Conquest has only quad bikes (+ UAV), per StrategyWiki.
- nav.c additions: nav_set_danger (soldier path cost x(1 + d/48)), nav_cut_towards (permanent link cut).

## 1.0.0 notes
- Vehicle spawner entities: vtable 0x1757400 (getType 0x835820 -> class 0x209F680), spawned vehicles vector at
  +0x370/+0x374. VehicleSpawnEntityData: SpawnDelay +0x80, InitializedSpawnDelay +0x84, Team +0x88, Amount +0x8c,
  BotBailOutDelay +0xa8, BotBailWhenHealthBelow +0xb0, MaxVehicles +0xc0, Vehicle blueprint +0xd8,
  Vehicles +0xdc, AutoSpawn +0x105, SpawnVehicleFromBeginning +0x108. world.c logs one line per spawner at load.
- Vehicle transform rows: right = m[0..2], up = m[4..6], forward = m[8..10]; pitch = asin(fwd.y),
  roll = asin(right.y), heading = atan2(fwd.x, fwd.z).
- Helicopter inputs are unknown on the server, so the pilot calibrates: Throttle as collective (+/-), then
  Pitch, Roll (fallback Strafe) and Yaw (fallback Strafe) at +0.6/+0.8 for 0.6 s each at 14 m, recording the sign of
  the attitude/heading change. Calibration is per vehicle data pointer (shared by all helicopters of a type).
- Ground clearance for helicopters: ray from origin-0.5 m down 300 m; when that starts under the ground (on the
  pad) a short ray from +1 m.
- Bug in 0.7-0.9: vehEnterUntil (the boarding deadline) was not cleared on entering, so vehicle_brain's
  "waiting to retry an exit" check froze the controls for the last second of the deadline.

Vehicles per map (Conquest), from StrategyWiki's BC2 vehicle and map pages:

| Map | US | RU | Neutral |
|---|---|---|---|
| Panama Canal (MP_001) | M1A2, M3A3 | T-90, BMD-3 | quads, UAV |
| Laguna Alta (MP_003) | M3A3 | BMD-3 | quads, UAV |
| Atacama Desert (MP_005) | M1A2, HMMWV, AH-64 | T-90, Cobra, Mi-28 | quads, UAV |
| Arica Harbor (MP_006CQ) | (M1A2?) | (T-90?) | quads, UAV - sources disagree; check the spawner log |
| White Pass (MP_007) | M3A3 | BMD-3 | quads, UAV |
| Nelson Bay (MP_008CQ) | - | - | quads, UAV |
| Laguna Presa (MP_009CQ) | - | BMD-3 | quads, PWC, UAV |
| Port Valdez (MP_012CQ) | AH-64 | Mi-28 | CAV, quads, UAV |
| Oasis (BC1_OASIS) | M1A2, HMMWV | T-90, Cobra | quads, PWC, UAV |
| Harvest Day (BC1_HARVEST_DAY) | M1A2 | T-90 | CAV, quads, UAV |
| Heavy Metal (MP_SP_005) | M1A2, AH-64, UH-60 | T-90, Mi-28, Mi-24 | CAV, quads, UAV |

## 1.1.0 notes
- Live 1.0 (Atacama, MP_005): 41 spawners. Names come from the spawner blueprint (Su39 x13 = jets, auto, 0 out;
  Mi28/AH64 "manual" spawners at the map edge = unused). Vehicle NameSid is empty for most vehicles. The AH-64 flew
  (calibration: Pitch -0.07, Roll +0.07, Yaw -0.10 per test push - small but enough).
- Spawned-vehicle list of a VehicleSpawnEntity at +0x370/+0x374 holds the vehicle entity pointers; world.c maps
  vehicles to spawners through it (nearest spawner within 12 m as fallback) for name, kind and team.
- Turrets: turret.c scans the vehicle object (0x800 bytes), objects it points to (0x400) and their pointees (0x200) for
  4x4 transforms (unit, orthogonal rows at 16-byte steps, translation within 12 m of the vehicle = world, or under
  10 m = local). Paths keep the smallest last offset; matrices inside the vehicle object come last. Per seat a test
  moves CameraYaw/Yaw/Roll (+0.6, then -0.6 to confirm) and CameraPitch/Pitch, after a 0.4 s settle that drops
  candidates moving by themselves. Calibration is per (vehicle data, seat). Aim is a PI loop on the angle relative to
  the hull frame; a path is re-verified on another instance by checking that the turret follows the command.
- Input actions 9/10 are EIACameraPitch / EIACameraYaw (analog). The tank hull steers with EIAYaw (live 1.0 test).

## 1.2.0 notes
- Live 1.1 (Atacama): vehicle names from spawners worked at load ('T90', 'Mi28', 'Cobra', ...), but a vehicle >12 m from
  its spawner at a later 20 s rescan lost name/kind/team (spawner +0x370 list did not contain the entity - it is not a
  plain entity-pointer list, or the vehicle is wrapped). Now: identity cache per (entity, data) for the level, type
  name per data pointer; DriveCalib.air set when heli_brain runs.
- Live 1.1 turret tests: 186-270 matrix candidates per vehicle; seat 0 'no input turned a turret' with CameraYaw/Roll
  (Yaw = steering) on the tanks; one seat found Roll (+0x60, depth 2). Added the VIEW pseudo-input (ST_YAW/ST_PITCH of the
  input state) and a three-push confirmation.
- Damage boost: health float at soldier+0x34 (BC2ObservedPlayer.controllable+0x34) written when a human loses health
  while a bot targets them; skipped when the result would be <= 1.

## 1.3.0 notes
- Live 1.2 (Nelson Bay, Port Valdez): character state 6 = swimming (60 "in the air" redeploys at y=171, the water surface).
  Turrets: Abrams seat 0 = Roll (-1) + Pitch (+1), T-90 seat 0 = Roll (-1), AH-64 seat 1 = Roll (-1), another tank Pitch
  (-1). Ground controls identical on every type: Throttle +z, Yaw steers, +1 -> -heading. Helicopters (AH-64, Mi-28):
  Throttle +1 lifts, +Pitch noses down (-0.06..-0.07 per test push), +Roll right side up (+0.06..+0.09), +Yaw -> -heading.
  These are the presets now (verified on the move; a helicopter that does not turn with full rudder for 3 s, or crashes,
  runs the full test).
- reflection: ProjectileEntityData.InitialSpeed +156, MissileEntityData.Gravity +208 / ApplyGravityWhenUnguided +272
  (not read yet - the rocket speed 140 m/s is an estimate).

## 1.5.0 findings
- `ServerSoldierEntity::onKilled` `0x7F57D0`: `cmp byte [player+0xD84],0` at `0x7F5804`, `je +0x15` at `0x7F580B` skips
  `leaveGame` (`0x64C130`) + `deletePlayer` (`0x62DE20`) for humans. KeepBotPlayers patches `74` -> `EB` so AI players survive
  death; restored on stop.
- Vehicle input `Brake` (2) reverses a slow ground vehicle (live 1.4: a T-54 held on Brake backed 37 m). Stop with Throttle.
- `ServerInteractionEntity` vtable `0x1746C68` (getType `0x7B5DD0` -> typeinfo `0x209E758`, static info `0x209DF48`, size 0x94).
  `ServerExplosionEntity` `0x1749770`. Entity data at +0x0C, active flag `+0x26` bit 3, transform `+0x28` as other entities.
  GameInteractionEntityData: TeamId +160, HoldToInteractTime +164, StaticModel +168, InteractionEntityType +172,
  IsHumanAllowed +184; InteractionEntityData: MaxUses +128, UseWithinAngle +136, UseWithinRadius +144.
- `ServerAmmoCrateEntity` vtable `0x17478F8` is a resupply crate (Radius / RefillDelay), not the M-COM.
- Vehicle health: read at entity +0x34 (ServerControllableEntity, as the soldier's) - to be confirmed in the log.
- Vietnam engineer kits list only `NAM_At_mine` for slot 2; leaving slot 2 unselected keeps the kit's launcher.

## 1.5.1 findings
- `ServerPlayer::update` (`0x6539B0`) returns at once for AI players (`cmp byte [this+0xD84],0 / jne`). It runs
  `entityInteract` (`0x64BBA0`), `ammoCrateInteract` (`0x61BE10`) and the SelectEntry inputs - none of it happens for bots.
- `entityInteract`: interaction in reach at `+0xC98` (weak ref: entity = [[+0xC98]] - 4), checked with getType `0x209E758`;
  hold time `0x7CC2F0` (data +164); Interact = input 27 held, accumulated in `+0xD7C`; then
  `ServerInteractionEntity::interact(soldier)` `0x7D98B0`; type `0x7CC2E0` (data +172): 2 sets `+0xCA4` (armed), 3 sets `+0xCA5` (defused).
  Needs the player's connection `+0xC18` for the HUD prompt, so Phoenix calls `interact` directly.
- `interact`: active (+0x26 bit 3), MaxUses (data +0x80) vs uses (+0x84), DelayBetweenUses (data +0xB4) vs last use (+0x80),
  soldier animation (data +0x8C), then fires its event (+0x92 = 1).

## 1.6.0 findings
- Rush levels keep live capture points: each side's spawn base (Valparaiso: "Fishing Port" over the harbour, "US Base",
  locked), moved forward per stage. They are not objectives - the M-COM interactions are.
- `ServerPlayer +0xC5C` (observation `entry`) is a pointer to the entry component while in a vehicle, 0 on foot - not a
  seat index. Driver test: controlled entity (`+0xC6C`) is a vehicle and its seat 0 occupant (`[[v+0x90]]+0xE8`) is the player.
- Weapon switching: soldier `+0x2D4` = WeaponSwitching, current index `+0x14C`, previous `+0x150`; weapons vector
  `+0x2D8/+0x2DC`. `setCurrentWeaponIndex` `0x7BE4D0` (`81 ec a8 00 00 00 56 8b f1 83 be 90 02 00 00 00`), index 0 =
  primary. Input `SwitchPrimaryWeapon` steps to the next weapon, which is how 1.5 engineers ended up with the repair tool.
- BF-Portable backend: accounts in `bf.a_emu_playerinfo`, password = md5(trim(htmlspecialchars(pw))) (website
  `classes/Auth.class.php`), BC2 logs in with the e-mail. Live BC2 servers in `a_bf_gameservers_bc2`
  (`client_type='rome_server'`, `last` = unix time of the last update). The client's emulator address is `host=` in
  `bfbc2.ini` next to `BFBC2Game.exe` (dinput8 hook).

## 1.6.3 findings
- `ServerInteractionEntity::interact` `0x7D98B0` refusals after the uses / delay checks: `0x7D994F jne` (soldier+0xC0 vfunc
  +0x28 true), `0x7D998A jne` (`0x6131D0`: scans entities of type `0x209F7D0` - team / area test with the soldier's
  position), `0x7D99A4` (data+0x8C animation: `0x7BEF70(soldier, anim, e+0x7C)` failed - sets e+0x91). Live 1.6.1: every
  bot call refused with the interaction on, 0 uses, no delay. 1.6.3 retries with those three jumps patched
  (`75 60`->`90 90`, `75 25`->`90 90`, `74 33`->`EB 33`) for the one call, restored right after.
- BC2 has no door entity (strings: no ServerDoor); doors are destructible panels - rays hit them as wall.

## 1.6.6 - killing a soldier, and the level's roads
- `leaveGame` (`0x64C130`) on a player with a LIVING soldier only detaches it (`releaseLevelDependencies`): the soldier
  entity stays in the world with no player - no input, no AI; hit reactions play but it takes no damage. A soldier
  detached mid-hop keeps its upward velocity. Never leaveGame a player whose soldier lives.
- Suicide: `ServerConnection::onReqSuicide` `0x7470D0` -> `[player vtable+0x30]` (the controllable) -> `jmp 0x7D7FB0`.
  `0x7D7FB0` is thiscall, no arguments, plain `ret`: it builds a damage info of 100.0 (`[0x18DDDEC]`, KILL_MSG_DEATH)
  and calls the entity's damage virtual (soldier vtable `0x1749F70` + 0xD0 = `0x7F7310`). The damage function does not
  call `onKilled` (soldier vtable + 0x98 = `0x7F57D0`) itself - treat the death as asynchronous (next update).
  Fingerprint `55 8B EC 83 E4 F0 81 EC CC 00 00 00 D9 05 EC DD`. Soldier health float at `+0x34`, its player at `+0x240`.
- Data-container vtables (getType at slot 2, slots 0/1 `0x552390` / `0x555A50` like SoldierEntityData `0x1859B78`):
  RoadData `0x17AA1AC` (getType `0x1661680`), VectorShapeData `0x17AA174`, TerrainSplineData `0x17E442C`,
  TerrainSplinePointData `0x17E4448`, TerrainSplineDecal `0x17E46B4`, TerrainGroupDecal `0x17E467C`,
  TerrainData `0x17E44D4`, TerrainDecal `0x17E4660`, TerrainDecalMaterial `0x17E4600`.
- Reflection offsets are exact. Arrays: a reference array (elements are object pointers) is 0x14 bytes with the count
  at `+0xC` and the element pointer at `+0x10`; a struct array is 0x10 bytes, begin `+8`, end `+0xC`.
  CString fields are `{begin, end, capacity}` (12 bytes).
  - RoadData: GameObjectData Name `+12`, Shape (VectorShapeData*) `+48`, RoadPoints (struct array) `+60`.
  - VectorShapeData: Points (struct array of Vec3, 16-byte stride) `+52`, IsClosed `+69`, IsGrounded `+68`.
  - TerrainSplineData: Points (reference array of TerrainSplinePointData) `+60`, Planes `+80`; point Position `+16`.
  - TerrainSplineDecal: Name `+16`, Material (TerrainDecalMaterial, Asset Name `+12`) `+28`, Enable `+33`, Spline `+40`.
- Not yet confirmed live: whether these instances exist in the dedicated server's memory and whether the points are in
  world space. Phoenix checks each line against the drive map before using it and logs the result (`roads:`).
