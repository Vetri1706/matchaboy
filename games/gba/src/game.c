/* SPDX-License-Identifier: GPL-3.0-only */
#include "arcade.h"

static int absn(int a) { return a<0?-a:a; }
static int clamp(int v,int low,int high) { return v<low?low:(v>high?high:v); }
static void clear_game(Game *g) { unsigned char *p=(unsigned char*)g; unsigned n=sizeof(*g); while(n--) *p++=0; }
API int road_center(int t) {
    int phase=(t/6)%80;
    return 120 + (phase<20?phase:phase<60?40-phase:phase-80);
}
API int parcel_target_x(int i) { const int xs[8]={204,36,204,120,36,204,120,36}; return xs[i&7]; }
API int parcel_target_y(int i) { const int ys[8]={112,48,48,128,112,80,48,80}; return ys[i&7]; }
API int tactics_wall(int x,int y) { return (x==2 && y==1) || (x==3 && y==3) || (x==1 && y==3); }

static void begin(Game *g) {
    int kind=g->kind; clear_game(g); g->kind=kind; g->phase=PLAY; g->hp=3;
    g->sound=1; g->x=120; g->y=124;
    if(kind==DRIFT) {
        g->time=60*40;
        for(int i=0;i<8;++i) { g->obj[i].y=-24-i*64; g->obj[i].x=86+(i*29)%69; g->obj[i].alive=1; }
    } else if(kind==CLOUD) {
        g->time=60*55; g->x=48; g->y=80;
        for(int i=0;i<4;++i) { g->obj[i].x=250+i*88; g->obj[i].y=42+(i*31)%72; g->obj[i].alive=1; }
    } else if(kind==PRISM) {
        g->time=60*100; g->x=120; g->y=126;
        for(int i=0;i<24;++i) g->bricks[i]=1;
        g->obj[0].x=120; g->obj[0].y=118; g->obj[0].vx=1; g->obj[0].vy=-2;
    } else if(kind==TACTICS) {
        g->time=0; g->x=0; g->y=4; g->hp=9; g->target=0;
        for(int i=0;i<3;++i) { g->obj[i].alive=1; g->obj[i].hp=2; g->obj[i].x=5; g->obj[i].y=i*2; }
    } else {
        g->time=60*60; g->x=36; g->y=80; g->target=0;
        for(int i=0;i<3;++i) {g->obj[i].x=76+i*34; g->obj[i].y=42+i*36; g->obj[i].vx=(i&1)?-1:1; g->obj[i].alive=1;}
    }
}
API void game_init(Game *g,int kind) { clear_game(g); g->kind=kind; g->phase=TITLE; }
static void finish(Game *g,int won) { g->phase=won?WON:LOST; g->sound=won?4:5; }
static void damage(Game *g) { if(g->cooldown) return; --g->hp; g->cooldown=70; g->sound=3; if(g->hp<=0) finish(g,0); }
static void drift(Game *g,int k) {
    int speed=(k&K_A)?3:2;
    if(k&K_B) speed=1;
    g->x=clamp(g->x+((k&K_RIGHT)?2:0)-((k&K_LEFT)?2:0),14,226);
    g->stage+=speed;
    int center=road_center(g->stage);
    if(absn(g->x-center)>42) { g->stage-=speed; if((g->tick%30)==0) damage(g); }
    for(int i=0;i<8;++i) {
        Object *o=&g->obj[i]; o->y+=speed;
        if(o->y>172) { o->y=-340-i*17; o->x=road_center(g->stage)+((i%3)-1)*24; ++g->score; }
        if(absn(o->x-g->x)<13 && absn(o->y-g->y)<18) damage(g);
    }
    if(g->stage>=3600) finish(g,1);
}
static void cloud(Game *g,int k) {
    g->y=clamp(g->y+((k&K_DOWN)?2:0)-((k&K_UP)?2:0),26,140);
    int speed=(k&K_A)?3:2;
    for(int i=0;i<4;++i) {
        Object *o=&g->obj[i]; o->x-=speed;
        if(absn(o->x-g->x)<13 && absn(o->y-g->y)>22) damage(g);
        if(o->alive && o->x<g->x-12) { o->alive=0; ++g->score; g->sound=2; }
        if(o->x<-20) {o->x=338; o->y=43+((g->score*17+i*23)%69); o->alive=1;}
    }
    if(g->score>=24) finish(g,1);
}
static void prism(Game *g,int k) {
    Object *b=&g->obj[0];
    g->x=clamp(g->x+((k&K_RIGHT)?3:0)-((k&K_LEFT)?3:0),24,216);
    if(!g->carrying) { b->x=g->x; b->y=118; if(g->pressed&K_A) {g->carrying=1; g->sound=1;} return; }
    int old_y=b->y;
    b->x+=b->vx; b->y+=b->vy;
    if(b->x<7 || b->x>232) { b->vx=-b->vx; b->x=clamp(b->x,7,232); g->sound=2; }
    if(b->y<22) {b->vy=absn(b->vy); g->sound=2;}
    if(b->vy>0 && old_y<=119 && b->y>=119 && absn(b->x-g->x)<25) {
        b->vy=-2; b->y=118; b->vx=(b->x<g->x-8)?-2:((b->x>g->x+8)?2:(b->vx<0?-1:1)); g->sound=2;
    }
    for(int i=0;i<24;++i) if(g->bricks[i]) {
        int bx=8+(i%8)*28, by=29+(i/8)*15;
        if(b->x>=bx-2 && b->x<bx+25 && b->y>=by-2 && b->y<by+11) {
            g->bricks[i]=0; ++g->score; b->vy=-b->vy; g->sound=2; break;
        }
    }
    if(b->y>145) { --g->hp; g->sound=3; g->carrying=0; b->vx=1; b->vy=-2; if(!g->hp) finish(g,0); }
    if(g->score==24) finish(g,1);
}
static int occupied(Game *g,int x,int y) { for(int i=0;i<3;++i) if(g->obj[i].alive && g->obj[i].x==x && g->obj[i].y==y) return 1; return 0; }
static void enemies(Game *g) {
    for(int i=0;i<3 && g->hp>0;++i) {
        Object *e=&g->obj[i]; if(!e->alive) continue;
        int dist=absn(e->x-g->x)+absn(e->y-g->y);
        if(dist==1) {--g->hp; g->sound=3; continue;}
        int dx=g->x>e->x?1:-1,dy=g->y>e->y?1:-1;
        int nx=e->x,ny=e->y;
        if(e->x!=g->x && !tactics_wall(e->x+dx,e->y) && !occupied(g,e->x+dx,e->y)) nx+=dx;
        else if(e->y!=g->y && !tactics_wall(e->x,e->y+dy) && !occupied(g,e->x,e->y+dy)) ny+=dy;
        if(nx!=g->x || ny!=g->y) {e->x=nx;e->y=ny;}
    }
    if(g->hp<=0) finish(g,0);
}
static void tactics(Game *g,int k) {
    (void)k;
    if(g->pressed&K_B) g->target=(g->target+1)%3;
    int x=g->x,y=g->y, turn=0;
    if(g->pressed&K_LEFT) --x; else if(g->pressed&K_RIGHT) ++x;
    else if(g->pressed&K_UP) --y; else if(g->pressed&K_DOWN) ++y;
    if(x!=g->x || y!=g->y) {
        if(x>=0 && x<6 && y>=0 && y<5 && !tactics_wall(x,y) && !occupied(g,x,y)) {g->x=x;g->y=y;turn=1;}
    }
    if(g->pressed&K_A) {
        int best=-1;
        if(g->obj[g->target].alive && absn(g->obj[g->target].x-g->x)+absn(g->obj[g->target].y-g->y)<=2) best=g->target;
        else for(int i=0;i<3;++i) if(g->obj[i].alive && absn(g->obj[i].x-g->x)+absn(g->obj[i].y-g->y)<=2) {best=i;break;}
        if(best>=0) {Object *e=&g->obj[best]; --e->hp; g->target=best;g->sound=2;if(e->hp==0){e->alive=0;++g->score;}turn=1;}
        else {g->sound=3;g->cooldown=25;}
    }
    if(g->score==3) {finish(g,1);return;}
    if(turn) {++g->moves; enemies(g); if(g->moves>=40) finish(g,0);}
}
static void parcel(Game *g,int k) {
    int speed=(k&K_B)?1:2;
    g->x=clamp(g->x+((k&K_RIGHT)?speed:0)-((k&K_LEFT)?speed:0),14,226);
    g->y=clamp(g->y+((k&K_DOWN)?speed:0)-((k&K_UP)?speed:0),30,136);
    for(int i=0;i<3;++i) {
        Object *o=&g->obj[i];o->x+=o->vx;if(o->x<65||o->x>177)o->vx=-o->vx;
        if(absn(o->x-g->x)<10&&absn(o->y-g->y)<10) damage(g);
    }
    if(g->pressed&K_A) {
        if(!g->carrying && absn(g->x-36)<15 && absn(g->y-80)<15) {g->carrying=1;g->sound=1;}
        else if(g->carrying && absn(g->x-parcel_target_x(g->target))<16 && absn(g->y-parcel_target_y(g->target))<16) {++g->score;++g->target;g->carrying=0;g->sound=2;}
    }
    if(g->score>=5) finish(g,1);
}
API void game_step(Game *g,int keys) {
    g->pressed=keys & ~g->prev;g->prev=keys;g->sound=0;++g->frame;
    if(g->phase!=PLAY) {if(g->pressed&K_START) {begin(g);g->prev=keys;}return;}
    if(g->pressed&K_SELECT) {g->phase=TITLE;return;}
    ++g->tick;if(g->cooldown>0)--g->cooldown;
    if(g->time>0 && --g->time==0) {finish(g,0);return;}
    if(g->kind==DRIFT)drift(g,keys);else if(g->kind==CLOUD)cloud(g,keys);
    else if(g->kind==PRISM)prism(g,keys);else if(g->kind==TACTICS)tactics(g,keys);else parcel(g,keys);
}
