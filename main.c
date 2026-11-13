// MEU PLAYER MP3 - PS3 PSL1GHT - FUNCIONANDO
// Cima/Baixo = escolher | X = tocar | Quadrado = pausar | L1/R1 = anterior/proxima | Esq/Dir = volume
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdint.h>
#include <math.h>
#include <ctype.h>
#include <dirent.h>
#include <unistd.h>
#include <malloc.h>
#include <ppu-types.h>
#include <sysutil/sysutil.h>
#include <sysutil/video.h>
#include <rsx/rsx.h>
#include <io/pad.h>
#include <audio/audio.h>

#define MINIMP3_IMPLEMENTATION
#include "minimp3.h"

#define CB_SIZE (0x100000)
#define HOST_SIZE (32*1024*1024)
#define MAXTR 64
#define PATHLEN 300
#define NBLK 8
#define BLKS 256

static gcmContextData *ctx;
static u32 W,H, *fb[2], fboff[2], *back;
static int cur=0, first_flip=1;
static volatile int running=1;
static float S=1.0f;
static void audio_pump(void);
static void sysutil_cb(u64 s,u64 p,void*u){ (void)p;(void)u; if(s==SYSUTIL_EXIT_GAME) running=0; }

static void init_screen(void){
  void *host=memalign(1024*1024,HOST_SIZE); rsxInit(&ctx,CB_SIZE,HOST_SIZE,host);
  videoState st; videoGetState(VIDEO_PRIMARY,0,&st);
  u32 resid=VIDEO_RESOLUTION_720; videoResolution res; videoGetResolution(resid,&res);
  W=res.width; H=res.height; videoConfiguration vc; memset(&vc,0,sizeof(vc));
  vc.resolution=resid; vc.format=VIDEO_BUFFER_FORMAT_XRGB; vc.aspect=VIDEO_ASPECT_AUTO; vc.pitch=W*4;
  videoConfigure(VIDEO_PRIMARY,&vc,NULL,0); videoGetState(VIDEO_PRIMARY,0,&st);
  gcmSetFlipMode(GCM_FLIP_VSYNC);
  for(int i=0;i<2;i++){ fb[i]=(u32*)rsxMemalign(64,W*H*4); gcmAddressToOffset(fb[i],&fboff[i]); gcmSetDisplayBuffer(i,fboff[i],W*4,W,H); }
  back=(u32*)memalign(64,W*H*4); memset(back,0,W*H*4); gcmResetFlipStatus();
}
static void wait_flip(void){ while(gcmGetFlipStatus()!=0){ audio_pump(); usleep(500);} gcmResetFlipStatus(); }
static void present(void){ if(!first_flip) wait_flip(); else gcmResetFlipStatus(); memcpy(fb[cur],back,W*H*4); gcmSetFlip(ctx,cur); rsxFlushBuffer(ctx); gcmSetWaitFlip(ctx); cur^=1; first_flip=0; }
static void fill_rect(int x,int y,int w,int h,u32 c){ int x0=x<0?0:x,y0=y<0?0:y,x1=x+w,y1=y+h; if(x1>(int)W)x1=W; if(y1>(int)H)y1=H; for(int yy=y0;yy<y1;yy++){ u32*p=back+yy*W; for(int xx=x0;xx<x1;xx++) p[xx]=c; } }
static void R(float x,float y,float w,float h,u32 c){ fill_rect((int)(x*S),(int)(y*S),(int)(w*S)+1,(int)(h*S)+1,c); }

typedef struct{char c; const char *p;} Glyph;
static const Glyph glyphs[]={
 {'0',"###" "#.#" "#.#" "#.#" "###"},{'1',".#." "##." ".#." ".#." "###"},
 {'2',"###" "..#" "###" "#.." "###"},{'3',"###" "..#" "###" "..#" "###"},
 {'4',"#.#" "#.#" "###" "..#" "..#"},{'5',"###" "#.." "###" "..#" "###"},
 {'6',"###" "#.." "###" "#.#" "###"},{'7',"###" "..#" "..#" "..#" "..#"},
 {'8',"###" "#.#" "###" "#.#" "###"},{'9',"###" "#.#" "###" "..#" "###"},
 {':',"... " ".#." "..." ".#." "..."},{'.',"... " "..." "..." "..." ".#."},
 {'-',"... " "..." "###" "..." "..."},{'_',"... " "..." "..." "..." "###"},
 {'>',"#.." "##." "###" "##." "#.."},{'+',"... " ".#." "###" ".#." "..."},
 {'/',"..#" "..#" ".#." "#.." "#.."},
 {'A',".#." "#.#" "###" "#.#" "#.#"},{'B',"##." "#.#" "##." "#.#" "##."},
 {'C',"###" "#.." "#.." "#.." "###"},{'D',"##." "#.#" "#.#" "#.#" "##."},
 {'E',"###" "#.." "###" "#.." "###"},{'F',"###" "#.." "###" "#.." "#.."},
 {'G',"###" "#.." "#.#" "#.#" "###"},{'H',"#.#" "#.#" "###" "#.#" "#.#"},
 {'I',"###" ".#." ".#." ".#." "###"},{'J',"..#" "..#" "..#" "#.#" "###"},
 {'K',"#.#" "#.#" "##." "#.#" "#.#"},{'L',"#.." "#.." "#.." "#.." "###"},
 {'M',"#.#" "###" "###" "#.#" "#.#"},{'N',"###" "#.#" "#.#" "#.#" "#.#"},
 {'O',"###" "#.#" "#.#" "#.#" "###"},{'P',"###" "#.#" "###" "#.." "#.."},
 {'Q',"###" "#.#" "#.#" "###" "..#"},{'R',"##." "#.#" "##." "#.#" "#.#"},
 {'S',"###" "#.." "###" "..#" "###"},{'T',"###" ".#." ".#." ".#." ".#."},
 {'U',"#.#" "#.#" "#.#" "#.#" "###"},{'V',"#.#" "#.#" "#.#" "#.#" ".#."},
 {'W',"#.#" "#.#" "###" "###" "#.#"},{'X',"#.#" "#.#" ".#." "#.#" "#.#"},
 {'Y',"#.#" "#.#" ".#." ".#." ".#."},{'Z',"###" "..#" ".#." "#.." "###"},
};
static const char *find_glyph(char c){ for(unsigned i=0;i<sizeof(glyphs)/sizeof(glyphs[0]);i++) if(glyphs[i].c==c) return glyphs[i].p; return NULL; }
static int text_width(const char *s,int sc){ return (int)strlen(s)*4*sc-sc; }
static void draw_text(const char *s,int x,int y,int sc,u32 c){ for(;*s;s++){ const char *g=find_glyph((char)toupper((unsigned char)*s)); if(g) for(int r=0;r<5;r++) for(int k=0;k<3;k++) if(g[r*3+k]=='#') fill_rect(x+k*sc,y+r*sc,sc,sc,c); x+=4*sc; } }
static void draw_center(const char *s,int xc,int y,int sc,u32 c){ draw_text(s,xc-text_width(s,sc)/2,y,sc,c); }

static char tpath[MAXTR][PATHLEN]; static char tname[MAXTR][48];
static int ntr=0, sel=0, playing=-1, paused=0;
static const char *dirs[]={"/dev_hdd0/game/MUSI00001/USRDIR/music","/dev_hdd0/music","/dev_usb000/music","/dev_usb001/music"};
static int has_audio(const char *n){ size_t l=strlen(n); if(l<5) return 0; const char *e=n+l-4; if(e[0]!='.') return 0; int mp3=tolower((unsigned char)e[1])=='m'&&tolower((unsigned char)e[2])=='p'&&tolower((unsigned char)e[3])=='3'; int wav=tolower((unsigned char)e[1])=='w'&&tolower((unsigned char)e[2])=='a'&&tolower((unsigned char)e[3])=='v'; return mp3||wav; }
static void scan_music(void){ ntr=0; for(unsigned d=0;d<sizeof(dirs)/sizeof(dirs[0]);d++){ DIR *dp=opendir(dirs[d]); if(!dp) continue; struct dirent *e; while((e=readdir(dp))!=NULL&&ntr<MAXTR){ if(!has_audio(e->d_name)) continue; snprintf(tpath[ntr],PATHLEN,"%s/%s",dirs[d],e->d_name); snprintf(tname[ntr],sizeof(tname[ntr]),"%s",e->d_name); size_t ln=strlen(tname[ntr]); if(ln>4) tname[ntr][ln-4]=0; ntr++; } closedir(dp); } for(int i=0;i<ntr;i++) for(int j=i+1;j<ntr;j++) if(strcasecmp(tname[j],tname[i])<0){ char a[PATHLEN],b[48]; strcpy(a,tpath[i]); strcpy(tpath[i],tpath[j]); strcpy(tpath[j],a); strcpy(b,tname[i]); strcpy(tname[i],tname[j]); strcpy(tname[j],b); } }

static u32 aport; static audioPortConfig acfg; static float *abuf; static volatile u32 *arix; static int wblk=0,audio_ok=0;
static float volume=0.8f, level=0.0f;
static int16_t *pcm=NULL; static long pcm_total=0; static double pcm_pos=0, pcm_step=1.0; static long pcm_rate=44100; static int track_done=0;
static void free_pcm(void){ if(pcm) free(pcm); pcm=NULL; pcm_total=0; pcm_pos=0; track_done=0; }
static unsigned rd16(const unsigned char *p){ return p[0]|(p[1]<<8); }
static unsigned long rd32(const unsigned char *p){ return p[0]|(p[1]<<8)|(p[2]<<16)|((unsigned long)p[3]<<24); }

static int open_wav_file(const char *path){
  FILE *f=fopen(path,"rb"); if(!f) return 0; unsigned char h[12],ck[8];
  if(fread(h,1,12,f)!=12||memcmp(h,"RIFF",4)||memcmp(h+8,"WAVE",4)){ fclose(f); return 0; }
  int fmt_ok=0,bits=16,ch=2; long rate=44100, data_pos=0,data_len=0;
  for(;;){ if(fread(ck,1,8,f)!=8){ fclose(f); return 0; } unsigned long sz=rd32(ck+4);
    if(!memcmp(ck,"fmt ",4)){ unsigned char ff[16]; if(sz<16||fread(ff,1,16,f)!=16){ fclose(f); return 0; } unsigned tag=rd16(ff); ch=rd16(ff+2); rate=(long)rd32(ff+4); bits=rd16(ff+14); fmt_ok=(tag==1||tag==0xFFFE)&&bits==16&&(ch==1||ch==2)&&rate>0; if(sz>16) fseek(f,(long)(sz-16+(sz&1)),SEEK_CUR); else if(sz&1) fseek(f,1,SEEK_CUR);
    } else if(!memcmp(ck,"data",4)){ if(!fmt_ok){ fclose(f); return 0; } data_pos=ftell(f); data_len=(long)sz; break; } else fseek(f,(long)(sz+(sz&1)),SEEK_CUR); }
  long frames=data_len/(ch*2); pcm=(int16_t*)malloc((frames+2)*2*sizeof(int16_t)); if(!pcm){ fclose(f); return 0; }
  fseek(f,data_pos,SEEK_SET); unsigned char *tmp=(unsigned char*)malloc(frames*ch*2); if(fread(tmp,1,frames*ch*2,f)!=(size_t)(frames*ch*2)){ free(tmp); fclose(f); free_pcm(); return 0; }
  for(long i=0;i<frames;i++){ int16_t l=(int16_t)(tmp[i*ch*2]|(tmp[i*ch*2+1]<<8)); int16_t r=l; if(ch==2) r=(int16_t)(tmp[i*ch*2+2]|(tmp[i*ch*2+3]<<8)); pcm[2*i]=l; pcm[2*i+1]=r; }
  free(tmp); fclose(f); pcm_total=frames; pcm_rate=rate; pcm_pos=0; pcm_step=(double)rate/48000.0; track_done=0; return 1;
}

static int open_mp3_file(const char *path){
  FILE *f=fopen(path,"rb"); if(!f) return 0; fseek(f,0,SEEK_END); long fsize=ftell(f); fseek(f,0,SEEK_SET); if(fsize<=0){ fclose(f); return 0; }
  uint8_t *data=(uint8_t*)malloc(fsize); if(!data){ fclose(f); return 0; } if(fread(data,1,fsize,f)!=(size_t)fsize){ free(data); fclose(f); return 0; } fclose(f);
  mp3dec_t dec; mp3dec_init(&dec); mp3dec_frame_info_t info;
  long cap=44100*120; pcm=(int16_t*)malloc(cap*2*sizeof(int16_t)); if(!pcm){ free(data); return 0; }
  long total=0, off=0; int16_t tmp[MINIMP3_MAX_SAMPLES_PER_FRAME];
  while(off<fsize){ int samples=mp3dec_decode_frame(&dec,data+off,(int)(fsize-off),tmp,&info); if(info.frame_bytes<=0) break; off+=info.frame_bytes; if(samples>0){ if(total+samples>=cap){ cap*=2; pcm=(int16_t*)realloc(pcm,cap*2*sizeof(int16_t)); if(!pcm){ free(data); return 0; } } for(int i=0;i<samples;i++){ int16_t l=tmp[2*i]; int16_t r=(info.channels==1?l:tmp[2*i+1]); pcm[2*(total+i)]=l; pcm[2*(total+i)+1]=r; } total+=samples; pcm_rate=info.hz; } if(info.frame_bytes==0) break; }
  free(data); if(total==0){ free_pcm(); return 0; } pcm_total=total; pcm_pos=0; pcm_step=(double)pcm_rate/48000.0; track_done=0; return 1;
}

static int open_track_file(const char *path){ free_pcm(); size_t l=strlen(path); if(l>=4&&tolower((unsigned char)path[l-3])=='m'&&tolower((unsigned char)path[l-2])=='p'&&tolower((unsigned char)path[l-1])=='3') return open_mp3_file(path); return open_wav_file(path); }
static void fill_block(float *out){ float peak=0; for(int n=0;n<BLKS;n++){ float l=0,r=0; if(pcm&&playing>=0&&!paused&&!track_done){ long i=(long)pcm_pos; float fr=(float)(pcm_pos-(double)i); if(i+1>=pcm_total) track_done=1; else{ const int16_t *a=pcm+2*i; float al=a[0],ar=a[1],bl=a[2],br=a[3]; l=(al+(bl-al)*fr)/32768.0f*volume; r=(ar+(br-ar)*fr)/32768.0f*volume; pcm_pos+=pcm_step; } } out[2*n]=l; out[2*n+1]=r; float m=fabsf(l)>fabsf(r)?fabsf(l):fabsf(r); if(m>peak) peak=m; } level=level*0.85f+peak*0.15f; }
static void audio_pump(void){ if(!audio_ok) return; u32 rd=*arix; int guard=NBLK; while((int)rd!=wblk&&guard-->0){ fill_block(abuf+wblk*BLKS*2); wblk=(wblk+1)%NBLK; } }
static void audio_start(void){ audioPortParam p; memset(&p,0,sizeof(p)); p.numChannels=2; p.numBlocks=NBLK; p.attr=0; p.level=1.0f; if(audioInit()!=0) return; if(audioPortOpen(&p,&aport)!=0) return; audioGetPortConfig(aport,&acfg); abuf=(float*)(uintptr_t)acfg.audioDataStart; arix=(volatile u32*)(uintptr_t)acfg.readIndex; memset(abuf,0,NBLK*BLKS*2*sizeof(float)); wblk=0; audioPortStart(aport); audio_ok=1; }
static void play_track(int i){ if(i<0||i>=ntr) return; playing=-1; if(open_track_file(tpath[i])){ playing=i; sel=i; paused=0; } }

static int prev_btn[8]; static int edge(int idx,int now){ int e=now&&!prev_btn[idx]; prev_btn[idx]=now; return e; }
static void handle_input(void){ padInfo info; padData d; ioPadGetInfo(&info); if(!info.status[0]) return; ioPadGetData(0,&d); if(edge(0,d.BTN_UP)&&ntr) sel=(sel+ntr-1)%ntr; if(edge(1,d.BTN_DOWN)&&ntr) sel=(sel+1)%ntr; if(edge(2,d.BTN_CROSS)) play_track(sel); if(edge(3,d.BTN_SQUARE)&&playing>=0) paused=!paused; if(edge(4,d.BTN_R1)&&ntr) play_track(((playing>=0?playing:sel)+1)%ntr); if(edge(5,d.BTN_L1)&&ntr) play_track(((playing>=0?playing:sel)+ntr-1)%ntr); if(d.BTN_LEFT&&volume>0.0f) volume-=0.01f; if(d.BTN_RIGHT&&volume<1.0f) volume+=0.01f; if(volume<0) volume=0; if(volume>1) volume=1; }
static void fmt_time(char *b,long sec){ sprintf(b,"%ld:%02ld",sec/60,sec%60); }
static void render(void){ char buf[64]; int ui=(int)(4*S+0.5f); if(ui<3) ui=3; for(int i=0;i<36;i++){ u32 col=((u32)(14+i/3)<<16)|((u32)(18+i/2)<<8)|(u32)(40+i); R(0,i*20,1280,21,col); } draw_text("MEU PLAYER MP3",(int)(40*S),(int)(24*S),ui*2,0x0055D6FF); if(ntr==0){ draw_center("NO FILES",(int)W/2,(int)(260*S),ui*3,0x00FF4D4D); draw_center("PUT MP3 IN MUSIC",(int)W/2,(int)(260*S)+20*ui,ui*2,0x00FFFFFF); return; } int rows=9; int top=sel-rows/2; if(top<0) top=0; if(top>ntr-rows) top=ntr-rows; if(top<0) top=0; for(int r=0;r<rows&&top+r<ntr;r++){ int i=top+r; int y=(int)(110*S)+r*(int)(44*S); if(i==sel) R(30,110+r*44-4,1220,40,0x00223A66); u32 col=(i==playing)?0x0066FF66:0x00FFFFFF; if(i==playing) draw_text(">",(int)(46*S),y,ui*2,col); char nm[40]; snprintf(nm,sizeof(nm),"%s",tname[i]); draw_text(nm,(int)(90*S),y,ui*2,col); } R(40,560,1200,12,0x00303A55); if(playing>=0&&pcm_total>0){ float frac=(float)(pcm_pos/(double)pcm_total); if(frac>1) frac=1; R(40,560,1200*frac,12,0x0055D6FF); char t1[16],t2[16]; fmt_time(t1,(long)(pcm_pos/48000.0)); fmt_time(t2,pcm_total/pcm_rate); sprintf(buf,"%s / %s",t1,t2); draw_text(buf,(int)(40*S),(int)(585*S),ui*2,0x00FFFFFF); draw_text(paused?"PAUSED":"PLAYING",(int)(520*S),(int)(585*S),ui*2,paused?0x00FFD23F:0x0066FF66); } else draw_text("PRESS X TO PLAY",(int)(40*S),(int)(585*S),ui*2,0x00809BBF); draw_text("VOL",(int)(900*S),(int)(585*S),ui*2,0x00FFFFFF); R(990,592,250,14,0x00303A55); R(990,592,250*volume,14,0x00FFD23F); float lv=level*2.2f; if(lv>1) lv=1; for(int b=0;b<16;b++){ float t=(float)b/16.0f; u32 c=(t<lv)?(t<0.6f?0x0066FF66:(t<0.85f?0x00FFD23F:0x00FF4D4D)):0x00203047; R(40+b*24,640,18,22,c); } draw_text("X PLAY  SQUARE PAUSE  L1 R1 SKIP",(int)(540*S),(int)(648*S),ui,0x00809BBF); }
int main(int argc,char **argv){ (void)argc;(void)argv; init_screen(); S=(float)W/1280.0f; ioPadInit(7); sysUtilRegisterCallback(0,sysutil_cb,NULL); scan_music(); audio_start(); while(running){ sysUtilCheckCallback(); handle_input(); if(playing>=0&&track_done){ int nx=(playing+1)%ntr; if(ntr>1) play_track(nx); else{ free_pcm(); playing=-1; } } audio_pump(); render(); present(); } free_pcm(); if(audio_ok){ audioPortStop(aport); audioPortClose(aport); audioQuit(); } return 0; }