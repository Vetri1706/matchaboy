/* SPDX-License-Identifier: GPL-3.0-only
 * The compact 5x7 glyphs and every shape below are original, hand-authored here.
 */
#include "arcade.h"
typedef unsigned short u16;
static volatile u16 *fb;
enum { INK, NAVY, WHITE, MINT, TEAL, GOLD, ORANGE, PINK, BLUE, PURPLE, GRAY, ROAD, GREEN, RED, PALE, SKY };
static const unsigned char font[39][7]={
 {14,17,17,31,17,17,17},{30,17,17,30,17,17,30},{14,17,16,16,16,17,14},
 {30,17,17,17,17,17,30},{31,16,16,30,16,16,31},{31,16,16,30,16,16,16},
 {14,17,16,23,17,17,14},{17,17,17,31,17,17,17},{31,4,4,4,4,4,31},
 {7,2,2,2,18,18,12},{17,18,20,24,20,18,17},{16,16,16,16,16,16,31},
 {17,27,21,21,17,17,17},{17,25,21,19,17,17,17},{14,17,17,17,17,17,14},
 {30,17,17,30,16,16,16},{14,17,17,17,21,18,13},{30,17,17,30,20,18,17},
 {15,16,16,14,1,1,30},{31,4,4,4,4,4,4},{17,17,17,17,17,17,14},
 {17,17,17,17,17,10,4},{17,17,17,21,21,21,10},{17,17,10,4,10,17,17},
 {17,17,10,4,4,4,4},{31,1,2,4,8,16,31},
 {14,17,19,21,25,17,14},{4,12,4,4,4,4,14},{14,17,1,2,4,8,31},
 {30,1,1,14,1,1,30},{2,6,10,18,31,2,2},{31,16,16,30,1,1,30},
 {14,16,16,30,17,17,14},{31,1,2,4,8,8,8},{14,17,17,14,17,17,14},
 {14,17,17,15,1,1,14},{0,4,4,0,4,4,0},{0,1,2,4,8,16,0},{0,0,0,31,0,0,0}
};
static volatile u16 dma_color __attribute__((aligned(4)));
static void span_fill(volatile u16 *p,int n,u16 pair) {
    if(n>=6){
        /* DMA reads memory outside the C abstract machine. Keep its source
         * volatile so the compiler must materialize the colour before MMIO. */
        dma_color=pair;
        *(volatile unsigned*)0x040000D4=(unsigned)&dma_color;
        *(volatile unsigned*)0x040000D8=(unsigned)p;
        *(volatile unsigned*)0x040000DC=0x81000000u|(unsigned)n;
    } else while(n--)*p++=pair;
}
static void rect(int x,int y,int w,int h,int c) {
    if(x<0){w+=x;x=0;}if(y<0){h+=y;y=0;}if(x+w>240)w=240-x;if(y+h>160)h=160-y;
    if(w<=0||h<=0)return;
    u16 pair=(u16)(c|(c<<8));
    if(x==0&&w==240){span_fill(fb+y*120,h*120,pair);return;}
    for(int yy=y;yy<y+h;++yy){
        volatile u16 *p=fb+yy*120+x/2;int n=w;
        if(x&1){*p=(*p&255)|(c<<8);++p;--n;}
        span_fill(p,n/2,pair);p+=n/2;
        if(n&1)*p=(*p&0xFF00)|c;
    }
}
static void text(int x,int y,const char *s,int c,int size) {
    while(*s){int ch=*s++,i=-1;if(ch>='A'&&ch<='Z')i=ch-'A';else if(ch>='0'&&ch<='9')i=ch-'0'+26;
        else if(ch==':')i=36;else if(ch=='/')i=37;else if(ch=='-')i=38;
        if(i>=0 && size==1 && x>=0 && x+5<240 && y>=0 && y+7<160){
            u16 color=(u16)(c|(c<<8));volatile u16 *p=fb+y*120+x/2;
            for(int row=0;row<7;++row,p+=120){unsigned bits=font[i][row];if(!bits)continue;
                u16 a,b,d;
                if(x&1){a=(bits&16)?0xFF00:0;b=((bits&8)?255:0)|((bits&4)?0xFF00:0);d=((bits&2)?255:0)|((bits&1)?0xFF00:0);}
                else {a=((bits&16)?255:0)|((bits&8)?0xFF00:0);b=((bits&4)?255:0)|((bits&2)?0xFF00:0);d=(bits&1)?255:0;}
                p[0]=(p[0]&~a)|(color&a);p[1]=(p[1]&~b)|(color&b);p[2]=(p[2]&~d)|(color&d);
            }
        }else if(i>=0 && size!=1)for(int row=0;row<7;++row)for(int col=0;col<5;++col)if(font[i][row]&(16>>col))rect(x+col*size,y+row*size,size,size,c);
        x+=6*size;
    }
}
static void number(int x,int y,int n,int c) {char b[8];int len=0;if(n<0)n=0;do{b[len++]=(char)('0'+n%10);n/=10;}while(n&&len<7);for(int i=0;i<len/2;++i){char v=b[i];b[i]=b[len-1-i];b[len-1-i]=v;}b[len]=0;text(x,y,b,c,1);}
static void frame(int x,int y,int w,int h,int color) {rect(x,y,w,1,color);rect(x,y+h-1,w,1,color);rect(x,y,1,h,color);rect(x+w-1,y,1,h,color);}
static void car(int x,int y,int color) {
    rect(x-8,y-10,3,6,INK);rect(x+5,y-10,3,6,INK);rect(x-8,y+5,3,6,INK);rect(x+5,y+5,3,6,INK);
    rect(x-5,y-12,10,25,color);rect(x-4,y-6,8,7,NAVY);rect(x-4,y+6,8,3,NAVY);rect(x-3,y-11,2,2,WHITE);rect(x+2,y-11,2,2,WHITE);
}
static void plane(int x,int y,int c) {rect(x-11,y-2,24,5,c);rect(x-3,y-9,7,19,c);rect(x-9,y-5,3,11,c);rect(x+3,y-1,9,3,WHITE);}
static void parcel_icon(int x,int y) {rect(x-5,y-5,10,10,GOLD);rect(x-1,y-5,2,10,ORANGE);rect(x-5,y-1,10,2,ORANGE);}
static void person(int x,int y,int c) {rect(x-3,y-7,6,5,PALE);rect(x-5,y-2,10,7,c);rect(x-4,y+5,3,4,INK);rect(x+1,y+5,3,4,INK);}
static void title_art(int kind,int tick) {
    if(kind==DRIFT){rect(10,24,220,49,TEAL);rect(10,36,220,29,ROAD);for(int x=12;x<230;x+=22)rect(x,50,12,2,PALE);car(65,50,MINT);car(172,50,PINK);}
    else if(kind==CLOUD){rect(10,24,220,49,SKY);rect(26,32,34,5,WHITE);rect(20,37,48,7,WHITE);rect(155,57,55,8,WHITE);plane(109+(tick/8)%12,49,GOLD);}
    else if(kind==PRISM){for(int i=0;i<16;++i)rect(19+(i%8)*26,25+(i/8)*14,23,10,3+i%7);rect(98,65,44,5,MINT);rect(135,55,4,4,WHITE);}
    else if(kind==TACTICS){for(int i=0;i<24;++i)rect(25+(i%8)*24,24+(i/8)*16,22,14,(i&1)?TEAL:ROAD);person(61,50,MINT);person(157,34,PINK);person(181,66,ORANGE);}
    else {rect(10,24,220,49,TEAL);rect(20,43,202,18,ROAD);for(int i=0;i<6;++i)rect(27+i*34,24,24,15,BLUE+i%3);person(110,52,MINT);parcel_icon(126,45);}
}
static void title(const Game *g) {
    const char *names[5]={"DRIFT CIRCUIT","CLOUD PILOT","PRISM BREAK","TINY TACTICS","PARCEL DASH"};
    const char *goal[5]={"FINISH THE 3600M TIME TRIAL","FLY THROUGH ALL 24 GATES","BREAK ALL 24 PRISM BRICKS","DEFEAT THREE SENTRIES","DELIVER FIVE PARCELS IN 60S"};
    const char *keys1[5]={"LEFT RIGHT STEER   A BOOST","UP DOWN FLY   A SPEED UP","LEFT RIGHT PADDLE  A LAUNCH","ARROWS MOVE   A ATTACK","ARROWS MOVE   A PICK OR DROP"};
    const char *keys2[5]={"B BRAKE   KEEP OFF THE VERGE","AVOID WALLS   THREE SHIELDS","THREE BALLS   AIM YOUR BOUNCE","RANGE TWO   B CYCLES TARGET","B SLOW WALK   AVOID TRAFFIC"};
    title_art(g->kind,g->frame);
    text(12,6,"MATCHABOY ORIGINALS",MINT,1);text(12,79,names[g->kind],WHITE,2);
    text(12,99,goal[g->kind],GOLD,1);text(12,113,keys1[g->kind],PALE,1);text(12,125,keys2[g->kind],PALE,1);
    rect(12,141,216,13,TEAL);text(33,144,"PRESS START TO PLAY",WHITE,1);
}
static void hud(const Game *g) {
    const char *titles[5]={"DRIFT CIRCUIT","CLOUD PILOT","PRISM BREAK","TINY TACTICS","PARCEL DASH"};
    rect(0,0,240,19,NAVY);text(6,6,titles[g->kind],WHITE,1);
    text(150,6,g->kind==TACTICS?"HP":"LIFE",MINT,1);number(180,6,g->hp,MINT);
    if(g->kind!=TACTICS){number(202,6,g->time/60,GOLD);text(223,6,"S",GOLD,1);}else{number(202,6,40-g->moves,GOLD);text(222,6,"T",GOLD,1);}
    rect(0,143,240,17,NAVY);
}
static void drift_draw(const Game *g) {
    rect(0,19,240,124,TEAL);
    int center=road_center(g->stage);
    rect(center-51,19,102,124,WHITE);rect(center-47,19,94,124,ROAD);
    for(int y=20+(g->stage%22);y<143;y+=22){rect(center-2,y,4,10,PALE);rect(center-54,y,3,8,PINK);rect(center+51,y,3,8,PINK);}
    for(int i=0;i<8;++i)if(g->obj[i].y>8&&g->obj[i].y<153)car(g->obj[i].x,g->obj[i].y,i&1?ORANGE:PINK);
    if(!g->cooldown||(g->cooldown&4))car(g->x,g->y,MINT);
    rect(8,26,4,100,NAVY);rect(8,126-(g->stage*100)/3600,4,(g->stage*100)/3600,MINT);
}
static void cloud_draw(const Game *g) {
    rect(0,19,240,124,SKY);
    for(int i=0;i<6;++i){int x=(270+i*63-g->tick/2)%300-30;rect(x,29+i*17,33,3,WHITE);rect(x+6,25+i*17,18,5,WHITE);}
    for(int i=0;i<4;++i){const Object *o=&g->obj[i];rect(o->x-7,19,14,o->y-28-19,TEAL);rect(o->x-7,o->y+28,14,143-o->y-28,TEAL);rect(o->x-9,o->y-32,18,5,GOLD);rect(o->x-9,o->y+27,18,5,GOLD);}
    if(!g->cooldown||(g->cooldown&4))plane(g->x,g->y,ORANGE);
}
static void prism_draw(const Game *g) {
    frame(3,20,234,121,TEAL);
    for(int i=0;i<24;++i)if(g->bricks[i]){int x=8+(i%8)*28,y=29+(i/8)*15;rect(x,y,24,10,3+(i%7));rect(x+2,y+1,20,2,PALE);}
    rect(g->x-23,121,46,5,MINT);rect(g->x-20,121,40,1,WHITE);rect(g->obj[0].x-2,g->obj[0].y-2,4,4,WHITE);
    if(!g->carrying)text(72,89,"A TO LAUNCH",GOLD,1);
}
static void tactics_draw(const Game *g) {
    for(int y=0;y<5;++y)for(int x=0;x<6;++x){int xx=8+x*25,yy=21+y*24;rect(xx,yy,23,22,(x+y)&1?TEAL:ROAD);if(tactics_wall(x,y)){rect(xx+4,yy+3,15,16,GRAY);rect(xx+4,yy+3,15,2,PALE);}}
    person(19+g->x*25,31+g->y*24,MINT);
    for(int i=0;i<3;++i)if(g->obj[i].alive){int x=19+g->obj[i].x*25,y=31+g->obj[i].y*24;person(x,y,PINK);if(g->target==i)frame(x-9,y-11,19,22,GOLD);for(int h=0;h<g->obj[i].hp;++h)rect(x-4+h*5,y+10,4,2,WHITE);}
    text(165,27,"SENTRIES",GOLD,1);number(189,41,3-g->score,WHITE);
    text(165,59,"A ATTACK",WHITE,1);text(165,71,"RANGE 2",PALE,1);text(165,91,"B TARGET",WHITE,1);
    text(165,110,"MOVE OR",PALE,1);text(165,121,"FIRE/TURN",PALE,1);
}
static void parcel_draw(const Game *g) {
    rect(0,19,240,124,TEAL);
    rect(60,22,120,118,ROAD);rect(7,68,226,25,ROAD);
    for(int i=0;i<4;++i){rect(80+i*26,23,2,114,GRAY);rect(61,43+i*27,117,2,GRAY);}
    for(int i=0;i<6;++i){int x=(i<3)?14:194,y=25+(i%3)*39;rect(x,y,32,24,BLUE+i%3);rect(x+4,y+3,24,3,PALE);rect(x+4,y+10,5,6,GOLD);rect(x+22,y+10,5,6,GOLD);}
    rect(22,68,28,25,MINT);text(23,72,"POST",INK,1);parcel_icon(36,85);
    if(g->carrying){int x=parcel_target_x(g->target),y=parcel_target_y(g->target);frame(x-13,y-13,26,26,GOLD);rect(x-3,y-19,6,5,GOLD);}
    for(int i=0;i<3;++i){const Object *o=&g->obj[i];rect(o->x-7,o->y-5,14,10,PINK);rect(o->x-4,o->y-3,8,6,NAVY);}
    if(!g->cooldown||(g->cooldown&4))person(g->x,g->y,MINT);
    if(g->carrying)parcel_icon(g->x+8,g->y-7);
}
void draw_game(const Game *g) {
    if(g->phase==TITLE){rect(0,0,240,160,INK);title(g);return;}
    if(g->kind==PRISM||g->kind==TACTICS)rect(0,0,240,160,INK);
    if(g->kind==DRIFT)drift_draw(g);else if(g->kind==CLOUD)cloud_draw(g);else if(g->kind==PRISM)prism_draw(g);else if(g->kind==TACTICS)tactics_draw(g);else parcel_draw(g);
    hud(g);
    /* Render each footer once; it stays separate from the scrolling playfield. */
    if(g->kind==DRIFT){text(8,147,"DIST",PALE,1);number(38,147,g->stage,WHITE);text(70,147,"/3600  A BOOST B BRAKE",PALE,1);}
    else if(g->kind==CLOUD){text(8,147,"GATES",PALE,1);number(45,147,g->score,WHITE);text(64,147,"/24   UP DOWN FLY",PALE,1);}
    else if(g->kind==PRISM){text(8,147,"BRICKS",PALE,1);number(52,147,g->score,WHITE);text(72,147,"/24  A LAUNCH",PALE,1);}
    else if(g->kind==TACTICS)text(8,147,g->cooldown?"NO ENEMY IN RANGE":"MOVE OR FIRE: ENEMIES GET A TURN",g->cooldown?ORANGE:PALE,1);
    else {text(8,147,"DELIVERED",PALE,1);number(68,147,g->score,WHITE);text(79,147,g->carrying?"/5  FIND GOLD ADDRESS":"/5  A AT POST: PICK UP",PALE,1);}
    if(g->phase==WON||g->phase==LOST){rect(18,48,204,75,NAVY);frame(18,48,204,75,g->phase==WON?MINT:PINK);
        text(36,59,g->phase==WON?"MISSION CLEAR":"ROUND OVER",g->phase==WON?MINT:PINK,2);
        const char *won[5]={"TIME TRIAL COMPLETE","ALL 24 GATES CLEARED","ALL 24 BRICKS BROKEN","THREE SENTRIES DEFEATED","FIVE PARCELS DELIVERED"};
        text(30,82,g->phase==WON?won[g->kind]:(g->time==0&&g->kind!=TACTICS?"OUT OF TIME":"TRY A NEW APPROACH"),WHITE,1);
        text(30,107,"PRESS START TO RESTART",GOLD,1);
    }
}
#ifndef HOST_TEST
void video_draw(const Game *g,int page) {fb=(volatile u16*)(page?0x0600A000:0x06000000);draw_game(g);}
#define RGB(r,g,b) ((r)|((g)<<5)|((b)<<10))
void video_init(void) {
    const u16 colors[16]={RGB(2,4,7),RGB(4,7,11),RGB(30,31,30),RGB(12,30,23),RGB(5,14,15),RGB(31,26,10),RGB(31,16,7),RGB(30,11,20),RGB(8,17,29),RGB(20,14,29),RGB(14,17,20),RGB(8,10,15),RGB(9,22,13),RGB(30,7,9),RGB(25,29,27),RGB(9,22,29)};
    volatile u16 *p=(volatile u16*)0x05000000;for(int i=0;i<16;++i)p[i]=colors[i];
    *(volatile u16*)0x04000000=0x0404;
}
#endif
