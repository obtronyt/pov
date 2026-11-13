// Persistence of vision display on ESP8266.
// A hall sensor pulses once per motor revolution; the loop estimates the
// current angular slice from that period and shifts one 12 bit column out to
// the LED driver. Animations only write into frame[], they never touch the
// driver themselves.

#define TLC_CLK 14
#define TLC_DIN 13
#define TLC_EN 5
#define TLC_LATCH 12
#define BL_ADC A0
#define HE_SENS 4

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <math.h>
#include <stdarg.h>
#include <string.h>
#include "alpha.h"
#include "smiley.h"

// edit hotspot name and password before flashing
const char WIFI_SSID[] = "my-hotspot";
const char WIFI_PASS[] = "my-password";
const char MDNS_NAME[] = "pov";

#define SLICE_NUM 360UL
#define ALPHA_SLICE_NUM 120UL
#define ANIMATE_SLICE_NUM 60UL
#define TEXT_MAX 32
#define KICK_DONE 9

#define ANIM_AUTO 0x01  // taken by the auto cycle
#define ANIM_TEXT 0x02  // draws animText, web form offers the text box
#define ANIM_PICK 0x04  // offered in the web dropdown

// kick foot ball phase timings, tuned so the whole sequence runs ~11 s
#define FB_WALK_MS 2500
#define FB_OUCH_MS 2500
#define FB_BALL_MS 100
#define FB_BALL_STEP 2

// sweep group: four pure-maths patterns, SG_PHASE_MS each
#define SG_PHASES 4
#define SG_PHASE_MS 8000
#define SG_SPIRAL_MS 50
#define SG_RADAR_MS 12
#define SG_RIPPLE_MS 90
#define SG_MOIRE_MS 45
#define SG_RADAR_TAIL 24

struct Anim {
  const char *name;
  void (*begin)();
  void (*tick)();
  uint16_t slices;
  uint32_t dwellMs;
  uint8_t flags;
};

// ---------------------------------------------------------------- state

volatile uint32_t isrLastPulse = 0;
volatile uint32_t isrMtrTimePeriod = 0;

uint32_t now_milli = 0;
uint32_t previousSlice = 0;
uint32_t dynamicSlice = SLICE_NUM;
uint16_t frame[SLICE_NUM] = {0};

uint8_t animId = 0;
uint32_t animStarted = 0;
bool autoMode = true;
bool animationInProgress = false;  // animation must not be cut short
bool animDone = false;             // one shot animation reached its end
char animText[TEXT_MAX + 1] = "H E L L O";

uint32_t lastSlideExecute = 0;
uint32_t lastAnimateExecute = 0;

uint16_t random_index[SLICE_NUM] = {0};
uint16_t sine_wave[SLICE_NUM] = {0};
uint8_t pixelLife[SLICE_NUM][12] = {0};

uint16_t sliderPos = 0;
uint16_t sineIndex = 0;
uint16_t sineTrail[6] = {0};
uint32_t nextBlink = 0;
uint32_t blinkStarted = 0;
uint8_t blinkFrame = 0;
uint8_t lookState = 0;
uint8_t lookStep = 0;
uint32_t lookMoved = 0;
int8_t lookDx = 3;
int8_t lookDy = 3;
uint8_t smileyFrame = 0;
uint8_t smileySkip = 0;
uint8_t smileyHold = 0;
int8_t smileyRotate = -1;
uint16_t kickerId = 0;
uint16_t ballFall = 0;
uint16_t ballTravel = 0;
uint16_t walkerId = 0;
uint16_t walkerX = 0;
uint8_t fbPhase = 0;
uint32_t fbPhaseStarted = 0;
uint8_t sgPhase = 0;
uint32_t sgPhaseStarted = 0;
uint16_t sgStep = 0;
uint16_t sparkPixel = 0;
uint32_t sparkStarted = 0;

ESP8266WebServer server(80);
bool wifiReady = false;
bool mdnsReady = false;
uint32_t lastWifiTry = 0;

// ---------------------------------------------------------------- driver

void tlc_write_12_bits(uint16_t inp) {
  for (uint8_t i = 0; i < 12; i++) {
    digitalWrite(TLC_CLK, LOW);
    digitalWrite(TLC_DIN, (inp >> i) & 1);
    digitalWrite(TLC_CLK, HIGH);
  }
  digitalWrite(TLC_LATCH, HIGH);
  delayMicroseconds(1);
  digitalWrite(TLC_LATCH, LOW);
}

void IRAM_ATTR mIsr() {
  uint32_t now = micros();
  isrMtrTimePeriod = now - isrLastPulse;
  isrLastPulse = now;
}

// ---------------------------------------------------------------- drawing

void populate_sine_wave() {
  float phase = 0.2f;
  for (uint16_t i = 0; i < SLICE_NUM; i++) {
    float val = sin((2 * PI * i * 6) / SLICE_NUM + phase);
    phase += 0.2f;
    val = (val + 1.0f) * 0.5f;
    sine_wave[i] = 1 << (uint16_t)(val * 11 + 0.5f);
  }
}

// start_row 0 puts the glyph on D1, start_pos 0 is the 90 deg mark
void load_string(const char *s, uint16_t start_pos, uint8_t start_row, bool merge) {
  if (s == NULL)
    return;
  if (!merge)
    memset(frame, 0, sizeof(frame));
  uint16_t *temp = frame + (start_pos >= dynamicSlice ? dynamicSlice - 1 : start_pos);
  uint8_t len = strlen(s);
  uint8_t cols[5];
  // 5 glyph rows on top of start_row must stay inside the 12 LED column
  start_row = start_row > 7 ? 7 : start_row;
  for (uint8_t i = 0; i < len; i++) {
    memcpy_P(cols, &alpha[(s[i] - ' ') * 5], 5);
    for (uint8_t j = 0; j < 5; j++) {
      *temp |= ((uint16_t)cols[j]) << start_row;
      temp++;
      if (temp == frame + dynamicSlice)
        temp = frame;
    }
  }
}

void sliding_string(const char *s, uint32_t slide_time_ms) {
  if ((now_milli - lastSlideExecute) > slide_time_ms) {
    load_string(s, sliderPos, 0, false);
    sliderPos = (sliderPos + 1) % dynamicSlice;
    lastSlideExecute = now_milli;
  }
}

uint16_t compute_mid(const char *s, bool reverse) {
  uint16_t mid = strlen(s) / 2;
  if (!reverse)
    return dynamicSlice - (mid * 5);
  return (dynamicSlice / 2) - (mid * 5);
}

void set_pixel(uint16_t x, uint16_t y, bool on) {
  if (on)
    frame[x % dynamicSlice] |= 1 << (y % 12);
  else
    frame[x % dynamicSlice] &= (1 << (y % 12)) ^ 0xFFFF;
}

// 10x8 rectangle with a 3x3 void for the pupil
void generate_eye(uint16_t start_col, uint8_t eye_ball_x, uint8_t eye_ball_y, char blink) {
  uint16_t eye_frame[8] = {0};
  for (uint8_t i = 0; i < 10; i++) {
    for (uint8_t j = 0; j < 8; j++) {
      if (blink) {
        if (i == 5 || i == 6)
          eye_frame[j] |= 1 << i;
      } else if (!(i >= eye_ball_y && i < eye_ball_y + 3 && j >= eye_ball_x && j < eye_ball_x + 3)) {
        eye_frame[j] |= 1 << (i + 1);
      }
    }
  }
  for (uint8_t i = 0; i < 8; i++)
    frame[(start_col + i) % dynamicSlice] = eye_frame[i];
}

void randomize_array() {
  for (uint16_t i = 0; i < SLICE_NUM; i++)
    random_index[i] = i;
  for (uint16_t i = 0; i < SLICE_NUM; i++) {
    uint16_t j = (uint16_t)random(SLICE_NUM);
    uint16_t temp = random_index[j];
    random_index[j] = random_index[i];
    random_index[i] = temp;
  }
}

// ---------------------------------------------------------------- animations

void tick_none() {
}

void begin_title() {
  const char *s = "UBUCON 2026";
  load_string(s, compute_mid(s, false), 0, false);
}

void begin_loading() {
  sliderPos = 0;
  lastSlideExecute = now_milli;
  load_string("L O A D I N G", 0, 0, false);
}

void tick_loading() {
  sliding_string("L O A D I N G", 100);
}

// text animations size the circle to the message so long strings do not
// overwrite their own start
void text_fit_slices() {
  uint32_t need = (uint32_t)strlen(animText) * 5;
  if (need < ALPHA_SLICE_NUM)
    need = ALPHA_SLICE_NUM;
  if (need > SLICE_NUM)
    need = SLICE_NUM;
  dynamicSlice = need;
}

void begin_text() {
  text_fit_slices();
  load_string(animText, compute_mid(animText, false), 0, false);
}

void begin_scroll_text() {
  text_fit_slices();
  sliderPos = 0;
  lastSlideExecute = now_milli;
  load_string(animText, 0, 0, false);
}

void tick_scroll_text() {
  sliding_string(animText, 100);
}

void begin_sine() {
  sineIndex = 0;
  memset(sineTrail, 0, sizeof(sineTrail));
}

void tick_sine() {
  if (now_milli - lastAnimateExecute < 50)
    return;
  lastAnimateExecute = now_milli;
  for (uint8_t i = 5; i > 0; i--)
    sineTrail[i] = sineTrail[i - 1];
  sineTrail[0] = sineIndex;
  memset(frame, 0, sizeof(frame));
  for (uint8_t i = 0; i < 6; i++)
    frame[sineTrail[i]] = sine_wave[sineTrail[i]];
  sineIndex = (sineIndex + 1) % SLICE_NUM;
  if (sineIndex == 0)
    memset(sineTrail, 0, sizeof(sineTrail));
}

// The pupils walk a fixed pattern while a blink fires on its own random
// interval and takes over the eyes for as long as it lasts. The mouth is drawn
// once in begin and survives because generate_eye only touches the eye slices.
void begin_eyes() {
  lookState = 0;
  lookStep = 0;
  lookMoved = 0;
  lookDx = 3;
  lookDy = 3;
  blinkFrame = 0;
  blinkStarted = 0;
  nextBlink = 0;
  load_string("---------", (dynamicSlice / 2) - 5, 0, true);
  generate_eye(dynamicSlice - 10, 3, 3, 0);
  generate_eye(10, 3, 3, 0);
}

void tick_eyes() {
  if (nextBlink == 0)
    nextBlink = now_milli + (uint32_t)random(800, 1200);

  if (blinkFrame) {
    if (now_milli - blinkStarted <= 400)
      return;  // hold the closed eyes, pupils stay where they were
    blinkFrame = 0;
    nextBlink = now_milli + (uint32_t)random(800, 1200);
    lookMoved = now_milli;
    generate_eye(dynamicSlice - 10, (uint8_t)lookDx, (uint8_t)lookDy, 0);
    generate_eye(10, (uint8_t)lookDx, (uint8_t)lookDy, 0);
    return;
  }

  if (now_milli >= nextBlink) {
    blinkFrame = 1;
    blinkStarted = now_milli;
    generate_eye(dynamicSlice - 10, 3, 3, 1);
    generate_eye(10, 3, 3, 1);
    return;
  }

  if (now_milli - lookMoved < 150)
    return;
  lookMoved = now_milli;
  switch (lookState) {
    case 0:  // centre to right
      lookDx = 3 + lookStep;
      lookDy = 3;
      if (++lookStep > 2) {
        lookStep = 0;
        lookState = 1;
      }
      break;
    case 2:  // centre to diagonal right
      lookDx = 3 + lookStep;
      lookDy = 3 + lookStep;
      if (++lookStep > 2) {
        lookStep = 0;
        lookState = 3;
      }
      break;
    case 4:  // centre to left
      lookDx = 3 - lookStep;
      lookDy = 3;
      if (++lookStep > 2) {
        lookStep = 0;
        lookState = 5;
      }
      break;
    case 6:  // centre to diagonal left
      lookDx = 3 - lookStep;
      lookDy = 3 - lookStep;
      if (++lookStep > 2) {
        lookStep = 0;
        lookState = 0;
      }
      break;
    default:  // odd states park the pupil back in the centre
      lookDx = 3;
      lookDy = 3;
      lookState++;
      break;
  }
  generate_eye(dynamicSlice - 10, (uint8_t)lookDx, (uint8_t)lookDy, 0);
  generate_eye(10, (uint8_t)lookDx, (uint8_t)lookDy, 0);
}

void begin_smiley() {
  smileyFrame = 0;
  smileySkip = 0;
  smileyHold = 0;
  smileyRotate = -1;
}

void tick_smiley() {
  if (now_milli - lastAnimateExecute <= 100)
    return;
  if (smileyHold > 10) {
    smileySkip = 0;
    smileyHold = 0;
  }
  if (smileySkip) {
    smileyHold++;
    return;
  }
  memcpy_P(frame, smiley_face_animation[smileyFrame], sizeof(frame));
  lastAnimateExecute = now_milli;
  if (smileyFrame == 5 || smileyFrame == 0) {
    smileyRotate = -smileyRotate;
    smileySkip = 1;
  }
  smileyFrame = (uint8_t)(smileyFrame + smileyRotate);
}

void begin_battery() {
  const char *s = "B A T T E R Y";
  const float conversionFactor = 10.0F / (10.0F + 100.0F);
  const float lowLevel = 3.45F;
  const float highLevel = 4.2F;
  int bl = 0;
  for (uint8_t i = 0; i < 5; i++)
    bl += analogRead(BL_ADC);
  bl /= 5;
  float battVol = (bl / 1023.0F) / conversionFactor;
  float battLevel = max((battVol - lowLevel) / (highLevel - lowLevel), 0.0F);
  uint8_t battLevelMapped = ALPHA_SLICE_NUM * battLevel;
  battLevelMapped = battLevelMapped > ALPHA_SLICE_NUM ? ALPHA_SLICE_NUM : battLevelMapped;
  memset(frame, 0, sizeof(frame));
  for (uint8_t i = 0; i < battLevelMapped; i++)
    frame[i] = 0x0007;  // bottom three LEDs form the bar
  load_string(s, compute_mid(s, false), 4, true);
}

// ---- kick foot ball, run as three phases by begin_football/tick_football ----

void fb_walk_begin() {
  walkerId = 0;
  walkerX = 0;
  lastAnimateExecute = 0;
}

void fb_walk_tick() {
  if (now_milli - lastAnimateExecute < 200)
    return;
  memset(frame, 0, sizeof(frame));
  uint16_t *temp = frame + walkerX;
  for (uint8_t i = 0; i < 10; i++) {
    *temp = walker[walkerId * 10 + i];
    temp++;
    if (temp == frame + dynamicSlice)
      temp = frame;
  }
  walkerId = (walkerId + 1) % 4;
  walkerX = (walkerX + 1) % dynamicSlice;
  lastAnimateExecute = now_milli;
}

void fb_kick_begin() {
  kickerId = 0;
  ballFall = 0;
  ballTravel = 0;
  lastAnimateExecute = 0;
}

void fb_kick_tick() {
  const uint16_t ballBits = 0x3;
  const uint16_t hitIndex = 2;
  uint16_t *temp = NULL;
  // the ball-travel phase runs on a faster clock than the poses
  uint32_t interval = (kickerId == 4) ? FB_BALL_MS : 220;
  if (now_milli - lastAnimateExecute < interval)
    return;
  switch (kickerId) {
    case 0:  // stand
      memset(frame, 0, sizeof(frame));
      memcpy(frame, kicker, sizeof(uint16_t) * 10);
      kickerId++;
      break;
    case 1:  // ball drops
      memset(frame, 0, sizeof(frame));
      memcpy(frame, kicker, sizeof(uint16_t) * 10);
      frame[8] |= ballBits << ballFall;
      frame[9] |= ballBits << ballFall;
      if (++ballFall >= 10)
        kickerId++;
      break;
    case 2:
    case 3:  // wind up and strike
      memset(frame, 0, sizeof(frame));
      memcpy(frame, kicker + (10 * (kickerId - 1)), sizeof(uint16_t) * 10);
      kickerId++;
      ballFall = 10;
      // distance the ball covers before it comes back round to the kicker
      ballTravel = (hitIndex + dynamicSlice - ballFall) % dynamicSlice;
      break;
    case 4:  // ball travels round and comes back
      if (ballTravel == 0) {
        kickerId++;
        break;
      }
      memset(frame, 0, sizeof(frame));
      memcpy(frame, walker + ((ballFall % 4) * 10), sizeof(uint16_t) * 10);
      {
        uint16_t step = ballTravel < FB_BALL_STEP ? ballTravel : FB_BALL_STEP;
        uint16_t ballBounce = (ballFall % 4) + 5;
        temp = frame + ballFall;
        *temp = ballBits << ballBounce;
        ballFall = (ballFall + step) % dynamicSlice;
        ballTravel -= step;
        temp = frame + ballFall;
        *temp = ballBits << ballBounce;
      }
      break;
    case 5:
    case 6:
    case 7:  // taking the hit
      memset(frame, 0, sizeof(frame));
      memcpy(frame, kicker + (10 * (kickerId - 2)), sizeof(uint16_t) * 10);
      kickerId++;
      break;
    case 8:
      memset(frame, 0, sizeof(frame));
      memcpy(frame, kicker + (10 * 5), sizeof(uint16_t) * 10);
      kickerId = KICK_DONE;
      break;
    default:
      break;
  }
  lastAnimateExecute = now_milli;
}

void fb_ouch_draw() {
  const char *s = "O U C H";
  memset(frame, 0, sizeof(frame));
  memcpy(frame, kicker + (10 * 5), sizeof(uint16_t) * 10);
  load_string(s, compute_mid(s, true), 0, true);
}

// Each phase is drawn at the slice count it was authored for, so the figure
// keeps its intended size; previousSlice is invalidated on every change so the
// next column goes out against the new scale.
void fb_enter_phase(uint8_t phase, uint16_t slices) {
  fbPhase = phase;
  fbPhaseStarted = now_milli;
  dynamicSlice = slices;
  previousSlice = 0xFFFFFFFFUL;
}

void begin_football() {
  animationInProgress = true;
  fb_enter_phase(0, 100);
  fb_walk_begin();
}

void tick_football() {
  switch (fbPhase) {
    case 0:  // walk in
      fb_walk_tick();
      if (now_milli - fbPhaseStarted >= FB_WALK_MS) {
        fb_enter_phase(1, ANIMATE_SLICE_NUM);
        fb_kick_begin();
      }
      break;
    case 1:  // drop, strike, chase, take the hit
      fb_kick_tick();
      if (kickerId == KICK_DONE) {
        fb_enter_phase(2, 100);
        fb_ouch_draw();
      }
      break;
    default:  // hold the ouch pose, then hand back to the dispatcher
      if (now_milli - fbPhaseStarted >= FB_OUCH_MS) {
        animationInProgress = false;
        animDone = true;
      }
      break;
  }
}

void begin_sparkle() {
  sparkPixel = 0;
  sparkStarted = now_milli;
  randomize_array();
  memset(frame, 0, sizeof(frame));
  memset(pixelLife, 0, sizeof(pixelLife));
}

void tick_sparkle() {
  if (now_milli - lastAnimateExecute < 1)
    return;
  lastAnimateExecute = now_milli;
  for (uint16_t i = 0; i < SLICE_NUM; i++) {
    for (uint8_t j = 0; j < 12; j++) {
      if (pixelLife[i][j] > 0 && --pixelLife[i][j] == 0)
        set_pixel(i, j, false);
    }
  }
  for (uint8_t m = 0; m < 5; m++) {
    uint16_t row = random(12);
    set_pixel(random_index[sparkPixel], row, true);
    pixelLife[random_index[sparkPixel]][row] = random(5, 15);
    if (++sparkPixel >= SLICE_NUM) {
      randomize_array();
      sparkPixel = 0;
    }
  }
  if (now_milli - sparkStarted > 7000) {
    animDone = true;
    sparkStarted = now_milli;
  }
}

// ---- sweep group, four pure-maths patterns driven by tick_sweep_group ----
//
// All four run at the full SLICE_NUM resolution and are integer only: the
// divisors below are compile-time constants, so the compiler turns them into
// multiply-and-shift and nothing in the hot path touches a float.

// Three arms, because i/10 covers 0..35 across the circle, which is 3 x 12.
void sg_spiral_tick() {
  if (now_milli - lastAnimateExecute < SG_SPIRAL_MS)
    return;
  lastAnimateExecute = now_milli;
  for (uint16_t i = 0; i < SLICE_NUM; i++)
    frame[i] = 1 << ((i / 10 + sgStep) % 12);
  sgStep++;
}

// Full column at the head, then progressively fewer rings behind it, so the
// tail thins towards the tip instead of fading (the driver has no brightness).
void sg_radar_tick() {
  if (now_milli - lastAnimateExecute < SG_RADAR_MS)
    return;
  lastAnimateExecute = now_milli;
  memset(frame, 0, sizeof(frame));
  for (uint8_t k = 0; k < SG_RADAR_TAIL; k++) {
    uint16_t pos = (sgStep + SLICE_NUM - k) % SLICE_NUM;
    frame[pos] = 0x0FFF >> ((k * 12) / SG_RADAR_TAIL);
  }
  sgStep = (sgStep + 1) % SLICE_NUM;
}

// Every slice identical, so this is two rings marching outward together.
void sg_ripple_tick() {
  if (now_milli - lastAnimateExecute < SG_RIPPLE_MS)
    return;
  lastAnimateExecute = now_milli;
  uint16_t mask = (1 << (sgStep % 12)) | (1 << ((sgStep + 6) % 12));
  for (uint16_t i = 0; i < SLICE_NUM; i++)
    frame[i] = mask;
  sgStep++;
}

// Triangle wave across the 12 rings, period 24 steps.
uint8_t tri_ring(uint16_t x) {
  x %= 24;
  return x < 12 ? x : 23 - x;
}

// Two counter-rotating waves at 5 and 8 cycles per revolution. Both divisors
// land on an exact multiple of 24 after a full circle (120 and 192), so the
// pattern closes with no seam at slice 0, and the 5-against-8 beat is what
// produces the drifting interference.
void sg_moire_tick() {
  if (now_milli - lastAnimateExecute < SG_MOIRE_MS)
    return;
  lastAnimateExecute = now_milli;
  uint16_t back = 24 - (sgStep % 24);
  for (uint16_t i = 0; i < SLICE_NUM; i++)
    frame[i] = (1 << tri_ring(i / 3 + sgStep)) | (1 << tri_ring((i * 8) / 15 + back));
  sgStep++;
}

void sg_enter_phase(uint8_t phase) {
  sgPhase = phase;
  sgPhaseStarted = now_milli;
  sgStep = 0;
  lastAnimateExecute = 0;
  memset(frame, 0, sizeof(frame));
}

void begin_sweep_group() {
  sg_enter_phase(0);
}

void tick_sweep_group() {
  switch (sgPhase) {
    case 0:
      sg_spiral_tick();
      break;
    case 1:
      sg_radar_tick();
      break;
    case 2:
      sg_ripple_tick();
      break;
    default:
      sg_moire_tick();
      break;
  }
  if (now_milli - sgPhaseStarted >= SG_PHASE_MS)
    sg_enter_phase((sgPhase + 1) % SG_PHASES);
}

void begin_fin() {
  const char *s = "- - F I N - -";
  load_string(s, compute_mid(s, false), 0, false);
}

// ---------------------------------------------------------------- dispatch

// ANIM_AUTO entries without ANIM_PICK are auto-cycle only: they are the
// intro/outro cards and never appear in the dropdown.
const Anim anims[] = {
  {"Title",          begin_title,       tick_none,        ALPHA_SLICE_NUM,   3000, ANIM_AUTO},
  {"Loading",        begin_loading,     tick_loading,     ALPHA_SLICE_NUM,   3000, ANIM_AUTO},
  {"Sine",           begin_sine,        tick_sine,        SLICE_NUM,         6000, ANIM_AUTO | ANIM_PICK},
  {"Eyes",           begin_eyes,        tick_eyes,        ALPHA_SLICE_NUM,   9000, ANIM_AUTO | ANIM_PICK},
  {"Smiley",         begin_smiley,      tick_smiley,      SLICE_NUM,         9000, ANIM_AUTO | ANIM_PICK},
  {"Battery",        begin_battery,     tick_none,        ALPHA_SLICE_NUM,   3000, ANIM_AUTO | ANIM_PICK},
  {"Kick Foot Ball", begin_football,    tick_football,    100,               3000, ANIM_AUTO | ANIM_PICK},
  {"Sparkle",        begin_sparkle,     tick_sparkle,     SLICE_NUM,         7000, ANIM_AUTO | ANIM_PICK},
  {"Sweep group",    begin_sweep_group, tick_sweep_group, SLICE_NUM,        32000, ANIM_AUTO | ANIM_PICK},
  {"Fin",            begin_fin,         tick_none,        ALPHA_SLICE_NUM,   3000, ANIM_AUTO},
  {"Text",           begin_text,        tick_none,        ALPHA_SLICE_NUM,   6000, ANIM_TEXT | ANIM_PICK},
  {"Scroll text",    begin_scroll_text, tick_scroll_text, ALPHA_SLICE_NUM,   8000, ANIM_TEXT | ANIM_PICK}
};

const uint8_t ANIM_COUNT = sizeof(anims) / sizeof(anims[0]);

void select_anim(uint8_t id) {
  if (id >= ANIM_COUNT)
    id = 0;
  animId = id;
  animStarted = millis();
  dynamicSlice = anims[id].slices;
  previousSlice = 0xFFFFFFFFUL;
  animationInProgress = false;
  animDone = false;
  nextBlink = 0;
  lastAnimateExecute = 0;
  lastSlideExecute = 0;
  memset(frame, 0, sizeof(frame));
  anims[id].begin();
}

uint8_t next_auto_anim(uint8_t from) {
  uint8_t id = from;
  for (uint8_t n = 0; n < ANIM_COUNT; n++) {
    id = (id + 1) % ANIM_COUNT;
    if (anims[id].flags & ANIM_AUTO)
      return id;
  }
  return 0;
}

// ---------------------------------------------------------------- web

// The whole page is assembled here before a single send(). With 14 entries it
// comes to roughly 1.3 kB, so it leaves in one segment and costs the display
// one handleClient() call.
char pageBuf[2048];
size_t pageLen = 0;

void page_reset() {
  pageLen = 0;
  pageBuf[0] = 0;
}

void page_add(const char *fmt, ...) {
  if (pageLen >= sizeof(pageBuf) - 1)
    return;
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(pageBuf + pageLen, sizeof(pageBuf) - pageLen, fmt, ap);
  va_end(ap);
  if (n > 0)
    pageLen += ((size_t)n < sizeof(pageBuf) - pageLen) ? (size_t)n : sizeof(pageBuf) - pageLen - 1;
}

void page_add_escaped(const char *s) {
  for (; *s; s++) {
    switch (*s) {
      case '&': page_add("&amp;"); break;
      case '<': page_add("&lt;"); break;
      case '>': page_add("&gt;"); break;
      case '"': page_add("&quot;"); break;
      default: page_add("%c", *s); break;
    }
  }
}

void handle_root() {
  page_reset();
  page_add("<!DOCTYPE html><html><head><meta name=viewport "
           "content=\"width=device-width,initial-scale=1\"><title>POV</title></head>"
           "<body><h3>POV</h3><form action=/set><select id=a name=id>"
           "<option value=auto%s>Auto</option>",
           autoMode ? " selected" : "");
  for (uint8_t i = 0; i < ANIM_COUNT; i++) {
    // an auto-only animation set by hand still gets listed while it is running,
    // so the form shows what is actually on screen
    if (!(anims[i].flags & ANIM_PICK) && !(!autoMode && animId == i))
      continue;
    page_add("<option value=%u%s>%s</option>", i,
             (!autoMode && animId == i) ? " selected" : "", anims[i].name);
  }
  page_add("</select> <span id=b><input name=t maxlength=%u value=\"", TEXT_MAX);
  page_add_escaped(animText);
  page_add("\"></span> <button>Set</button></form><script>var T=[");
  for (uint8_t i = 0; i < ANIM_COUNT; i++)
    if (anims[i].flags & ANIM_TEXT)
      page_add("%u,", i);
  page_add("],a=document.getElementById('a'),b=document.getElementById('b');"
           "function t(){b.style.display=T.indexOf(+a.value)<0?'none':'inline'}"
           "a.onchange=t;t()</script></body></html>");
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "text/html", pageBuf);
}

// The 5x7 font only covers 0x20 to 0x5A, so fold to upper case and drop
// anything outside that range.
void set_anim_text(const String &in) {
  size_t n = 0;
  for (size_t i = 0; i < in.length() && n < TEXT_MAX; i++) {
    char c = in[i];
    if (c >= 'a' && c <= 'z')
      c -= 32;
    animText[n++] = (c >= ' ' && c <= 'Z') ? c : ' ';
  }
  animText[n] = 0;
  if (n == 0)
    strcpy(animText, " ");
}

void handle_set() {
  String id = server.arg("id");
  if (id == "auto") {
    autoMode = true;
    animStarted = millis();
  } else {
    char *end = NULL;
    unsigned long n = strtoul(id.c_str(), &end, 10);
    if (end != id.c_str() && *end == 0 && n < ANIM_COUNT) {
      if ((anims[n].flags & ANIM_TEXT) && server.hasArg("t"))
        set_anim_text(server.arg("t"));
      autoMode = false;
      select_anim((uint8_t)n);
    }
  }
  server.sendHeader("Location", "/", true);
  server.send(303, "text/plain", "");
}

void service_wifi() {
  server.handleClient();
  if (WiFi.status() == WL_CONNECTED) {
    if (!wifiReady) {
      wifiReady = true;
      Serial.print("ip ");
      Serial.println(WiFi.localIP());
      mdnsReady = MDNS.begin(MDNS_NAME);
      if (mdnsReady)
        MDNS.addService("http", "tcp", 80);
    }
    if (mdnsReady)
      MDNS.update();
    return;
  }
  wifiReady = false;
  mdnsReady = false;
  if (millis() - lastWifiTry < 5000)
    return;
  lastWifiTry = millis();
  WiFi.begin(WIFI_SSID, WIFI_PASS);
}

// ---------------------------------------------------------------- main

void setup() {
  Serial.begin(115200);
  randomSeed(millis());
  populate_sine_wave();

  pinMode(HE_SENS, INPUT);
  pinMode(TLC_CLK, OUTPUT);
  pinMode(TLC_EN, OUTPUT);
  pinMode(TLC_DIN, OUTPUT);
  pinMode(TLC_LATCH, OUTPUT);
  digitalWrite(TLC_EN, LOW);
  attachInterrupt(digitalPinToInterrupt(HE_SENS), mIsr, FALLING);

  WiFi.mode(WIFI_STA);
  WiFi.setSleepMode(WIFI_NONE_SLEEP);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  lastWifiTry = millis();
  server.on("/", handle_root);
  server.on("/set", handle_set);
  server.begin();

  now_milli = millis();
  select_anim(0);
}

void loop() {
  uint32_t localMtrTimePeriod;
  uint32_t localLastPulse;

  now_milli = millis();
  service_wifi();

  noInterrupts();
  localMtrTimePeriod = isrMtrTimePeriod;
  localLastPulse = isrLastPulse;
  interrupts();

  // no pulse yet, nothing to synchronise against
  if (localMtrTimePeriod == 0)
    return;

  uint32_t elapsed = now_milli - animStarted;
  if (animDone) {
    if (autoMode)
      select_anim(next_auto_anim(animId));
    else if (elapsed >= anims[animId].dwellMs)
      select_anim(animId);  // replay one shot animations when held manually
  } else if (autoMode && !animationInProgress && elapsed >= anims[animId].dwellMs) {
    select_anim(next_auto_anim(animId));
  }

  anims[animId].tick();

  // guess how far the motor turned since the last pulse, wrapping if loop()
  // took longer than one rotation
  uint32_t loopElapsed = (micros() - localLastPulse) % localMtrTimePeriod;
  uint32_t currentSlice = (loopElapsed * dynamicSlice) / localMtrTimePeriod;
  if (currentSlice != previousSlice) {
    tlc_write_12_bits(frame[currentSlice % dynamicSlice]);
    previousSlice = currentSlice;
  }
}
