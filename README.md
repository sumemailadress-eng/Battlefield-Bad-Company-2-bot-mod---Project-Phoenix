# Project Phoenix — bots for Battlefield: Bad Company 2

Phoenix adds AI soldiers to a **BF-Portable Bad Company 2 dedicated server**, so you can play full matches offline or on a LAN.
It doesn't change any game files. A small DLL is loaded into the running server, where it creates native AI players
and drives them through the same input path the game's own AI uses. Bots capture flags, arm and defuse M-COMs, drive
tanks, jeeps, quads and boats, fly helicopters, man stationary guns, revive, repair and resupply. They also learn each map
as they play it.

Current version: **1.6.9** (release 1.3). The full history is in [CHANGELOG.md](CHANGELOG.md), and the engine reverse-engineering notes
are in [docs/ENGINE-NOTES.md](docs/ENGINE-NOTES.md).

> Phoenix is a fan project for preserving and playing a game whose official servers are gone. It needs your own
> copy of Bad Company 2 and a BF-Portable server setup. Battlefield and Bad Company are trademarks of Electronic Arts;
> this project isn't affiliated with or endorsed by EA or DICE.

---

## Features

### Game modes
- **Conquest**: a team director gives every bot a job. Bots attack the nearest flags in groups, defend, guard and retake
  lost flags. They stay in the capture zone until the flag falls, then move on (1.6.8: a vehicle with no route out of a
  freshly taken flag no longer holds its crew there - bots walk on to the next flag).
- **Rush**: attackers arm M-COMs and defenders defuse them. Phoenix finds the M-COM entities in server memory, works out
  which stage is active, and plans routes **into buildings and up stairs** to M-COMs placed indoors. It surveys each
  building, finds doorways and doors, and knifes doors open.
- **Squad Rush and Squad Deathmatch**: bots fight in their squads (squads 3 and 4 use the kits of teams 1 and 2).
- Hardcore and the Vietnam maps work as well.

### Soldiers
- All four kits (assault, engineer, medic, recon), with a random weapon per life taken from that kit's unlock list.
  `UnlockAll` gives bots every unlock.
- Line-of-sight checks with the server's own ray casts, reaction time, aim settling and bursts per weapon class. Bots
  aim down sights, strafe, reload, throw grenades, use the knife up close and never fire through teammates.
- Equipment: medics revive and drop med kits, assault drops ammo, engineers repair friendly vehicles (they only pull out
  the repair tool to repair) and fire launchers at armour and helicopters, and recon calls mortar strikes.
- Fireteams and real squads: bots move and fight in small groups.
- **Always on a route**: soldiers plan routes to every goal (a flag, an M-COM, a teammate) at any distance unless the
  straight line is already clear, and get back onto their route after a fight, a side-step or a push - re-planning
  from where they stand when the old route is out of reach.
- Recovery when stuck: jump, crouch, knife through fences and doors, side-step or detour. A bot that is truly pinned is
  killed cleanly and respawns.

### Navigation
- Phoenix builds a **navigation grid** for each map by ray-casting the server's physics world. A* plans the routes. The
  first visit to a map takes a few minutes, and the grid is saved after that in the shared navcache,
  `<server folder>\PhoenixNavcache` (1.6.9; `NavcacheDir=` in `phoenix.ini` picks another folder).
- Stairs, ladders, doors, water and the danger zones where bots keep dying. Swimmers head for the best bank they can
  climb within 120 m, on the way to their goal, and take a route again once they are out (1.6.9).
- A bot standing on a part of the grid that isn't connected to the rest walks back to connected ground, however far,
  and takes a route from there (1.6.9).
- **Learning**: stuck spots and the move that got a bot past each one, deaths and kills, good sniper and window spots,
  and the routes human players walk and drive. All of it is saved per map in `PhoenixNavcache\<map>.learn`.

### Vehicles
- Bots drive ground vehicles and boats, fly helicopters (with a geofence so they don't leave the map) and use
  stationary weapons. Each vehicle type's controls and turret are calibrated automatically the first time a bot uses it.
- **Road detection** (1.6.6): Phoenix reads the level's own road data (road objects and terrain road splines) from
  server memory. It checks every road line against the ground and makes the road cells the preferred lanes. Routes
  keep to them (1.6.9): the planner weighs the road properly, and a smoothed route may not cut a corner that costs more
  than the road it replaces.
- **Vehicle scan** (1.6.5): a one-time ray scan per map that finds kerbs, road dividers, steps and low walls a wheel
  can't cross. The planner never routes across them.
- **Routing** (1.6.6): routes always start and end in the same connected drivable area. If a flag is off the road, the
  route stops short and the crew walks the rest. Vehicles never drive blind.
- **Path following** (1.6.6): drivers follow the route line instead of cutting corners, steer back when they drift, and
  slow down for bends.
- **Hazard probes** (1.6.6): rays ahead of the bumper detect barrels, road blockers, stairs, steps and drops. The spot is
  cut from the map, along the whole line of a barrier (1.6.9), and the vehicle stops and re-routes before it hits anything.
- **Crews** (1.6.8): when the driver gets out, a passenger moves over and drives on, or the crew gets out at once.
- When a vehicle is boxed in, it backs out the way it came. Spots where vehicles got stuck are remembered across rounds.
- **Your driving teaches them**: cells you drive are preferred routes. You can also **draw routes** for vehicles in the
  learning map.

### Tools
- **PhoenixLauncher.exe**: one window to start the backend (XAMPP MySQL/Apache, redirector, emulator), the server, the
  bots and the game. **Stop All** shuts everything down safely. It also has bot settings, a map-list maker, a
  **Players** window that registers accounts on your BF-Portable backend, and a **LAN games** window that finds servers
  on your network and joins them.
- **LearnViewer.html**: a map of what the bots learned. It shows deaths, kills, human routes, stuck spots, the map's
  roads and where vehicles got stuck, and you can draw vehicle routes in it.
- Logs for every session go to `reports\<date-time>\runtime.log`. Windows user names are written as `~`.

---

## Requirements

- Windows 10 or 11.
- Bad Company 2 and a working **BF-Portable** BC2 setup (server `Frost.Game.Main_Win32_Final.exe`, the BFBC2 emulator
  backend and XAMPP).
- Phoenix is built for one specific server build. It checks the server executable's SHA-256 and the engine functions it
  uses before it does anything, and refuses to attach to a different build.

## Install

1. Download the latest release zip from the [Releases page](https://github.com/sumemailadress-eng/Battlefield-Bad-Company-2-bot-mod---Project-Phoenix/releases)
   (the `Project-Phoenix-BC2-...zip` under **Assets**) and extract it anywhere, for example next to your BF-Portable
   folder. That zip has `PhoenixLauncher.exe`, `bin\PhoenixBC2.dll` and `bin\PhoenixAttach.exe` ready to run.
   **Code → Download ZIP** and the "Source code" archives are the source only - no programs; build them yourself
   (below) or use the release zip.
   The release zip includes a `navcache` folder with the maps already surveyed and what the bots learned on them, so
   the first round on those maps starts straight away. It is merged into your `PhoenixNavcache` when the bots attach.
   **Updating from an older version?** Nothing to copy. Since 1.6.9 the maps already surveyed and everything the bots
   learned are kept in one folder next to your server, `<server folder>\PhoenixNavcache` (for example
   `D:\BF-Portable\Battlefield_bc2_Server\PhoenixNavcache`), and every install uses it. The first time a new install
   attaches, the `navcache` folder of that install is merged in. To bring in an older install's `navcache`, copy
   its files into `PhoenixNavcache`, or start the bots once from that install.
2. If you want the bots to show up on the scoreboard with names, run `backend\Add-Bot-Accounts.bat` once with MySQL
   running. It adds one backend account per bot.
3. Open `phoenix.ini` and set the number of bots and the difficulty, or set them in the launcher.

## Play

**With the launcher (easiest)**
1. Run `PhoenixLauncher.exe`.
2. Point it at your server (BF-Portable's `BFBC2.bat` or the server .exe) and your game. **Find XAMPP** fills in the
   backend programs.
3. Press **Start All**. This starts the backend, then the server, then the bots, and after the delay you set, the game.
4. Join your server. Bots join both teams a few seconds after the level is running.
5. When you're done, press **Stop All**.

**By hand**
1. Start the BC2 server the usual way.
2. Run `Start-Bots.bat`. It waits for the server. Allow the administrator prompt, because the server runs as
   administrator.
3. Join the server.
4. `Stop-Bots.bat` removes every bot. To load a new Phoenix version, restart the server first.

**The first round on a new map** is slower to get going because the bots survey the map: the navigation grid, the
stair check and the vehicle scan. Everything is saved in `PhoenixNavcache`, so the next visit starts straight away.
Until the grid is ready, bots don't drive ground vehicles.

## Settings (`phoenix.ini`)

| Setting | Default | What it does |
|---|---|---|
| `BotsPerTeam` | 15 | Bots on each team (humans are extra) |
| `FillTeamsTo` | 0 | Alternatively, fill each team up to this many players |
| `Difficulty` | 2 | 1 easy, 2 normal, 3 hard |
| `AimError` | 1.0 | Lower is more accurate |
| `RespawnDelay` | 6 | Seconds before a dead bot redeploys |
| `Vehicles` | 2 | 0 off, 1 passengers only, 2 bots drive |
| `Helicopters`, `Boats`, `StationaryWeapons` | 1 | Let bots use them |
| `Learning`, `LearnFromHumans` | 1 | Learn from play, and from human players' routes |
| `RushObjectives` | 1 | Arm and defuse M-COMs in Rush |
| `FrozenRespawn` | 30 | Seconds pinned in place before a bot is respawned |
| `KeepBotPlayers` | 1 | Dead bots keep their player, so scores add up |
| `NavcacheDir` | (empty) | Where maps and learning are kept. Empty means `<server folder>\PhoenixNavcache` |

Every option is commented in the file itself. The settings are read when the bots attach.

## Draw routes for vehicles

1. In the launcher, click **Bot learning...**, or open `LearnViewer.html` directly.
2. Click **Open navcache folder** and pick the `PhoenixNavcache` folder next to your server (the launcher names it).
3. Choose a map, click **Draw vehicle route**, click along the road, then **Finish route**, then **Save routes**. Save the
   file as `PhoenixNavcache\<map>.routes`.

Bots read the file again whenever it changes, even mid-round.

---

## Building from source

Phoenix is plain C, built with a 32-bit MinGW-w64 toolchain (gcc or clang), either on Linux or on Windows with MSYS2.

```sh
./build.sh          # bin/PhoenixBC2.dll, bin/PhoenixAttach.exe, PhoenixLauncher.exe, harness/phoenix_harness
CC=i686-w64-mingw32-gcc ./build.sh
```

Without MinGW, on Windows with LLVM clang and the Visual Studio 2022 Build Tools (static runtime, no extra DLLs):

```sh
./build-msvc.sh     # bin/PhoenixBC2.dll (set CLANG, VC, SDK, SDKVER if your versions differ)
```

| Folder | Contents |
|---|---|
| `runtime/` | The bot runtime: world reading, bot brains, vehicles, learning, roads, indoor routes, launcher |
| `src/` | Navigation grid and A* (`nav.c`), the player observer and the simulation-tick hook |
| `harness/` | An offline test harness. A mock BC2 server and game simulation run the real runtime on Linux |
| `viewer/` | The learning map (`learn-viewer.html`; `LearnViewer.html` is the standalone copy) |
| `backend/` | Bot accounts for the BF-Portable database |
| `docs/` | Engine notes: addresses, structures and what was learned about the BC2 server |

The harness runs whole matches without the game. Environment switches select scenarios: `RUSH`, `HOUSE`, `TOWN`,
`DIVIDER`, `BARRELS`, `COURTYARD`, `LATEBLOCK`, `LOWBLOCK`, `ROADS`, `PINTEST`, `HUMANDRIVE`, `WATERTEST`,
`LEVELCHANGE=<s>`, `STOPAT=<s>`, `VERBOSE`.

```sh
QUIET=1 RUSH=1 HOUSE=1 ./harness/phoenix_harness 400
```

## Reporting problems

Attach the `reports\<date-time>\runtime.log` from the session. Every bot decision that matters is logged there: routes,
vehicles, stuck spots, Rush objectives and road detection.

## License

[PolyForm Noncommercial 1.0.0](LICENSE). You may use, change and share Phoenix for any noncommercial purpose.

## Credits

The navigation grid and parts of the bot behaviour are adapted from the author's Battlefield 3 bot mod (AltBotMod /
Project Caspian). Thanks to the BF-Portable project for keeping Bad Company 2 playable.
