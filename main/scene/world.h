#ifndef FEEDER_WORLD_H
#define FEEDER_WORLD_H
// ---------------------------------------------------------------------------
// world.h -- the feeder and who comes to it.
//
// The aquarium's sim.cpp in spirit: coins at start instead of settings,
// personalities drawn per bird instead of one rule for all, a startle that
// everyone reacts to in their own way. Every bird is a small state machine,
//
//   off -> arriving -> sitting (sit | peck | look back | move)* -> leaving -> off
//
// and its pose is a function of the state. Nothing interpolates between poses.
// The feeder hangs from the branch as a damped pendulum; birds landing on it and
// leaving give it a push in proportion to their weight.
// ---------------------------------------------------------------------------
#include <Arduino.h>
#include "bird_art.h"
#include "feeder_art.h"
#include "gfx.h"

enum Species : uint8_t {
  SP_VRABEC, SP_KONADRA, SP_MODRINKA, SP_ZVONEK, SP_STEHLIK, SP_BRHLIK,
  SP_KOS, SP_HRDLICKA, SP_STRAKAPOUD, SP_SOJKA, SP_VEVERKA, N_SPECIES
};

enum Pose : uint8_t { PO_SEDI, PO_KLOVE, PO_OHLIZI, PO_PRISTAVA, PO_PRIKRCENY, PO_HLAVOU_DOLU, N_POSES };

enum BirdState : uint8_t { BS_OFF, BS_ARRIVE, BS_SIT, BS_HOP, BS_DEPART };

enum Personality : uint8_t { PERS_NERVOUS, PERS_STEADY, PERS_QUARRELSOME };

struct Bird {
  uint8_t state, sp, pose, pers;
  int8_t  spot;              // where it sits or is heading, -1 none
  bool    mirror;            // facing right (the art faces left)
  bool    twoTries;          // the collared dove's clumsy landing, first go
  float   x, y;              // anchor in scene pixels
  float   fromX, fromY, ctrlX, ctrlY, toX, toY;   // the flight's curve
  float   t, dur;            // flight time; t < 0 is a delay before starting
  float   stay;              // seconds left at the spot
  float   act;               // seconds left in the current pose
  float   groundX, groundY;  // the blackbird's place on the ground
};

static const int MAX_BIRDS = 16;

struct Coins {
  int   bg;
  int   snow;                // 0 none, 1 light, 2 heavy
  bool  windy;
  int   flockMin, flockMax;  // house sparrows
  bool  greenfinches;
  int   rare;                // SP_STRAKAPOUD, SP_SOJKA, SP_VEVERKA or -1
  float cloudPeriod, cloudDir;
};

struct World {
  Coins    coins;
  bool     winter;
  int      month;
  float    hour;
  float    theta, omega;     // the feeder's swing, rad and rad/s
  float    windN, gust;      // fast and slow wind noise, about 1 each
  float    windNow;          // the two together, as it blows this frame (fall.h)
  float    branchL, branchLv, branchR, branchRv;   // tip of the twig to the left / the limb to the right, px
  float    branchDisp[SCR_W];                      // how far down the branch is at each column, px
  Bird     birds[MAX_BIRDS];
  int8_t   spotBird[N_FEEDER_SPOTS];
  float    nextVisit;
  float    autoStartle;
  float    calm;
  float    returnIn[N_SPECIES];   // a visit owed after a startle, < 0 none
  float    t;
  uint32_t rng;
  int      visits, startles;
};

void worldInit(World& w, uint32_t seed, int month, float hour, int forceBg);
void worldSetClock(World& w, float hour);
void worldStep(World& w, float dt);
void worldStartle(World& w, const char* why);
// A visit of this species now, whatever the coins say (tests and the console).
// Returns how many came; 0 when there was no free place for them.
int  worldVisit(World& w, int sp);
int  speciesByKey(const char* key);   // -1 if there is no such species

const BirdSprite* worldSprite(const Bird& b);
bool  birdOnFeeder(const Bird& b);    // sitting on a spot that swings
bool  birdVisible(const Bird& b);
float speciesCm(int sp);
const char* speciesKey(int sp);
int   worldDescribe(const World& w, char* out, int n);

#endif
