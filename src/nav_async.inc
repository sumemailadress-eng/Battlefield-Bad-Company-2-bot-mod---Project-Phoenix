// Main-thread route queue. One A* workspace is shared by all bot requests.
// Searches and shortcut checks resume next tick; unfinished searches never
// produce a guessed route or make a destination look unreachable.
#define NAV_PATH_OWNERS 128
typedef struct NavRequest {
    const NavGrid *grid;
    unsigned serial;
    const unsigned char *mask;
    NavRayFn ray;
    int longLegs, state, phase, maxPts, start, goal, closed, popped;
    int rescueAttempts, rescueCursor, prefix, n, partial, routeRestarts, bestNode;
    float bestDistance;
    int scanNode,scanDirection,scanPhysics;
    int *nodes, len, anchor, next, trial;
    double lastUsed;
    double startedAt;
    float source[3], target[3], prefixPos[3], fromPos[3];
    float vehicleWidth,vehicleAvoid[4];
    float points[NAV_MAX_OUT*3];
    short links[NAV_MAX_OUT];
    char error[128];
} NavRequest;
static NavRequest g_requests[NAV_PATH_OWNERS];
static int g_requestOwner=-1, g_activeRequest=-1, g_nextRequest;
static int g_searchSize, g_searchHeapCap, g_searchHeapN;
static float *g_searchCost;
static int *g_searchParent;
static unsigned *g_searchStamp, g_searchGeneration;
static HeapItem *g_searchHeap;
static int g_lastPartial, g_lastDeferred;

void nav_set_path_owner(int owner) { g_requestOwner=owner; }
int nav_active_path_owner(void) { return g_activeRequest>=0 ? g_activeRequest : g_requestOwner; }
int nav_last_partial(void) { return g_lastPartial; }
int nav_last_deferred(void) { return g_lastDeferred; }
// Physics-invalid edges are remembered per driver. The shared infantry grid
// stays intact, and a later request can select a DIFFERENT street.
typedef struct VehicleEdgeBlock {unsigned serial;int from,to;double until;} VehicleEdgeBlock;
static VehicleEdgeBlock g_vehicleEdges[NAV_PATH_OWNERS][32];
static unsigned g_vehicleEdgeNext[NAV_PATH_OWNERS];
static int request_edge_blocked(const NavRequest *q,int from,int to)
{
    if(!q->mask || g_activeRequest<0)return 0;
    VehicleEdgeBlock *blocks=g_vehicleEdges[g_activeRequest];
    for(int k=0;k<32;k++)if(blocks[k].serial==q->serial && blocks[k].until>q->lastUsed &&
        ((blocks[k].from==from && blocks[k].to==to) || (blocks[k].from==to && blocks[k].to==from)))return 1;
    return 0;
}
static int request_near_block(const NavRequest *q,int node)
{
    if(!q->mask || g_activeRequest<0)return 0;
    float p[3];node_pos(q->grid,node,p);
    VehicleEdgeBlock *blocks=g_vehicleEdges[g_activeRequest];
    for(int k=0;k<32;k++)if(blocks[k].serial==q->serial && blocks[k].until>q->lastUsed){
        float b[3];node_pos(q->grid,blocks[k].to,b);
        if(fabsf(p[1]-b[1])<4 && hypotf(p[0]-b[0],p[2]-b[2])<20)return 1;
    }
    return 0;
}
static void request_block_edge(NavRequest *q,int from,int to)
{
    if(g_activeRequest<0 || from<0 || to<0)return;
    unsigned k=g_vehicleEdgeNext[g_activeRequest]++%32;
    g_vehicleEdges[g_activeRequest][k]=(VehicleEdgeBlock){q->serial,from,to,now_ms()+30000};
}
/* 1.6.8: a vehicle request's rays start at the vehicle, inside its own hull: the 7 lines up to 1.8 m either side hit
   the vehicle itself, the first leg was "obstructed", the edges round the start were blocked one by one and the search
   ended "A* exhausted after 1 nodes" - a tank parked at a flag never got a route out. The part of a segment within
   5.5 m of the request's start is not tested (the vehicle is there; the drive map already keeps it off walls). */
#define OWN_HULL_R 5.5f
static int request_segment_clear(const NavRequest *q,const float *a,const float *b)
{
    if(!q->mask)return nav_segment_clear(q->ray,a,b);
    float s[3],e[3];memcpy(s,a,12);memcpy(e,b,12);
    float da=hypotf(a[0]-q->source[0],a[2]-q->source[2]),db=hypotf(b[0]-q->source[0],b[2]-q->source[2]);
    if(da<OWN_HULL_R && db<OWN_HULL_R)return 1;
    if(da<OWN_HULL_R || db<OWN_HULL_R){
        const float *in=da<OWN_HULL_R?a:b,*out=da<OWN_HULL_R?b:a;float *clip=da<OWN_HULL_R?s:e;
        float vx=out[0]-in[0],vz=out[2]-in[2],L2=vx*vx+vz*vz;if(L2<1e-4f)return 1;
        /* the point where the segment leaves the circle round the start */
        float fx=in[0]-q->source[0],fz=in[2]-q->source[2],bq=2*(fx*vx+fz*vz),cq=fx*fx+fz*fz-OWN_HULL_R*OWN_HULL_R,disc=bq*bq-4*L2*cq;
        float t=disc>0?(-bq+sqrtf(disc))/(2*L2):1;t=fmaxf(0,fminf(1,t));
        for(int j=0;j<3;j++)clip[j]=in[j]+(out[j]-in[j])*t;
    }
    return nav_segment_clear(q->ray,s,e);
}
static void request_clear(int owner)
{
    nav_release(g_requests[owner].nodes);
    memset(&g_requests[owner],0,sizeof g_requests[owner]);
    if(g_activeRequest==owner)g_activeRequest=-1;
}
void nav_cancel_path(int owner) { if(owner>=0 && owner<NAV_PATH_OWNERS)request_clear(owner); }
static void cancel_requests_on_mask(const unsigned char *mask) { for(int k=0;k<NAV_PATH_OWNERS;k++)if(g_requests[k].state && g_requests[k].mask==mask)request_clear(k); }
int nav_pending_paths(void)
{
    int n=0;for(int k=0;k<NAV_PATH_OWNERS;k++)if(g_requests[k].state==1 || g_requests[k].state==2)n++;return n;
}
void nav_cancel_paths(void)
{
    for(int k=0;k<NAV_PATH_OWNERS;k++)request_clear(k);
    g_requestOwner=-1;g_nextRequest=0;
    memset(g_vehicleEdges,0,sizeof g_vehicleEdges);memset(g_vehicleEdgeNext,0,sizeof g_vehicleEdgeNext);
}
static void request_finish(NavRequest *q,int success,const char *reason)
{
    if(!success)q->n=0;
    snprintf(q->error,sizeof q->error,"%s after %d nodes",reason,q->closed);
    nav_release(q->nodes);q->nodes=NULL;q->state=3;q->lastUsed=now_ms();
}
static int request_begin(NavRequest *q)
{
    if(!q->startedAt)q->startedAt=now_ms();
    const NavGrid *g=q->grid;
    float *s=q->prefix?q->prefixPos:q->source;
    q->start=nearest_node(g,s[0],s[1],s[2]);
    q->goal=nearest_node(g,q->target[0],q->target[1],q->target[2]);
    if(q->goal<0 && !isnan(q->target[1]))q->goal=nearest_node(g,q->target[0],NAN,q->target[2]);
    if(q->start<0 && q->mask && !q->longLegs && q->goal>=0 && !q->prefix){q->phase=3;return 1;}
    if(q->start<0 || q->goal<0){request_finish(q,0,q->start<0?"no start node":"no goal node");return 0;}
    float directDistance=hypotf(q->source[0]-q->target[0],q->source[2]-q->target[2]);
    // 1.6.8: 30 m (was 120): "clear, so drive straight" sent vehicles across the fields beside the road - in the 1.6.7
    // log the most common vehicle route was the vehicle and the flag, nothing between. Further out, A* finds the road.
    if(q->mask && !q->prefix && directDistance<30 && nav_direct_route(g,q->ray,q->source,q->target)){
        memcpy(q->points,q->target,12);q->links[0]=-1;q->n=1;
        request_finish(q,1,"ok (clear direct driving route)");return 0;
    }
    int size=g->w*g->h*L;
    if(g_searchSize!=size){
        nav_release(g_searchCost);nav_release(g_searchParent);nav_release(g_searchStamp);nav_release(g_searchHeap);
        g_searchCost=nav_alloc((size_t)size*sizeof *g_searchCost);
        g_searchParent=nav_alloc((size_t)size*sizeof *g_searchParent);
        g_searchStamp=nav_alloc((size_t)size*sizeof *g_searchStamp);
        g_searchHeapCap=size+1024;
        g_searchHeap=nav_alloc((size_t)g_searchHeapCap*sizeof *g_searchHeap);
        g_searchSize=size;g_searchGeneration=0;
        if(!g_searchCost || !g_searchParent || !g_searchStamp || !g_searchHeap){
            nav_release(g_searchCost);nav_release(g_searchParent);nav_release(g_searchStamp);nav_release(g_searchHeap);
            g_searchCost=NULL;g_searchParent=NULL;g_searchStamp=NULL;g_searchHeap=NULL;g_searchSize=0;
            request_finish(q,0,"navigation allocation failed");return 0;
        }
    }
    if(!g_searchCost || !g_searchParent || !g_searchStamp || !g_searchHeap){request_finish(q,0,"navigation allocation failed");return 0;}
    g_searchGeneration+=2;
    if(!g_searchGeneration){memset(g_searchStamp,0,(size_t)size*sizeof *g_searchStamp);g_searchGeneration=2;}
    g_searchHeapN=0;q->closed=q->popped=0;q->phase=1;q->scanNode=-1;
    q->bestNode=q->start;
    float startPos[3];node_pos(g,q->start,startPos);
    q->bestDistance=hypotf(startPos[0]-q->target[0],startPos[2]-q->target[2]);
    g_searchCost[q->start]=0;g_searchParent[q->start]=-1;g_searchStamp[q->start]=g_searchGeneration;
    heap_push(g_searchHeap,&g_searchHeapN,0,q->start);
    return 1;
}
static void request_relax(const NavGrid *g,int cur,int nb,float cost,int gx,int gz,float hw)
{
    if(g_searchStamp[nb]==g_searchGeneration+1)return;
    if(g_searchStamp[nb]>=g_searchGeneration && cost>=g_searchCost[nb])return;
    g_searchCost[nb]=cost;g_searchParent[nb]=cur;g_searchStamp[nb]=g_searchGeneration;
    int hx=abs(gx-(nb/L)%g->w),hz=abs(gz-(nb/L)/g->w);
    float h=(float)(hx>hz?hx+0.4142f*hz:hz+0.4142f*hx)*g->cell;
    if(g_searchHeapN<g_searchHeapCap)heap_push(g_searchHeap,&g_searchHeapN,cost+h*hw,nb);
}
static int request_rescue(NavRequest *q,double deadline)
{
    const NavGrid *g=q->grid;
    const unsigned char *savedMask=g_pathMask;g_pathMask=NULL;
    int origin=nearest_node(g,q->source[0],q->source[1],q->source[2]);g_pathMask=savedMask;
    if(origin<0 || q->longLegs || q->rescueAttempts>=3){request_finish(q,0,"A* exhausted");return 0;}
    int cx=(origin/L)%g->w,cz=(origin/L)/g->w,index=0;
    for(int r=1;r<=(q->mask?12:3);r++)for(int dz=-r;dz<=r;dz++)for(int dx=-r;dx<=r;dx++){
        if(abs(dx)!=r && abs(dz)!=r)continue;
        for(int l=0;l<L;l++){
            if(index++<q->rescueCursor)continue;
            if(now_ms()>=deadline)return 0;
            q->rescueCursor=index;
            int x=cx+dx,z=cz+dz;
            if(x<0 || z<0 || x>=g->w || z>=g->h)continue;
            int node=(z*g->w+x)*L+l;
            if(!(g->flags[node]&NAV_WALKABLE) || !g->edges[node] || (q->mask && !q->mask[node]) ||
               (q->closed>0 && g_searchSize==g->w*g->h*L && g_searchStamp && g_searchStamp[node]==g_searchGeneration+1) ||
               fabsf(g->height[node]-g->height[origin])>1.5f)continue;
            float p[3];node_pos(g,node,p);
            if(q->mask){g_pathMask=NULL;int connected=straight_walkable(g,origin,node);g_pathMask=savedMask;if(!connected)continue;}
            if(q->ray && !request_segment_clear(q,q->source,p))continue;
            q->rescueAttempts++;q->prefix=1;memcpy(q->prefixPos,p,12);
            return request_begin(q);
        }
    }
    request_finish(q,0,"A* exhausted");return 0;
}
static void request_prepare_output(NavRequest *q)
{
    q->len=0;
    for(int c=q->goal;c>=0;c=g_searchParent[c])q->len++;
    q->nodes=nav_alloc((size_t)q->len*sizeof *q->nodes);
    if(!q->nodes){request_finish(q,0,"navigation allocation failed");return;}
    int k=q->len;
    for(int c=q->goal;c>=0;c=g_searchParent[c])q->nodes[--k]=c;
    q->anchor=-1;q->trial=-1;q->n=0;q->phase=2;
    memcpy(q->fromPos,q->source,12);
    if(isnan(q->fromPos[1]))q->fromPos[1]=q->grid->height[q->start];
    if(q->prefix){memcpy(q->points,q->prefixPos,12);memcpy(q->fromPos,q->prefixPos,12);q->links[0]=-1;q->n=1;}
}
/* 1.6.6: a vehicle search that ran out (time, or the goal is not reachable on the drive map) still gives the best it
   found - the route to the reachable node closest to the goal - when that gets the vehicle at least 12 m closer.
   1.6.5 gave nothing, and the bot drove straight at the flag: 170 times in one Panama Canal round. */
static int request_partial_best(NavRequest *q)
{
    if(!q->mask || q->phase!=1 || q->bestNode<0 || q->bestNode==q->start)return 0;
    if(g_searchStamp[q->bestNode]!=g_searchGeneration+1)return 0;   // closed in this search: its parents are valid
    const NavGrid *g=q->grid;float p[3];node_pos(g,q->bestNode,p);
    float original=hypotf(q->source[0]-q->target[0],q->source[2]-q->target[2]);
    if(original-q->bestDistance<12 || hypotf(p[0]-q->source[0],p[2]-q->source[2])<12)return 0;
    q->goal=q->bestNode;q->partial=1;request_prepare_output(q);
    return q->phase==2;
}
static void request_advance(NavRequest *q,double deadline)
{
    const NavGrid *g=q->grid;
    // Even a resumed neighbor scan must observe the active job's deadline.
    if(q->phase==1 && now_ms()-q->startedAt>8000 && !request_partial_best(q)){request_finish(q,0,"A* time limit");return;}
    if(q->phase>=2 && now_ms()-q->startedAt>16000){request_finish(q,0,"A* time limit");return;}
    if(!q->phase && !request_begin(q))return;
    if(q->phase==3){if(!request_rescue(q,deadline) || now_ms()>=deadline)return;}
    int gx=(q->goal/L)%g->w,gz=(q->goal/L)/g->w;
    const unsigned char *roads=ROAD_MASK(g),*trees=TREE_DIST(g),*danger=DANGER(g);
    // Distance-led searches avoid flooding a mountain map merely because
    // roads cost 0.5. Traversed costs still prefer roads; every route is checked.
    // AltBotMod V12: soldiers 1.2 with roads (V11 0.9): on Riverside's grid the same routes (lengths within 4%) for
    // about half the nodes expanded - the live planner managed ~1 route a second for 59 bots, so bots waited in a queue.
    float hw=q->mask?0.7f:roads?1.2f:1.5f;   // 1.6.1 vehicles 0.7 (was 1.5: greedy - beelines over open ground beat the road, costs barely counted)
    while(q->phase==1 && (g_searchHeapN || q->scanNode>=0)){
        int cur=q->scanNode;
        if(cur<0){
        HeapItem item=heap_pop(g_searchHeap,&g_searchHeapN);cur=item.node;
        if(g_searchStamp[cur]==g_searchGeneration+1)continue;
        if((++q->popped&31)==0 && now_ms()>=deadline){heap_push(g_searchHeap,&g_searchHeapN,item.f,cur);return;}
        if(cur==q->goal){request_prepare_output(q);break;}
        g_searchStamp[cur]=g_searchGeneration+1;
        int searchLimit=q->mask?250000:120000;
        if(++q->closed>searchLimit){request_finish(q,0,"A* expansion limit");return;}
        if(q->mask){
            float p[3];node_pos(g,cur,p);float distance=hypotf(p[0]-q->target[0],p[2]-q->target[2]);
            if(distance<q->bestDistance){q->bestDistance=distance;q->bestNode=cur;}
            // A useful, checked continuation beats monopolizing the queue with
            // a 100k-node failed vehicle search while infantry waits at base.
            if(q->closed>=16000){
                node_pos(g,q->bestNode,p);
                float original=hypotf(q->source[0]-q->target[0],q->source[2]-q->target[2]);
                if(original-q->bestDistance>12 && hypotf(p[0]-q->source[0],p[2]-q->source[2])>12){
                    q->goal=q->bestNode;q->partial=1;request_prepare_output(q);break;
                }
                request_finish(q,0,"bounded driving search found no forward exit");return;
            }
        }
        if((q->popped&255)==0 && now_ms()-q->startedAt>8000){if(request_partial_best(q))break;request_finish(q,0,"A* time limit");return;}
        q->scanNode=cur;q->scanDirection=0;q->scanPhysics=q->ray && request_near_block(q,cur);
        }
        for(int d=q->scanDirection;d<8;d++){
            if(now_ms()>=deadline){q->scanDirection=d;return;}
            q->scanDirection=d+1;
            int nb=step_node(g,cur,d);
            if(nb<0 || (q->mask && (!q->mask[nb] || request_edge_blocked(q,cur,nb))))continue;
            if(q->mask && nav_link_bad(g,cur,nb,d))continue;   // 1.6.6: a kerb, a divider, a step (vehicle scan)
            if(q->mask && g_vehicleAvoid[3]>0){float p[3];node_pos(g,nb,p);if(vehicle_avoids_point(p[0],p[1],p[2]))continue;}
            if(q->scanPhysics){
                float from[3],to[3];node_pos(g,cur,from);node_pos(g,nb,to);
                if(!request_segment_clear(q,from,to)){request_block_edge(q,cur,nb);continue;}
            }
            if((d&1) && step_node(g,cur,(d+7)&7)<0 && step_node(g,cur,(d+1)&7)<0)continue;
            float climb=g->height[nb]-g->height[cur],step=((d&1)?1.4142f:1.0f)*g->cell;
            static const float driveCost[8]={1.0f,0.5f,1.0f,1.6f,2.5f,12.0f,0.3f,0.35f};   // 6: humans drove here (1.6), 7: the level's road (1.6.6)
            float mul=q->mask?driveCost[q->mask[nb]&7]:(g->flags[nb]&NAV_WATER)?WATER_COST_MUL:
                roads && roads[nb]==1?0.6f:roads && roads[nb]==2?0.85f:1.0f;
            if(!q->mask && trees && trees[nb]<=TREE_NEAR_DM)mul*=6.0f;
            if(!q->mask)mul*=DANGER_MUL(danger,nb);
            if(!q->mask && g->edges[nb]!=0xff && !(g->flags[nb]&NAV_WATER))mul*=1.5f;
            request_relax(g,cur,nb,g_searchCost[cur]+step*mul+(climb>0?climb*2.0f:0),gx,gz,hw);
        }
        q->scanNode=-1;
        for(int d=0;d<8 && !q->mask;d+=2){
            if(!(g->fences[cur]&(1<<d)))continue;
            int c=cur/L,nx=c%g->w+kDir[d][0],nz=c/g->w+kDir[d][1];
            if(nx<0 || nz<0 || nx>=g->w || nz>=g->h)continue;
            int nc=nz*g->w+nx;
            int nl=neighbour_layer(g,c,cur%L,nc,0);
            if(nl>=0)request_relax(g,cur,nc*L+nl,g_searchCost[cur]+g->cell+FENCE_COST,gx,gz,hw);
        }
        for(int k=0;k<g_linkCount && !q->mask;k++)if(g_links[k].g==g && g_links[k].from==cur)
            request_relax(g,cur,g_links[k].to,g_searchCost[cur]+g_links[k].cost,gx,gz,hw);
    }
    if(q->phase==1 && !g_searchHeapN && q->scanNode<0){
        if(q->closed<64 && !q->longLegs && q->rescueAttempts<3 && q->maxPts>1){q->phase=3;return;}
        if(!request_partial_best(q)){request_finish(q,0,"A* exhausted");return;}
    }
    while(q->phase==2 && q->anchor<q->len-1 && q->n<q->maxPts){
        if(now_ms()>=deadline)return;
        int link=q->anchor>=0?link_between(g,q->nodes[q->anchor],q->nodes[q->anchor+1]):-1;
        if(q->trial<0){
            q->next=q->anchor+1;
            int limit=q->len-1,maxLeg=q->mask && !q->longLegs?32:q->longLegs?250:64;
            if(limit>q->anchor+maxLeg)limit=q->anchor+maxLeg;
            if(link<0)for(int j=q->anchor+1;j<limit;j++)if(link_between(g,q->nodes[j],q->nodes[j+1])>=0){limit=j;break;}
            q->trial=link<0?limit:q->next;
        }
        while(link<0 && q->trial>q->next){
            if(now_ms()>=deadline)return;
            int j=q->trial--;
            if(q->anchor>=0 && !straight_walkable(g,q->nodes[q->anchor],q->nodes[j]))continue;
            float to[3];node_pos(g,q->nodes[j],to);
            if(q->ray && !request_segment_clear(q,q->fromPos,to))continue;
            q->next=j;break;
        }
        if(link<0 && q->next==q->anchor+1 && q->anchor>=0 && q->n<q->maxPts-1){
            int a=q->nodes[q->anchor],b=q->nodes[q->next],ca=a/L,cb=b/L;
            int dx=cb%g->w-ca%g->w,dz=cb/g->w-ca/g->w,d=-1;
            for(int k=0;k<8;k+=2)if(kDir[k][0]==dx && kDir[k][1]==dz)d=k;
            if(d>=0 && (g->fences[a]&(1<<(d+1)))){
                float pa[3],pb[3],off=0;node_pos(g,a,pa);node_pos(g,b,pb);
                for(int k=0;k<4 && q->ray;k++)if(pair_clear(q->ray,pa[0],pa[1],pa[2],pb[0],pb[1],pb[2],kDoorOffsets[k])){off=kDoorOffsets[k];break;}
                float vx=pb[0]-pa[0],vz=pb[2]-pa[2],ln=sqrtf(vx*vx+vz*vz),*o=&q->points[q->n*3];
                o[0]=(pa[0]+pb[0])*0.5f-vz/ln*off;o[1]=fmaxf(pa[1],pb[1]);o[2]=(pa[2]+pb[2])*0.5f+vx/ln*off;
                q->links[q->n]=-1;memcpy(q->fromPos,o,12);q->n++;
            }
        }
        if(q->mask && q->ray){
            float to[3];node_pos(g,q->nodes[q->next],to);
            if(!request_segment_clear(q,q->fromPos,to)){
                int from=q->anchor>=0?q->nodes[q->anchor]:q->start;
                request_block_edge(q,from,q->nodes[q->next]);
                // Do not discard clear waypoints because a distant corner is
                // blocked. Drive that validated prefix, then resume planning.
                float progress=hypotf(q->fromPos[0]-q->source[0],q->fromPos[2]-q->source[2]);
                if(q->n && progress>4){q->partial=1;request_finish(q,1,"ok (clear prefix before obstacle)");return;}
                if(q->anchor>=0 && q->routeRestarts++<4){
                    nav_release(q->nodes);q->nodes=NULL;q->n=0;
                    request_begin(q);return;
                }
                request_finish(q,0,"vehicle segment obstructed; checked alternatives exhausted");return;
            }
        }
        int starts=q->next<q->len-1?link_between(g,q->nodes[q->next],q->nodes[q->next+1]):-1;
        float *out=&q->points[q->n*3];
        if(link>=0)memcpy(out,g_links[link].b,12);
        else if(starts>=0)memcpy(out,g_links[starts].a,12);
        else node_pos(g,q->nodes[q->next],out);
        q->links[q->n]=(short)link;memcpy(q->fromPos,out,12);q->n++;
        q->anchor=q->next;q->trial=-1;
    }
    if(q->phase==2){q->partial=q->partial || q->anchor<q->len-1;request_finish(q,1,q->partial?"ok (waypoint continuation)":"ok");}
}
void nav_process_paths(double maxMs)
{
    double begin=now_ms(),deadline=begin+fmax(0.05,fmin(8.0,maxMs));
    for(int k=0;k<NAV_PATH_OWNERS;k++)if(g_requests[k].state && begin-g_requests[k].lastUsed>5000.0)request_clear(k);
    while(now_ms()<deadline){
        if(g_activeRequest<0){
            for(int k=0;k<NAV_PATH_OWNERS;k++){
                int owner=(g_nextRequest+k)%NAV_PATH_OWNERS;
                if(g_requests[owner].state==1){g_activeRequest=owner;g_nextRequest=(owner+1)%NAV_PATH_OWNERS;g_requests[owner].state=2;break;}
            }
            if(g_activeRequest<0)break;
        }
        NavRequest *q=&g_requests[g_activeRequest];
        if(q->serial!=q->grid->serial){request_clear(g_activeRequest);continue;}
        const unsigned char *saved=g_pathMask;int longSaved=g_longLegs;float widthSaved=g_vehicleWidth,avoidSaved[4];memcpy(avoidSaved,g_vehicleAvoid,sizeof avoidSaved);
        g_pathMask=q->mask;g_longLegs=q->longLegs;g_vehicleWidth=q->vehicleWidth;memcpy(g_vehicleAvoid,q->vehicleAvoid,sizeof g_vehicleAvoid);
        request_advance(q,deadline);
        g_pathMask=saved;g_longLegs=longSaved;g_vehicleWidth=widthSaved;memcpy(g_vehicleAvoid,avoidSaved,sizeof g_vehicleAvoid);
        if(q->state==3)g_activeRequest=-1;else break;
    }
}
static int same_coordinate(float a,float b) { return (isnan(a)&&isnan(b)) || fabsf(a-b)<0.001f; }
int nav_find_path(const NavGrid *g,NavRayFn ray,float sx,float sy,float sz,float tx,float ty,float tz,float *out,int maxPts)
{
    g_lastPartial=g_lastDeferred=0;
    if(!g || !out || maxPts<1)return 0;
    if(!isfinite(sx) || !isfinite(sz) || !isfinite(tx) || !isfinite(tz) || isinf(sy) || isinf(ty)){
        snprintf(g_lastError,sizeof g_lastError,"invalid route position");return 0;
    }
    if(g_requestOwner<0 || g_requestOwner>=NAV_PATH_OWNERS){
        int n=nav_find_path_sync(g,ray,sx,sy,sz,tx,ty,tz,out,maxPts);
        // Console diagnostics use the synchronous planner, outside the bot tick queue.
        g_lastPartial=n>0 && strstr(g_lastError,"clear prefix")!=NULL;
        return n;
    }
    int owner=g_requestOwner;NavRequest *q=&g_requests[owner];
    float dx=sx-q->source[0],dz=sz-q->source[2];
    if(q->state && (q->grid!=g || q->serial!=g->serial || q->mask!=g_pathMask || q->longLegs!=g_longLegs || q->vehicleWidth!=g_vehicleWidth || (q->mask && memcmp(q->vehicleAvoid,g_vehicleAvoid,sizeof g_vehicleAvoid)) ||
       q->ray!=ray || !same_coordinate(tx,q->target[0]) || !same_coordinate(ty,q->target[1]) ||
       !same_coordinate(tz,q->target[2]) || dx*dx+dz*dz>16.0f ||
       (!isnan(sy) && !isnan(q->source[1]) && fabsf(sy-q->source[1])>3.0f)))request_clear(owner);
    q->lastUsed=now_ms();
    if(!q->state){
        q->grid=g;q->serial=g->serial;q->ray=ray;q->mask=g_pathMask;q->longLegs=g_longLegs;q->state=1;
        q->vehicleWidth=g_vehicleWidth;memcpy(q->vehicleAvoid,g_vehicleAvoid,sizeof g_vehicleAvoid);
        q->source[0]=sx;q->source[1]=sy;q->source[2]=sz;q->target[0]=tx;q->target[1]=ty;q->target[2]=tz;
        q->maxPts=maxPts<NAV_MAX_OUT?maxPts:NAV_MAX_OUT;
    }
    if(q->state!=3){g_lastDeferred=1;snprintf(g_lastError,sizeof g_lastError,"route queued / in progress");return 0;}
    int n=q->n<maxPts?q->n:maxPts;
    memcpy(out,q->points,(size_t)n*3*sizeof *out);memcpy(g_outLink,q->links,(size_t)n*sizeof *g_outLink);
    g_lastPartial=q->partial || n<q->n;snprintf(g_lastError,sizeof g_lastError,"%s",q->error);
    request_clear(owner);return n;
}
