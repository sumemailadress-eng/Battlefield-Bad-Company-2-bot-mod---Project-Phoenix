# Project Phoenix BC2 — changelog (newest first)

Phoenix adds AI soldiers to your BF-Portable BC2 dedicated server without changing any game files.
It loads into the running server, creates native AI players and drives them through the same input
the game's own AI used.

## Use it

**Easiest: PhoenixLauncher.exe.** Set the server (BF-Portable's `BFBC2.bat` or the server .exe) and the game once - they
are remembered in `launcher.ini` - choose the bot settings, and press **Start All**: server, bots and (after the delay
you set) the game. Start Server / Start Bots / Stop Bots / Start Game / Logs are there separately too. Bot settings are
written into `phoenix.ini`.

The launcher also starts the **backend** first: up to three programs in order, each with its own wait - XAMPP's `mysql_start.bat` and `apache_start.bat` (**Find XAMPP** fills them in) and the redirector / emulator. A program whose .exe is already running is skipped. **Stop All** removes the bots, closes the server and any backend .exe it started, then shuts XAMPP's MySQL / Apache down safely with XAMPP's own `xampp_stop.exe`.

By hand:
1. Start your BC2 server the usual way (BF-Portable / `BFBC2.bat`).
2. Double-click **Start-Bots.bat**. You can do this before or after the map loads; it waits for the
   server. If Windows asks for administrator permission, allow it (the server runs as administrator).
3. Join the server. Bots join both teams (15 v 15 by default) a few seconds after the level is running.
4. **Stop-Bots.bat** removes every bot. To load a new Phoenix version, restart the server first.

Settings are in **phoenix.ini** (bots per team, difficulty, respawn delay, navigation). They are read when
Start-Bots.bat attaches.

## What changed in 1.6.8 - off the first flag, vehicles on the roads, no crews left sitting

- **Bots no longer stay at the first flag they take.** The vehicle that spawns at a newly taken flag often had no route out ("A* exhausted after 1 nodes"). A bot got in, waited 25 s for a route, got out - and 6 s later it or a teammate got back in ("back to the ride we came in"). In the 1.6.7 log a whole team sat at CommCenter this way. Now a vehicle its crew left for want of a route (no drivable route, waited 25 s, no progress in 35 s) is left alone for 90 s, and the bots walk their route to the next flag.
- **Why there was no route: the vehicle's own hull.** The planner checks a vehicle route with physics rays that start at the vehicle - inside its own hull - so the first leg always looked obstructed and the edges round the start were blocked one by one. The 5.5 m around the vehicle are no longer ray-tested (the drive map already keeps it off walls).
- **Vehicles follow the roads.** 1.6.7 drove a straight line at the flag whenever open ground was drivable within 120 m - the most common vehicle route in the log was two points, the vehicle and the flag, across the fields beside the road. That short cut is now for the last 30 m only, and a smoothed route leg with one end on a road may leave it for 3 cells at most, so the route turns off the road where the planner turned off.
- **Passengers and gunners when the driver gets out.** Seat 0 could still name a driver who had left, so a crew sat in a parked vehicle until "the ride is not getting closer" 35 s later. A driver now counts only while the game has him controlling the vehicle. With no driver, a passenger moves over and drives when bots drive that vehicle and the flag is far enough; otherwise everyone gets out after 1.5 s.
- Built with clang (LLVM) + the Visual Studio 2022 Build Tools libraries (static runtime), see `build-msvc.sh`; the MinGW `build.sh` still works.

## What changed in 1.6.7 - always on a route

- **Back on the route after a fight.** Fighting (planted feet, strafing, a knife rush), a side-step or a push can leave a soldier metres off its route; 1.6.6 then walked straight at its old waypoint through whatever stood in between. Twice a second each bot now checks its route: a later waypoint that is closer and in clear sight is taken; more than 5 m off the current leg, or walled off from the next waypoint, and it plans a new route from where it stands ("move: Joe is 7 m off its route - a new route from here").
- **Routes at every distance.** 1.6.6 only planned routes to goals more than 6 m away and walked the last 6 m straight - into the wall between it and a flag point or an M-COM. Now a goal 1.5-6 m away is walked straight only when that straight line is the route (clear at knee and chest height, same floor); anything else gets a planned route, however close. In the test house the upstairs M-COM was armed in half the time.
- Vehicles already re-join their route line (1.6.6): a driver off the line steers back to it, and plans again when far off - also after a tank turns its hull onto a target.

## What changed in 1.6.6 - roads, vehicles that stay on them, no more ghost bots, the Atacama crash

**Ghost bots and soldiers floating into the sky - fixed.** A bot pinned in place (wedged, or standing on air) was "redeployed" with the engine's leaveGame, which only *detaches* a living soldier from its player. The soldier stayed in the world with nobody controlling it: standing still, no AI, hit reactions but no damage - and if it was detached in the middle of the little hop that frees pinned bots, it kept drifting upwards. One Panama Canal round left 109 of them. Phoenix now kills the soldier with the engine's own suicide (what the console's suicide does) and the bot respawns as the same player. Stop-Bots does the same: living bot soldiers are killed first (crews are taken out of their vehicles first), then the players are removed - no soldiers left behind.

**Crash on the change to Atacama - fixed.** The vehicle-spawner list had five labels for six kinds; the first stationary-gun spawner read past the table.

**Vehicle routing.**
- *Never drive blind.* 1.6.5 drove in a straight line at the flag whenever the planner found no route - 170 times in one round, and that is where most stairwells, dividers and alleys came from. Now a vehicle without a route waits for the planner (25 s at most) and its crew gets out if there is no road at all.
- *Connected drive areas.* Every drivable cell knows which connected area it belongs to. A route starts in the big area next to the vehicle and ends at the cell of *that* area closest to the flag. A flag in a courtyard or a building gets a route that stops short ("route ends 31 m short of it, the crew walks from there").
- *Partial routes.* A search that runs out of time still returns the route to the closest point it reached; the driver re-plans from its end.
- *No crossing a kerb or divider.* The vehicle scan's bad links are now blocked in the planner itself (1.6.5 only made the cells beside them dearer, so a route could still cross a divider).

**Driving the route.** Drivers follow the route *line* (pure pursuit): they aim at a point a speed-dependent distance ahead along the route, never past a sharp corner (they aim just beyond the corner instead of cutting across the inside), and only where the straight line to that point stays on the drive map. A driver that drifts more than 5 m off the line steers back to it slowly; 25 m off, it plans again from where it is.

**Hazards ahead of the bumper (wheeled vehicles).** Every quarter second: rays at 0.5 m over the ground ahead (barrels, road blockers, low walls - below the 1.2 m wall feelers) and the ground every 0.8 m for 5-10 m (steps, stairs, kerbs too high for the vehicle, drops). A hazard on the route within 7 m is cut from the drive map and the route planned round it *before* the bumper gets there ("Joe sees a low obstacle 6 m ahead of 'Vodnik' - 1 drive cell cut, new route").

**The map's own roads.** A BC2 level carries its roads as data: road objects (`RoadData`, the centre line the single-player AI drivers used) and terrain road decals painted along terrain splines. The server loads that data, so Phoenix now finds it with the same memory scan as the flags and vehicles, checks every line against the ground (a line that does not run over drivable ground for most of its length - a river, a cliff decal - is dropped) and makes the road cells the cheapest class after human-driven ones. The runtime log lists what was found (`roads: ...`), and the lines are saved as `navcache\<map>.roads`; the learning map draws them (layer *Map's roads*). Not every level may have this data on the server - the log says so, and vehicles then use the drive map, the vehicle scan, your driving and your drawn routes as before.

**Other.** Logs and reports no longer show the Windows user name (`C:\Users\~\`).

## What changed in 1.6.5 - a vehicle scan of every map, stuck spots remembered, backing out of alleys

**Did vehicles learn from getting stuck before? No - only for that round.** A vehicle stuck twice at a spot cut it from the drive map, but only in memory: the next round (or a server restart) it was back. Soldiers' stuck spots were saved; vehicles' were not.
- Now each spot is saved as `vstuck x y z` in the map's `.learn` file and cut from the drive map every round.
- The learning map shows these spots as crossed squares, with a "Where vehicles got stuck" list.

**Vehicle scan (roads and where a vehicle can really pass).** The drive map came from the soldiers' grid: flat, open 2.5 m cells. At that size it can't see what stops a wheel and not a soldier: a kerb, a road divider, a low wall, a flight of steps.
- Once per map, after the grid is ready, Phoenix now checks every link between two neighbouring drive cells with rays:
  - the ground every 30 cm along it (a rise of more than 0.4 m at once is a step, a kerb or a divider);
  - two rays at bumper and bonnet height (0.4 / 0.9 m).
- A thin board doesn't count, because a vehicle breaks a wooden fence. Concrete does count.
- A cell with two or more bad links leaves the drive map. A cell with one becomes the dearest kind: routes cross it only when there is no other way, and never on a short cut.
- The scan uses about 600 rays a tick, taking 20-40 s on a big map while bots play. It is saved as `navcache\<map>.dscan`, so it runs once per map. The log says `nav: vehicle scan done - N link(s) a wheel cannot cross`.
- Your driven and drawn routes always win over the scan.

**Backing out of alleys.** A stuck vehicle used to look for the freest way out across its front half and drive at it. In a dead end everything ahead is a wall, so it turned back and forth there.
- Every vehicle now drops a breadcrumb every 4 m.
- When it is boxed in, it reverses for up to 4 s with its tail towards where it was 10-20 m ago, then drives on. The log says `backing out the way it came`.

## What changed in 1.6.4 - draw the routes vehicles take, tank guns that point the right way

**Draw vehicle routes on the map.**
1. Open **Bot learning...** in the launcher, then **Open navcache folder**. Every map with a `.nav` is listed now, even one with nothing learned yet.
2. Pick the map and press **Draw vehicle route**. Click along the road from start to end. Drag to pan and scroll to zoom. **Undo point** (or Backspace) takes back the last click, **Finish route** (or Enter) ends it, and you can draw as many as you like.
3. Press **Save routes** and save `<map>.routes` into the `navcache` folder. In Chrome / Edge a Save dialog opens; other browsers download it and you move it there.

Bot drivers prefer a drawn route like one you drove: it costs 0.3 a metre against 1.0 for open ground, and it's about 7 m wide. Phoenix checks the file every 10 s, so you can draw while a round is running and the next routes use it. The side panel lists each route with **Show** and **Delete**. Phoenix never writes this file.

**Tank main guns aimed off.** Phoenix finds a turret by watching which part of the vehicle model turns when it tries the controls, then aims with that part's forward axis. On some models (the T-90 and Abrams on Panama Canal) that part's axis isn't the barrel's - it is a quarter or half turn off. So the gun was laid at the target with the wrong side, and fired sideways or behind. BC2 spawns turrets facing the hull's front, so where the part pointed before the test (to the nearest quarter turn) is now taken as "ahead". When there's an offset, the log says `turret part is turned N degrees from the gun - corrected`. Elevation gets the same correction.

## What changed in 1.6.3 - M-COMs that arm, buildings, stairs and doors

**The game refused every bot arm and defuse.** In your Arica Harbor and Atacama logs, every try at an M-COM came back "refused by the game" while the M-COM was on, unused and with no delay. That covers attackers at 1 m and defenders standing on an armed one.
- Past those checks the game's `interact` turns a player away for three more reasons:
  - a test on the soldier,
  - a test for areas where interaction is blocked,
  - the arming animation not starting for that soldier.
- All three are about a human's body and HUD. A bot that has held Interact at the M-COM for the full hold time is now let through: Phoenix asks again with those three checks skipped for that one call, then puts the game's code back.
- The log says which check stopped it ("arm animation would not start" or "soldier / area check") and "done (with the human-only checks skipped)" when the retry works.

**Ways into buildings (stairwells, upper floors, doors).** The level grid is 2.5 m a cell. A BC2 stairwell is about 1 m wide and turns back on itself, and a closed door is a panel the grid's rays see as wall. That's why ten White Pass defenders stood at the foot of the stairs under M-COM A (6.7 m up) with no route ("path 0/0").
- Phoenix now surveys the building itself at 0.5 m with rays, from the M-COM outwards. For each step it checks the floor (a stair step rises 0.55 m at most), head room and that no wall is in between. It keeps going until it is out under the open sky on the ground, then stores that way in.
- Every M-COM is surveyed when it comes on, and so is any spot within 45 m that the grid has no route to.
- A closed door is told apart from a thin wall: the panel is under 20 cm thick and set in something different (a frame, a thicker wall, or open space beside it). The survey goes through it only when there is no open way, and bots knife it open on the way in.
- Bots walk the way in step by step. They don't swerve round stair steps, which the obstacle checks took for walls. They stay off a stair's open side where they can, and climb each step before taking the next.
- A bot that makes no progress on the way in for 10 s falls back to the usual unsticking for 15 s.
- The log shows `indoor: way in to (...) found - from (...), N point(s), climbs X m, D door(s) to knife`, `indoor: X takes the surveyed way in` and `indoor: X knifes the door`.

**Helicopters stay over the battlefield.** The battlefield is the box round every flag, M-COM and spawn, plus 60 m. A helicopter nearing its edge turns on the spot back towards the middle, then flies in, instead of flying out until BC2 kills the pilot.

**Arming distance.** Bots arm from under 1.9 m (2.4 m once pressed against the crate). The M-COM's UseWithinRadius is 2 m, and 1.6 armed from up to 3.5 m.

## What changed in 1.6.2 - drivers keep to the road

- **The route planner was greedy for vehicles.** It weighted "straight at the goal" 1.5x over what the route costs, so a beeline over open ground beat a road every time, and road and human-route costs barely counted. Now that weight is 0.7, so the cheaper road (x0.5) and your routes (x0.3) win.
- **No more cutting corners off the road.** A route is smoothed into longer straight legs. 1.6.1 cut every bend straight across the field. Now a leg between two road cells must stay on road, and a leg between two cells you drove must stay on what you drove.
- **Straight drives only up close.** The "just drive straight there" shortcut is now used only up to 120 m (was 200 m); further than that, the vehicle plans a route.
- **Longer searches before a partial route.** Searches go on four times longer before settling for a part-way route.
- Phoenix can't see the game's road markings. A "road" is ground the grid measured as smooth. The roads it knows for sure are the ones you drive: every route you drive is preferred from then on, on every map.

## What changed in 1.6.1 - the Isla Inocentes crash

- **The server crashed about a minute into Isla Inocentes Rush.** When a bot swims, Phoenix marks that water on the map. 1.6.0 then threw away the vehicle route map and rebuilt it, but vehicle routes still being planned held on to the old one, and reading it crashed the server.
  - The water is now taken off the vehicle map where it is.
  - Any route still using a map that is thrown away is cancelled first.
  - The crash was reproduced offline with a memory checker and is gone in 1.6.1.
- **Only the current Rush stage.** Isla Inocentes has the M-COMs of all three stages switched on from the start, so 1.6.0 sent attackers to all six. Phoenix now remembers every M-COM it has seen this round, in order of distance from the attackers' spawns, and plays only the first stage that still has one standing (2 per stage, 1 in Squad Rush).
- **Stop All shuts MySQL down safely.** It runs XAMPP's own `xampp_stop.exe` (found next to `mysql_start.bat`), after the server and the emulator are closed, so the database is never killed mid-write.
- **Jet skis (PWC) are boats.** 1.6.0 tried to drive them on land routes.
- **Preferred human routes are used by every vehicle route.** The faster route planner had its own cost table, which still treated "humans drove here" like open ground.

## What changed in 1.6.0 - Rush that works, your driving, players and LAN games

**Rush: bots arm and defuse the M-COMs**
- Your Valparaiso log showed why nothing happened. On Rush maps the capture points are still "on": they are each side's spawn base, moving forward with every stage. 1.5.1 saw them, played Conquest on them, and never looked for the M-COMs. 1.6 ignores those bases in Rush and Squad Rush and always finds the M-COMs of the current stage.
- That also caused the swimming. The defenders' "flag" was Fishing Port, whose marker stands over the harbour. Defenders walked to it and into the water.
- An M-COM counts as in reach from 2.6 m, or from 3.5 m once the bot stops getting any closer (a crate or a wall in the way). Then the bot holds Interact for the arm or defuse time.
- When the game refuses an arm or defuse, the log says why: whether the interaction is on, how often it has been used, and its delay.

**Water**
- A swimming bot marks that water on the map's grid, and routes keep out of it from then on. It's saved in the .learn file.
- The bot then swims for the nearest bank it can climb. After 25 s in the water it is redeployed.
- A bot that keeps getting stuck at the water's edge is redeployed instead of wading round the harbour for 18 minutes (Dave did, in your log).

**Engineers: the repair tool is only out while repairing**
- After any gadget (repair tool, launcher, rockets, kit item), Phoenix selects the primary weapon directly. 1.5 pressed "switch weapon", which steps to the *next* weapon, and from the launcher the next one is the repair tool.
- A bot that has a gadget slot out with nothing to do with it gets its primary back within a second. With an enemy in sight it happens at once.
- A bot about to fire with a gadget slot in hand switches to its gun instead of firing.

**Bots learn from your driving (and your corrections)**
- 1.5 never recorded a human driving. It read the player's "entry" field as a seat number, but it is a pointer, so you never looked like a driver. Your Panama Canal log shows "drive map opened along 0 cells".
- 1.6 checks that you are in the driver's seat of a ground vehicle and records the vehicle's track, three cells wide.
- Where humans drove is no longer just *allowed* for bot drivers, it is **preferred**: it costs 0.3, against 0.5 for paved road and 1.0 for open ground. A tank route you corrected on Panama Canal is the route bots take next time.
- It is saved in that map's .learn file and shows in the learning map.
- Boats, helicopters, passenger seats and gunner seats teach nothing.

**Launcher: Players... and LAN games...**
- **Players...** lists the accounts on this PC's BF-Portable backend and registers new ones. You give a soldier name, an e-mail (the BC2 login; it does not have to be real) and a password.
  - It writes the same row and password hash as BF-Portable's website Register page (`bf.a_emu_playerinfo`), using the emulator's own database login from `BFBC2-EMU\config.ini`, so no MySQL root password is needed. MySQL has to be running; if it isn't, the launcher starts it.
  - **Remove selected...** deletes an account, after asking first. Bot accounts (9001-9048) are not listed and can't be removed here.
- **LAN games...** scans your network (the /24 around each of this PC's addresses, about 10 s). It looks for PCs running BF-Portable's BC2 emulator (TCP 18390) and BC2 servers (RCON 47400-47403, which answers `serverInfo` without a login), and also lists the servers this PC's backend knows. Each entry shows name, mode, map and players.
  - **Join** (or double-click) sets `host=` in the game's `bfbc2.ini` to that PC (`127.0.0.1` for this PC; backed up once as `bfbc2.ini.phoenix.bak`) and starts the game. Then join the server from the in-game server browser.
  - You can also type the other PC's address.
  - Your account has to exist on the PC you join, so register it there with its launcher's **Players...**.
  - The other PC needs its emulator and server running, `emulator_ip` in its `BFBC2-EMU\config.ini` set to its LAN address, and its firewall open for TCP 18390/18395 and UDP 19567 (BF-Portable's `Info\Ports for BF-Portable.txt` lists them all).

## What changed in 1.5.1 - Rush, and a map of what bots learned

**Rush and Squad Rush (bots arm and defuse now)**
- Why 1.5.0 bots ignored the M-COMs: Phoenix only looked for M-COMs when no flag object at all was in memory. Cold War Rush has inactive flag objects, so it never looked.
- And even standing on an M-COM holding Interact would not have armed it. The game processes a player's Interact in ServerPlayer::update, which it skips for AI players. 1.5.1 does what that code does for a human: when a bot has held Interact on the M-COM for the hold time, Phoenix calls the M-COM's interaction with the bot's soldier.
- M-COMs are read properly now: each has an arm interaction (type 2) and a defuse interaction (type 3), and the defuse one switches on when the M-COM is armed. Only the stage that is on is used, and the next stage is found when it starts.
- Attackers walk onto the M-COM (upstairs too - routes use the floors and bots knife doors on their way), hold Interact, arm it, then stay and defend it.
- When an M-COM is armed, up to 6 of the nearest defenders are sent from anywhere on the map to defuse it.
- When you (or any human) step up to an M-COM, arm or defuse it, the log says so, and an M-COM Phoenix did not know about starts a new search.

**What bots learned - the learning map**
- New: `LearnViewer.html`, opened from the launcher's **Bot learning...** button (or double-click it). Click **Open navcache folder** and pick this folder's `navcache`.
- For every map it draws the terrain from the bots' navigation grid. Over it: where bots die and kill, where they got stuck and what got them through (jump, knife, crouch, detour, back off), the spots cut from the map, sniper and window spots with their kills and deaths, and the routes you walked and drove.
- Hover anything for its numbers; click a row in the side lists to fly to it. Scroll to zoom, drag to pan.

## What changed in 1.5.0

From your 22:54 games on Hill 137, Phu Bai Valley and Operation Hastings, plus everything on your list.

**"<bot> switched teams" (mostly the RU side)**
- The cause: BC2 deletes an AI player when its soldier dies, so every bot life was a brand-new player. New players reuse the player numbers of the ones that left. When RU Jack got a number US Bob had just had, every client saw that number change teams and printed "switched teams" - all game long.
- 1.5 keeps a dead bot's player, the way the game keeps a human's. The bot redeploys as the same player, so there is nothing to announce and its kills and deaths add up on the scoreboard. (Phoenix flips one jump in the server's onKilled code and puts it back when bots stop.)
- `KeepBotPlayers=1` (in `[Experimental]`, also a launcher checkbox). If kept players ever refuse to redeploy, the log says so and Phoenix goes back to the old way by itself.

**Reversing when getting into a vehicle**
- The log showed it plainly: a T-54 waiting for passengers backed up 37 m, and one doing its turret test backed up 20 m. BC2's Brake input is the reverse pedal once a vehicle is slow, and bots held Brake to stop.
- Bots never press Brake now. A moving vehicle is slowed with the throttle against its motion; a slow one just idles.
- The "drives the other way round" check no longer fires on a vehicle still rolling back after a reverse (the Gaz-69 and M151 flipped twice in two seconds).

**Tanks going forward-back-forward-back**
- Stuck vehicles used to back up 0.6 s and drive at the same wall again every 2.5 s. Now a stuck vehicle looks for the freest way out. A tank pivots onto it; a wheeled vehicle backs off for a second with its nose swinging that way. Then it drives out for 2.5 s before taking its route again.
- Braking in front of an obstacle is done with the throttle, not Brake, so there is no back-and-forth.
- Routes: half the drives in your log had no route at all. A route request restarted every time the vehicle moved 4 m, so a long route was never finished. Requests now keep their starting point. If no route is found, the log says why and it is tried again in 4 s.

**Tanks not shooting while driving, gunners missing**
- The main gun fired only inside ~1.5 m of the aim point; it now fires within ~3 m and a degree (shells burst). Machine guns fire in bursts once close.
- The elevation test now tries up *and* down. The T-54, Gaz-69 and Huey logged "no elevation found" because their guns rest at one end of their travel.
- Each vehicle keeps its own turret calibration. Live, the T-54's turret was in a different place on two tanks, and the other one kept re-testing.

**Engineers and the repair tool, rockets, grenade launchers**
- On the Vietnam maps the engineer kit lists only the AT mine for the launcher slot, and Phoenix picked it - so every Vietnam engineer had mines and no RPG-7 / M72 LAW. Mines are never picked now, so the kit keeps its own launcher.
- Rockets at soldiers need a launcher, and each life has 5 rockets (one is always kept for armour).
- Assault soldiers fire their 40 mm (M79 / XM320 / GP-30) at enemy vehicles within 80 m. The buckshot and smoke versions are rarely picked.
- Engineers repair vehicles of their side that stand damaged close by: they walk up, take the repair tool out, aim at the vehicle and hold. Vehicle health is read like a soldier's; the log shows it next to each vehicle type.

**Stationary weapons**
- A bot near an empty mounted gun or launcher takes it when no enemy is within 30 m of it and there is something to shoot (enemies 40-220 m out, or it is defending).
- It leaves after 15 s with nothing to shoot, after 90 s, if an enemy gets within 10 m, or if it is needed at a flag. `StationaryWeapons=1`.

**Vietnam vehicles**
- The PBR boats were never used. Bots now drive boats when there is water towards their flag (`Boats=1`). Your navigation grids have no water on these maps, so the driver steers by looking: the longest run of water that still points at the flag. It beaches the boat near the flag and everyone gets out.
- The one-seat stationary guns are used (above).
- Some spawners on these maps never put out a vehicle in Conquest (Hill 137's "manual" ones, and the unnamed ones with "0 out"), so there is nothing there to use.

**Rush and Squad Rush**
- The M-COMs are found as the server's interaction entities. The log lists every one, with its team, hold time and position (`objectives:` lines).
- Attackers are sent to the M-COMs, walk onto them and hold Interact for the hold time to arm them. Defenders guard all M-COMs. When an attacker has been at one, the nearest defenders go and hold Interact on it to defuse.
- When an M-COM is destroyed the next stage is found by itself. `RushObjectives=1`.

**Squad Deathmatch**
- Four squads: spawn points for teams 3 and 4 are recognised, every squad gets up to 4 bots (fewer when humans are in it), and everyone not in your squad is an enemy. Teams 3 and 4 use the kits of 1 and 2 if they have none of their own.

**Learning from you**
- Bots learn the routes human players take (`LearnFromHumans=1`). Every cell a human walks through becomes a preferred route for bots on foot. Every cell a human drives through becomes drivable for bot drivers at the cheapest cost, even ground the drive map had ruled out. Where a human walks on something the grid has no floor for (a climb, a gap), that spot is probed again so bots can use it too.
- Kept in `navcache\<map>.learn` (`walk` / `drive` lines) with the rest, and faded a little each time the map loads.

**Scoreboard**
- The ghost-field comparison is now logged when a human is first seen in game, not only at the start. One game with you in it gives me the two lines to compare for the Tab list.

**Launcher**
- New checkboxes: Stationary guns, Boats, Learn from humans, Rush M-COMs, Keep bot players.

## What changed in 1.4.0 - map list maker, Hardcore, Vietnam

- **PhoenixLauncher: "Maps / Hardcore / Vietnam..."** (under the Start buttons) opens the map list maker.
  - Pick the game mode (Conquest, Rush, Squad Rush, Squad Deathmatch), then click the maps you want. They play in list order.
  - The list is ticked with what your current `maplist.txt` already has.
  - **Rounds per map** (1-9).
  - **Vietnam**: shows the five Vietnam maps (Vantage Point, Hill 137, Cao Son Temple, Phu Bai Valley, Operation Hastings; Conquest only) and sets the server's `GameModID = VIETNAM`. Unticking it sets `GameModID = BC2` again.
  - **Hardcore**: sets `vars.hardCore true` / `false`.
- **Save map list** writes into the server's instance folder (the `-serverInstancePath` in your server .bat, normally `Instance\`):
  - `maplist.txt`
  - `AdminScripts\Startup.txt` (the `vars.hardCore` line)
  - `ServerOptions.ini` (the `GameModID` line)
  The first save keeps the originals as `*.phoenix.bak` next to them. A read-only flag is cleared.
- Restart the server after saving.
- Bots play Conquest fully (flags, vehicles, helicopters). In Rush and the squad modes they fight and hunt enemies but do not arm or defuse M-COMs yet.
- `GameModID = VIETNAM` is the value the BC2 server uses for the expansion. If the server log says the Vietnam levels are not available after a restart, tell me what it prints.

## What changed in 1.3.2

- **No more "<bot> switched teams".** Every bot life is a new player, and setting its team the normal way announced a
  team switch each respawn. The team is now written before the player is registered (`QuietTeamJoin=1`). If the server
  then refuses to deploy bots, Phoenix goes back to the old way by itself and says so in the log.
- **Squads.** `RealSquads=1` is now the default: bots go into real squads of four.
- **Scoreboard hunt.** Names over heads come from the online identity, which now works; the Tab list uses something
  else. The log now writes every player field the server sends to clients, for you and for a bot
  (`players: ghost fields human: ...` / `players: ghost fields Bob: ...`) so the difference can be found.

## What changed in 1.3.1 - backend

- **Launcher + BF-Portable.** On first start, or with **Find BF-Portable**, the launcher fills in the backend from your
  BF-Portable folder: XAMPP's `mysql_start.bat`, `Redirector\Redirector.cmd` and `BFBC2-EMU\mase_bc2.exe` (in that order,
  6 / 2 / 5 s apart), and the server's `BFBC2.bat`. **Start All** then starts MySQL, the redirector, the emulator, the
  server, the bots and the game. Programs already running are skipped.
- **Bot accounts for the scoreboard (experiment).** Your own player carries the persona of your backend account (its account
  id) at +0x18; the bots only had a made-up identity at +0xB0. `backend\Add-Bot-Accounts.bat` adds one account per bot
  name to the backend (`bf.a_emu_playerinfo`, ids 9001-9048 - nothing else is touched, and the SQL file says how to remove
  them). With `ScoreboardNames=2` (default) each bot gets its account's persona in both places, the way a login does.
  Run the .bat once with MySQL up, then start a game and check Tab.

## What changed in 1.3.0

**Rockets hit.** 1.2 aimed rockets at soldiers 0.002 rad higher per metre on top of a chest aim - about 2.5 m over the
head at 50 m. Now:
- Rockets aimed at soldiers go at their feet (for the splash), with only the small arc a ~140 m/s rocket needs.
- The 40 mm grenade gets its real lob.
- Rockets at vehicles aim at the middle of the hull.

**Knife.**
- An enemy within 3 m gets the knife (one hit).
- An enemy within 7 m who is facing away, or while the bot is reloading, gets rushed and knifed.

**Supplies.**
- Medics drop medkits for a hurt teammate nearby (you first) and for themselves.
- Assault soldiers drop ammo boxes near teammates, more often next to you and after a fight.
- The log shows `supplies: ...`.

**Driving without the reversing.**
- Every BC2 ground vehicle tested so far drives the same way, so bots now drive off at once instead of running the
  rolling and reversing driving test.
- The first metres check the controls. If the vehicle disagrees, the sign is flipped and the log says
  `... the other way round - fixed`.
- Backing up when blocked is now 0.6 s.
- Tanks and IFVs turn on the spot when they have to face a new direction.

**Helicopters.**
- They take off at once with the known BC2 controls. The pad test runs only if a helicopter does not respond to them.
- Rockets and guns now aim over a distant target for the drop; 1.2 aimed straight at it, so shots fell short and low.
- Tank and gunner turrets try Roll and Pitch first. Live 1.2 showed that is how the Abrams, T-90 and AH-64 gunner
  turrets work, so turrets are found faster.

**Water (Nelson Bay).** 1.2 took swimming bots for "stuck in the air" and redeployed about 60 of them. A swimming bot
now:
- plans a new route;
- marks that water as expensive, so later routes go round it or over the bridges.

## What changed in 1.2.0 - fixes from your Atacama games, deadlier bots, a launcher

**Bugs fixed**
- **The hovering RU chopper / the bot that got out mid-air.** 1.1 forgot what a vehicle was once it had moved more
  than 12 m from its spawner, so the Mi-28 became an "unknown" vehicle and its pilot drove it like a car. Throttle held
  it in the air, steering only turned it, and the car "stuck" rules finally made the pilot jump out. Vehicles now keep
  their identity for the whole round, and anything that has flown stays a helicopter.
- **Nobody gets out of a vehicle in the air.** A pilot lands first; a passenger waits for the ground.
- **A bot that the game would not let out** sits still for a while instead of retrying every 3 s.
- **A driving test that failed once** (a tank boxed in at spawn) no longer bans that vehicle type for the whole round.
  Bots try again, and give up only after three failures.
- **A landed helicopter waiting for a passenger** that never gets out (a human, say) takes off again after 15 s.
- **Line of sight from vehicles** is now checked from just outside the hull. From inside, the check hit the vehicle
  itself, which is probably why gunners did not shoot back while you were hitting them.

**Vehicles: both teams use them**
- Crews that deploy at their base go straight into the tank or helicopter instead of walking down to it.
- Up to five drivers per team, plus every helicopter.
- Armour is always taken when a bot passes it.
- Bots board from further away.
- New vehicles (respawns, or spawners that open when a flag falls) are found within 10 s.

**Turrets**
- The test now also tries the seat's own aim direction (the mouse view). On many Frostbite vehicles the turret follows
  where the gunner looks. If yours do, the log shows `turret turns with the aim (view)`.
- Gunners can run the test on the move.
- Other seats keep still while one seat tests.
- A third push confirms the result.
- Turret guns lead moving targets.

**Driving**
- Three feelers from the front bumper steer around walls, rocks and vehicles before the vehicle hits them, and brake
  when something is close.
- The "stuck" logic only counts time spent pushing without moving. When blocked in front, the vehicle backs up a short
  way. When not blocked (wheels hung up), it rocks free. After two tries the spot is cut from the route. 1.1 reversed
  every 5 s whenever it had covered less than 3 m.

**Helicopters fight each other**
- An enemy helicopter in the air within 500 m takes priority. The pilot stands off at 150-320 m at the enemy's height
  (30-70 m), points the nose at it, leads it and fires rockets when lined up.
- Helicopter gunners shoot at enemy helicopters first.

**Deadlier soldiers**
- Tighter aim spread, quicker reactions, faster aim settling.
- Wider awareness: they hear fire and footsteps within 30 m.
- Longer engage ranges.
- They lead moving targets and hold over for bullet drop at range.
- Rockets are only fired when properly lined up.

**Bot damage to players: BotDamage=1.2.** When you lose health while a bot is shooting at you, the loss is raised by
20%. A hit that would kill you anyway is left to the game, so kills are still credited normally. Set 1.0 for normal
damage.

**Other changes**
- **15 v 15 by default** (`BotsPerTeam=15`).
- **Doors:** a bot whose route goes through a closed door (or a thin wall) right in front of it knifes it open.

**Still open: names on the scoreboard.** The server registers the bots with their names, but the BC2 client leaves AI
players off its scoreboard. That needs a change to the game client, which this mod doesn't touch.

## What changed in 1.1.0 - vehicles that fight

Your 1.0 game on Atacama showed three problems: RU barely used its vehicles, the Mi-28 was treated as a car,
and nothing in a vehicle fired. All three are addressed here.

**Every vehicle is identified.** Most BC2 vehicles have no readable name of their own (the RU T-90s, Cobra and Mi-28
showed as "(no name)"), so bots guessed. Each vehicle now takes its name and its team from the spawner that made it,
so the Mi-28 is flown as a helicopter and the T-90s are driven. The Su-39 jets in the spawner list never appear in
Conquest and are ignored. If an unnamed vehicle still turns out to fly during the driving test, it is handed to the
helicopter pilot code instead of being written off.

**Crews.** A bot whose flag is far away may deploy at the team spawn next to an idle tank, jeep or helicopter of its
own team and take it (up to three drivers per team, plus every helicopter). Bots no longer take the other team's
vehicles from its base.

**Guns.** The server's turrets are aimed with rate inputs and nobody has mapped where a BC2 vehicle keeps its turret, so
the first bot in each seat of each vehicle type finds it. It looks through the vehicle's memory for transform matrices,
moves one input at a time (CameraYaw / Yaw / Roll, then CameraPitch / Pitch), keeps the matrix that turns with it and
back again, and ignores anything that moves by itself (rotors, wheels). This takes a few seconds; a driver holds still
for it. After that:
- **Tanks and IFVs** aim the main gun at the nearest enemy soldier or enemy-crewed vehicle in sight (up to 220 m),
  slow down while fighting and fire when the gun is on target.
- **Gunner seats** (the jeep and Bradley guns, the Apache / Havoc / Hind / Black Hawk gunners) aim and fire the same way.
  A bot takes the helicopter gunner seat whenever a teammate is about to fly, and stays in it.
- **Helicopter pilots** make attack runs. Over the flag they dive nose-first at a target and fire the rockets when the
  nose is on it, break away inside 70 m, come round and go again. On the way to a flag they fire at targets in front
  of the nose.
- If no turret can be found for a seat, a tank driver turns the hull onto the target for a few seconds and fires.

The log shows each step: `turret turns with input 10 (+1)`, `gun elevates with input 9`, `turret ready`. If the same
turret path doesn't work on another vehicle of the type, the test runs again on that one.

**Rocket launchers.** Engineers now go after vehicles. When an enemy-crewed tank, jeep or boat is within 150 m, or a
helicopter within 130 m, and no enemy soldier is closer than 25 m, the engineer:
- stops;
- aims ahead of a moving target, and a little high for the rocket's drop;
- fires;
- fires again every 6-8 s while the vehicle is in sight.
The log shows each shot: `combat: <bot> fires a rocket at an enemy helicopter 104 m away`. The other classes put
rifle fire on low helicopters (within 110 m) when no soldier is in front of them.

## What changed in 1.0.0 - vehicles and helicopters

**Helicopters (new, `Helicopters=1`).** Bots fly the Apache, Havoc, Hind and Black Hawk. Nobody knows in advance how
the BC2 server's helicopter inputs behave, so the first bot pilot of each helicopter type runs a short control test
at the pad: it lifts off with full collective (Throttle) and then, hovering about 14 m up, gives a short push on
Pitch, Roll and Yaw and measures which way the helicopter tilts or turns (if the rudder isn't on Yaw it tries the A-D
input). The log shows every step (`vehicles: helicopter test - ...`). After that the pilot:
- climbs to about 35 m above the ground and cruises to its flag, climbing over hills ahead of it;
- with passengers aboard, lands on open, flat ground 40-70 m short of the flag (no roof or trees over it, nothing
  within 9 m), drops them off and takes off again;
- circles the flag at about 45 m, turning the nose onto enemies and firing when they're in front of it;
- moves on when the director gives it the next flag.
If a test input does nothing, the pilot lands, gets out and bots leave that type alone. A pilot that loses control
(more than ~70 degrees of bank or pitch) counts a crash against the type; after three, bots stop using it.
Passengers stay in while the helicopter is in the air and get out once it is on the ground near their flag.

**Getting back into vehicles.** A bot that got out of a vehicle walks back to it (up to 120 m) for the next flag
when the flag is far away - drivers always reclaim their own vehicle. Up to three drivers per team; a
driver keeps the vehicle claimed for 35 s while it walks over, so teammates don't take it.

**Better driving.**
- The route look-ahead grows with speed (8-22 m).
- Drivers brake before hairpins and sharp corners and come off the throttle when the nose is far off the route.
- A vehicle stuck twice at the same spot marks that spot as undrivable for its width and plans a new route
  around it.
- Tank and IFV crews aim the turret at the nearest visible enemy and fire bursts. Gunners in passenger seats do
  the same.
- Fixed: for one second, 14 or 30 s after boarding, every driver's controls froze (a leftover boarding timer).

**Map vehicle scan.** At each level load the log now lists every vehicle spawner on the map: what it spawns, which
team, where, whether it is enabled and its respawn delay (`vehicles: N vehicle spawner(s) on this map`). The table of
US/RU vehicles and helicopters per map is in `docs\ENGINE-NOTES.md` and below.

| Map (Conquest) | US | RU | Neutral / both |
|---|---|---|---|
| Panama Canal | M1A2, M3A3 Bradley | T-90, BMD-3 | quads, UAV |
| Laguna Alta | M3A3 Bradley | BMD-3 | quads, UAV |
| Atacama Desert | M1A2, HMMWV, **AH-64 Apache** | T-90, Cobra, **Mi-28 Havoc** | quads, UAV |
| Arica Harbor | M1A2 (*see note*) | T-90 (*see note*) | quads, UAV |
| White Pass | M3A3 Bradley | BMD-3 | quads, UAV |
| Nelson Bay | - | - | quads, UAV |
| Laguna Presa | - | BMD-3 | quads, jetskis, UAV |
| Port Valdez | **AH-64 Apache** | **Mi-28 Havoc** | CAV, quads, UAV |
| Oasis | M1A2, HMMWV | T-90, Cobra | quads, jetskis, UAV |
| Harvest Day | M1A2 | T-90 | CAV, quads, UAV |
| Heavy Metal | M1A2, **AH-64 Apache**, **UH-60 Black Hawk** | T-90, **Mi-28 Havoc**, **Mi-24 Hind** | CAV, quads, UAV |
| Vietnam maps | M48 Patton, UH-1 Huey (varies by map) | T-54, UH-1 Huey (varies by map) | patrol boats, jeeps |

Note on Arica Harbor: one source lists tanks on Conquest, another lists only quads. Your server only showed quads,
and the new spawner list in the log will settle it. Jetskis and the UAV are never used by bots.

## What changed in 0.9.0 - bots that learn

Each map now has a learned-data file, `navcache\<map>.learn`, kept next to the map's navigation grid. It's plain
text: copy the `.learn` (and the `.nav`) to another server's `navcache` folder to share it. Bots read it when the
map loads and write it every minute (and when bots are stopped). Older knowledge fades a little at each load, so
the bots keep adapting. `Learning=0` in phoenix.ini turns it off.

What they learn:
- **Stuck spots and the fix.** Where a bot got stuck, heading which way, and which action got it through: jump,
  knife, crouch, side-step or backing off. The next bot that walks into that spot does the winning action at once,
  before getting stuck. A spot that no bot ever gets through is cut out of the map grid for good, so routes avoid it.
- **Where bots die.** Deaths are counted in 10 m cells and routes cost more through deadly ground - up to six times
  the distance - so bots find other ways in. Flanking routes go round the side where fewer bots died.
- **Good positions.** Sniper and window spots score kills and deaths; spots that get kills without getting their
  sniper killed are chosen more often.

The log shows what is loaded (`learn: ... stuck spot(s), ... death cell(s) ...`) and every fix that worked
(`learn: at (x, y, z) 'jump' got a bot through`).

Also: the scoreboard identity from 0.8.1 is kept (it is harmless), but bots still don't appear on the client's
scoreboard - the game client hides them itself. Weapons and gadgets from 0.8.1 confirmed working in the log.

## What changed in 0.8.1

- **Weapons, for real.** The soldier's weapon setup matches chosen weapons against the weapon's *data* object, one
  level below the weapon asset 0.8.0 added - nothing matched, so the kit default (USAS-12) stayed. Bots now carry
  the random weapon picked for them (rifles, LMGs, SMGs, sniper rifles; shotguns rare).
- **Gadgets on the right keys.** DynamicGadget1 is each kit's fixed gadget: ammo box, medkit, the engineer's repair
  drill, the recon's C4. Rockets, the 40 mm launcher and the defibrillator are the kit's slot-2 gadget, on the
  GrenadeLauncher key. Engineers no longer "shoot" the drill, recon no longer drop C4 at their feet, and bots
  never pick AT mines or the tracer dart for those slots. The log lists every kit's gadget slots by name.
- **Vehicles on Arica Harbor.** The memory scan kept the first 64 matches of the vehicle type and on this map those
  were stale copies - the real vehicles were never seen. It now keeps up to 2048 and logs how many it found. A
  vehicle that does not roll during the steering test (against a wall) is backed up and tested again instead of
  being written off.
- **Scoreboard (experimental).** A human's scoreboard entry comes from the online identity (id + persona name) that
  their connection gives the server. Bots had none. Each bot now gets one with its name (`ScoreboardNames=1`).
  The log shows the human's identity next to the bots' so the format can be compared.

## What changed in 0.8.0

**Conquest**
- **Bots stay on a flag until it is captured.** 0.7 sent a bot elsewhere 25 s after it reached a flag unless the
  game's "being captured by" field was set - and BC2 leaves that field empty while a *neutral* flag is taken. So
  bots walked off half-captured flags. Now a bot stays while the capture bar is moving, enemies contest the flag,
  or for up to 60 s; it only leaves a flag where nothing happens.
- **They stand close to the flag.** The real capture area is level geometry, not data. Bots stand within ~6 m of
  the pole, and if a flag does not start capturing with bots on it, they move in closer (logged).
- **No more mass retreat.** A flag under attack pulls back 2-4 of the nearest bots within 90 m (0.7: up to 6 from
  160 m). A few bots hold the flags already taken (more as the team holds more), the rest push on.
- **Flanks.** About 4 in 10 attackers take a route off to one side of the direct line.
- **Snipers and window shooters.** For every flag the bots work out positions that see it: upper floors, rooftops
  and raised ground 35-125 m away for recon, upper-floor/window spots 8-40 m away for bots guarding it. Recon
  bots take one most lives, watch the flag and shoot from there for a minute or two.

**Navigation**
- **Routes no longer decay.** Every stuck spot used to cut links out of the map grid for good; now they come back
  after 90 s, and links are never cut where a teammate was in the way or on stairs (that trapped bots on one floor).
- **Blown-open walls and new rubble.** The grid is re-checked with rays around stuck bots and, every 8 s, around a
  flag - a wall blown open becomes a way through, a wreck or rubble pile becomes an obstacle.
- **Stairs.** After a map loads, every staircase link is re-checked (wall faces dropped, missed flights added,
  rail tops removed), so bots reach upper floors.
- **Vehicles drive on roads.** Drivers plan on the drivable part of the grid (open ground, roads preferred, away
  from walls) and park where the drivable route ends, then walk.

**Other**
- **Real loadouts.** Most kit weapons have no resource id, so bots could not select them and all got the kit's
  last default - the USAS-12. Phoenix now gives each bot a random weapon per slot directly (shotguns rare).
- The log names the map (`level: Levels/MP_006CQ - Arica Harbor`).
- Vehicle exits that the game refuses no longer loop.
- Scoreboard check: the server does register bots in its networked player table with their names. The log shows
  `players: Bob registered ... in the ghosted player table: yes`. If they still don't appear on your scoreboard,
  the client is filtering AI players.

## What changed in 0.7.2

- **Bots stopped walking properly after a few minutes (the main 0.7.1 bug).** Phoenix had a safety check
  from 0.6.3: if a bot moved faster than a soldier can run, it assumed the server was already moving the
  bots and switched its own movement step off. A bot riding in a vehicle tripped it, and from then on no
  bot walked properly. That is why they captured the first flag and then hung around, why the Russians never
  got anywhere, why so many bots were stuck, and why some were floating. The check is gone.
- **Walking in the sky.** The "push a pinned bot free" hop only happens from the ground now, and a bot with
  nothing under it for 4 seconds is redeployed.
- **On to the next flag.** No guards by default (`GuardEvery=0`): once a flag is captured everyone moves on
  to the next one. A flag that is being attacked is still defended. Getting stuck no longer makes a bot drop
  its flag; it re-plans around the spot, and only switches flags after failing three times.
- **Vehicles keep going.** Drivers park inside the flag zone (crews capture from inside the vehicle), wait for
  the flag to fall and drive on to the next flag. If nothing happens in 20 s they get out and capture on foot.
  Drivers wait for teammates who are walking over to get in, and bots hop back into the vehicle they came in
  (up to 70 m away) when the next flag is far.
- **Aim.** 0.7.1 had bots shoot while running to the flag, and BC2 spreads shots a lot while moving. Bots
  that see an enemy now plant their feet and aim down sights (close or on-route enemies up to 10 s, others
  5 s, recon at any range), then push on to the flag.
- **Unnamed vehicles are used too** (quads, jeeps and such have no name in the level data). The driving test
  catches anything that flies, and passengers never get out in mid-air.

## What changed in 0.7.1

- **Flags first (AltBotMod's director, ported).** Every second each team hands out jobs: *attack* a flag
  the team does not own, *defend* an owned flag that is being taken, *guard* a front-line flag (a small
  share, rotated every 90 s), or *patrol* when the team owns everything. Attacks go to the flags nearest
  the team's side first, one group per 4 bots (`GroupSize`), spread over different flags before doubling
  up, and roll forward to the next flags as flags fall. A flag the team just lost is retaken first.
- **No more idling.** A captured flag releases its attackers at once. An attack stays committed while the
  bot keeps getting closer (45 s) and while it is capturing; a bot that sat on a flag 25 s without
  capturing, or could not get through, is sent to another flag. Bots that are waiting to respawn already
  have their next flag, and deploy at the team spawn nearest it.
- **Fighting on the way.** Bots stop to fight enemies that are close (< 18 m) or on their route to the flag
  (AltBotMod's threat-in-path rule); others are shot while the bot keeps moving.
- **Following removed.** Fireteams no longer chase their leader and no longer follow the human.
- **Ledges, walls and the wooden car barriers.** Bots probe straight ahead at six heights. Something low
  with free space above (ledge, low wall, beam) is jumped while pushing forward for a second; a bar at
  head height is crouched under. If the jump fails the old escalation follows (knife it, back off, re-plan).
- **Pinned bots.** Some bots in 0.7.0 stood still for minutes with every input ignored (wedged in rubble or
  geometry). A bot that wants to walk but has not moved 0.6 m in 6 s is now pushed free with a hop away
  from the obstacle; after `FrozenRespawn` seconds (30) it is redeployed. The log shows its physics state.
- **Vehicles (experimental).** Bots whose flag is far ride along as passengers when a teammate (bot or
  human) drives a ground vehicle nearby, and get out near their flag. With `Vehicles=2` a bot can also
  drive: it takes an empty ground vehicle near its spawn, waits a few seconds for passengers, runs a short
  driving test (which input steers, which way is forward) and drives the route to its flag. The log lists
  every vehicle found and the driving test results. Helicopters, boats and emplacements are not used.

New phoenix.ini settings: `GroupSize`, `GuardEvery`, `Vehicles`, `FrozenRespawn`. `FollowHuman` is ignored.

## What changed in 0.7.0

- **All weapons.** A soldier only receives kit weapons whose unlock bit is set on its player. Human players
  get their unlocks when they connect; bots had none, so they always spawned with the default kit. Bots now
  get every unlock, and each life picks a random weapon per slot from the level's own kits - so on Vietnam
  maps they use the Vietnam kits automatically.
- **Grenades** are held for a moment and lobbed (8-35 m), every 15-30 s in a fight.
- **Equipment**: assault grenade launcher (20-80 m), engineer rockets (15-90 m), assault ammo boxes near
  teammates, medic medkits when they or a teammate are hurt, recon motion mines on flags.
- **Revive**: medics run to downed human teammates within 35 m and use the defibrillator. (Bots cannot be
  revived: the server deletes an AI player the moment its soldier dies.)
- **Fireteams**: bots on a team are grouped in fours, take their leader's flag and catch up when they fall
  more than ~25 m behind. Every minute or two a fireteam may follow a human on its team for a while.
- **Getting stuck**: progress is now measured towards the next waypoint, so strafing back and forth no
  longer hides being stuck. First a bot knifes what is in front of it (fences break), then backs off and
  jumps, then marks the spot blocked and re-plans, then side-steps, and finally picks another flag.
- The log lists what each kit contains per slot, and the first loadouts bots picked.

## What changed in 0.6.4

- **The real reason bots stood still.** Because bots are AI players, the server turns their soldier's
  physics character into an *AI character* (`CharacterEntity::setAI`). An AI character ignores throttle and
  strafe completely: it derives them from a velocity that BC2's single-player AI system would set. That
  system is not running on the multiplayer server, so the velocity stayed zero, and so did the bots.
  Crouch, jump, aim and shooting do not go through that path, which is why those worked.
- Phoenix now switches each bot's character to player-input mode (the same mode a human player's soldier
  uses) and runs its movement step every frame. If bots that try to walk still do not move within ~10 s,
  Phoenix automatically switches to driving the AI character's velocity directly instead. The log says
  which one worked: `move: bots are walking (...)`.

## What changed in 0.6.3

- **Movement, second half.** 0.6.2 set the bots' movement input but then skipped the character physics
  step because a physics flag said "the world steps this character". On this server it does not; the
  server's own per-input path always runs the step. Phoenix now always runs it. If the server ever also
  steps them (bots running at double speed), Phoenix notices and stops its own step.
- **No more "AI already spawned from the spawnPoint" dialog.** That assert is a single-player
  load/save check. Phoenix sets the assert's own "Ignore Always" switch at start, so neither you nor
  the bots trigger the dialog. (While that dialog is open the server is frozen.)
- Flags whose data radius is 1 m (seen on this map's B and C) get a 10 m capture zone.
- If a bot still does not move, the status line logs its character-physics state.

## What changed in 0.6.2

- **Bots really walk now.** The BC2 server only moves a soldier when a network input packet from a
  game client arrives for it: the connection code runs the soldier's movement step (movement input
  and, with `Physics.SingleStepCharacter`, the character physics step). AI players have no client, so
  the server read the bots' throttle for aiming and weapons but never moved them. Phoenix now runs that
  same step for every bot each server frame. `MovementStep=0` in phoenix.ini turns it off.

## What changed in 0.6.1

- **Input frames are stamped.** BC2 only applies an AI input frame if it carries the current simulation frame number
  (the native AI stamps it at state `+0xB8`). 0.6 left it at 0, so the soldier accepted aim, fire, crouch
  and jump but ignored throttle and strafe. Every input is now stamped with the server frame.
- **Real conquest flags are used.** The level's flags have radius 0 in their data (the size lives in the
  trigger), and 0.6 rejected them. They now default to a 15 m capture zone.
- **Rays see walls and buildings.** Rays now test both collision filters and keep the nearest hit, so
  paths and line of sight no longer go through houses. The navigation cache is rebuilt once.
- **No more "AI already spawned from the spawnPoint" assert.** A spawn point remembers the last AI soldier
  it produced; Phoenix releases that claim before reusing it and spreads bots over spawn points.
- **Names**: Bob, Jack, Melanie, Joe, Ana, Mike, Sarah, …
- Status lines now show a few bots' position, distance walked, goal flag and movement input.

## What the bots do

- **10 v 10** (configurable), using the four kits: assault, engineer, medic and recon. Each life
  picks a class and a random weapon from that kit's own unlock list, read from the level's team data.
- **Respawn**: BC2 deletes an AI player when its soldier dies, so each life is a fresh native player. Bots
  come back after `RespawnDelay` seconds, at a spawn point near the flag they want.
- **Conquest**: bots find the level's capture points in server memory (position, radius, owner, who is
  capturing). A team director gives every bot a flag job (attack nearest-first in groups, defend, a few
  guards, retake lost flags) and bots stay inside the zone until it is captured, then move on.
- **Pathfinding with rays**: bots call the server's own physics ray-cast. While moving they probe a fan of
  directions at knee and chest height, hop low obstacles and avoid walls and drops. In the background they
  survey the whole map with rays into a navigation grid (the AltBotMod / Project Caspian grid) and then plan
  routes with A*. The first visit of a map takes a few minutes; the grid is saved in `navcache\`.
- **Combat**: line of sight by ray, reaction time, aim settling, bursts per class, aiming down sights,
  strafing, reloads, grenades and the knife at close range. They will not fire through teammates.

Not in this version: boats and the UAV, Rush / Squad Deathmatch objectives (bots hunt enemies there), engineer repairs,
scoreboard entries for bots (the BC2 server does not send AI players to the scoreboard; their kills still count).

## Logs

Each start writes `reports\<date-time>\runtime.log`. A status line every 15 s shows bots per team,
flag owners and navigation progress.

## Files

- `bin\PhoenixBC2.dll` — the bot runtime (loaded into the server)
- `bin\PhoenixAttach.exe` — loads/stops it
- `phoenix.ini` — settings
- `navcache\` — saved navigation grids (created on first use)
- `source\` — full source and the offline test harness
