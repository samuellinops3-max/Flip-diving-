// Salto Mortal PS3 - jogo original inspirado em saltos ornamentais
// X = pular / girar | START = reiniciar | ESQ/DIR = trocar fase (no trampolim)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <ctype.h>
#include <malloc.h>
#include <unistd.h>
#include <ppu-lv2.h>
#include <io/pad.h>
#include <rsx/rsx.h>
#include <sysutil/video.h>
#include <sysutil/sysutil.h>

#define W 640            // resolucao logica do jogo
#define H 360
#define WATER_Y 310
#define PI 3.14159265f
#define G 800.0f         // gravidade
#define JUMP_V 260.0f    // impulso do pulo
#define OMEGA 20.0f      // velocidade de rotacao (rad/s)
#define DRIFT 70.0f      // avanco horizontal
#define TOL 0.6f         // tolerancia de angulo na entrada (rad)
#define MAX_FLIPS 5
#define DT (1.0f/60.0f)

#define CB_SIZE   0x100000
#define HOST_SIZE (32*1024*1024)

/* ---------- video / RSX ---------- */
typedef struct { int height, width; uint32_t *ptr; uint32_t offset; } rsxBuffer;
static gcmContextData *context;
static videoState vstate;
static videoConfiguration vconfig;
static videoResolution vres;
static rsxBuffer buffers[2];
static int currentBuffer = 0;
static volatile int running = 1;

static void wait_flip(void) {
  while (gcmGetFlipStatus() != 0) usleep(200);
  gcmResetFlipStatus();
}
static int do_flip(s32 buf) {
  if (gcmSetFlip(context, buf) == 0) {
    rsxFlushBuffer(context);
    gcmSetWaitFlip(context);
    return 1;
  }
  return 0;
}
static int make_buffer(rsxBuffer *b, int w, int h, int id) {
  int pitch = w * 4;
  b->ptr = (uint32_t *)rsxMemalign(64, pitch * h);
  if (!b->ptr) return 0;
  if (rsxAddressToOffset(b->ptr, &b->offset) != 0) return 0;
  if (gcmSetDisplayBuffer(id, b->offset, pitch, w, h) != 0) return 0;
  b->width = w; b->height = h;
  return 1;
}
static void init_screen(void) {
  void *host = memalign(1024 * 1024, HOST_SIZE);
  rsxInit(&context, CB_SIZE, HOST_SIZE, host);
  videoGetState(0, 0, &vstate);
  videoGetResolution(vstate.displayMode.resolution, &vres);
  memset(&vconfig, 0, sizeof(vconfig));
  vconfig.resolution = vstate.displayMode.resolution;
  vconfig.format = VIDEO_BUFFER_FORMAT_XRGB;
  vconfig.pitch = vres.width * 4;
  vconfig.aspect = vstate.displayMode.aspect;
  videoConfigure(0, &vconfig, NULL, 0);
  videoGetState(0, 0, &vstate);
  gcmSetFlipMode(GCM_FLIP_VSYNC);
  make_buffer(&buffers[0], vres.width, vres.height, 0);
  make_buffer(&buffers[1], vres.width, vres.height, 1);
  gcmResetFlipStatus();
  do_flip(1);
}
static void sysutil_cb(u64 status, u64 param, void *ud) {
  if (status == SYSUTIL_EXIT_GAME) running = 0;
}

/* ---------- desenho 2D (tela logica 640x360, escalada pro buffer RSX) ---------- */
static uint32_t fb[W * H];

static void rect(int x, int y, int w, int h, uint32_t c) {
  if (x < 0) { w += x; x = 0; }
  if (y < 0) { h += y; y = 0; }
  if (x + w > W) w = W - x;
  if (y + h > H) h = H - y;
  if (w <= 0 || h <= 0) return;
  for (int j = y; j < y + h; j++) {
    uint32_t *p = &fb[j * W + x];
    for (int i = 0; i < w; i++) p[i] = c;
  }
}
static void circle(float cx, float cy, float r, uint32_t c) {
  int x0 = (int)floorf(cx - r), x1 = (int)ceilf(cx + r);
  int y0 = (int)floorf(cy - r), y1 = (int)ceilf(cy + r);
  for (int y = y0; y <= y1; y++) {
    if (y < 0 || y >= H) continue;
    for (int x = x0; x <= x1; x++) {
      if (x < 0 || x >= W) continue;
      float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
      if (dx * dx + dy * dy <= r * r) fb[y * W + x] = c;
    }
  }
}
static uint32_t lerpc(uint32_t a, uint32_t b, float t) {
  int ar = (a >> 16) & 255, ag = (a >> 8) & 255, ab = a & 255;
  int br = (b >> 16) & 255, bg = (b >> 8) & 255, bb = b & 255;
  return ((int)(ar + (br - ar) * t) << 16) | ((int)(ag + (bg - ag) * t) << 8) | (int)(ab + (bb - ab) * t);
}
static void present(rsxBuffer *b) {
  static int xmap[2048];
  static int built = 0;
  if (!built) {
    for (int x = 0; x < b->width && x < 2048; x++) xmap[x] = x * W / b->width;
    built = 1;
  }
  for (int y = 0; y < b->height; y++) {
    const uint32_t *src = &fb[(y * H / b->height) * W];
    uint32_t *dst = b->ptr + y * b->width;
    for (int x = 0; x < b->width; x++) dst[x] = src[xmap[x]];
  }
}

/* fonte 3x5 (A-Z, 0-9, alguns simbolos) */
static const uint16_t FONT_L[26] = {
  0x2BED,0x6BAE,0x3923,0x6B6E,0x79A7,0x79A4,0x396B,0x5BED,0x7497,0x126A,0x5BAD,0x4927,0x5FED,
  0x6B6D,0x2B6A,0x6BA4,0x2B7B,0x6BAD,0x388E,0x7492,0x5B6F,0x5B6A,0x5BFD,0x5AAD,0x5A92,0x72A7};
static const uint16_t FONT_D[10] = {
  0x7B6F,0x2C97,0x73E7,0x73CF,0x5BC9,0x79CF,0x79EF,0x7249,0x7BEF,0x7BCF};

static void text_raw(int x, int y, const char *s, int sc, uint32_t col) {
  for (; *s; s++, x += 4 * sc) {
    char c = (char)toupper((unsigned char)*s);
    uint16_t g = 0;
    if (c >= 'A' && c <= 'Z') g = FONT_L[c - 'A'];
    else if (c >= '0' && c <= '9') g = FONT_D[c - '0'];
    else if (c == '!') g = 0x2482;
    else if (c == '-') g = 0x01C0;
    else if (c == '+') g = 0x05D0;
    else if (c == ':') g = 0x0410;
    for (int r = 0; r < 5; r++)
      for (int k = 0; k < 3; k++)
        if ((g >> (14 - (r * 3 + k))) & 1) rect(x + k * sc, y + r * sc, sc, sc, col);
  }
}
static int text_w(const char *s, int sc) { return (int)strlen(s) * 4 * sc - sc; }
static void text(int x, int y, const char *s, int sc, uint32_t col) {
  int o = sc / 2 > 0 ? sc / 2 : 1;
  text_raw(x + o, y + o, s, sc, 0x101018);
  text_raw(x, y, s, sc, col);
}
static void text_c(int y, const char *s, int sc, uint32_t col) {
  text((W - text_w(s, sc)) / 2, y, s, sc, col);
}

/* ---------- fases ---------- */
typedef struct {
  const char *name; int boardY;
  uint32_t skyTop, skyBot, st, st2, water, waterL;
} Level;
static const Level LV[3] = {
  {"PISCINA",     250, 0x6EC6F5, 0xE8F8FF, 0xB8BEC8, 0x8E96A3, 0x1CA9E6, 0x8FE3FF},
  {"PENHASCO",    160, 0x3B7DD8, 0xFFD6A0, 0x8B6B47, 0x5E4630, 0x1B5FA8, 0x6FB4F0},
  {"PREDIO ALTO",  80, 0x0A0F2E, 0x4B4E8F, 0x4A4F5C, 0x30343F, 0x167C8F, 0x56C7D6},
};

/* ---------- estado do jogo ---------- */
enum { ST_BOARD, ST_AIR, ST_SPLASH, ST_OVER };
static int state, level, score, best, flipsThis, divesOK, lastPts;
static float dx, dy, vy, ang, tgt, stateT, banner, T;
static char msg[32];

typedef struct { float x, y, vx, vy, life; } Drop;
static Drop drops[48];

static void spawn_splash(float x, int n, float power) {
  for (int i = 0, k = 0; i < 48 && k < n; i++) {
    if (drops[i].life <= 0) {
      drops[i].x = x; drops[i].y = WATER_Y;
      drops[i].vx = ((rand() % 200) - 100) * 0.6f * power;
      drops[i].vy = -(80 + rand() % 140) * power;
      drops[i].life = 1.0f; k++;
    }
  }
}
static void reset_dive(void) {
  state = ST_BOARD; dx = 220; dy = LV[level].boardY - 14;
  vy = 0; ang = 0; tgt = 0; flipsThis = 0; stateT = 0;
}
static void new_game(void) {
  score = 0; level = 0; divesOK = 0; banner = 0;
  for (int i = 0; i < 48; i++) drops[i].life = 0;
  reset_dive();
}
static void land(void) {
  float err = fabsf(tgt - ang);
  stateT = 0;
  if (err < TOL) {   // entrou de cabeca
    int tri = flipsThis * (flipsThis + 1) / 2;   // 1,3,6,10,15
    int pts = (50 + tri * 100 + (int)((1.0f - err / TOL) * 50.0f)) * (level + 1);
    score += pts; lastPts = pts; divesOK++;
    if (score > best) best = score;
    snprintf(msg, sizeof msg, "%s", err < 0.2f ? "ENTRADA PERFEITA!" : "BOA ENTRADA!");
    state = ST_SPLASH; spawn_splash(dx, 14, 0.8f);
  } else {           // bateu a barriga
    if (score > best) best = score;
    snprintf(msg, sizeof msg, "BATEU A BARRIGA!");
    state = ST_OVER; spawn_splash(dx, 30, 1.4f);
  }
}
static void update(int xp, int startp, int lp, int rp) {
  T += DT;
  if (banner > 0) banner -= DT;
  for (int i = 0; i < 48; i++) if (drops[i].life > 0) {
    drops[i].life -= DT; drops[i].vy += 500 * DT;
    drops[i].x += drops[i].vx * DT; drops[i].y += drops[i].vy * DT;
    if (drops[i].y > WATER_Y && drops[i].vy > 0) drops[i].life = 0;
  }
  if (startp) { new_game(); return; }
  switch (state) {
  case ST_BOARD:
    if (lp && level > 0) { level--; reset_dive(); }
    if (rp && level < 2) { level++; reset_dive(); }
    if (xp) { vy = -JUMP_V; tgt = PI; flipsThis = 0; state = ST_AIR; }
    break;
  case ST_AIR: {
    if (xp && flipsThis < MAX_FLIPS) { tgt += 2 * PI; flipsThis++; }
    vy += G * DT; dy += vy * DT; dx += DRIFT * DT;
    float d = tgt - ang, step = OMEGA * DT;
    if (d > step) ang += step; else if (d < -step) ang -= step; else ang = tgt;
    float low = dy + 14.0f * fabsf(cosf(ang)) + 4.0f * fabsf(sinf(ang));
    if (low >= WATER_Y) land();
    break; }
  case ST_SPLASH:
    stateT += DT; dy += 180 * DT;
    if (stateT > 1.6f) {
      if (divesOK % 3 == 0 && level < 2) { level++; banner = 2.2f; }
      reset_dive();
    }
    break;
  case ST_OVER:
    stateT += DT; dy += 40 * DT;
    break;
  }
}

/* ---------- render ---------- */
static void part(float u, float r, uint32_t c) {
  circle(dx + u * sinf(ang), dy - u * cosf(ang), r, c);
}
static void draw_diver(void) {
  for (float u = -11; u <= -6; u += 2.5f) part(u, 3.0f, 0xF1C27D);  // pernas
  part(-5, 3.6f, 0x1D3557);                                           // sunga
  for (float u = -3; u <= 4.5f; u += 2.5f) part(u, 4.3f, 0xE63946);   // tronco
  part(10, 4.0f, 0xF1C27D);                                           // cabeca
  part(11.3f, 3.0f, 0x3B2A20);                                        // cabelo
}
static void render(void) {
  const Level *L = &LV[level];
  int by = L->boardY;

  for (int y = 0; y < H; y++) {
    float t = (float)y / WATER_Y; if (t > 1) t = 1;
    rect(0, y, W, 1, lerpc(L->skyTop, L->skyBot, t));
  }
  if (level == 0) circle(540, 60, 22, 0xFFE066);
  if (level == 1) { circle(470, 265, 40, 0xFF9F43); circle(470, 265, 30, 0xFFC27A); }
  if (level == 2) {
    circle(540, 60, 18, 0xEDEFFF); circle(547, 55, 16, 0x0E1438);
    for (int i = 0; i < 45; i++)
      if (((int)(T * 2) + i) % 7) rect((i * 97 + 13) % W, (i * 53 + 7) % 200, 2, 2, 0xFFFFFF);
  } else {
    uint32_t cc = level == 0 ? 0xFFFFFF : 0xFFE9CF;
    for (int i = 0; i < 3; i++) {
      float cx = fmodf(80 + i * 230 + T * 8.0f, 760.0f) - 60.0f, cy = 50 + i * 28;
      circle(cx, cy, 14, cc); circle(cx + 16, cy - 6, 18, cc); circle(cx + 34, cy, 14, cc);
      rect((int)cx, (int)cy, 36, 14, cc);
    }
  }

  // estrutura (piscina / penhasco / predio)
  rect(0, by, 170, H - by, L->st);
  if (level == 0) for (int y = by + 10; y < H; y += 14) rect(0, y, 170, 1, L->st2);
  if (level == 1) {
    rect(0, by, 160, 6, 0x4C9A2A); rect(120, by + 40, 50, H, L->st2);
    rect(20, by + 30, 40, 6, L->st2); rect(60, by + 70, 50, 6, L->st2);
  }
  if (level == 2) {
    rect(0, by, 170, 4, L->st2);
    for (int gy = by + 14; gy < H - 10; gy += 22)
      for (int gx = 12; gx < 150; gx += 26)
        rect(gx, gy, 14, 12, ((gx / 26 + gy / 22) % 3 == 0) ? 0xFFD76A : 0x2B2F3A);
  }
  // trampolim
  rect(150, by, 95, 5, 0x2A9D8F); rect(150, by, 95, 1, 0x7FE0D3); rect(150, by + 5, 14, 6, 0x1E6F66);

  if (state != ST_BOARD || 1) draw_diver();

  // agua (cobre o mergulhador quando entra)
  rect(170, WATER_Y, W - 170, H - WATER_Y, L->water);
  for (int x = 170; x < W; x += 10) {
    float wy = WATER_Y + 2.0f * sinf(T * 3 + x * 0.08f);
    rect(x, (int)wy, 10, 3, L->waterL);
    rect(x + 3, WATER_Y + 18 + (x / 10) % 3 * 7, 14, 1, L->waterL);
  }
  if (state == ST_SPLASH || state == ST_OVER) {
    int rr = (int)(stateT * 120); if (rr > 100) rr = 100;
    rect((int)dx - rr, WATER_Y - 1, rr * 2, 2, 0xFFFFFF);
  }
  for (int i = 0; i < 48; i++) if (drops[i].life > 0) rect((int)drops[i].x, (int)drops[i].y, 2, 2, 0xFFFFFF);

  // HUD
  char b[64];
  snprintf(b, sizeof b, "PONTOS %d", score);       text(8, 6, b, 3, 0xFFFFFF);
  snprintf(b, sizeof b, "RECORDE %d", best);       text(W - 8 - text_w(b, 2), 8, b, 2, 0xFFE066);
  snprintf(b, sizeof b, "FASE %d %s", level + 1, L->name); text(8, 28, b, 2, 0xFFFFFF);
  if (state == ST_AIR) { snprintf(b, sizeof b, "MORTAIS %d", flipsThis); text_c(44, b, 4, 0xFFE066); }
  if (state == ST_BOARD) {
    text_c(140, "APERTE X PARA PULAR", 3, 0xFFFFFF);
    text_c(166, "X NO AR DA MORTAL - TERMINE ANTES DA AGUA", 2, 0xFFFFFF);
    text_c(H - 18, "START REINICIA - ESQ DIR TROCA FASE", 2, 0xFFFFFF);
  }
  if (banner > 0 && state == ST_BOARD) {
    snprintf(b, sizeof b, "FASE %d: %s", level + 1, L->name); text_c(90, b, 4, 0xFFE066);
  }
  if (state == ST_SPLASH) {
    text_c(100, msg, 5, 0xFFE066);
    snprintf(b, sizeof b, "+%d PONTOS", lastPts); text_c(140, b, 4, 0xFFFFFF);
  }
  if (state == ST_OVER && stateT > 0.5f) {
    text_c(90, msg, 5, 0xFF5A5A);
    snprintf(b, sizeof b, "PONTOS %d", score); text_c(135, b, 4, 0xFFFFFF);
    text_c(175, "START REINICIA", 3, 0xFFE066);
  }
}

/* ---------- entrada ---------- */
static void read_pad(int *cross, int *start, int *left, int *right) {
  padInfo info; padData d;
  *cross = *start = *left = *right = 0;
  ioPadGetInfo(&info);
  for (int i = 0; i < MAX_PADS; i++) {
    if (info.status[i]) {
      ioPadGetData(i, &d);
      if (d.BTN_CROSS) *cross = 1;
      if (d.BTN_START) *start = 1;
      if (d.BTN_LEFT)  *left = 1;
      if (d.BTN_RIGHT) *right = 1;
    }
  }
}

int main(int argc, const char *argv[]) {
  init_screen();
  ioPadInit(7);
  sysUtilRegisterCallback(0, sysutil_cb, NULL);
  new_game();

  int pc = 0, ps = 0, pl = 0, pr = 0;
  while (running) {
    sysUtilCheckCallback();
    wait_flip();
    int c, s, l, r;
    read_pad(&c, &s, &l, &r);
    update(c && !pc, s && !ps, l && !pl, r && !pr);
    pc = c; ps = s; pl = l; pr = r;
    render();
    present(&buffers[currentBuffer]);
    do_flip(currentBuffer);
    currentBuffer = !currentBuffer;
  }
  ioPadEnd();
  return 0;
}