// Mergulho Flip - saltos ornamentais, offline, nativo para PS3 (PSL1GHT)
// X = pular | analogico/direcional/L1/R1 = girar | QUADRADO = grupar (gira mais rapido)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
#include <malloc.h>
#include <ppu-types.h>
#include <sysutil/sysutil.h>
#include <sysutil/video.h>
#include <rsx/rsx.h>
#include <io/pad.h>

#define CB_SIZE   (0x100000)
#define HOST_SIZE (32 * 1024 * 1024)

#define PI      3.14159265f
#define DT      (1.0f / 60.0f)
#define WATERY  560.0f
#define NDIVES  5
#define NPART   64

/* ---------- video ---------- */
static gcmContextData *ctx;
static u32 W, H;
static u32 *fb[2];
static u32 fboff[2];
static u32 *back;
static int cur = 0, first_flip = 1;
static volatile int running = 1;
static float S = 1.0f;

static void sysutil_cb(u64 status, u64 param, void *usrdata)
{
    (void)param; (void)usrdata;
    if (status == SYSUTIL_EXIT_GAME) running = 0;
}

static void init_screen(void)
{
    void *host = memalign(1024 * 1024, HOST_SIZE);
    rsxInit(&ctx, CB_SIZE, HOST_SIZE, host);

    videoState st;
    videoGetState(VIDEO_PRIMARY, 0, &st);
    u32 resid = st.displayMode.resolution;
    if (videoGetResolutionAvailability(VIDEO_PRIMARY, VIDEO_RESOLUTION_720, VIDEO_ASPECT_AUTO, 0))
        resid = VIDEO_RESOLUTION_720;

    videoResolution res;
    videoGetResolution(resid, &res);
    W = res.width;
    H = res.height;

    videoConfiguration vc;
    memset(&vc, 0, sizeof(vc));
    vc.resolution = resid;
    vc.format = VIDEO_BUFFER_FORMAT_XRGB;
    vc.aspect = VIDEO_ASPECT_AUTO;
    vc.pitch = W * 4;
    videoConfigure(VIDEO_PRIMARY, &vc, NULL, 0);
    videoGetState(VIDEO_PRIMARY, 0, &st);

    gcmSetFlipMode(GCM_FLIP_VSYNC);

    for (int i = 0; i < 2; i++) {
        fb[i] = (u32 *)rsxMemalign(64, W * H * 4);
        gcmAddressToOffset(fb[i], &fboff[i]);
        gcmSetDisplayBuffer(i, fboff[i], W * 4, W, H);
    }
    back = (u32 *)memalign(64, W * H * 4);
    memset(back, 0, W * H * 4);
    gcmResetFlipStatus();
}

static void wait_flip(void)
{
    while (gcmGetFlipStatus() != 0) usleep(200);
    gcmResetFlipStatus();
}

static void present(void)
{
    if (!first_flip) wait_flip();
    else gcmResetFlipStatus();
    memcpy(fb[cur], back, W * H * 4);
    gcmSetFlip(ctx, cur);
    rsxFlushBuffer(ctx);
    gcmSetWaitFlip(ctx);
    cur ^= 1;
    first_flip = 0;
}

/* ---------- desenho ---------- */
static void fill_rect(int x, int y, int w, int h, u32 c)
{
    int x0 = x < 0 ? 0 : x, y0 = y < 0 ? 0 : y;
    int x1 = x + w, y1 = y + h;
    if (x1 > (int)W) x1 = W;
    if (y1 > (int)H) y1 = H;
    for (int yy = y0; yy < y1; yy++) {
        u32 *p = back + yy * W;
        for (int xx = x0; xx < x1; xx++) p[xx] = c;
    }
}

static void circle(int cx_, int cy_, int r, u32 c)
{
    if (r < 1) r = 1;
    if (cx_ + r < 0 || cx_ - r >= (int)W || cy_ + r < 0 || cy_ - r >= (int)H) return;
    for (int dy = -r; dy <= r; dy++) {
        int y = cy_ + dy;
        if (y < 0 || y >= (int)H) continue;
        int hw = (int)sqrtf((float)(r * r - dy * dy));
        int x0 = cx_ - hw, x1 = cx_ + hw;
        if (x0 < 0) x0 = 0;
        if (x1 >= (int)W) x1 = W - 1;
        u32 *p = back + y * W;
        for (int x = x0; x <= x1; x++) p[x] = c;
    }
}

/* coordenadas virtuais 1280x720 */
static void R(float x, float y, float w, float h, u32 c)
{
    fill_rect((int)(x * S), (int)(y * S), (int)(w * S) + 1, (int)(h * S) + 1, c);
}

static void C(float x, float y, float r, u32 c)
{
    circle((int)(x * S), (int)(y * S), (int)(r * S), c);
}

static void bone(float x0, float y0, float x1, float y1, float r, u32 c)
{
    float dx = x1 - x0, dy = y1 - y0;
    int n = (int)(sqrtf(dx * dx + dy * dy) / (r * 0.5f)) + 1;
    for (int i = 0; i <= n; i++) {
        float t = (float)i / n;
        C(x0 + dx * t, y0 + dy * t, r, c);
    }
}

/* fonte 3x5 minima */
typedef struct { char c; const char *p; } Glyph;
static const Glyph glyphs[] = {
    {'0', "###" "#.#" "#.#" "#.#" "###"}, {'1', ".#." "##." ".#." ".#." "###"},
    {'2', "###" "..#" "###" "#.." "###"}, {'3', "###" "..#" "###" "..#" "###"},
    {'4', "#.#" "#.#" "###" "..#" "..#"}, {'5', "###" "#.." "###" "..#" "###"},
    {'6', "###" "#.." "###" "#.#" "###"}, {'7', "###" "..#" "..#" "..#" "..#"},
    {'8', "###" "#.#" "###" "#.#" "###"}, {'9', "###" "#.#" "###" "..#" "###"},
    {'+', "..." ".#." "###" ".#." "..."}, {'/', "..#" "..#" ".#." "#.." "#.."},
    {'A', ".#." "#.#" "###" "#.#" "#.#"}, {'B', "##." "#.#" "##." "#.#" "##."},
    {'C', "###" "#.." "#.." "#.." "###"}, {'D', "##." "#.#" "#.#" "#.#" "##."},
    {'E', "###" "#.." "###" "#.." "###"}, {'F', "###" "#.." "###" "#.." "#.."},
    {'G', "###" "#.." "#.#" "#.#" "###"}, {'H', "#.#" "#.#" "###" "#.#" "#.#"},
    {'I', "###" ".#." ".#." ".#." "###"}, {'J', "..#" "..#" "..#" "#.#" "###"},
    {'K', "#.#" "#.#" "##." "#.#" "#.#"}, {'L', "#.." "#.." "#.." "#.." "###"},
    {'M', "#.#" "###" "###" "#.#" "#.#"}, {'N', "###" "#.#" "#.#" "#.#" "#.#"},
    {'O', "###" "#.#" "#.#" "#.#" "###"}, {'P', "###" "#.#" "###" "#.." "#.."},
    {'Q', "###" "#.#" "#.#" "###" "..#"}, {'R', "##." "#.#" "##." "#.#" "#.#"},
    {'S', "###" "#.." "###" "..#" "###"}, {'T', "###" ".#." ".#." ".#." ".#."},
    {'U', "#.#" "#.#" "#.#" "#.#" "###"}, {'V', "#.#" "#.#" "#.#" "#.#" ".#."},
    {'W', "#.#" "#.#" "###" "###" "#.#"}, {'X', "#.#" "#.#" ".#." "#.#" "#.#"},
    {'Y', "#.#" "#.#" ".#." ".#." ".#."}, {'Z', "###" "..#" ".#." "#.." "###"},
};

static const char *find_glyph(char c)
{
    for (unsigned i = 0; i < sizeof(glyphs) / sizeof(glyphs[0]); i++)
        if (glyphs[i].c == c) return glyphs[i].p;
    return NULL;
}

static int text_width(const char *s, int sc) { return (int)strlen(s) * 4 * sc - sc; }

static void draw_text(const char *s, int x, int y, int sc, u32 c)
{
    for (; *s; s++) {
        const char *g = find_glyph(*s);
        if (g)
            for (int r = 0; r < 5; r++)
                for (int k = 0; k < 3; k++)
                    if (g[r * 3 + k] == '#') fill_rect(x + k * sc, y + r * sc, sc, sc, c);
        x += 4 * sc;
    }
}

static void draw_center(const char *s, int xc, int y, int sc, u32 c)
{
    draw_text(s, xc - text_width(s, sc) / 2, y, sc, c);
}

/* ---------- jogo ---------- */
enum { ST_READY, ST_AIR, ST_SPLASH, ST_END };

static int state = ST_READY, dive_no = 1, total = 0, best = 0;
static int quality = 0, last_pts = 0, last_flips = 0;
static float st_t = 0, gt = 0;
static float cx, cy, vy, ang, omega, rot_acc, tuck;
static float pad_rot = 0;
static int pad_tuck = 0, pad_x = 0, x_prev = 0;

static const float PLAT[3] = { 360.0f, 300.0f, 240.0f };
static const char *qname[4] = { "BELLY FLOP", "OK", "GOOD", "PERFECT" };
static const int qbase[4] = { 5, 30, 60, 100 };
static const float qfac[4] = { 0.0f, 0.4f, 0.7f, 1.0f };
static const u32 qcol[4] = { 0x00FF4D4D, 0x00FFD23F, 0x0066FF66, 0x0055D6FF };

static float ppx[NPART], ppy[NPART], pvx[NPART], pvy[NPART], plife[NPART];

static float frand(void) { return (float)rand() / (float)RAND_MAX; }
static float lerpf(float a, float b, float t) { return a + (b - a) * t; }

static float wrap_pi(float a)
{
    while (a > PI) a -= 2 * PI;
    while (a < -PI) a += 2 * PI;
    return a;
}

static float plat_y(void)
{
    int l = (dive_no - 1) / 2;
    if (l > 2) l = 2;
    return PLAT[l];
}

static void init_dive(void)
{
    state = ST_READY;
    st_t = 0;
    cx = 235.0f;
    cy = plat_y() - 48.0f;
    vy = 0; ang = 0; omega = 0; rot_acc = 0; tuck = 0;
}

static void splash(int q, float x)
{
    int n = (q == 0) ? 40 : (q == 1) ? 24 : (q == 2) ? 16 : 10;
    float spread = (q == 0) ? 260.0f : (q == 1) ? 200.0f : (q == 2) ? 150.0f : 90.0f;
    float up = (q == 0) ? 520.0f : (q == 1) ? 340.0f : (q == 2) ? 260.0f : 200.0f;
    int made = 0;
    for (int i = 0; i < NPART && made < n; i++) {
        if (plife[i] > 0) continue;
        ppx[i] = x + (frand() - 0.5f) * 30.0f;
        ppy[i] = WATERY;
        pvx[i] = (frand() - 0.5f) * spread;
        pvy[i] = -(120.0f + frand() * up);
        plife[i] = 1.0f + frand() * 0.4f;
        made++;
    }
}

static void read_pad(void)
{
    padInfo info;
    padData d;
    pad_rot = 0; pad_tuck = 0; pad_x = 0;
    ioPadGetInfo(&info);
    if (info.status[0]) {
        ioPadGetData(0, &d);
        float ax = ((int)d.ANA_L_H - 128) / 128.0f;
        if (fabsf(ax) < 0.25f) ax = 0;
        if (d.BTN_RIGHT || d.BTN_R1 || d.BTN_R2) ax = 1;
        if (d.BTN_LEFT || d.BTN_L1 || d.BTN_L2) ax = -1;
        pad_rot = ax;
        pad_tuck = d.BTN_SQUARE || d.BTN_CIRCLE;
        pad_x = d.BTN_CROSS;
    }
}

static void land(void)
{
    float e = fabsf(wrap_pi(ang - PI)) + tuck * 0.6f;
    int q = (e < 0.28f) ? 3 : (e < 0.65f) ? 2 : (e < 1.15f) ? 1 : 0;
    last_flips = (int)(fabsf(rot_acc) / (2 * PI) + 0.12f);
    last_pts = qbase[q] + (int)(last_flips * 50 * qfac[q]);
    quality = q;
    total += last_pts;
    cy = WATERY - 30.0f;
    vy *= 0.45f;
    state = ST_SPLASH;
    st_t = 0;
    splash(q, cx);
}

static void update_game(void)
{
    gt += DT;
    st_t += DT;

    for (int i = 0; i < NPART; i++) {
        if (plife[i] <= 0) continue;
        plife[i] -= DT;
        pvy[i] += 900.0f * DT;
        ppx[i] += pvx[i] * DT;
        ppy[i] += pvy[i] * DT;
        if (ppy[i] > WATERY + 4) plife[i] = 0;
    }

    switch (state) {
    case ST_READY:
        if (pad_x && !x_prev) { state = ST_AIR; vy = -450.0f; st_t = 0; }
        break;
    case ST_AIR: {
        float tgt = pad_rot * 6.0f * (pad_tuck ? 1.8f : 1.0f);
        omega += (tgt - omega) * 0.2f;
        ang += omega * DT;
        rot_acc += omega * DT;
        tuck += ((pad_tuck ? 1.0f : 0.0f) - tuck) * 0.25f;
        vy += 900.0f * DT;
        cy += vy * DT;
        cx += 190.0f * DT;
        if (cy >= WATERY - 30.0f) land();
        break;
    }
    case ST_SPLASH:
        cy += vy * DT;
        vy *= 0.96f;
        cx += 30.0f * DT;
        ang += omega * DT;
        omega *= 0.95f;
        tuck *= 0.9f;
        if (st_t > 2.2f) {
            if (dive_no >= NDIVES) {
                if (total > best) best = total;
                state = ST_END;
            } else {
                dive_no++;
                init_dive();
            }
        }
        break;
    case ST_END:
        if (pad_x && !x_prev) { total = 0; dive_no = 1; init_dive(); }
        break;
    }
    x_prev = pad_x;
}

/* ---------- render ---------- */
static void lp(float l, float s, float *ox, float *oy)
{
    float sa = sinf(ang), ca = cosf(ang);
    *ox = cx + sa * l + ca * s;
    *oy = cy - ca * l + sa * s;
}

static void pt(float l0, float s0, float l1, float s1, float *ox, float *oy)
{
    lp(lerpf(l0, l1, tuck), lerpf(s0, s1, tuck), ox, oy);
}

static void draw_diver(void)
{
    float hx, hy, shx, shy, hpx, hpy, kx, ky, fx, fy, ex, ey, hdx, hdy;
    u32 skin = 0x00F2C79B;
    pt(38, 0, 20, 8, &hx, &hy);
    pt(22, 0, 12, 5, &shx, &shy);
    pt(-14, 0, -8, 0, &hpx, &hpy);
    pt(-31, 0, 6, 18, &kx, &ky);
    pt(-48, 0, -12, 10, &fx, &fy);
    pt(37, 0, 8, 15, &ex, &ey);
    pt(52, 0, 2, 16, &hdx, &hdy);

    bone(hpx, hpy, kx, ky, 7, skin);
    bone(kx, ky, fx, fy, 6, skin);
    bone(shx, shy, hpx, hpy, 9, 0x00E8453C);
    C(hpx, hpy, 10, 0x002A4BB8);
    C(hx, hy, 11, skin);
    bone(shx, shy, ex, ey, 5, skin);
    bone(ex, ey, hdx, hdy, 5, skin);
}

static void draw_water(void)
{
    int x0 = (int)(362 * S);
    int deep = (int)((WATERY + 90) * S);
    for (int x = x0; x < (int)W; x++) {
        float vx = x / S;
        int y0 = (int)((WATERY + sinf(vx * 0.04f + gt * 2.5f) * 3.0f) * S);
        for (int y = y0; y < (int)H; y++) {
            u32 *p = &back[y * W + x];
            if (y - y0 < 3) {
                *p = 0x00D8F4FF;
            } else {
                u32 wc = (y < deep) ? 0x001F86C8 : 0x00104A8A;
                *p = ((*p >> 1) & 0x007F7F7F) + ((wc >> 1) & 0x007F7F7F);
            }
        }
    }
}

static void render(void)
{
    char buf[32];
    int ui = (int)(4 * S + 0.5f);
    if (ui < 3) ui = 3;

    /* ceu */
    for (int i = 0; i < 37; i++) {
        u32 col = ((u32)(70 + i * 3) << 16) | ((u32)(140 + i * 2) << 8) | (u32)(215 + i / 2);
        R(0, i * 20, 1280, 21, col);
    }
    C(1120, 110, 50, 0x00FFE066);
    for (int k = 0; k < 3; k++) {
        float x = fmodf(gt * 12.0f + k * 480.0f, 1500.0f) - 110.0f;
        float y = 80.0f + k * 60.0f;
        C(x, y, 28, 0x00F2F6FF);
        C(x + 30, y + 6, 22, 0x00F2F6FF);
        C(x - 30, y + 8, 20, 0x00F2F6FF);
    }

    /* deck e torre */
    R(0, WATERY, 362, 160, 0x00B9B3A6);
    R(0, WATERY, 362, 8, 0x00E2DDD0);
    R(354, WATERY, 10, 160, 0x00DADADA);
    float py = plat_y();
    R(120, py + 12, 90, WATERY - py - 12, 0x006B7684);
    R(100, py, 175, 12, 0x00D0D6E0);
    R(100, py + 12, 175, 4, 0x00959DAA);

    draw_diver();
    draw_water();

    for (int i = 0; i < NPART; i++)
        if (plife[i] > 0) C(ppx[i], ppy[i], 3.5f, 0x00E8F6FF);

    /* HUD */
    sprintf(buf, "DIVE %d/%d", dive_no, NDIVES);
    draw_text(buf, 24, 20, ui, 0x00FFFFFF);
    draw_text("SCORE", 24, 20 + 7 * ui, ui, 0x00FFFFFF);
    sprintf(buf, "%d", total);
    draw_text(buf, 24, 20 + 14 * ui, ui + 2, 0x00FFD23F);
    draw_text("BEST", (int)W - 24 - text_width("BEST", ui), 20, ui, 0x00FFFFFF);
    sprintf(buf, "%d", best);
    draw_text(buf, (int)W - 24 - text_width(buf, ui + 2), 20 + 7 * ui, ui + 2, 0x00FFD23F);

    if (state == ST_AIR) {
        int fl = (int)(fabsf(rot_acc) / (2 * PI) + 0.12f);
        sprintf(buf, "FLIPS %d", fl);
        draw_center(buf, (int)W / 2, 20, ui * 2, 0x00FFFFFF);
    }
    if (state == ST_READY || state == ST_AIR) {
        draw_text("X JUMP", 24, (int)H - 14 * ui, ui, 0x00FFFFFF);
        draw_text("L1 R1 SPIN  SQUARE TUCK", 24, (int)H - 7 * ui, ui, 0x00FFFFFF);
    }
    if (state == ST_SPLASH) {
        draw_center(qname[quality], (int)W / 2, (int)(150 * S), ui * 3, qcol[quality]);
        sprintf(buf, "+%d", last_pts);
        draw_center(buf, (int)W / 2, (int)(150 * S) + 20 * ui, ui * 2, 0x00FFD23F);
        sprintf(buf, "FLIPS %d", last_flips);
        draw_center(buf, (int)W / 2, (int)(150 * S) + 32 * ui, ui, 0x00FFFFFF);
    }
    if (state == ST_END) {
        int bw = (int)(520 * S), bh = (int)(330 * S);
        int bx = (int)W / 2 - bw / 2, by = (int)H / 2 - bh / 2;
        fill_rect(bx, by, bw, bh, 0x00000000);
        draw_center("RESULT", (int)W / 2, by + 20, ui * 2, 0x0055D6FF);
        draw_center("TOTAL", (int)W / 2, by + 20 + 14 * ui, ui, 0x00FFFFFF);
        sprintf(buf, "%d", total);
        draw_center(buf, (int)W / 2, by + 20 + 21 * ui, ui * 3, 0x00FFD23F);
        draw_center("BEST", (int)W / 2, by + 20 + 40 * ui, ui, 0x00FFFFFF);
        sprintf(buf, "%d", best);
        draw_center(buf, (int)W / 2, by + 20 + 47 * ui, ui * 2, 0x0066FF66);
        draw_center("PRESS X", (int)W / 2, by + bh - 10 * ui, ui, 0x00FFFFFF);
    }
}

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    init_screen();
    S = (float)W / 1280.0f;
    ioPadInit(7);
    sysUtilRegisterCallback(0, sysutil_cb, NULL);
    srand(20261113);
    init_dive();

    while (running) {
        sysUtilCheckCallback();
        read_pad();
        update_game();
        render();
        present();
    }
    return 0;
}