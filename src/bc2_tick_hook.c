#include "bc2_tick_hook.h"
#include <string.h>
static const unsigned char expectedPrologue[6]={0x81,0xec,0xb0,0,0,0};
static void relative_jump(unsigned char *out,uint32_t instruction,uint32_t target)
{ uint32_t rel=target-(instruction+5u);out[0]=0xe9;memcpy(out+1,&rel,4); }
int bc2_tick_hook_plan(uint32_t target,uint32_t replacement,uint32_t trampoline,
                       const unsigned char prologue[6],unsigned char patch[6],unsigned char gate[11])
{
    if(!prologue || !patch || !gate || target<65536 || replacement<65536 || trampoline<65536 ||
       target>0xfffffff0u || trampoline>0xfffffff0u || target==replacement || target==trampoline || replacement==trampoline)
        return BC2_HOOK_INVALID;
    if(memcmp(prologue,expectedPrologue,6))return BC2_HOOK_CODE_MISMATCH;
    memcpy(gate,expectedPrologue,6);relative_jump(gate+6,trampoline+6,target+6);
    relative_jump(patch,target,replacement);patch[5]=0x90;return BC2_HOOK_OK;
}
#if defined(_WIN32) && (defined(__i386__) || defined(_M_IX86))
#include <windows.h>
#if defined(_MSC_VER)
#define PHX_THISCALL __thiscall
#define PHX_FASTCALL __fastcall
#else
#define PHX_THISCALL __attribute__((thiscall))
#define PHX_FASTCALL __attribute__((fastcall))
#endif
typedef void (PHX_THISCALL *PlayerUpdate)(void *,float);
static struct {
    int installed,inCallback;
    BC2TickHookConfig config;
    unsigned char *target,*trampoline,patch[6];
} hook;
static void PHX_FASTCALL after_player_update(void *manager,void *unused,float dt)
{
    (void)unused;
    ((PlayerUpdate)(void *)hook.trampoline)(manager,dt);
    if(!hook.inCallback){
        hook.inCallback=1;
        hook.config.afterUpdate(hook.config.context,(uint32_t)(uintptr_t)manager,dt,GetCurrentThreadId());
        hook.inCallback=0;
    }
}
int bc2_tick_hook_install(const BC2TickHookConfig *config)
{
    if(!config || !config->afterUpdate || !config->exactImageHashVerified ||
       config->imageBase<65536 || config->imageBase>0xfd000000u)return BC2_HOOK_INVALID;
    if(!config->executionQuiescent && (!config->beginPatch || !config->endPatch))return BC2_HOOK_NOT_QUIESCENT;
    if(hook.installed)return BC2_HOOK_ALREADY_INSTALLED;
    unsigned char *target=(unsigned char *)(uintptr_t)(config->imageBase+0x253dd0u);
    if(memcmp(target,expectedPrologue,6))return BC2_HOOK_CODE_MISMATCH;
    unsigned char *gate=(unsigned char *)VirtualAlloc(NULL,11,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    if(!gate)return BC2_HOOK_PROTECT_FAILED;
    unsigned char patch[6],gateBytes[11];
    int planned=bc2_tick_hook_plan((uint32_t)(uintptr_t)target,(uint32_t)(uintptr_t)after_player_update,
                                    (uint32_t)(uintptr_t)gate,target,patch,gateBytes);
    if(planned!=BC2_HOOK_OK){VirtualFree(gate,0,MEM_RELEASE);return planned;}
    memcpy(gate,gateBytes,11);DWORD ignored=0,old=0;
    if(!VirtualProtect(gate,11,PAGE_EXECUTE_READ,&ignored)){
        VirtualFree(gate,0,MEM_RELEASE);return BC2_HOOK_PROTECT_FAILED;
    }
    if(!FlushInstructionCache(GetCurrentProcess(),gate,11)){
        VirtualFree(gate,0,MEM_RELEASE);return BC2_HOOK_PROTECT_FAILED;
    }
    int guarded=!config->executionQuiescent;
    if(guarded && !config->beginPatch(config->context,(uint32_t)(uintptr_t)target,6)){
        VirtualFree(gate,0,MEM_RELEASE);return BC2_HOOK_NOT_QUIESCENT;
    }
    if(memcmp(target,expectedPrologue,6) || !VirtualProtect(target,6,PAGE_EXECUTE_READWRITE,&old)){
        if(guarded)config->endPatch(config->context);
        VirtualFree(gate,0,MEM_RELEASE);return BC2_HOOK_PROTECT_FAILED;
    }
    hook.config=*config;hook.target=target;hook.trampoline=gate;memcpy(hook.patch,patch,6);
    memcpy(target,patch,6);
    int flushed=FlushInstructionCache(GetCurrentProcess(),target,6);
    int restored=VirtualProtect(target,6,old,&ignored);
    if(!flushed || !restored){
        /* The caller guaranteed quiescence, so rollback is possible before
           any execution of the new branch. Do not free a referenced gate. */
        DWORD rollback=0;
        if(VirtualProtect(target,6,PAGE_EXECUTE_READWRITE,&rollback)){
            memcpy(target,expectedPrologue,6);
            int resetFlushed=FlushInstructionCache(GetCurrentProcess(),target,6);
            int resetProtected=VirtualProtect(target,6,old,&ignored);
            if(resetFlushed && resetProtected){VirtualFree(gate,0,MEM_RELEASE);memset(&hook,0,sizeof hook);}
            else hook.installed=1;
        }else hook.installed=1;
        if(guarded)config->endPatch(config->context);
        return BC2_HOOK_PROTECT_FAILED;
    }
    hook.installed=1;
    if(guarded)config->endPatch(config->context);
    return BC2_HOOK_OK;
}
int bc2_tick_hook_remove(int quiescent)
{
    if(!quiescent)return BC2_HOOK_NOT_QUIESCENT;
    if(!hook.installed)return BC2_HOOK_INVALID;
    if(memcmp(hook.target,hook.patch,6) && memcmp(hook.target,expectedPrologue,6))return BC2_HOOK_CODE_MISMATCH;
    DWORD old=0,ignored=0;
    if(!VirtualProtect(hook.target,6,PAGE_EXECUTE_READWRITE,&old))return BC2_HOOK_PROTECT_FAILED;
    memcpy(hook.target,expectedPrologue,6);
    int flushed=FlushInstructionCache(GetCurrentProcess(),hook.target,6);
    int restored=VirtualProtect(hook.target,6,old,&ignored);
    if(!flushed || !restored)return BC2_HOOK_PROTECT_FAILED;
    /* No thread can reference the gate under the removal contract. */
    VirtualFree(hook.trampoline,0,MEM_RELEASE);memset(&hook,0,sizeof hook);
    return BC2_HOOK_OK;
}
#else
int bc2_tick_hook_install(const BC2TickHookConfig *config)
{ (void)config;return BC2_HOOK_UNAVAILABLE; }
int bc2_tick_hook_remove(int quiescent)
{ (void)quiescent;return BC2_HOOK_UNAVAILABLE; }
#endif
