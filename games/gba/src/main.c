/* SPDX-License-Identifier: GPL-3.0-only */
#include "arcade.h"
typedef unsigned short u16;
#define REG16(a) (*(volatile u16*)(a))
Game game __attribute__((section(".game")));
void video_init(void);
void video_draw(const Game*,int);
/* Original short pentatonic motifs. Frequency values drive real GBA PSG. */
static const unsigned short notes[5][8]={
 {1798,1849,1881,1899,1881,1849,1798,1849},
 {1849,1881,1923,1899,1881,1849,1881,1899},
 {1798,1881,1849,1923,1899,1881,1849,1881},
 {1720,1798,1849,1798,1720,1798,1881,1849},
 {1881,1849,1798,1849,1881,1899,1923,1881}
};
static void audio_init(void) {REG16(0x04000084)=0x80;REG16(0x04000080)=0x3377;REG16(0x04000082)=2;REG16(0x04000060)=0;}
static void audio_step(void) {
    if((game.frame%24)==0) {REG16(0x04000062)=0x5180;REG16(0x04000064)=0x8000|notes[game.kind][(game.frame/24)%8];}
    if(game.sound){unsigned f=game.sound==3?1400:game.sound==4?1950:game.sound==5?1250:game.sound==2?1900:1849;
        REG16(0x04000068)=0xA180;REG16(0x0400006C)=0x8000|f;
    }
}
int main(void) {
    game_init(&game,GAME_KIND);video_init();audio_init();
    int page=1;
    for(;;){
        unsigned keys=(~REG16(0x04000130))&0x3FF;
        game_step(&game,(int)keys);audio_step();video_draw(&game,page);
        while(REG16(0x04000006)>=160){}
        while(REG16(0x04000006)<160){}
        REG16(0x04000000)=(u16)(0x0404|(page?0x10:0));page^=1;
    }
}
/* Freestanding integer helpers: no compiler runtime is shipped in the ROM. */
unsigned __aeabi_uidiv(unsigned n,unsigned d) {unsigned q=0,r=0;if(!d)return 0;for(int i=31;i>=0;--i){r=(r<<1)|((n>>i)&1);if(r>=d){r-=d;q|=1u<<i;}}return q;}
int __aeabi_idiv(int n,int d) {int neg=(n<0)^(d<0);unsigned q=__aeabi_uidiv(n<0?0u-(unsigned)n:(unsigned)n,d<0?0u-(unsigned)d:(unsigned)d);return neg?-(int)q:(int)q;}
void *memset(void *dst,int value,unsigned n){unsigned char*p=dst;while(n--)*p++=(unsigned char)value;return dst;}
void *memcpy(void *dst,const void *src,unsigned n){unsigned char*d=dst;const unsigned char*s=src;while(n--)*d++=*s++;return dst;}
void __aeabi_memclr4(void *dst,unsigned n){memset(dst,0,n);}
void __aeabi_memclr(void *dst,unsigned n){memset(dst,0,n);}
void __aeabi_memcpy(void *dst,const void *src,unsigned n){memcpy(dst,src,n);}
void __aeabi_memcpy4(void *dst,const void *src,unsigned n){memcpy(dst,src,n);}
