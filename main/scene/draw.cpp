#pragma GCC optimize("O3")
#include "draw.h"
#include "fall.h"
#include "gfx.h"
#include "sky.h"
#include "snow.h"
#include <math.h>
#include <stdlib.h>

// Distance haze: a bird on the feeder is in the focal plane; the ground behind it
// is metres further off, hazier and out of focus.
static const float HAZE_NEAR = 0.03f, HAZE_GROUND = 0.28f;
static const int HAZE_R = 200, HAZE_G = 206, HAZE_B = 214;
// A contact shadow only under a bird standing on the ground; under birds on the
// feeder it looked as if it hung in the air.
static const float SHADOW_ALPHA = 0.22f;

// On the ground the size comes from the depth map, row by row (GROUND_SCALE,
// baked by tools/make_assets.py); this is the blur of the copy it is drawn from,
// a box in sprite pixels: a full-size, sharp blackbird could not be standing
// metres back there.
static const int GROUND_BLUR = 2;   // radius: a 5x5 box

static void IRAM_ATTR drawSprite(const RGBA8* px, int w, int h, float ax, float ay, float x, float y, float ang,
                                 bool mirror, float scale, const float lg[3], float haze, int rowFrom = 0,
                                 int rowTo = FB_H) {
  const int cid = xPortGetCoreID();
  const float ca = cosf(ang), sa = sinf(ang);
  const float lx0 = (mirror ? ax - w : -ax) * scale, lx1 = (mirror ? ax : w - ax) * scale;
  const float ly0 = -ay * scale, ly1 = (h - ay) * scale;
  const float cx[4] = {lx0, lx1, lx1, lx0}, cy[4] = {ly0, ly0, ly1, ly1};
  float xmin = 1e9f, xmax = -1e9f, ymin = 1e9f, ymax = -1e9f;
  for (int i = 0; i < 4; i++) {
    const float X = x + cx[i] * ca - cy[i] * sa, Y = y + cx[i] * sa + cy[i] * ca;
    if (X < xmin) xmin = X;
    if (X > xmax) xmax = X;
    if (Y < ymin) ymin = Y;
    if (Y > ymax) ymax = Y;
  }
  int ix0 = (int)floorf(xmin) - 1, ix1 = (int)ceilf(xmax) + 1;
  int iy0 = (int)floorf(ymin) - 1, iy1 = (int)ceilf(ymax) + 1;
  if (ix0 < gClipX0[cid]) ix0 = gClipX0[cid];
  if (ix1 > gClipX1[cid]) ix1 = gClipX1[cid];
  if (iy0 < rowFrom) iy0 = rowFrom;
  if (iy1 > rowTo) iy1 = rowTo;
  if (ix0 >= ix1 || iy0 >= iy1) return;

  const float k = 1.0f - haze;
  const float PR = lg[0] * k * (31.0f / 255.0f) / 255.0f;
  const float PG = lg[1] * k * (63.0f / 255.0f) / 255.0f;
  const float PB = lg[2] * k * (31.0f / 255.0f) / 255.0f;
  const float HR = HAZE_R * lg[0] * haze * (31.0f / 255.0f) / 255.0f;
  const float HG = HAZE_G * lg[1] * haze * (63.0f / 255.0f) / 255.0f;
  const float HB = HAZE_B * lg[2] * haze * (31.0f / 255.0f) / 255.0f;
  const float msign = mirror ? -1.0f : 1.0f;
  const float inv = 1.0f / scale;

  for (int Y = iy0; Y < iy1; Y++) {
    const float dy = (float)Y + 0.5f - y;
    for (int X = ix0; X < ix1; X++) {
      const float dx = (float)X + 0.5f - x;
      const float u = (dx * ca + dy * sa) * msign * inv + ax - 0.5f;
      const float v = (-dx * sa + dy * ca) * inv + ay - 0.5f;
      if (u <= -1.0f || v <= -1.0f || u >= (float)w || v >= (float)h) continue;
      const int u0 = (int)floorf(u), v0 = (int)floorf(v);
      const float fu = u - u0, fv = v - v0;
      float a = 0, r = 0, g = 0, b = 0;
      for (int j = 0; j < 2; j++) {
        const int vv = v0 + j;
        if (vv < 0 || vv >= h) continue;
        const float wv = j ? fv : 1.0f - fv;
        for (int i = 0; i < 2; i++) {
          const int uu = u0 + i;
          if (uu < 0 || uu >= w) continue;
          const RGBA8& p = px[vv * w + uu];
          if (!p.a) continue;
          const float wt = (i ? fu : 1.0f - fu) * wv * p.a;
          a += wt;
          r += wt * p.r;
          g += wt * p.g;
          b += wt * p.b;
        }
      }
      if (a < 4.0f) continue;
      int al = (int)(a * (32.0f / 255.0f) + 0.5f);
      if (al > 32) al = 32;
      int pr = (int)(r * PR + a * HR), pg = (int)(g * PG + a * HG), pb = (int)(b * PB + a * HB);
      px_blend_pm(X, Y, pr > 31 ? 31 : pr, pg > 63 ? 63 : pg, pb > 31 ? 31 : pb, al);
    }
  }
}

// --- the ground bird, out of focus ---------------------------------------------
// A blurred copy of each pose it is drawn in, made the first time it is needed:
// a box blur on premultiplied alpha, so the edge softens into the snow instead of
// picking up a dark halo.
static const int FAR_MAX = 16;
static const BirdSprite* s_farKey[FAR_MAX];
static RGBA8* s_farPx[FAR_MAX];

static const RGBA8* blurred(const BirdSprite* s) {
  for (int i = 0; i < FAR_MAX; i++)
    if (s_farKey[i] == s) return s_farPx[i];
  int slot = -1;
  for (int i = 0; i < FAR_MAX; i++)
    if (!s_farKey[i]) { slot = i; break; }
  if (slot < 0) return s->px;
  const int w = s->w, h = s->h, R = GROUND_BLUR;
  RGBA8* out = (RGBA8*)malloc(sizeof(RGBA8) * (size_t)w * h);
  if (!out) return s->px;
  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w; x++) {
      float a = 0, r = 0, g = 0, b = 0;
      int n = 0;
      for (int j = -R; j <= R; j++) {
        const int yy = y + j;
        for (int i = -R; i <= R; i++) {
          const int xx = x + i;
          n++;
          if (xx < 0 || yy < 0 || xx >= w || yy >= h) continue;
          const RGBA8& p = s->px[yy * w + xx];
          a += p.a;
          r += (float)p.r * p.a;
          g += (float)p.g * p.a;
          b += (float)p.b * p.a;
        }
      }
      RGBA8& o = out[y * w + x];
      o.a = (uint8_t)(a / n);
      o.r = a > 0 ? (uint8_t)(r / a) : 0;
      o.g = a > 0 ? (uint8_t)(g / a) : 0;
      o.b = a > 0 ? (uint8_t)(b / a) : 0;
    }
  }
  s_farKey[slot] = s;
  s_farPx[slot] = out;
  return out;
}

void feederDrawFree() {
  for (int i = 0; i < FAR_MAX; i++) {
    free(s_farPx[i]);
    s_farPx[i] = nullptr;
    s_farKey[i] = nullptr;
  }
}

float groundScale(float y) {
  int row = (int)y;
  row = row < 0 ? 0 : (row >= FB_H ? FB_H - 1 : row);
  return GROUND_SCALE[row];
}

// --- the scene ---------------------------------------------------------------------
static bool nearBand(float x, float reach, int x0, int x1) {
  return x + reach >= x0 && x - reach < x1;
}

static void drawBird(const World& w, const Bird& b, int x0, int x1, bool ground) {
  const BirdSprite* s = worldSprite(b);
  if (!s) return;
  // a branch or the trunk sits at its own depth; the feeder and the air are at
  // the sprites' own size
  float scale = 1.0f;
  if (ground) scale = groundScale(b.y);
  else if (b.state == BS_SIT && b.spot >= 0 && !FEEDER_SPOTS[b.spot].onFeeder) scale = FEEDER_SPOTS[b.spot].scale;
  const float reach = ((s->w > s->h ? s->w : s->h) + 2.0f) * scale;
  if (!nearBand(b.x, reach, x0, x1)) return;
  float lg[3];
  skySpriteGain(b.x, lg);
  const float ang = birdOnFeeder(b) ? w.theta : 0.0f;
  const RGBA8* px = ground ? blurred(s) : s->px;
  drawSprite(px, s->w, s->h, s->ax, s->ay, b.x, b.y, ang, b.mirror, scale, lg, ground ? HAZE_GROUND : HAZE_NEAR);
}

static void drawGroundShadow(const Bird& b, int x0, int x1) {
  if (b.state != BS_SIT && b.state != BS_HOP) return;
  const float rx = speciesCm(b.sp) * PX_PER_CM * 0.32f * groundScale(b.groundY), ry = rx * 0.28f;
  const float cx = b.state == BS_HOP ? b.x : b.groundX;
  if (!nearBand(cx, rx + 2, x0, x1)) return;
  float xs[12], ys[12];
  for (int i = 0; i < 12; i++) {
    const float a = (float)i * (6.2831853f / 12.0f);
    xs[i] = cx + cosf(a) * rx;
    ys[i] = b.groundY + sinf(a) * ry + 0.5f;
  }
  PolyPaint p;
  p.r0 = p.g0 = p.b0 = 0;
  p.a0 = SHADOW_ALPHA;
  fillPolyAA(xs, ys, 12, p);
}

// The blackbird lives out there the whole time: flying in and away too, so it
// neither pops in size on landing nor crosses in front of the feeder.
static bool onGround(const Bird& b) {
  return b.sp == SP_KOS;
}

// On the tray, inside the feeder: behind its roof and front posts. A bird coming down onto the tray goes in behind them for the last
// quarter of its way, so it does not flick from in front to behind on landing.
static bool inTray(const Bird& b) {
  if (b.spot < 0 || FEEDER_SPOTS[b.spot].kind != SPOT_TRAY) return false;
  if (b.state == BS_SIT) return true;
  return (b.state == BS_ARRIVE || b.state == BS_HOP) && !b.twoTries && b.t > 0.75f * b.dur;
}

void feederRenderBand(const World& w, int x0, int x1) {
  clipBand(x0, x1);
  skyApply(x0, x1, w.branchDisp);
  fallDrawBack(x0, x1, w.branchDisp);

  for (int i = 0; i < MAX_BIRDS; i++) {
    const Bird& b = w.birds[i];
    if (!birdVisible(b) || !onGround(b)) continue;
    drawGroundShadow(b, x0, x1);
    drawBird(w, b, x0, x1, true);
  }

  const bool feederHere = nearBand(FEEDER_PIVOT_X, (float)FEEDER_W + FEEDER_H, x0, x1);
  float feederLight[3];
  skySpriteGain(FEEDER_PIVOT_X, feederLight);
  if (feederHere)
    drawSprite(FEEDER_ARTS[w.coins.bg], FEEDER_W, FEEDER_H, FEEDER_ANCHOR_X, FEEDER_ANCHOR_Y, FEEDER_PIVOT_X, FEEDER_PIVOT_Y,
               w.theta, false, 1.0f, feederLight, 0.0f);

  // inside: behind the roof and the front post, which are drawn again over them -
  // only over the box those birds take: a second whole feeder cost 5 fps, and
  // with the tray full the columns alone are most of it
  float trayX0 = 1e9f, trayX1 = -1e9f, trayY0 = 1e9f, trayY1 = -1e9f;
  for (int i = 0; i < MAX_BIRDS; i++) {
    const Bird& b = w.birds[i];
    if (!inTray(b)) continue;
    drawBird(w, b, x0, x1, false);
    const BirdSprite* s = worldSprite(b);
    if (!s) continue;
    const float side = (s->ax > s->w - s->ax ? s->ax : s->w - s->ax) + 4.0f;   // either way it faces, a little turned
    if (b.x - side < trayX0) trayX0 = b.x - side;
    if (b.x + side > trayX1) trayX1 = b.x + side;
    if (b.y - s->ay - 4.0f < trayY0) trayY0 = b.y - s->ay - 4.0f;
    if (b.y + (s->h - s->ay) + 4.0f > trayY1) trayY1 = b.y + (s->h - s->ay) + 4.0f;
  }
  if (feederHere && trayX1 > trayX0) {
    const int core = xPortGetCoreID();
    const int cx0 = gClipX0[core], cx1 = gClipX1[core];
    const int fx0 = (int)floorf(trayX0) > cx0 ? (int)floorf(trayX0) : cx0;
    const int fx1 = (int)ceilf(trayX1) < cx1 ? (int)ceilf(trayX1) : cx1;
    if (fx0 < fx1) {
      clipBand(fx0, fx1);
      const int ry0 = (int)floorf(trayY0), ry1 = (int)ceilf(trayY1);
      drawSprite(FEEDER_FRONTS[w.coins.bg], FEEDER_W, FEEDER_H, FEEDER_ANCHOR_X, FEEDER_ANCHOR_Y, FEEDER_PIVOT_X,
                 FEEDER_PIVOT_Y, w.theta, false, 1.0f, feederLight, 0.0f, ry0 < 0 ? 0 : ry0,
                 ry1 > FB_H ? FB_H : ry1);
      clipBand(cx0, cx1);
    }
  }

  // outside: the perches and the side of the feeder, in front of it
  for (int i = 0; i < MAX_BIRDS; i++) {
    const Bird& b = w.birds[i];
    if (birdOnFeeder(b) && !inTray(b)) drawBird(w, b, x0, x1, false);
  }

  for (int i = 0; i < MAX_BIRDS; i++) {
    const Bird& b = w.birds[i];
    if (!birdVisible(b) || birdOnFeeder(b) || onGround(b) || inTray(b)) continue;
    drawBird(w, b, x0, x1, false);
  }

  fallDrawFront(x0, x1);
  snowDraw(x0, x1);
  clipBand(0, FB_W);
}
