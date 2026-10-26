#pragma once
#include <stdint.h>
typedef void (*BC2AfterPlayerUpdate)(void *context,uint32_t manager,float dt,uint32_t thread);
typedef struct BC2TickHookConfig {
    uint32_t imageBase;
    int exactImageHashVerified;
    /* Either provide a quiescent startup point, or a paired patch guard.
       The guard runs after trampoline preparation and before patching. */
    int executionQuiescent;
    BC2AfterPlayerUpdate afterUpdate;
    void *context;
    int (*beginPatch)(void *context,uint32_t target,unsigned bytes);
    void (*endPatch)(void *context);
} BC2TickHookConfig;
enum BC2TickHookResult { BC2_HOOK_INVALID=-1,BC2_HOOK_UNAVAILABLE=-2,
    BC2_HOOK_CODE_MISMATCH=-3,BC2_HOOK_PROTECT_FAILED=-4,
    BC2_HOOK_ALREADY_INSTALLED=-5,BC2_HOOK_NOT_QUIESCENT=-6,BC2_HOOK_OK=1 };
/* Exact prologue only: sub esp,0xb0 is six bytes with no relative operands.
   Builds a 6-byte JMP/NOP patch and an 11-byte copied-prologue trampoline.
   Does not read/write a process or install anything. */
int bc2_tick_hook_plan(uint32_t target,uint32_t replacement,uint32_t trampoline,
                       const unsigned char prologue[6],unsigned char patch[6],unsigned char gate[11]);
/* In-process Windows x86 detour. Callback follows the original update. */
int bc2_tick_hook_install(const BC2TickHookConfig *config);
int bc2_tick_hook_remove(int executionQuiescent);
