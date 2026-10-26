#pragma once
#include <stdint.h>
#include "bc2_input_actions.h"
/* Read-only observation adapter for the exact collected CA8169FA build.
   This API does not call engine methods or create players. */
#define BC2_COLLECTED_SHA256 "CA8169FA03A29E02AF1BF8DB8770AB0E68F13A95B010C52ECA23733068298347"
#define BC2_OBSERVER_UNITS 64
typedef int (*BC2ReadMemory)(void *context,uint32_t address,void *out,unsigned bytes);
typedef struct BC2ObservedPlayer {
    uint32_t pointer,id,team,controllable,entry,input,state;
    int inGame,isAI,inputValid;
    float analog[11],dt;
    uint32_t digital[2];
    int soldierValid,transformValid;
    uint32_t transform;
    float position[3],right[3],up[3],forward[3],health;
} BC2ObservedPlayer;
typedef struct BC2Observation {
    uint32_t context,manager,aiFactory;
    int maxPlayers,count;
    BC2ObservedPlayer players[BC2_OBSERVER_UNITS];
} BC2Observation;
enum BC2ObserveResult { BC2_OBSERVE_INVALID=-1, BC2_OBSERVE_NOT_READY=0, BC2_OBSERVE_READY=1 };
/* Caller verifies the executable SHA-256 before using this build profile.
   imageBase is the live module base; all image-relative addresses are rebased.
   The read callback must return 1 only for a complete successful read. */
int bc2_observe(BC2ReadMemory read,void *context,uint32_t imageBase,BC2Observation *out);
float bc2_observed_action(const BC2ObservedPlayer *player,enum BC2InputAction action);
