// Host video of the feeder: the preview's world, sky, snow, leaves and drawing,
// every frame at the board's 448x368 landscape, raw RGB24 on stdout for ffmpeg.
//
//   video <seconds> <fps> <seed> <background 1-3> <hour> <month> | ffmpeg ...
//
// From the environment, as in preview.cpp:
//   FEEDER_VISIT=vrabec@3,kos@11    a visit of that species at that second
//   FEEDER_FALL=leaf@8,near@23,flurry@14
//   FEEDER_STARTLE=53               a tap on the glass (a list works too)
//   FEEDER_NOAUTO=1                 only these visits, no chance ones
//
// Built with any g++ (MSYS2 on Windows) from the repository root.
// -DFEEDER_NO_CLOUDS turns off the cloud wave and the sun band (sky.cpp): fine
// on the board, but on a big monitor they read as dark stripes sweeping across.
//
//   g++ -O2 -DAQ_HOST -DFEEDER_NO_CLOUDS -I main -I main/scene -I main/engine
//       main/engine/{gfx,fastmath,stretch}.cpp main/scene/*.cpp tools/video.cpp -o video
//
// The three one-minute samples (quiet, four visits each):
//
//   FEEDER_NOAUTO=1 FEEDER_VISIT=vrabec@3,kos@11,konadra@24,hrdlicka@37 FEEDER_FALL=flurry@14 FEEDER_STARTLE=53
//     video 60 25 8 1 10 1        -> winter overcast
//   FEEDER_NOAUTO=1 FEEDER_VISIT=kos@4,stehlik@13,hrdlicka@27,modrinka@42 FEEDER_FALL=flurry@20
//     video 60 25 3 2 8.5 1       -> winter morning
//   FEEDER_NOAUTO=1 FEEDER_VISIT=vrabec@4,konadra@17,sojka@31,brhlik@45 FEEDER_FALL=leaf@8,near@23,leaf@38,leaf@52
//     video 60 25 4 3 11 10       -> autumn
//
// each piped into
//
//   ffmpeg -f rawvideo -pix_fmt rgb24 -s 448x368 -r 25 -i - -vf scale=896:736:flags=neighbor
//          -c:v libx264 -crf 18 -preset slow -pix_fmt yuv420p out.mp4
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <io.h>

#define STBI_ONLY_JPEG
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include "draw.h"
#include "fall.h"
#include "fastmath.h"
#include "feeder_bg.h"
#include "gfx.h"
#include "sky.h"
#include "snow.h"
#include "stretch.h"
#include "world.h"

static const int BAND_COLS = 56;

uint32_t esp_random(void) { return 4; }

static World world;

struct Event { int what; float at; bool done; };

static int parse(const char* env, Event* ev, int max, int (*kind)(const char*)) {
  const char* v = getenv(env);
  if (!v) return 0;
  char buf[512];
  strncpy(buf, v, sizeof(buf) - 1);
  buf[sizeof(buf) - 1] = 0;
  int n = 0;
  for (char* tok = strtok(buf, ","); tok && n < max; tok = strtok(nullptr, ",")) {
    char* at = strchr(tok, '@');
    if (at) *at = 0;
    ev[n++] = {kind(tok), (float)atof(at ? at + 1 : tok), false};
  }
  return n;
}
static int fallKind(const char* s) { return s[0] == 'l' ? FALL_LEAVES : (s[0] == 'n' ? FALL_NEAR_LEAF : FALL_FLURRY); }
static int startleKind(const char*) { return 0; }

int main(int argc, char** argv) {
  if (argc < 7) {
    fprintf(stderr, "video <seconds> <fps> <seed> <background 1-3> <hour> <month>\n");
    return 1;
  }
  const float seconds = (float)atof(argv[1]);
  const float fps = (float)atof(argv[2]);
  const uint32_t seed = (uint32_t)strtoul(argv[3], nullptr, 10);
  const int bg = atoi(argv[4]) - 1;
  const float hour = (float)atof(argv[5]);
  const int month = atoi(argv[6]);

  Event visits[32], falls[16], startles[8];
  const int nv = parse("FEEDER_VISIT", visits, 32, speciesByKey);
  const int nf = parse("FEEDER_FALL", falls, 16, fallKind);
  const int ns = parse("FEEDER_STARTLE", startles, 8, startleKind);
  const bool noAuto = getenv("FEEDER_NOAUTO") != nullptr;

  fastMathInit();
  stretchInit();
  worldInit(world, seed, month, hour, bg);
  const Coins& c = world.coins;
  int w, h, comp;
  uint8_t* rgb = stbi_load_from_memory(FEEDER_BG[c.bg].data, (int)FEEDER_BG[c.bg].len, &w, &h, &comp, 3);
  if (!rgb || w != FB_W || h != FB_H) {
    fprintf(stderr, "background %d: %dx%d\n", c.bg + 1, w, h);
    return 1;
  }
  BGBUF = (uint16_t*)malloc((size_t)FB_W * FB_H * 2);
  for (int y = 0; y < FB_H; y++)
    for (int x = 0; x < FB_W; x++) {
      const uint8_t* p = rgb + (y * FB_W + x) * 3;
      BGBUF[x * FB_H + (FB_H - 1 - y)] = rgb565(p[0], p[1], p[2]);
    }
  stbi_image_free(rgb);
  skyInit(c.bg, world.winter, c.cloudPeriod, c.cloudDir);
  skySetClock(hour);
  snowInit(c.snow, c.windy ? 9.0f : 3.0f, seed + 7);
  fallInit(c.bg == BG_AUTUMN, seed + 11);
  char desc[200];
  worldDescribe(world, desc, sizeof(desc));
  fprintf(stderr, "%s\n", desc);

  _setmode(_fileno(stdout), _O_BINARY);
  static uint16_t scratch[64 * FB_H];
  static uint16_t panel[DISP_W * DISP_H];
  static uint8_t out[DISP_W * DISP_H * 3];
  const float dt = 1.0f / fps;
  const int frames = (int)(seconds * fps);
  for (int i = 0; i < frames; i++) {
    const float t = i * dt;
    for (int k = 0; k < nv; k++)
      if (!visits[k].done && t >= visits[k].at) {
        visits[k].done = true;
        if (visits[k].what < 0) continue;
        fprintf(stderr, "%6.2f visit %s: %d came\n", t, speciesKey(visits[k].what), worldVisit(world, visits[k].what));
      }
    for (int k = 0; k < nf; k++)
      if (!falls[k].done && t >= falls[k].at) {
        falls[k].done = true;
        fallForce((FallForce)falls[k].what);
      }
    for (int k = 0; k < ns; k++)
      if (!startles[k].done && t >= startles[k].at) {
        startles[k].done = true;
        worldStartle(world, "tap");
        fprintf(stderr, "%6.2f startle\n", t);
      }
    if (noAuto) {
      world.nextVisit = 1e9f;
      world.autoStartle = 1e9f;
    }
    worldStep(world, dt);
    skyStep(dt);
    snowStep(dt);
    fallStep(dt, world.windNow, c.windy);

    FB = scratch;
    for (int x0 = 0; x0 < DISP_W; x0 += BAND_COLS) {
      int sx0, sx1;
      stretchSource(x0, x0 + BAND_COLS, &sx0, &sx1);
      gBandX0 = sx0;
      gBandX1 = sx1;
      const int sm = sx0 + (((sx1 - sx0) / 2) & ~1);
      feederRenderBand(world, sx0, sm);
      feederRenderBand(world, sm, sx1);
      stretchBand(scratch, sx0, panel + x0 * DISP_H, x0, x0, x0 + BAND_COLS);
    }
    for (int y = 0; y < DISP_H; y++)
      for (int x = 0; x < DISP_W; x++) {
        const uint16_t v = panel[x * DISP_H + (DISP_H - 1 - y)];
        uint8_t* o = out + (y * DISP_W + x) * 3;
        const int r = (v >> 11) & 0x1F, g = (v >> 5) & 0x3F, b = v & 0x1F;
        o[0] = (uint8_t)((r << 3) | (r >> 2));
        o[1] = (uint8_t)((g << 2) | (g >> 4));
        o[2] = (uint8_t)((b << 3) | (b >> 2));
      }
    fwrite(out, 1, sizeof(out), stdout);
  }
  fprintf(stderr, "%d frames, %d visits\n", frames, world.visits);
  return 0;
}
