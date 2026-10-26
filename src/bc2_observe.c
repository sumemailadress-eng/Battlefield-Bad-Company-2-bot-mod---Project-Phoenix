#include "bc2_observe.h"
#include <math.h>
#include <string.h>
#define PREFERRED_BASE 0x400000u
#define REBASED(base,address) ((base)+((address)-PREFERRED_BASE))
static int ptr(uint32_t p) { return p>=0x10000u && p<=0xffffe000u && !(p&3); }
static int u32(BC2ReadMemory read,void *ctx,uint32_t at,uint32_t *out)
{ return read(ctx,at,out,4); }
static float dot3(const float *a,const float *b) { return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]; }
static int matrix_valid(const float m[16])
{
    for(int r=0;r<4;r++)for(int k=0;k<3;k++)if(!isfinite(m[r*4+k]))return 0;
    for(int r=0;r<3;r++){float n=dot3(m+r*4,m+r*4);if(n<0.8f || n>1.2f)return 0;}
    if(fabsf(dot3(m,m+4))>0.15f || fabsf(dot3(m,m+8))>0.15f || fabsf(dot3(m+4,m+8))>0.15f)return 0;
    float cross[3]={m[1]*m[6]-m[2]*m[5],m[2]*m[4]-m[0]*m[6],m[0]*m[5]-m[1]*m[4]};
    return dot3(cross,m+8)>0.8f && fabsf(m[12])<100000 && fabsf(m[13])<100000 && fabsf(m[14])<100000;
}
static void observe_soldier(BC2ReadMemory read,void *ctx,uint32_t base,BC2ObservedPlayer *p)
{
    uint32_t vt=0,owner=0,root=0;uint16_t flags=0;float matrix[16];
    if(!ptr(p->controllable) || !u32(read,ctx,p->controllable,&vt) ||
       vt!=REBASED(base,0x1749f70u) || !u32(read,ctx,p->controllable+0x240,&owner) || owner!=p->pointer ||
       !read(ctx,p->controllable+0x34,&p->health,4) || !isfinite(p->health) || p->health<0 || p->health>100000)return;
    p->soldierValid=1;
    /* Mirror the verified GameEntity transform accessor at 0x70daf0.
       Do not assume the world transform always lives at entity +0x450. */
    if(!read(ctx,p->controllable+0x26,&flags,2) || !u32(read,ctx,p->controllable+0x28,&root) || !ptr(root))return;
    p->transform=root;
    if(flags&0x80){
        uint8_t index=0;if(!read(ctx,root+0xe,&index,1))return;
        uint64_t target=(uint64_t)root+0x10+(uint32_t)index*0x50;
        if(target>0xffffe000u)return;
        p->transform=(uint32_t)target;
    }
    if(!ptr(p->transform) || !read(ctx,p->transform,matrix,sizeof matrix) || !matrix_valid(matrix))return;
    memcpy(p->right,matrix,12);memcpy(p->up,matrix+4,12);memcpy(p->forward,matrix+8,12);memcpy(p->position,matrix+12,12);
    p->transformValid=1;
}
int bc2_observe(BC2ReadMemory read,void *ctx,uint32_t base,BC2Observation *out)
{
    if(!out)return BC2_OBSERVE_INVALID;
    memset(out,0,sizeof *out);
    if(!read || !ptr(base) || base>0xffffffffu-0x19c2000u)return BC2_OBSERVE_INVALID;
    out->context=REBASED(base,0x19c4cc0u);
    uint32_t vt=0,begin=0,end=0,again=0,max=0;
    if(!u32(read,ctx,out->context,&vt) || vt!=REBASED(base,0x170e120u))return BC2_OBSERVE_NOT_READY;
    if(!u32(read,ctx,out->context+0x30,&out->manager))return BC2_OBSERVE_INVALID;
    if(!out->manager)return BC2_OBSERVE_NOT_READY;
    if(!ptr(out->manager) || !u32(read,ctx,out->manager+4,&max) || !max || max>BC2_OBSERVER_UNITS ||
       !u32(read,ctx,out->manager+0x58,&begin) || !u32(read,ctx,out->manager+0x5c,&end))return BC2_OBSERVE_INVALID;
    out->maxPlayers=(int)max;
    if(begin!=end && (!ptr(begin) || !ptr(end) || end<begin || end-begin>max*4))return BC2_OBSERVE_INVALID;
    out->count=(int)((end-begin)/4);
    for(int i=0;i<out->count;i++){
        BC2ObservedPlayer *p=&out->players[i];uint8_t inGame=0,ai=0;
        if(!u32(read,ctx,begin+(uint32_t)i*4,&p->pointer) || !ptr(p->pointer) ||
           !u32(read,ctx,p->pointer,&vt) || vt!=REBASED(base,0x1716c84u) ||
           !u32(read,ctx,p->pointer+0x154,&p->id) || p->id>=max ||
           !u32(read,ctx,p->pointer+0xa8,&p->team) || p->team>16 ||
           !read(ctx,p->pointer+0xc80,&inGame,1) || !read(ctx,p->pointer+0xd84,&ai,1) || inGame>1 || ai>1)
            return BC2_OBSERVE_INVALID;
        p->inGame=inGame;p->isAI=ai;
        for(int j=0;j<i;j++)if(p->id==out->players[j].id || p->pointer==out->players[j].pointer)return BC2_OBSERVE_INVALID;
        if(!u32(read,ctx,p->pointer+0xc3c,&p->controllable) || !u32(read,ctx,p->pointer+0xc5c,&p->entry) ||
           !u32(read,ctx,p->pointer+0xc38,&p->input))return BC2_OBSERVE_INVALID;
        if(ptr(p->input) && u32(read,ctx,p->input+0x1a0,&p->state) && ptr(p->state) &&
           read(ctx,p->state+8,p->analog,sizeof p->analog) &&
           read(ctx,p->state+0x98,p->digital,sizeof p->digital) && read(ctx,p->state+0xa0,&p->dt,4)){
            p->inputValid=isfinite(p->dt) && p->dt>=0 && p->dt<=1;
            for(int a=0;a<11;a++)if(!isfinite(p->analog[a]) || fabsf(p->analog[a])>1.01f)p->inputValid=0;
            uint32_t current=0;
            if(!u32(read,ctx,p->input+0x1a0,&current) || current!=p->state)p->inputValid=0;
        }
        observe_soldier(read,ctx,base,p);
        if(!u32(read,ctx,p->pointer+0xc3c,&again) || again!=p->controllable ||
           !u32(read,ctx,p->pointer+0xc5c,&again) || again!=p->entry)return BC2_OBSERVE_NOT_READY;
    }
    if(!u32(read,ctx,out->manager+0x58,&again) || again!=begin ||
       !u32(read,ctx,out->manager+0x5c,&again) || again!=end ||
       !u32(read,ctx,out->context+0x30,&again) || again!=out->manager)return BC2_OBSERVE_NOT_READY;
    u32(read,ctx,REBASED(base,0x1b38b10u),&out->aiFactory);
    return BC2_OBSERVE_READY;
}
float bc2_observed_action(const BC2ObservedPlayer *p,enum BC2InputAction action)
{
    if(!p || !p->inputValid || action<BC2_EiaThrottle || action>=BC2_EiaUndefined)return 0;
    if(action<11)return p->analog[action];
    return (p->digital[(unsigned)action/32]>>(action%32))&1u?1.0f:0.0f;
}
