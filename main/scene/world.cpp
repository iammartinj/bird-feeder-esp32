#pragma GCC optimize("O3")
#include "world.h"
#include "feeder_bg.h"
#include <math.h>
#include <stdio.h>

// --- the pendulum (brief) ---------------------------------------------------
static const float OMEGA0 = 2.1f;                 // rad/s, a period of about 3 s
static const float ZETA = 0.12f;
static const float MAX_THETA = 5.0f * 0.0174533f;
// Push per landing, per cm^3 of bird: mass goes with volume. Aimed at the collared
// dove (32 cm) swinging it about 3 deg, which leaves a great tit about 0.25 deg;
// the plain impulse gave 1.9 deg in the preview (damping eats the first swing),
// hence the 1.5.
static const float PUSH_PER_CM3 = 1.5f * (3.0f * 0.0174533f * OMEGA0) / (32.0f * 32.0f * 32.0f);
// Wind: fast flutter and slow gusts, each a random walk pulled back to zero, the
// same whatever the frame rate. It pushes the feeder and the branch alike.
static const float WIND_TAU_S = 0.8f, GUST_TAU_S = 7.0f;
static const float WIND_CALM = 0.02f, WIND_MODERATE = 0.045f;   // rad/s^2
// The branch, so the picture never stands still: the thin twig to
// the left of the knot and the thick limb to the right swing as two springs; the
// photo bends with them (sky.h), most at the tip, not at all at the fork.
static const float BRANCH_L_OMEGA = 6.2831853f / 1.7f, BRANCH_R_OMEGA = 6.2831853f / 2.6f;
static const float BRANCH_ZETA = 0.2f;
static const float BRANCH_GAIN_L = 13.0f, BRANCH_GAIN_R = 4.0f;   // px of tip per rad/s^2 of wind
static const float BRANCH_MAX_PX = 2.5f;
static const float BRANCH_ROOT_DX = 6.0f;                          // the fork, right of the knot
static float s_branchShape[SCR_W];

// --- visits ------------------------------------------------------------------
static const float GAP_BUSY_S = 15.0f;            // morning and late afternoon
static const float GAP_QUIET_S = 40.0f;           // around noon
static const float GAP_MAX_S = 90.0f;             // a thing on a desk, not a real feeder
static const float RARE_VISIT_CHANCE = 0.08f;

static const uint8_t W_PERCH = 1 << SPOT_PERCH, W_TRAY = 1 << SPOT_TRAY, W_SIDE = 1 << SPOT_SIDE,
                     W_BRANCH = 1 << SPOT_BRANCH, W_TRUNK = 1 << SPOT_TRUNK, W_GROUND = 1 << SPOT_GROUND;

struct SpeciesDef {
  const char* key;
  float   cm;
  uint8_t where;
  uint8_t groupMin, groupMax;
  float   stayMin, stayMax;
  float   weight;            // how often it is the one that comes; 0 = rare guest
  uint8_t returnOrder;       // after a startle: 0 first .. 3 last
  bool    large;             // small birds give way to it on the tray
  float   nervous[3];        // share of nervous, steady, quarrelsome
};

static const SpeciesDef SPECIES[N_SPECIES] = {
  {"vrabec",     14, W_TRAY | W_PERCH | W_BRANCH, 3, 6,  20,  90, 3.0f, 3, false, {0.25f, 0.35f, 0.40f}},
  {"konadra",    14, W_PERCH | W_BRANCH,          1, 1,   2,   6, 3.0f, 0, false, {0.60f, 0.40f, 0.00f}},
  {"modrinka",   11, W_PERCH | W_BRANCH,          1, 1,   2,   5, 2.0f, 0, false, {0.70f, 0.30f, 0.00f}},
  {"zvonek",     15, W_TRAY,                      1, 2,  60, 300, 1.0f, 2, false, {0.00f, 1.00f, 0.00f}},
  {"stehlik",    12, W_PERCH,                     2, 2,  15,  40, 1.0f, 1, false, {0.50f, 0.50f, 0.00f}},
  {"brhlik",     14, W_TRUNK | W_BRANCH,          1, 1,   5,  15, 1.0f, 1, false, {0.60f, 0.40f, 0.00f}},
  {"kos",        25, W_GROUND,                    1, 1,  60, 200, 0.8f, 2, false, {0.20f, 0.80f, 0.00f}},
  {"hrdlicka",   32, W_TRAY,                      1, 2,  40, 120, 0.8f, 2, true,  {0.10f, 0.90f, 0.00f}},
  {"strakapoud", 23, W_SIDE,                      1, 1,  20,  40, 0.0f, 1, true,  {0.30f, 0.70f, 0.00f}},
  {"sojka",      34, W_TRAY,                      1, 1,  10,  20, 0.0f, 2, true,  {0.40f, 0.60f, 0.00f}},
  {"veverka",    22, W_TRAY | W_BRANCH,           1, 1,  60, 180, 0.0f, 2, true,  {0.20f, 0.80f, 0.00f}},
};

static const char* POSE_NAMES[N_POSES] = {"sedi", "klove", "ohlizi_se", "pristava", "prikrceny", "hlavou_dolu"};
static const BirdSprite* s_sprites[N_SPECIES][N_POSES];

float speciesCm(int sp) { return SPECIES[sp].cm; }
const char* speciesKey(int sp) { return SPECIES[sp].key; }

// --- small things --------------------------------------------------------------
static float frand(World& w) {
  w.rng ^= w.rng << 13;
  w.rng ^= w.rng >> 17;
  w.rng ^= w.rng << 5;
  return (float)(w.rng >> 8) / 16777216.0f;
}
static float rnd(World& w, float a, float b) { return a + (b - a) * frand(w); }
static int irnd(World& w, int a, int b) { int v = a + (int)(frand(w) * (b - a + 1)); return v > b ? b : v; }

static bool isWinterMonth(int m) { return m == 12 || m == 1 || m == 2; }

const BirdSprite* worldSprite(const Bird& b) {
  const BirdSprite* s = s_sprites[b.sp][b.pose];
  return s ? s : s_sprites[b.sp][PO_SEDI];
}

bool birdVisible(const Bird& b) { return b.state != BS_OFF && !(b.state == BS_ARRIVE && b.t < 0); }

bool birdOnFeeder(const Bird& b) {
  return (b.state == BS_SIT) && b.spot >= 0 && FEEDER_SPOTS[b.spot].onFeeder;
}

static void spotPos(const World& w, int i, float& x, float& y) {
  const FeederSpot& s = FEEDER_SPOTS[i];
  if (s.onFeeder) {
    const float c = cosf(w.theta), sn = sinf(w.theta);
    x = FEEDER_PIVOT_X + s.x * c - s.y * sn;
    y = FEEDER_PIVOT_Y + s.x * sn + s.y * c;
  } else {
    x = s.x;
    y = s.y;
    if (s.kind == SPOT_BRANCH) {
      const int ix = (int)x;
      y += w.branchDisp[ix < 0 ? 0 : (ix >= SCR_W ? SCR_W - 1 : ix)];
    }
  }
}

static float nightStart(const World& w) { return w.winter ? 17.0f : 20.0f; }
static float dayStart(const World& w) { return w.winter ? 7.0f : 5.0f; }
static bool isNight(const World& w) { return w.hour < dayStart(w) || w.hour >= nightStart(w); }

// --- coins -----------------------------------------------------------------------
void worldInit(World& w, uint32_t seed, int month, float hour, int forceBg) {
  memset(&w, 0, sizeof(w));
  w.rng = seed ? seed * 2654435761u + 1 : 1;
  for (int i = 0; i < 8; i++) frand(w);
  w.month = month;
  w.winter = isWinterMonth(month);
  w.hour = hour;

  // the bend along the branch: nothing at the fork, most at the tip
  const float root = FEEDER_PIVOT_X + BRANCH_ROOT_DX;
  for (int x = 0; x < SCR_W; x++) {
    const float u = x < root ? (root - x) / root : (x - root) / (SCR_W - root);
    s_branchShape[x] = (x < root ? 1.0f : -1.0f) * powf(u, 1.6f);
  }

  for (int s = 0; s < N_SPECIES; s++) {
    for (int p = 0; p < N_POSES; p++) {
      char name[32];
      snprintf(name, sizeof(name), "%s_%s", SPECIES[s].key, POSE_NAMES[p]);
      s_sprites[s][p] = birdSprite(name);
    }
  }

  Coins& c = w.coins;
  c.bg = w.winter ? (frand(w) < 0.5f ? BG_WINTER_OVERCAST : BG_WINTER_MORNING) : BG_AUTUMN;
  if (forceBg >= 0 && forceBg < N_FEEDER_BG) c.bg = forceBg;
  const float sn = frand(w);
  c.snow = (c.bg == BG_WINTER_OVERCAST) ? (sn < 0.4f ? 0 : (sn < 0.8f ? 1 : 2)) : 0;
  c.windy = frand(w) < 0.4f;
  if (frand(w) < 0.5f) { c.flockMin = 3; c.flockMax = 4; } else { c.flockMin = 4; c.flockMax = 6; }
  c.greenfinches = frand(w) < 0.6f;
  const float r = frand(w);
  const bool autumnJay = month >= 9 && month <= 11;
  // one start in 30, 20 (autumn only) and 50, each in a band of its own
  c.rare = -1;
  const float jayBand = autumnJay ? 1.0f / 20 : 0.0f;
  if (r < 1.0f / 30) c.rare = SP_STRAKAPOUD;
  else if (r < 1.0f / 30 + jayBand) c.rare = SP_SOJKA;
  else if (r < 1.0f / 30 + jayBand + 1.0f / 50) c.rare = SP_VEVERKA;
  c.cloudPeriod = rnd(w, 20, 60);
  c.cloudDir = frand(w) < 0.5f ? -1.0f : 1.0f;

  for (int i = 0; i < N_FEEDER_SPOTS; i++) w.spotBird[i] = -1;
  for (int s = 0; s < N_SPECIES; s++) w.returnIn[s] = -1;
  w.nextVisit = rnd(w, 2, 8);
  w.autoStartle = rnd(w, 300, 900);
}

void worldSetClock(World& w, float hour) { w.hour = hour; }

int worldDescribe(const World& w, char* out, int n) {
  static const char* BG[] = {"winter overcast", "winter morning", "autumn"};
  static const char* SNOW[] = {"no snow", "light snow", "heavy snow"};
  return snprintf(out, n, "%s, %s, %s, sparrows %d-%d, greenfinches %s, rare guest %s",
                  BG[w.coins.bg], SNOW[w.coins.snow], w.coins.windy ? "windy" : "calm", w.coins.flockMin,
                  w.coins.flockMax, w.coins.greenfinches ? "yes" : "no",
                  w.coins.rare >= 0 ? SPECIES[w.coins.rare].key : "none");
}

// --- spots ---------------------------------------------------------------------------
static int birdIndex(const World& w, const Bird& b) { return (int)(&b - w.birds); }

static void release(World& w, Bird& b) {
  const int idx = birdIndex(w, b);
  for (int i = 0; i < N_FEEDER_SPOTS; i++)
    if (w.spotBird[i] == idx) w.spotBird[i] = -1;
}

static bool trayFree(const World& w, int needed) {
  int free = 0;
  for (int i = 0; i < N_FEEDER_SPOTS; i++)
    if (FEEDER_SPOTS[i].kind == SPOT_TRAY && w.spotBird[i] < 0) free++;
  return free >= needed;
}

// Claims a free spot of a kind the species uses; the dove takes two places on
// the tray and the squirrel all of it.
static int claimSpot(World& w, Bird& b) {
  const SpeciesDef& d = SPECIES[b.sp];
  int cand[N_FEEDER_SPOTS], n = 0;
  for (int i = 0; i < N_FEEDER_SPOTS; i++) {
    const FeederSpot& s = FEEDER_SPOTS[i];
    if (!(d.where & (1 << s.kind)) || w.spotBird[i] >= 0) continue;
    if (s.kind == SPOT_TRAY && b.sp == SP_HRDLICKA && !trayFree(w, 2)) continue;
    if (s.kind == SPOT_TRAY && b.sp == SP_VEVERKA && !trayFree(w, 3)) continue;
    cand[n++] = i;
  }
  if (!n) return -1;
  const int spot = cand[irnd(w, 0, n - 1)];
  const int idx = birdIndex(w, b);
  w.spotBird[spot] = (int8_t)idx;
  int extra = FEEDER_SPOTS[spot].kind == SPOT_TRAY ? (b.sp == SP_HRDLICKA ? 1 : (b.sp == SP_VEVERKA ? 2 : 0)) : 0;
  for (int i = 0; i < N_FEEDER_SPOTS && extra; i++) {
    if (FEEDER_SPOTS[i].kind == SPOT_TRAY && w.spotBird[i] < 0) { w.spotBird[i] = (int8_t)idx; extra--; }
  }
  if (FEEDER_SPOTS[spot].kind == SPOT_GROUND) {
    const FeederSpot& s = FEEDER_SPOTS[spot];
    b.groundX = rnd(w, s.x + 15, s.x1 - 15);
    b.groundY = rnd(w, s.y + 4, s.y1 - 4);
  }
  return spot;
}

static void targetOf(const World& w, const Bird& b, float& x, float& y) {
  if (b.spot < 0) { x = b.toX; y = b.toY; return; }
  if (FEEDER_SPOTS[b.spot].kind == SPOT_GROUND) { x = b.groundX; y = b.groundY; return; }
  spotPos(w, b.spot, x, y);
}

// --- flight --------------------------------------------------------------------------------
static void push(World& w, const Bird& b, float dirX, float share) {
  const float cm = SPECIES[b.sp].cm;
  w.omega += -dirX * share * PUSH_PER_CM3 * cm * cm * cm;
}

static void startArrival(World& w, Bird& b, float delay) {
  float tx, ty;
  targetOf(w, b, tx, ty);
  const int edge = irnd(w, 0, 2);
  if (SPECIES[b.sp].where == W_GROUND || edge == 0) {
    b.fromX = tx < 160 ? -20.0f : 340.0f;
    b.fromY = SPECIES[b.sp].where == W_GROUND ? ty - rnd(w, 20, 50) : rnd(w, 10, 110);
  } else if (edge == 1) {
    b.fromX = tx < 160 ? 340.0f : -20.0f;
    b.fromY = rnd(w, 10, 90);
  } else {
    b.fromX = rnd(w, 40, 280);
    b.fromY = -20.0f;
  }
  b.state = BS_ARRIVE;
  b.t = -delay;
  const bool big = SPECIES[b.sp].large || b.sp == SP_KOS;
  b.dur = big ? rnd(w, 0.6f, 1.0f) : rnd(w, 0.4f, 0.7f);
  b.twoTries = (b.sp == SP_HRDLICKA);
  b.x = b.fromX;
  b.y = b.fromY;
  b.mirror = tx > b.fromX;
  b.pose = PO_PRISTAVA;
}

static void startDepart(World& w, Bird& b) {
  if (b.state == BS_DEPART || b.state == BS_OFF) return;
  const bool onFeeder = b.spot >= 0 && FEEDER_SPOTS[b.spot].onFeeder && b.state == BS_SIT;
  release(w, b);
  b.spot = -1;
  b.state = BS_DEPART;
  b.fromX = b.x;
  b.fromY = b.y;
  b.mirror = frand(w) < 0.5f ? b.mirror : !b.mirror;
  b.toX = b.mirror ? 345.0f : -25.0f;
  b.toY = b.sp == SP_KOS ? b.y - rnd(w, 10, 30) : rnd(w, -25, 40);
  b.t = 0;
  b.dur = rnd(w, 0.5f, 0.8f);
  if (onFeeder) push(w, b, b.mirror ? -1.0f : 1.0f, 0.5f);
}

static void startHop(World& w, Bird& b, int newSpot, float gx) {
  b.fromX = b.x;
  b.fromY = b.y;
  if (newSpot >= 0) {
    release(w, b);
    b.spot = (int8_t)newSpot;
    w.spotBird[newSpot] = (int8_t)birdIndex(w, b);
  } else {
    b.groundX = gx;
  }
  float tx, ty;
  targetOf(w, b, tx, ty);
  b.mirror = tx > b.x;
  b.state = BS_HOP;
  b.t = 0;
  b.dur = b.sp == SP_KOS ? 0.25f : 0.35f;
}

// a gentle arc: the control point sits above the middle of the way
static void curve(Bird& b, float tx, float ty, float lift) {
  const float mx = (b.fromX + tx) * 0.5f, my = (b.fromY + ty) * 0.5f;
  b.ctrlX = mx;
  b.ctrlY = (b.fromY < ty ? b.fromY : ty) - lift + (my - (b.fromY < ty ? b.fromY : ty)) * 0.2f;
  b.toX = tx;
  b.toY = ty;
}

static void bezier(Bird& b, float u) {
  const float a = 1 - u;
  b.x = a * a * b.fromX + 2 * a * u * b.ctrlX + u * u * b.toX;
  b.y = a * a * b.fromY + 2 * a * u * b.ctrlY + u * u * b.toY;
}

// wings and crouch in turn while flying, a frame each (brief: 3 frames ~ 0.2 s)
static Pose flapPose(const Bird& b) {
  if (b.sp == SP_VEVERKA) return PO_PRISTAVA;
  return ((int)(b.t / 0.07f) & 1) ? PO_PRIKRCENY : PO_PRISTAVA;
}

// --- sitting ---------------------------------------------------------------------------------
static void land(World& w, Bird& b) {
  const SpeciesDef& d = SPECIES[b.sp];
  b.state = BS_SIT;
  float mult = b.pers == PERS_NERVOUS ? 0.6f : (b.pers == PERS_STEADY ? 1.5f : 1.0f);
  b.stay = rnd(w, d.stayMin, d.stayMax) * mult;
  b.act = 0;
  const FeederSpot& s = FEEDER_SPOTS[b.spot];
  if (s.onFeeder) push(w, b, b.toX > b.fromX ? 1.0f : -1.0f, 1.0f);
  // facing: outward on the end perches, into the picture on the side of the feeder
  if (strcmp(s.name, "perch_left") == 0) b.mirror = false;
  else if (strcmp(s.name, "perch_right") == 0) b.mirror = true;
  else if (s.kind == SPOT_SIDE) b.mirror = false;
  b.pose = (s.kind == SPOT_TRUNK && s_sprites[b.sp][PO_HLAVOU_DOLU]) ? PO_HLAVOU_DOLU : PO_SEDI;

  // what the arrival does to everyone else
  if (b.sp == SP_STRAKAPOUD) {
    for (int i = 0; i < MAX_BIRDS; i++) {
      Bird& o = w.birds[i];
      if (&o != &b && o.state == BS_SIT && o.sp != SP_KOS && o.stay > 1.0f) o.stay = rnd(w, 0.1f, 1.0f);
    }
  } else if (d.large && s.kind == SPOT_TRAY) {
    for (int i = 0; i < MAX_BIRDS; i++) {
      Bird& o = w.birds[i];
      if (&o == &b || o.state != BS_SIT || SPECIES[o.sp].large || o.spot < 0) continue;
      const SpotKind k = FEEDER_SPOTS[o.spot].kind;
      if ((k == SPOT_TRAY || k == SPOT_PERCH) && o.stay > 1.0f) o.stay = rnd(w, 0.1f, 1.0f);
    }
  }
}

static void choosePose(World& w, Bird& b) {
  const FeederSpot& s = FEEDER_SPOTS[b.spot];
  const float r = frand(w);
  if (s.kind == SPOT_TRUNK && s_sprites[b.sp][PO_HLAVOU_DOLU]) {
    b.pose = PO_HLAVOU_DOLU;
    b.act = rnd(w, 1, 3);
    return;
  }
  switch (b.sp) {
    case SP_KOS:
      // scratching: pecking and looking up in turn, now and then a hop
      if (r < 0.2f) {
        const FeederSpot& g = FEEDER_SPOTS[b.spot];
        // a hop of a few pixels: it is far off, back on the ground (draw.h)
        float gx = b.groundX + (frand(w) < 0.5f ? -1 : 1) * rnd(w, 2, 5);
        gx = gx < g.x + 10 ? g.x + 10 : (gx > g.x1 - 10 ? g.x1 - 10 : gx);
        startHop(w, b, -1, gx);
        return;
      }
      b.pose = b.pose == PO_KLOVE ? PO_OHLIZI : PO_KLOVE;
      b.act = rnd(w, 0.5f, 2.0f);
      return;
    case SP_ZVONEK:
      b.pose = r < 0.85f ? PO_SEDI : PO_KLOVE;
      b.act = rnd(w, 3, 8);
      return;
    case SP_VEVERKA:
      b.pose = r < 0.4f ? PO_PRIKRCENY : (r < 0.7f ? PO_KLOVE : PO_SEDI);
      b.act = rnd(w, 1.5f, 5);
      return;
    default:
      break;
  }
  const bool feeding = s.kind == SPOT_TRAY || s.kind == SPOT_PERCH || s.kind == SPOT_SIDE;
  if (b.pers == PERS_NERVOUS) {
    b.pose = r < 0.4f ? PO_OHLIZI : (r < 0.7f && feeding ? PO_KLOVE : PO_SEDI);
    b.act = rnd(w, 0.3f, 1.0f);
  } else if (b.pers == PERS_STEADY) {
    b.pose = r < 0.55f ? PO_SEDI : (r < 0.85f && feeding ? PO_KLOVE : PO_OHLIZI);
    b.act = rnd(w, 1.2f, 4.0f);
  } else {
    b.pose = r < 0.5f && feeding ? PO_KLOVE : (r < 0.8f ? PO_SEDI : PO_OHLIZI);
    b.act = rnd(w, 0.6f, 2.0f);
  }
  if (s.kind != SPOT_SIDE && frand(w) < 0.12f) b.mirror = !b.mirror;
}

// A quarrelsome sparrow shoves a small neighbour off the feeder: it moves to a
// free place, or leaves if there is none.
static void quarrel(World& w, Bird& b, float dt) {
  if (b.sp != SP_VRABEC || b.pers != PERS_QUARRELSOME || frand(w) > dt * 0.12f) return;
  const SpotKind k = FEEDER_SPOTS[b.spot].kind;
  if (k != SPOT_TRAY && k != SPOT_PERCH) return;
  for (int i = 0; i < MAX_BIRDS; i++) {
    Bird& o = w.birds[i];
    if (&o == &b || o.state != BS_SIT || o.spot < 0 || SPECIES[o.sp].large) continue;
    const SpotKind ok = FEEDER_SPOTS[o.spot].kind;
    if (ok != SPOT_TRAY && ok != SPOT_PERCH) continue;
    int free[N_FEEDER_SPOTS], n = 0;
    for (int s = 0; s < N_FEEDER_SPOTS; s++) {
      if (w.spotBird[s] < 0 && (SPECIES[o.sp].where & (1 << FEEDER_SPOTS[s].kind)) &&
          FEEDER_SPOTS[s].kind != SPOT_GROUND)
        free[n++] = s;
    }
    if (n) startHop(w, o, free[irnd(w, 0, n - 1)], 0);
    else startDepart(w, o);
    b.pose = PO_PRIKRCENY;
    b.act = 0.5f;
    return;
  }
}

// --- visits ---------------------------------------------------------------------------------------
static int presentCount(const World& w, int sp) {
  int n = 0;
  for (int i = 0; i < MAX_BIRDS; i++)
    if (w.birds[i].state != BS_OFF && w.birds[i].sp == sp) n++;
  return n;
}

static bool squirrelOnTray(const World& w) {
  for (int i = 0; i < MAX_BIRDS; i++) {
    const Bird& b = w.birds[i];
    if (b.state != BS_OFF && b.sp == SP_VEVERKA && b.spot >= 0 && FEEDER_SPOTS[b.spot].kind == SPOT_TRAY) return true;
  }
  return false;
}

static int spawnGroup(World& w, int sp) {
  const SpeciesDef& d = SPECIES[sp];
  int n = sp == SP_VRABEC ? irnd(w, w.coins.flockMin, w.coins.flockMax) : irnd(w, d.groupMin, d.groupMax);
  if (sp == SP_HRDLICKA && frand(w) < 0.7f) n = 1;   // "1, sometimes 2"
  int made = 0;
  for (int k = 0; k < n; k++) {
    Bird* b = nullptr;
    for (int i = 0; i < MAX_BIRDS; i++)
      if (w.birds[i].state == BS_OFF) { b = &w.birds[i]; break; }
    if (!b) break;
    memset(b, 0, sizeof(*b));
    b->sp = (uint8_t)sp;
    const float r = frand(w);
    b->pers = r < d.nervous[0] ? PERS_NERVOUS : (r < d.nervous[0] + d.nervous[1] ? PERS_STEADY : PERS_QUARRELSOME);
    b->spot = (int8_t)claimSpot(w, *b);
    if (b->spot < 0) break;
    startArrival(w, *b, sp == SP_STEHLIK ? k * 0.15f : k * rnd(w, 0.1f, 0.4f));
    made++;
  }
  if (made) w.visits++;
  return made;
}

static float meanGap(const World& w) {
  const float dawn = dayStart(w), dusk = nightStart(w);
  float gap = (w.hour < dawn + 3.0f || w.hour >= dusk - 3.0f) ? GAP_BUSY_S : GAP_QUIET_S;
  if (!w.winter) gap *= 2.0f;   // twice as busy in winter as in autumn
  return gap;
}

static void visit(World& w) {
  if (w.coins.rare >= 0 && frand(w) < RARE_VISIT_CHANCE && !presentCount(w, w.coins.rare)) {
    if (spawnGroup(w, w.coins.rare)) return;
  }
  float total = 0, weights[N_SPECIES];
  for (int s = 0; s < N_SPECIES; s++) {
    float wt = SPECIES[s].weight;
    if (s == SP_ZVONEK && !w.coins.greenfinches) wt = 0;
    if ((s == SP_KOS || s == SP_HRDLICKA) && presentCount(w, s)) wt = 0;
    if (s == SP_VRABEC && presentCount(w, s)) wt *= 0.2f;
    weights[s] = wt;
    total += wt;
  }
  if (total <= 0) return;
  float pick = frand(w) * total;
  for (int s = 0; s < N_SPECIES; s++) {
    pick -= weights[s];
    if (pick <= 0) { spawnGroup(w, s); return; }
  }
}

int worldVisit(World& w, int sp) {
  if (sp < 0 || sp >= N_SPECIES) return 0;
  return spawnGroup(w, sp);
}

int speciesByKey(const char* key) {
  for (int s = 0; s < N_SPECIES; s++)
    if (strcmp(SPECIES[s].key, key) == 0) return s;
  return -1;
}

void worldStartle(World& w, const char* why) {
  (void)why;
  w.startles++;
  static const float RETURN[4][2] = {{10, 18}, {15, 26}, {20, 32}, {28, 40}};
  for (int i = 0; i < MAX_BIRDS; i++) {
    Bird& b = w.birds[i];
    if (b.state == BS_OFF || b.state == BS_DEPART) continue;
    if (b.sp == SP_KOS) {
      // the blackbird only hops a bit further off and stays
      if (b.state == BS_SIT) {
        const FeederSpot& g = FEEDER_SPOTS[b.spot];
        float gx = b.groundX + (b.groundX < 160 ? -1 : 1) * rnd(w, 7, 12);
        gx = gx < g.x + 10 ? g.x + 10 : (gx > g.x1 - 10 ? g.x1 - 10 : gx);
        startHop(w, b, -1, gx);
      }
      continue;
    }
    if (b.state == BS_ARRIVE && b.t < 0) {   // had not set off yet
      release(w, b);
      b.state = BS_OFF;
    } else if (b.state == BS_ARRIVE || b.state == BS_HOP) {
      startDepart(w, b);
    } else {
      // in order of nerves, all gone within a second
      const float order = b.pers == PERS_NERVOUS ? 0.0f : (b.pers == PERS_QUARRELSOME ? 0.35f : 0.6f);
      b.stay = order + rnd(w, 0, 0.35f);
    }
    const SpeciesDef& d = SPECIES[b.sp];
    if (d.weight > 0 || b.sp == w.coins.rare)
      w.returnIn[b.sp] = rnd(w, RETURN[d.returnOrder][0], RETURN[d.returnOrder][1]);
  }
  w.calm = 10.0f;
  w.nextVisit = w.nextVisit < 12.0f ? 12.0f : w.nextVisit;
}

// --- the step ----------------------------------------------------------------------------------------
void worldStep(World& w, float dt) {
  w.t += dt;

  // the wind, so neither the feeder nor the branch ever quite stops
  w.windN += -w.windN * dt / WIND_TAU_S + sqrtf(2.0f * dt / WIND_TAU_S) * rnd(w, -1.732f, 1.732f);
  w.gust += -w.gust * dt / GUST_TAU_S + sqrtf(2.0f * dt / GUST_TAU_S) * rnd(w, -1.732f, 1.732f);
  w.windNow = 0.7f * w.windN + w.gust;
  const float wind = w.windNow * (w.coins.windy ? WIND_MODERATE : WIND_CALM);

  const float wl = BRANCH_L_OMEGA, wr = BRANCH_R_OMEGA;
  w.branchLv += (wl * wl * (BRANCH_GAIN_L * wind - w.branchL) - 2.0f * BRANCH_ZETA * wl * w.branchLv) * dt;
  w.branchRv += (wr * wr * (BRANCH_GAIN_R * wind - w.branchR) - 2.0f * BRANCH_ZETA * wr * w.branchRv) * dt;
  w.branchL += w.branchLv * dt;
  w.branchR += w.branchRv * dt;
  if (w.branchL > BRANCH_MAX_PX) w.branchL = BRANCH_MAX_PX;
  if (w.branchL < -BRANCH_MAX_PX) w.branchL = -BRANCH_MAX_PX;
  if (w.branchR > BRANCH_MAX_PX) w.branchR = BRANCH_MAX_PX;
  if (w.branchR < -BRANCH_MAX_PX) w.branchR = -BRANCH_MAX_PX;
  for (int x = 0; x < SCR_W; x++)
    w.branchDisp[x] = s_branchShape[x] > 0 ? s_branchShape[x] * w.branchL : -s_branchShape[x] * w.branchR;

  w.omega += (-OMEGA0 * OMEGA0 * w.theta - 2.0f * ZETA * OMEGA0 * w.omega + wind) * dt;
  w.theta += w.omega * dt;
  if (w.theta > MAX_THETA) { w.theta = MAX_THETA; w.omega = 0; }
  if (w.theta < -MAX_THETA) { w.theta = -MAX_THETA; w.omega = 0; }

  const bool night = isNight(w);

  // visits
  w.calm -= dt;
  w.autoStartle -= dt;
  if (w.autoStartle <= 0) {
    w.autoStartle = rnd(w, 300, 900);
    worldStartle(w, "auto");
  }
  if (!night) {
    for (int s = 0; s < N_SPECIES; s++) {
      if (w.returnIn[s] < 0) continue;
      w.returnIn[s] -= dt;
      if (w.returnIn[s] <= 0) {
        w.returnIn[s] = -1;
        if (!squirrelOnTray(w)) spawnGroup(w, s);
      }
    }
    w.nextVisit -= dt;
    if (w.nextVisit <= 0) {
      if (w.calm <= 0 && !squirrelOnTray(w)) visit(w);
      float gap = -log(1.0f - frand(w) * 0.999f) * meanGap(w);
      w.nextVisit = gap > GAP_MAX_S ? GAP_MAX_S : (gap < 3.0f ? 3.0f : gap);
    }
  } else {
    for (int s = 0; s < N_SPECIES; s++) w.returnIn[s] = -1;
  }

  // birds
  for (int i = 0; i < MAX_BIRDS; i++) {
    Bird& b = w.birds[i];
    switch (b.state) {
      case BS_OFF:
        break;

      case BS_ARRIVE: {
        b.t += dt;
        if (b.t < 0) break;
        float tx, ty;
        targetOf(w, b, tx, ty);
        if (b.twoTries) {
          // the dove comes in, misjudges it, lifts off and comes round again
          curve(b, tx, ty - 16.0f, 12.0f);
          const float u = b.t / b.dur;
          bezier(b, u > 1 ? 1 : u);
          if (u >= 1) {
            b.twoTries = false;
            b.fromX = b.x;
            b.fromY = b.y;
            b.t = 0;
            b.dur = 0.5f;
          }
        } else {
          curve(b, tx, ty, 18.0f);
          const float u = b.t / b.dur;
          bezier(b, u > 1 ? 1 : u);
          if (u >= 1) {
            b.x = tx;
            b.y = ty;
            land(w, b);
            break;
          }
        }
        b.pose = (b.dur - b.t < 0.12f) ? PO_PRISTAVA : flapPose(b);
        break;
      }

      case BS_HOP: {
        b.t += dt;
        float tx, ty;
        targetOf(w, b, tx, ty);
        curve(b, tx, ty, b.sp == SP_KOS ? 4.0f : 8.0f);
        const float u = b.t / b.dur;
        bezier(b, u > 1 ? 1 : u);
        b.pose = b.sp == SP_KOS ? PO_PRIKRCENY : flapPose(b);
        if (u >= 1) {
          b.x = tx;
          b.y = ty;
          const float keep = b.stay;
          land(w, b);
          b.stay = keep;
        }
        break;
      }

      case BS_SIT: {
        float tx, ty;
        targetOf(w, b, tx, ty);
        b.x = tx;
        b.y = ty;
        b.stay -= dt;
        b.act -= dt;
        if (night && b.stay > 5.0f) b.stay = 5.0f;
        if (b.stay <= 0) {
          startDepart(w, b);
          break;
        }
        if (b.act <= 0) choosePose(w, b);
        if (b.state == BS_SIT) quarrel(w, b, dt);
        break;
      }

      case BS_DEPART: {
        b.t += dt;
        if (b.t < 0.08f) {   // crouch before take-off
          b.pose = PO_PRIKRCENY;
          break;
        }
        curve(b, b.toX, b.toY, 10.0f);
        const float u = (b.t - 0.08f) / b.dur;
        bezier(b, u > 1 ? 1 : u);
        b.pose = flapPose(b);
        if (u >= 1) b.state = BS_OFF;
        break;
      }
    }
  }
}
