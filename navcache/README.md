# Routes that come with Project Phoenix

Vehicle routes drawn in the 3D map, for maps where the bots should drive set roads. Phoenix copies these files into the
shared navcache (`<BC2 server folder>\PhoenixNavcache`) when the bots attach:

- a map you have no routes for gets these;
- a map you already have routes for gets these only if this file is bigger (more routes); yours is then kept beside it
  as `<map>.vroutes.before-<version>`.

| File | Map | Mode | Routes |
|---|---|---|---|
| `bc2v2_cq5_383bcdd5.vroutes` | Atacama Desert (`MP_005`) | Conquest | 28 routes from the US HQ, 48 forks |
| `bc2v2_cq5_55b4b1e1.vroutes` | Port Valdez (`MP_012CQ`) | Conquest | 9 routes from the HQ, 18 forks |
| `bc2v2_cq6_385e9cca.vroutes` | Panama Canal (`MP_001`) | Conquest | 5 routes |
| `bc2v2_cq5_55fe5b11.vroutes` | White Pass (`MP_007`) | Conquest | 6 routes |
| `bc2v2_cq5_0da2db91.vroutes` | Laguna Presa (`MP_009CQ`) | Conquest | 7 routes |
| `bc2v2_cq2_fc5167d4.vroutes` | Arica Harbor (`MP_006`) | Rush | 16 routes |
| `bc2v2_cq5_5dd281ad.vroutes` | Phu Bai Valley (`NAM_MP_006CQ`) | Conquest (Vietnam) | 9 routes |
| `bc2v2_cq6_1ee2105b.vroutes` | Operation Hastings (`NAM_MP_007CQ`) | Conquest (Vietnam) | 10 routes |

The file name is the map's navcache key: the bots make the same key for the same map and mode on every PC, so the routes
fit. More maps will follow in later releases.

The release package's `navcache` folder also carries the surveyed grids (`.nav`), vehicle scans, roads, foot routes,
objectives and what the bots learned for every map played so far, so a new install starts with all of it.

Format, one route per line: `route ax ay az bx by bz n x y z x y z ...` (start, end, number of points, then the points).
