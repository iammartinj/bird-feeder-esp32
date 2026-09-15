// Host preview of the feeder: the scene run for a while on a computer and one
// frame written as a PNG in the board's 448x368 landscape, stretched from
// 320x240 band by band as the firmware does it.
//
//   preview <out.png> [seconds = 30] [seed = 1] [background 1-3, 0 = coin] [hour = 12] [month = 1]
//
// For the acceptance tests, from the environment:
//   FEEDER_VISIT=hrdlicka@5,kos@1   a visit of that species at that second
//   FEEDER_STARTLE=40               a tap on the glass at that second
//   FEEDER_FALL=leaf@2,near@3,flurry@1   leaves, a near leaf or a far snow flurry then
//   FEEDER_TRACE=1                  twice a second: the swing and who is where
//
// The world, sky, snow, drawing and stretch are the firmware's own sources; the
// photo is decoded here with plain stb_image. Built with any g++ (MSYS2 on
// Windows) from the repository root:
//
//   g++ -O2 -DAQ_HOST -I main -I main/scene -I main/engine -I tools/host
//       main/engine/{gfx,fastmath,stretch}.cpp main/scene/*.cpp tools/preview.cpp tools/host/pt_host.c
//       -o preview
#include <cstdio>
#include <cstdlib>
#include <cstring>

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

extern "C" {
#include "pt_host.h"
}

static const int BAND_COLS = 56;

uint32_t esp_random(void) { return 4; }

static World world;

static const char* STATE[] = {"off", "arrive", "sit", "hop", "depart"};

struct Visit { int sp; float at; bool done; };

int main(int argc, char** argv) {
  if (argc < 2) {
    fprintf(stderr, "preview <out.png> [seconds] [seed] [background 1-3] [hour] [month]\n");
    return 1;
  }
  const float seconds = argc > 2 ? (float)atof(argv[2]) : 30.0f;
  const uint32_t seed = argc > 3 ? (uint32_t)strtoul(argv[3], nullptr, 10) : 1;
  const int bg = argc > 4 ? atoi(argv[4]) - 1 : -1;
  const float hour = argc > 5 ? (float)atof(argv[5]) : 12.0f;
  const int month = argc > 6 ? atoi(argv[6]) : 1;

  Visit visits[16];
  int nvisits = 0;
  if (const char* v = getenv("FEEDER_VISIT")) {
    char buf[256];
    strncpy(buf, v, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = 0;
    for (char* tok = strtok(buf, ","); tok && nvisits < 16; tok = strtok(nullptr, ",")) {
      char* at = strchr(tok, '@');
      if (!at) continue;
      *at = 0;
      visits[nvisits++] = {speciesByKey(tok), (float)atof(at + 1), false};
    }
  }
  Visit falls[8];
  int nfalls = 0;
  if (const char* v = getenv("FEEDER_FALL")) {
    char buf[128];
    strncpy(buf, v, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = 0;
    for (char* tok = strtok(buf, ","); tok && nfalls < 8; tok = strtok(nullptr, ",")) {
      char* at = strchr(tok, '@');
      if (!at) continue;
      *at = 0;
      const int kind = tok[0] == 'l' ? FALL_LEAVES : (tok[0] == 'n' ? FALL_NEAR_LEAF : FALL_FLURRY);
      falls[nfalls++] = {kind, (float)atof(at + 1), false};
    }
  }
  const float startleAt = getenv("FEEDER_STARTLE") ? (float)atof(getenv("FEEDER_STARTLE")) : -1.0f;
  const bool trace = getenv("FEEDER_TRACE") != nullptr;

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

  const float dt = 1.0f / 13.5f;
  float traced = -1.0f;
  bool startled = false;
  for (int i = 0; i < (int)(seconds / dt); i++) {
    const float t = i * dt;
    for (int k = 0; k < nvisits; k++) {
      if (!visits[k].done && t >= visits[k].at) {
        visits[k].done = true;
        fprintf(stderr, "%6.2f visit %s: %d came\n", t, visits[k].sp >= 0 ? speciesKey(visits[k].sp) : "?",
                worldVisit(world, visits[k].sp));
      }
    }
    if (startleAt >= 0 && !startled && t >= startleAt) {
      startled = true;
      worldStartle(world, "tap");
      fprintf(stderr, "%6.2f startle\n", t);
    }
    for (int k = 0; k < nfalls; k++) {
      if (!falls[k].done && t >= falls[k].at) {
        falls[k].done = true;
        fallForce((FallForce)falls[k].sp);
      }
    }
    worldStep(world, dt);
    skyStep(dt);
    snowStep(dt);
    fallStep(dt, world.windNow, c.windy);
    if (trace && t - traced >= 0.5f) {
      traced = t;
      int present = 0, sitting = 0;
      char who[160] = "";
      for (int b = 0; b < MAX_BIRDS; b++) {
        const Bird& bd = world.birds[b];
        if (bd.state == BS_OFF) continue;
        present++;
        if (bd.state == BS_SIT) sitting++;
        char one[24];
        snprintf(one, sizeof(one), " %s:%s", speciesKey(bd.sp), STATE[bd.state]);
        if (strlen(who) + strlen(one) < sizeof(who)) strcat(who, one);
      }
      fprintf(stderr, "%6.2f feeder %+5.2f deg, branch %+4.2f %+4.2f px, %d here, %d sitting%s\n", t, world.theta * 57.2958f, world.branchL, world.branchR, present,
              sitting, who);
    }
  }

  static uint16_t scratch[64 * FB_H];
  static uint16_t panel[DISP_W * DISP_H];
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
  static uint16_t land[DISP_W * DISP_H];
  for (int y = 0; y < DISP_H; y++)
    for (int x = 0; x < DISP_W; x++) land[y * DISP_W + x] = panel[x * DISP_H + (DISP_H - 1 - y)];

  char desc[200];
  worldDescribe(world, desc, sizeof(desc));
  fprintf(stderr, "%s; %.0f s at %.1f h, month %d, %d visits, feeder %.2f deg\n", desc, seconds, hour, month,
          world.visits, world.theta * 57.2958f);
  for (int i = 0; i < MAX_BIRDS; i++) {
    const Bird& b = world.birds[i];
    if (b.state != BS_OFF)
      fprintf(stderr, "  %-10s %-6s spot %2d at %5.1f %5.1f pose %d stay %.0f\n", speciesKey(b.sp), STATE[b.state],
              b.spot, b.x, b.y, b.pose, b.stay);
  }
  return pt_host_write_png_sized(argv[1], land, DISP_W, DISP_H) ? 0 : 1;
}
