/*
 * EYEMECH ε3.2 — Arduino / ESP32-S3 port
 * Converted from MicroPython original by Will Cogley
 *
 * Hardware:
 *   ESP32-S3 + PCA9685 PWM board via I2C (SDA=4, SCL=5)
 *   6 servos: LR, UD, TL, BL, TR, BR
 *   All control (mode selection + movement) is via the web dashboard.
 */

#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <WebServer.h>
#include "PCA9685.h"
#include "InputProvider.h"
#include "WebInterface.h"
#include "EyeMath.h"   // pure, host-testable motion math: EaseType, clamp(), applyEase()
#include <ESPmDNS.h>   // F9: eyemech.local + /health (built into esp32:esp32 core)
#include <WebSocketsServer.h>   // F8: telemetry push on port 81 (arduinoWebSockets by Links2004)
#include <Preferences.h>   // Phase C: NVS persistence for servo calibration (built-in)

// ── Bench-test flag ─────────────────────────────────────────────────────────
//   DEBUG_PWM : log every servo command (channel, angle, pulse, I2C ACK)
//               to the Serial Monitor at 115200 baud.
#define DEBUG_PWM      0

// ── Pin definitions ────────────────────────────────────────────────────────
//   Only the I2C bus to the PCA9685 remains — there is no physical input.
#define PIN_I2C_SDA     4   // PCA9685 SDA
#define PIN_I2C_SCL     5   // PCA9685 SCL

// ── Servo channel mapping on PCA9685 ───────────────────────────────────────
#define CH_LR   0   // Left / Right eye pan
#define CH_UD   1   // Up / Down eye tilt
#define CH_TL   2   // Top-Left  eyelid
#define CH_BL   3   // Bottom-Left  eyelid
#define CH_TR   4   // Top-Right eyelid
#define CH_BR   5   // Bottom-Right eyelid

// ── Servo limits  [0]=CLOSED / MIN,  [1]=OPEN / MAX ───────────────────────
//   Some eyelids are mechanically inverted so their numbers go high→low.
struct ServoLimit { int mn; int mx; };

ServoLimit limits[6];   // soft endpoints (.mn/.mx) — populated by applyCal()

// Convenience index aliases matching the channel numbers above
#define LIM_LR  limits[CH_LR]
#define LIM_UD  limits[CH_UD]
#define LIM_TL  limits[CH_TL]
#define LIM_BL  limits[CH_BL]
#define LIM_TR  limits[CH_TR]
#define LIM_BR  limits[CH_BR]

// ── Hard mechanical safety limits (absolute physical travel per channel) ───
//   Applied to EVERY servo write via safeSetAngle(). Independent of limits[]
//   (which is soft open/close calibration): nothing — manual, auto, or
//   animation — may drive a servo past these mechanical hard stops.
//                          LR    UD   TL   BL   TR   BR
int SAFE_MIN[6];   // runtime hard stops — populated by applyCal()
int SAFE_MAX[6];

// ── Per-servo calibration record (single source of truth) ──────────────────
//   endA/endB -> limits[].mn/.mx (gaze: travel ends; lids: closed/open).
//   center used by gaze axes only. safeMin/Max -> the hard clamps.
struct ServoCal { int safeMin, safeMax, endA, endB, center; };

// Compiled defaults == previous hardcoded behaviour, so first boot is identical.
//                      safeMin safeMax endA endB center
ServoCal cal[6] = {
    {  40, 140,  40, 140, 90 },   // LR  pan
    {  40, 140,  40, 140, 90 },   // UD  tilt
    {  10,  90,  90,  10,  0 },   // TL  lid (inverted: closed=90, open=10)
    {  10,  90,  10,  90,  0 },   // BL  lid
    {  10,  90,  10,  90,  0 },   // TR  lid
    {  10,  90,  90,  10,  0 },   // BR  lid (inverted)
};

// ── Calibration jog state ───────────────────────────────────────────────────
int  calSel  = 0;         // selected channel for jog/capture (CH_LR)
// applyCal(), calToJson(), nvsSaveCal(), nvsLoadCal() are defined further down in
// the function region (after EyeMode / the prefs global) so Arduino's auto-prototype
// insertion point stays below the type declarations enterMode() depends on.

// ── Runtime state ──────────────────────────────────────────────────────────
PCA9685 pca;
Preferences prefs;   // Phase C: NVS access (namespace "em")
WebServer server(80);
WebSocketsServer webSocket(81);   // F8: push /state JSON to all connected clients

WebInputProvider webProvider;   // sole input source (dashboard virtual joystick)

// Operating mode — set exclusively from the web dashboard (/mode route).
enum EyeMode { MODE_SLEEP, MODE_ALIVE, MODE_MANUAL, MODE_CALIBRATION, MODE_PERFORM };
EyeMode currentMode = MODE_SLEEP;   // boot default — device starts asleep

bool controllerInitialized = false;

// ── Per-channel current-angle tracking ────────────────────────────────────
//   Maintained by safeSetAngle(). Seeded to a sentinel (-1) so the very first
//   write to each channel always reaches the PCA9685 (boot calibrate() must
//   establish a real pulse on all 6); after that it holds the real angle and
//   safeSetAngle() skips redundant I2C writes (see M1).
float curAngle[6] = { -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f };

// ── Tween engine ──────────────────────────────────────────────────────────
//   EaseType is defined in EyeMath.h (shared with the host unit tests).
struct TweenState {
    float    from;
    float    to;
    uint32_t startMs;
    uint16_t durMs;
    EaseType ease;
    bool     active;
};

TweenState tweens[6] = {};

// ── Unified non-blocking blink controller (Phase 2b) ──────────────────────
enum BlinkPhase { BP_IDLE, BP_CLOSING, BP_HOLD, BP_OPENING, BP_GAP };
BlinkPhase blinkPhase     = BP_IDLE;
uint32_t   blinkPhaseEnd  = 0;
uint8_t    blinkRemaining = 0;   // extra blinks queued by style (double/flutter)
bool       blinkQueued    = false;  // one-more request (manual /blink while busy)
uint16_t   bCloseMs=100, bHoldMs=40, bOpenMs=120, bGapMs=0;

// Blink-style waveform table (replaces the old switch). Index = style:
// 0 normal, 1 quick, 2 double, 3 slow/heavy, 4 flutter. `repeat` = extra blinks.
struct BlinkStyle { uint16_t closeMs, holdMs, openMs, gapMs; uint8_t repeat; };
const BlinkStyle BLINK_STYLES[5] = {
    { 100,  40, 120,   0, 0 },  // 0 normal
    {  70,  20,  80,   0, 0 },  // 1 quick
    {  90,  30, 100, 120, 1 },  // 2 double
    { 180, 120, 220,   0, 0 },  // 3 slow / heavy
    {  60,  10,  70,  60, 2 },  // 4 flutter
};

// ── Mood system ────────────────────────────────────────────────────────────
enum Mood { MOOD_CALM, MOOD_ALERT, MOOD_CURIOUS, MOOD_SLEEPY, MOOD_SKITTISH };
struct MoodParams {
    uint16_t dwellMin, dwellMax; // ms held between saccades
    float    ampFrac;            // fraction of half-travel used for saccades (0..1)
    float    centerBias;         // prob. of pulling a saccade toward center (0..1)
    float    blinksPerMin;       // (used by Phase 2b)
    uint8_t  blinkStyle;         // (used by Phase 2b) 0 normal,1 quick,2 double,3 slow,4 flutter
    float    aperture;           // lid openness scale 0..1
    float    jitterDeg;          // microsaccade amplitude (deg)
};
// Order: dwellMin,dwellMax, ampFrac, centerBias, blinksPerMin, blinkStyle, aperture, jitterDeg
const MoodParams MOODS[5] = {
    { 1500, 3000, 0.50f, 0.40f, 12.0f, 0, 0.85f, 1.0f }, // CALM
    {  300,  800, 1.00f, 0.15f, 18.0f, 1, 1.00f, 1.5f }, // ALERT
    {  600, 1500, 0.85f, 0.20f, 15.0f, 2, 0.95f, 1.2f }, // CURIOUS
    { 2500, 5000, 0.35f, 0.60f,  6.0f, 3, 0.50f, 0.4f }, // SLEEPY
    {  200,  600, 1.00f, 0.15f, 30.0f, 4, 1.00f, 2.0f }, // SKITTISH
};
Mood  currentMood      = MOOD_CALM;   // default
Mood  lastAppliedMood  = MOOD_CALM;   // C3: detect mood change in ALIVE to reschedule timers

#define ALIVE_MOOD_DRIFT 1   // autonomous mood random-walk in ALIVE (set 0 to pin mood to the last selection)
#define ALIVE_AMBIENT_CHOREO 1  // occasionally trigger a canned sequence in ALIVE (0 to disable)
uint32_t nextAmbientChoreoMs = 0;
uint32_t loopHz = 0;   // loop() call rate per second — read by /health (F9)

// F8: track last-pushed state so we only broadcast on change
int   wsPushedAngles[6] = { -2, -2, -2, -2, -2, -2 };
int   wsPushedAp        = -1;
int   wsPushedMode      = -1;
int   wsPushedMood      = -1;
int   wsPushedPerf      = -2;   // last-pushed canned-perf index (-1 = none); -2 forces first push
// Row = current mood, columns = relative weight of drifting TO each mood (incl. staying).
// Calm<->Curious common; Sleepy is sticky; Skittish is rare to enter and decays out fast.
//                              toCALM toALERT toCURI toSLEEP toSKIT
const uint8_t MOOD_DRIFT_W[5][5] = {
    {  50,   8,   30,   10,    2 },  // from CALM
    {  25,  35,   25,    3,   12 },  // from ALERT
    {  35,  12,   40,    8,    5 },  // from CURIOUS
    {  20,   3,   10,   65,    2 },  // from SLEEPY
    {  15,  30,   15,    2,   38 },  // from SKITTISH
};
uint32_t nextMoodDriftMs = 0;

float gAperture   = 1.0f;        // lid openness scale applied in updateLidsForUD

// ── Idle auto-sleep (F3) ──────────────────────────────────────────────────
#define IDLE_SLEEP_MS 300000UL   // 5 min of no web activity → enter SLEEP
uint32_t lastActivityMs = 0;     // seeded in setup(); touched by every user command

// ── Expression macros (F4) ────────────────────────────────────────────────
bool     expressionActive = false;
uint32_t expressionEnd    = 0;

// ── Choreography player state (Phase B) ───────────────────────────────────
struct Keyframe {
    int16_t  lr, ud;     // target angles 40..140; -1 = HOLD current value
    uint8_t  aperture;   // lid openness 0..100  -> gAperture = aperture/100
    uint8_t  blink;      // 0 = none; 1..5 = trigger blink style (blink-1) at keyframe start
    uint16_t easeMs;     // tween duration to reach lr/ud
    uint16_t holdMs;     // dwell after arrival before advancing
    uint8_t  ease;       // EaseType: 0 EASE_OUT, 1 EASE_IN_OUT, 2 EASE_LINEAR
};
#define MAX_KEYFRAMES 24
// M5: reject oversized /seq bodies. 24 keyframes * ~32 chars ≈ 768 B, so 2 KB is
// generous. NB this bounds the parse + caps the worst case; the WebServer has
// already buffered the body by the time the handler runs.
#define SEQ_MAX_BODY  2048
Keyframe perfSeq[MAX_KEYFRAMES];
uint8_t  perfLen      = 0;        // number of loaded keyframes
uint8_t  perfIdx      = 0;        // current keyframe index
uint32_t perfPhaseEnd = 0;        // millis() when current keyframe (ease+hold) completes
bool     perfLoop     = false;    // loop at end vs stop
int8_t   playingCanned = -1;      // canned index currently loaded (-1 = custom/none); shown on twin during PERFORM

// ── Canned sequences (flash-resident .rodata — plain const, no PROGMEM needed on ESP32) ──
//   Field order: { lr, ud, aperture, blink, easeMs, holdMs, ease }
//   lr/ud: 40..140 or -1=HOLD.  aperture: 0..100.  blink: 0=none 1..5=style.
//   ease: 0=EASE_OUT  1=EASE_IN_OUT  2=EASE_LINEAR

// 0 — Idle Curiosity: gentle wandering, mostly centred, soft blinks
const Keyframe CANNED_IDLE[] = {
    {  90,  90, 100, 0,  600, 1200, 1 },   // centre, settle
    { 100,  85, 100, 1,  500,  800, 0 },   // drift right-up, soft blink
    {  80,  90, 100, 0,  700,  600, 1 },   // drift left-centre
    {  90,  95, 100, 0,  800, 1000, 1 },   // drift down slightly
    { 105,  88, 100, 1,  600,  700, 0 },   // right-up, soft blink
    {  90,  90, 100, 0,  900, 1400, 1 },   // return centre, linger
    {  78,  92, 100, 0,  500,  500, 0 },   // quick left
    {  90,  90, 100, 1,  700, 1500, 1 },   // back centre, soft blink, long dwell
};

// 1 — Sleepy Settle: slow drift downward, aperture 100->40->15, slow blinks
const Keyframe CANNED_SLEEPY[] = {
    {  90,  90, 100, 0, 1200,  800, 1 },   // centre, full open
    {  90,  95,  85, 4, 1500, 1000, 1 },   // drift down, slow blink, lid begins closing
    {  88, 100,  60, 0, 2000, 1200, 1 },   // further down, aperture 60
    {  90, 105,  40, 4, 2500, 1500, 1 },   // low gaze, aperture 40, slow blink
    {  90, 110,  20, 0, 2000, 2000, 1 },   // very low, almost closed
    {  90, 112,  15, 4, 1500, 3000, 1 },   // floor, half-lidded, long dwell
};

// 2 — Startled: snap wide, dart, recenter, quick blink
const Keyframe CANNED_STARTLED[] = {
    {  90,  90, 100, 2,   80,  200, 0 },   // snap open, quick-double blink
    { 120,  75, 100, 0,  120,  300, 0 },   // dart right-up fast
    {  60,  80, 100, 0,  150,  250, 0 },   // dart left
    {  90,  85, 100, 0,  180,  400, 0 },   // recenter slight-up
    {  90,  90, 100, 2,  300,  600, 1 },   // settle, quick blink, breathe out
    {  90,  90, 100, 0,  500,  800, 1 },   // hold centre, calm
};

// 3 — Scan Room: deliberate left->right sweep, occasional blink
const Keyframe CANNED_SCAN[] = {
    {  55,  90, 100, 0,  800,  600, 2 },   // sweep to far left
    {  70,  90, 100, 1,  700,  400, 2 },   // step right, blink
    {  90,  90, 100, 0,  700,  500, 2 },   // centre
    { 110,  90, 100, 0,  700,  400, 2 },   // step right
    { 125,  90, 100, 1,  800,  600, 2 },   // far right, blink
    { 110,  90, 100, 0,  600,  300, 2 },   // step back left
    {  90,  90, 100, 0,  700,  500, 2 },   // centre
    {  70,  90, 100, 0,  700,  400, 2 },   // continue left
    {  55,  90, 100, 1,  800,  700, 2 },   // far left again, blink
    {  90,  90, 100, 0,  900,  800, 1 },   // return centre, ease-in-out
};

// 4 — Deep Thought: up-left thinking gaze, concentration narrowing, clarity-blink return
const Keyframe CANNED_THINK[] = {
    {  90,  90, 100, 0,  600,  400, 1 },   // centre, ground state
    {  72,  78,  85, 0,  900, 1500, 1 },   // drift up-left, aperture narrows
    {  65,  75,  80, 0,  400,  900, 1 },   // push further up-left, concentrate
    {  73,  78,  80, 0,  350,  700, 0 },   // small rightward drift within thinking zone
    {  65,  76,  80, 0,  280,  600, 0 },   // back left, ruminating again
    {  90,  90, 100, 1,  900,  700, 1 },   // return centre, normal blink — "aha"
};

// 5 — Double Take: casual look right, recognition snap-back, wide eyes, settle
const Keyframe CANNED_DOUBLE_TAKE[] = {
    { 112,  90, 100, 0,  700,  900, 1 },   // casual look right
    {  90,  90,  95, 1,  180,  150, 0 },   // quick partial look-back, slight blink
    { 116,  82, 100, 3,   90,  200, 0 },   // SNAP right+up, double blink, wide open
    { 108,  87, 100, 0,  400,  700, 1 },   // settle slightly right (follow-through)
    {  90,  90, 100, 1,  600,  600, 1 },   // return centre, normal blink (resolution)
};

// 6 — Nervous Dart: rapid jitter saccades, narrow aperture, flutter blink mid-sequence
const Keyframe CANNED_NERVOUS[] = {
    { 112,  82,  80, 0,  140,  180, 0 },   // dart up-right, aperture narrows
    {  72,  96,  78, 0,  120,  200, 0 },   // snap down-left
    { 116,  78,  78, 0,  130,  160, 0 },   // dart right-up
    {  74,  88,  78, 5,  110,  200, 0 },   // snap left, flutter blink fires here
    { 102,  80,  78, 0,  120,  200, 0 },   // dart right-slight (wind-down)
    {  90,  90, 100, 1,  600,  700, 1 },   // settle centre, open fully, normal blink
};

// 7 — Drowsy Nod: gradual droop toward sleep, involuntary jerk-awake snap, recovery
const Keyframe CANNED_NOD[] = {
    {  90,  90, 100, 0, 1000,  600, 1 },   // centre, start relaxing
    {  90,  98,  75, 4, 1600, 1000, 1 },   // drift down, slow blink (heavy lid)
    {  88, 108,  45, 0, 2200, 1200, 1 },   // further down, half-closed
    {  90, 115,  20, 0, 1800, 1800, 1 },   // near-asleep: low gaze, 20% aperture
    {  90,  78, 100, 3,   90,  400, 0 },   // JERK AWAKE: snap up, wide open, double blink
    {  90,  90, 100, 1,  500,  700, 1 },   // recover to centre, settle
};

// 8 — Slow Blink Greeting: deliberate full close + slow reopen (cat blink / contentment)
// Research: McComb et al. 2020, Scientific Reports — slow blinking as positive social signal
const Keyframe CANNED_SLOW_BLINK[] = {
    { -1, -1, 100, 0,  400,  600, 1 },   // HOLD current gaze
    { -1, -1,  55, 0, 1000,  400, 1 },   // begin slow close
    { -1, -1,  15, 0,  800,  500, 1 },   // near-closed, linger
    { -1, -1,   5, 0,  500,  800, 1 },   // fully closed — hold the trust signal
    { -1, -1, 100, 0, 1400,  700, 1 },   // REOPEN: slow, welcoming
};

// 9 — Side Eye: slow suspicious slide with narrowing, long judgemental hold, dismissal blink
const Keyframe CANNED_SIDE_EYE[] = {
    {  90,  90, 100, 0,  500,  400, 1 },   // centre, neutral
    { 108,  90,  75, 0,  900,  500, 1 },   // begin slide right, aperture narrowing
    { 118,  92,  60, 0, 1100, 2200, 1 },   // FULL SIDE-EYE: hold THE LOOK (2.2s)
    { 116,  90,  58, 0,  300,  600, 0 },   // micro-drift within the look
    {  90,  90, 100, 1,  800,  700, 1 },   // return centre, normal blink (dismissal)
};

// ── ALIVE gaze loop state ──────────────────────────────────────────────────
float    fixBaseLR = 90.0f, fixBaseUD = 90.0f; // current fixation target (jitter centers on this)
uint32_t nextSaccadeMs = 0;
uint32_t nextJitterMs  = 0;
bool     aliveInit     = false;
uint32_t nextBlinkMs   = 0;      // spontaneous blink scheduler

// ── ALIVE "feel" tuning constants (hoisted from the loop; tune the personality here) ──
constexpr float    APERTURE_SMOOTH    = 0.06f;  // mood-aperture cross-fade rate (~580 ms @ 12 ms)
constexpr float    SACCADE_BLINK_PROB = 0.30f;  // chance a saccade is accompanied by a blink
constexpr float    CENTER_BIAS_MULT   = 0.3f;   // shrink factor applied to a centre-biased saccade
constexpr int      SAC_DUR_BASE       = 50;     // saccade duration: base ms ...
constexpr float    SAC_DUR_PER_DEG    = 2.2f;   //   ... plus this per degree of amplitude
constexpr int      SAC_DUR_MIN        = 60;     //   ... clamped to [MIN, MAX] ms
constexpr int      SAC_DUR_MAX        = 220;
constexpr int      BIG_SACCADE_DEG    = 30;     // moves larger than this get the overshoot ease
constexpr uint16_t JITTER_DUR_MS      = 70;     // microsaccade jitter tween duration

// ═══════════════════════════════════════════════════════════════════════════
//  Random helpers
//  (clamp() lives in EyeMath.h alongside the easing math, shared with tests)
// ═══════════════════════════════════════════════════════════════════════════
float randf()                     { return (float)random(0, 10001) / 10000.0f; }
float randRange(float a, float b) { return a + (b - a) * randf(); }

// ═══════════════════════════════════════════════════════════════════════════
//  loadCanned() — copy a built-in canned sequence into perfSeq. (F10/F7)
// ═══════════════════════════════════════════════════════════════════════════
void loadCanned(int i) {
    const Keyframe* src = nullptr; uint8_t n = 0;
    switch (i) {
        case 0: src = CANNED_IDLE;     n = sizeof(CANNED_IDLE)/sizeof(Keyframe);     break;
        case 1: src = CANNED_SLEEPY;   n = sizeof(CANNED_SLEEPY)/sizeof(Keyframe);   break;
        case 2: src = CANNED_STARTLED; n = sizeof(CANNED_STARTLED)/sizeof(Keyframe); break;
        case 3: src = CANNED_SCAN;     n = sizeof(CANNED_SCAN)/sizeof(Keyframe);     break;
        case 4: src = CANNED_THINK;       n = sizeof(CANNED_THINK)/sizeof(Keyframe);       break;
        case 5: src = CANNED_DOUBLE_TAKE; n = sizeof(CANNED_DOUBLE_TAKE)/sizeof(Keyframe); break;
        case 6: src = CANNED_NERVOUS;     n = sizeof(CANNED_NERVOUS)/sizeof(Keyframe);     break;
        case 7: src = CANNED_NOD;         n = sizeof(CANNED_NOD)/sizeof(Keyframe);         break;
        case 8: src = CANNED_SLOW_BLINK;  n = sizeof(CANNED_SLOW_BLINK)/sizeof(Keyframe);  break;
        case 9: src = CANNED_SIDE_EYE;    n = sizeof(CANNED_SIDE_EYE)/sizeof(Keyframe);    break;
        default: return;
    }
    if (n > MAX_KEYFRAMES) n = MAX_KEYFRAMES;
    for (uint8_t k = 0; k < n; k++) perfSeq[k] = src[k];
    perfLen = n;
    playingCanned = (int8_t)i;   // remember which canned this is (for twin status text)
}

// ═══════════════════════════════════════════════════════════════════════════
//  safeSetAngle() — single choke-point for ALL servo writes.
//  Hard-clamps to the channel's physical travel (SAFE_MIN/MAX) so no path —
//  manual, auto, or animation — can drive a servo into a mechanical hard stop.
// ═══════════════════════════════════════════════════════════════════════════
void safeSetAngle(uint8_t channel, int angle) {
    if (channel < 6) {
        angle = clamp(angle, SAFE_MIN[channel], SAFE_MAX[channel]);
        // M1: skip the I2C transaction when the channel is already at this angle.
        // (curAngle starts at the -1 sentinel so the first write per channel
        // always goes through.) Kills the ~330 redundant lid writes/sec at idle.
        if ((int)curAngle[channel] == angle) return;
    }
    pca.setServoAngle(channel, angle);
    if (channel < 6) curAngle[channel] = (float)angle;
}

// ═══════════════════════════════════════════════════════════════════════════
//  calWrite() — calibration jog write. edge=true allows going past the stored
//  SAFE clamp (to find the true mechanical stop), still capped to absolute 0..180.
// ═══════════════════════════════════════════════════════════════════════════
void calWrite(uint8_t ch, int angle, bool edge) {
    if (ch >= 6) return;
    int lo = edge ? 0   : SAFE_MIN[ch];
    int hi = edge ? 180 : SAFE_MAX[ch];
    angle = clamp(angle, lo, hi);
    pca.setServoAngle(ch, angle);
    curAngle[ch] = (float)angle;
}

// ── Calibration apply + NVS persistence ─────────────────────────────────────
// Push cal[] into the live engine state. Call after any cal[] change.
void applyCal() {
    for (int i = 0; i < 6; i++) {
        SAFE_MIN[i]  = min(cal[i].safeMin, cal[i].safeMax);
        SAFE_MAX[i]  = max(cal[i].safeMin, cal[i].safeMax);
        limits[i].mn = cal[i].endA;
        limits[i].mx = cal[i].endB;
    }
}

// Mirror one lid's calibration onto its paired lid. The two servos are mounted as
// mirror images, so within the same travel range they move OPPOSITE ways — reflect
// each captured angle about the range midpoint (safeMin+safeMax) rather than copying.
// (Raw copy would flip the closed/open convention and push the servo off-range.)
void mirrorLid(int src, int dst) {
    int s = cal[src].safeMin + cal[src].safeMax;   // reflection axis = 2 * midpoint
    cal[dst].safeMin = cal[src].safeMin;
    cal[dst].safeMax = cal[src].safeMax;
    cal[dst].endA    = s - cal[src].endA;
    cal[dst].endB    = s - cal[src].endB;
    cal[dst].center  = cal[src].center;            // unused for lids
}

// Working cal[] as JSON: [[safeMin,safeMax,endA,endB,center], ...x6]
String calToJson() {
    String j = "[";
    for (int i = 0; i < 6; i++) {
        if (i) j += ",";
        j += "["; j += cal[i].safeMin; j += ","; j += cal[i].safeMax; j += ",";
        j += cal[i].endA; j += ","; j += cal[i].endB; j += ","; j += cal[i].center; j += "]";
    }
    return j + "]";
}

void nvsSaveCal() {
    prefs.begin("em", false);
    prefs.putString("cal", calToJson());
    prefs.end();
}

// Returns true if a stored calibration was found and parsed into cal[].
bool nvsLoadCal() {
    prefs.begin("em", true);
    bool has = prefs.isKey("cal");
    String j = has ? prefs.getString("cal", "") : "";
    prefs.end();
    if (j.length() == 0) return false;
    int ch = 0, pos = 1;   // skip the outer '[' so row 0's safeMin isn't parsed from "[40"
    while (ch < 6) {
        int ob = j.indexOf('[', pos); if (ob < 0) break;
        int v[5], vi = 0, p = ob + 1;
        while (vi < 5) {
            int comma = j.indexOf(',', p);
            int close = j.indexOf(']', p);
            if (close < 0) break;
            int end = (comma >= 0 && comma < close) ? comma : close;
            v[vi++] = j.substring(p, end).toInt();
            p = end + 1;
            if (end == close) break;
        }
        if (vi == 5) { cal[ch].safeMin=v[0]; cal[ch].safeMax=v[1]; cal[ch].endA=v[2]; cal[ch].endB=v[3]; cal[ch].center=v[4]; }
        pos = j.indexOf(']', ob) + 1;
        ch++;
    }
    return true;
}

// ═══════════════════════════════════════════════════════════════════════════
//  moveChannel() — start a duration-based eased tween on one channel.
//  If durMs==0 the angle is applied immediately and the tween stays inactive.
// ═══════════════════════════════════════════════════════════════════════════
void moveChannel(uint8_t ch, int target, uint16_t durMs, EaseType ease) {
    if (ch >= 6) return;
    if (durMs == 0) {
        safeSetAngle(ch, target);
        tweens[ch].active = false;
        return;
    }
    tweens[ch].from    = curAngle[ch];
    tweens[ch].to      = (float)target;
    tweens[ch].startMs = millis();
    tweens[ch].durMs   = durMs;
    tweens[ch].ease    = ease;
    tweens[ch].active  = true;
}

// ═══════════════════════════════════════════════════════════════════════════
//  tweenTick() — advance all active tweens; call once per loop().
// ═══════════════════════════════════════════════════════════════════════════
void tweenTick() {
    uint32_t now = millis();
    for (uint8_t ch = 0; ch < 6; ch++) {
        if (!tweens[ch].active) continue;

        float t = (float)(now - tweens[ch].startMs) / (float)tweens[ch].durMs;
        if (t >= 1.0f) {
            safeSetAngle(ch, (int)round(tweens[ch].to));
            tweens[ch].active = false;
            continue;
        }

        float et    = applyEase(tweens[ch].ease, t);   // shared with host tests (EyeMath.h)
        float value = tweens[ch].from + (tweens[ch].to - tweens[ch].from) * et;
        safeSetAngle(ch, (int)round(value));
    }
}

// The four eyelid channels, in the order lidOpenTargets() fills out[]: TL,BL,TR,BR.
const uint8_t LID_CH[4] = { CH_TL, CH_BL, CH_TR, CH_BR };

// ═══════════════════════════════════════════════════════════════════════════
//  lidOpenTargets() — compute the 4 lid OPEN positions for a given UD gaze,
//  scaled by gAperture. Shared by updateLidsForUD() and blinkOpen() so the
//  two never diverge.
//  out[]: { TL, BL, TR, BR }
// ═══════════════════════════════════════════════════════════════════════════
void lidOpenTargets(int udAngle, int out[4]) {
    int udMin = LIM_UD.mn, udMax = LIM_UD.mx;
    float p = (udMax != udMin) ? (float)(udAngle - udMin)/(float)(udMax - udMin) : 0.5f;
    p = constrain(p, 0.0f, 1.0f);
    float topClose = 0.6f*(1.0f-p), botClose = 0.6f*p;
    int tl = (int)(LIM_TL.mn + (LIM_TL.mx-LIM_TL.mn)*(1.0f-topClose));
    int bl = (int)(LIM_BL.mn + (LIM_BL.mx-LIM_BL.mn)*(1.0f-botClose));
    int tr = (int)(LIM_TR.mn + (LIM_TR.mx-LIM_TR.mn)*(1.0f-topClose));
    int br = (int)(LIM_BR.mn + (LIM_BR.mx-LIM_BR.mn)*(1.0f-botClose));
    out[0] = LIM_TL.mn + (int)round((tl-LIM_TL.mn)*gAperture);
    out[1] = LIM_BL.mn + (int)round((bl-LIM_BL.mn)*gAperture);
    out[2] = LIM_TR.mn + (int)round((tr-LIM_TR.mn)*gAperture);
    out[3] = LIM_BR.mn + (int)round((br-LIM_BR.mn)*gAperture);
}

// ═══════════════════════════════════════════════════════════════════════════
//  updateLidsForUD() — drive the 4 eyelids from the current UD gaze angle
//  WITHOUT writing CH_UD itself (which is owned by the tween engine in manual
//  mode). gAperture scales the open portion of each lid (1.0 = fully open per
//  calibration, lower = more closed / mood-controlled).
// ═══════════════════════════════════════════════════════════════════════════
void updateLidsForUD(int udAngle) {
    int t[4];
    lidOpenTargets(udAngle, t);
    for (uint8_t j = 0; j < 4; j++) safeSetAngle(LID_CH[j], t[j]);
}

// ═══════════════════════════════════════════════════════════════════════════
//  Blink controller helpers
// ═══════════════════════════════════════════════════════════════════════════
bool blinkBusy() { return blinkPhase != BP_IDLE; }

void blinkSetStyle(uint8_t style) {
    if (style > 4) style = 0;                 // unknown style -> normal
    const BlinkStyle& s = BLINK_STYLES[style];
    bCloseMs = s.closeMs; bHoldMs = s.holdMs; bOpenMs = s.openMs; bGapMs = s.gapMs;
    blinkRemaining = s.repeat;
}

void blinkClose() {
    for (uint8_t j = 0; j < 4; j++) moveChannel(LID_CH[j], limits[LID_CH[j]].mn, bCloseMs, EASE_OUT);
}

void blinkOpen() {
    int t[4]; lidOpenTargets((int)round(curAngle[CH_UD]), t);
    for (uint8_t j = 0; j < 4; j++) moveChannel(LID_CH[j], t[j], bOpenMs, EASE_OUT);
}

void startBlink(uint8_t style) {
    blinkSetStyle(style);
    blinkClose();
    blinkPhase    = BP_CLOSING;
    blinkPhaseEnd = millis() + bCloseMs;
}

void blinkTick() {
    if (blinkPhase == BP_IDLE) return;
    uint32_t now = millis();
    if ((int32_t)(now - blinkPhaseEnd) < 0) return;   // M2: millis()-wrap-safe
    switch (blinkPhase) {
        case BP_CLOSING:
            blinkPhase    = BP_HOLD;
            blinkPhaseEnd = now + bHoldMs;
            break;
        case BP_HOLD:
            blinkOpen();
            blinkPhase    = BP_OPENING;
            blinkPhaseEnd = now + bOpenMs;
            break;
        case BP_OPENING:
            if (blinkRemaining > 0) {
                blinkRemaining--;
                blinkPhase    = BP_GAP;
                blinkPhaseEnd = now + bGapMs;
            } else if (blinkQueued) {
                blinkQueued = false;
                startBlink(0);
            } else {
                blinkPhase = BP_IDLE;
            }
            break;
        case BP_GAP:
            blinkClose();
            blinkPhase    = BP_CLOSING;
            blinkPhaseEnd = now + bCloseMs;
            break;
        default:
            blinkPhase = BP_IDLE;
            break;
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  blinkIntervalMs() — exponentially distributed interval for spontaneous blinks
// ═══════════════════════════════════════════════════════════════════════════
uint32_t blinkIntervalMs(float perMin) {
    if (perMin < 0.1f) perMin = 0.1f;
    float mean = 60000.0f / perMin;
    float iv   = -mean * logf(randf() + 0.0001f);   // exponential distribution
    if (iv < 400.0f) iv = 400.0f;
    return (uint32_t)iv;
}

// ═══════════════════════════════════════════════════════════════════════════
//  calibrate() — center eyes, close eyelids (used during assembly)
// ═══════════════════════════════════════════════════════════════════════════
void calibrate() {
    safeSetAngle(CH_LR, cal[CH_LR].center);
    safeSetAngle(CH_UD, cal[CH_UD].center);
    for (uint8_t j = 0; j < 4; j++) safeSetAngle(LID_CH[j], limits[LID_CH[j]].mn);  // CLOSED
}

// ═══════════════════════════════════════════════════════════════════════════
//  neutral() — center eyes, open eyelids
// ═══════════════════════════════════════════════════════════════════════════
void neutral() {
    safeSetAngle(CH_LR, cal[CH_LR].center);
    safeSetAngle(CH_UD, cal[CH_UD].center);
    for (uint8_t j = 0; j < 4; j++) safeSetAngle(LID_CH[j], limits[LID_CH[j]].mx);  // OPEN
}

// ═══════════════════════════════════════════════════════════════════════════
//  blink() — close all eyelids immediately (used by enterSleep)
// ═══════════════════════════════════════════════════════════════════════════
void blink() {
    for (uint8_t j = 0; j < 4; j++) safeSetAngle(LID_CH[j], limits[LID_CH[j]].mn);  // close all lids
}

// ═══════════════════════════════════════════════════════════════════════════
//  enterSleep() — restful pose: center gaze, close eyelids (sent once)
// ═══════════════════════════════════════════════════════════════════════════
void enterSleep() {
    safeSetAngle(CH_LR, cal[CH_LR].center);
    safeSetAngle(CH_UD, cal[CH_UD].center);
    blink();   // close all four eyelids
    Serial.println("Sleep mode");
}

// ═══════════════════════════════════════════════════════════════════════════
//  initControllerMode() — center + open, called once when entering controller mode
// ═══════════════════════════════════════════════════════════════════════════
void initControllerMode() {
    neutral();
    blinkPhase  = BP_IDLE;
    blinkQueued = false;
    Serial.println("Controller mode initialized");
}

// ═══════════════════════════════════════════════════════════════════════════
//  resetLidLimits() — restore calibrated lid endpoints into limits[].
//  Called on every mode entry so nothing can leave the lid open/close range in a
//  modified state. Endpoints come from cal[] (set via the Calibration view).
// ═══════════════════════════════════════════════════════════════════════════
void resetLidLimits() {
    for (uint8_t j = 0; j < 4; j++) {
        uint8_t ch = LID_CH[j];
        limits[ch].mn = cal[ch].endA;
        limits[ch].mx = cal[ch].endB;
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  scalePotentiometer() — map the dashboard's raw 12-bit ADC value → servo angle
//  with a centre deadzone, into a tighter ±30° window (60..120) around centre.
//  servoIdx: CH_LR or CH_UD.
// ═══════════════════════════════════════════════════════════════════════════
int scalePotentiometer(int potValue, int servoIdx) {
    // Joystick mechanical centres (calibrate these to your hardware)
    const int CENTER_LR = 2960;
    const int CENTER_UD = 2970;
    const int DEADZONE  = 100;
    const int ADC_MIN   = 0;
    const int ADC_MAX   = 4095;

    int centerValue = (servoIdx == CH_LR) ? CENTER_LR : CENTER_UD;

    // Tighter servo range so the dashboard joystick can't slam the gaze to the stops
    const int outMin = 60;
    const int outMax = 120;

    // Dead-zone → return exact centre
    if (abs(potValue - centerValue) < DEADZONE) return 90;

    float scaled;
    if (potValue < centerValue) {
        scaled = outMin + (float)(potValue - ADC_MIN)
                         * (90 - outMin)
                         / (float)(centerValue - DEADZONE - ADC_MIN);
    } else {
        scaled = 90.0f + (float)(potValue - (centerValue + DEADZONE))
                         * (outMax - 90)
                         / (float)(ADC_MAX - (centerValue + DEADZONE));
    }

    return clamp((int)scaled, outMin, outMax);
}

// ═══════════════════════════════════════════════════════════════════════════
//  lookAt() — aim the gaze from normalized coordinates (F2).
//    x: left(-1) .. right(+1)   y: down(-1) .. up(+1)
//  Maps to LR/UD around centre (±LOOK_AMP_DEG), eases there, and — for ALIVE —
//  holds the commanded gaze by re-seeding the fixation base and pushing the next
//  saccade out, so the autonomous loop doesn't immediately steal it. Harmless in
//  other modes (they ignore fixBase*/nextSaccadeMs). safeSetAngle still clamps.
//  This is the seam external control / future face-tracking drives.
// ═══════════════════════════════════════════════════════════════════════════
#define LOOK_AMP_DEG   50    // full usable half-travel per axis (90±50 -> 40..140)
#define LOOK_EASE_MS  160    // glide duration to the commanded gaze
#define LOOK_HOLD_MS 1500    // how long ALIVE holds the gaze before resuming saccades
void lookAt(float x, float y) {
    int lrTarget = gazeAngle(x, cal[CH_LR].endA, cal[CH_LR].center, cal[CH_LR].endB);
    int udTarget = gazeAngle(y, cal[CH_UD].endA, cal[CH_UD].center, cal[CH_UD].endB);
    moveChannel(CH_LR, lrTarget, LOOK_EASE_MS, EASE_OUT);
    moveChannel(CH_UD, udTarget, LOOK_EASE_MS, EASE_OUT);
    fixBaseLR = (float)lrTarget; fixBaseUD = (float)udTarget;  // microsaccades stay near the target
    nextSaccadeMs = millis() + LOOK_HOLD_MS;                   // hold against the saccade loop
}

// ── Touch activity + optional wake from SLEEP (F3) ────────────────────────
void touchActivity() {
    lastActivityMs = millis();
    if (currentMode == MODE_SLEEP) enterMode(MODE_ALIVE);
}

// ── Expression macros (F4) ────────────────────────────────────────────────
bool expressionBusy() {
    if (!expressionActive) return false;
    if ((int32_t)(millis() - expressionEnd) >= 0) { expressionActive = false; return false; }
    return true;
}

void triggerExpression(const char* name) {
    if (blinkBusy()) return;   // don't fight an active blink
    int udAngle = (int)round(curAngle[CH_UD]);
    int open[4]; lidOpenTargets(udAngle, open);
    // open[]: { TL, BL, TR, BR }
    uint16_t holdMs = 350;

    if (strcmp(name, "wink_l") == 0) {
        moveChannel(CH_TL, limits[CH_TL].mn, 100, EASE_OUT);
        moveChannel(CH_BL, limits[CH_BL].mn, 100, EASE_OUT);
    } else if (strcmp(name, "wink_r") == 0) {
        moveChannel(CH_TR, limits[CH_TR].mn, 100, EASE_OUT);
        moveChannel(CH_BR, limits[CH_BR].mn, 100, EASE_OUT);
    } else if (strcmp(name, "squint") == 0) {
        gAperture = 0.22f;
        updateLidsForUD(udAngle);
        holdMs = 500;
    } else if (strcmp(name, "wide") == 0) {
        gAperture = 1.0f;
        updateLidsForUD(udAngle);
        holdMs = 300;
    } else if (strcmp(name, "skeptical") == 0) {
        int trHalf = (limits[CH_TR].mn + open[2]) / 2;
        moveChannel(CH_TL, open[0], 0, EASE_OUT);
        moveChannel(CH_BL, open[1], 0, EASE_OUT);
        moveChannel(CH_TR, trHalf, 120, EASE_OUT);
        moveChannel(CH_BR, open[3], 0, EASE_OUT);
        holdMs = 400;
    } else { return; }

    expressionActive = true;
    expressionEnd = millis() + holdMs;
}

// ═══════════════════════════════════════════════════════════════════════════
//  WebSocket event handler — log connect/disconnect (F8)
// ═══════════════════════════════════════════════════════════════════════════
void wsCb(uint8_t num, WStype_t type, uint8_t* payload, size_t length) {
    if (type == WStype_CONNECTED)         Serial.printf("WS[%u] connected\n", num);
    else if (type == WStype_DISCONNECTED) Serial.printf("WS[%u] disconnected\n", num);
}

// ═══════════════════════════════════════════════════════════════════════════
//  wsBroadcastState() — push state JSON to all WS clients if anything changed.
//  Same shape as /state HTTP response. Called every 12 ms frame; no-op on no change.
// ═══════════════════════════════════════════════════════════════════════════
void wsBroadcastState() {
    int ap   = (int)round(gAperture * 100.0f);
    int mode = (int)currentMode;
    int mood = (int)currentMood;
    int perf = (currentMode == MODE_PERFORM) ? (int)playingCanned : -1;
    bool changed = (mode != wsPushedMode || mood != wsPushedMood || ap != wsPushedAp || perf != wsPushedPerf);
    if (!changed) {
        for (uint8_t i = 0; i < 6; i++) {
            if ((int)curAngle[i] != wsPushedAngles[i]) { changed = true; break; }
        }
    }
    if (!changed) return;
    char buf[128];
    snprintf(buf, sizeof(buf),
        "{\"m\":%d,\"mood\":%d,\"a\":[%d,%d,%d,%d,%d,%d],\"bp\":%d,\"ap\":%d,\"perf\":%d}",
        mode, mood,
        (int)curAngle[0], (int)curAngle[1],
        (int)curAngle[2], (int)curAngle[3],
        (int)curAngle[4], (int)curAngle[5],
        (int)blinkPhase, ap, perf);
    webSocket.broadcastTXT(buf);
    wsPushedMode = mode; wsPushedMood = mood; wsPushedAp = ap; wsPushedPerf = perf;
    for (uint8_t i = 0; i < 6; i++) wsPushedAngles[i] = (int)curAngle[i];
}

// ─── Reactive behavior bus (F10) ───────────────────────────────────────────
enum Behavior { BEH_STARTLE, BEH_GREET, BEH_TRACK, BEH_SETTLE };

void triggerBehavior(Behavior b, float x = 0.0f, float y = 0.0f) {
    switch (b) {
        case BEH_STARTLE:
            gAperture = 1.0f;
            loadCanned(2);   // CANNED_STARTLED
            perfLoop = false;
            enterMode(MODE_PERFORM);
            break;
        case BEH_GREET: {
            static const Keyframe GREET[] = {
                { 90, 78, 100, 1, 200, 300, 0 },   // look up, quick blink
                { 90, 90, 100, 0, 400, 500, 1 },   // return centre
            };
            uint8_t n = sizeof(GREET)/sizeof(Keyframe);
            for (uint8_t k = 0; k < n; k++) perfSeq[k] = GREET[k];
            perfLen = n; perfLoop = false;
            enterMode(MODE_PERFORM);
            break;
        }
        case BEH_TRACK:
            lookAt(x, y);
            break;
        case BEH_SETTLE:
            moveChannel(CH_LR, 90, 400, EASE_IN_OUT);
            moveChannel(CH_UD, 90, 400, EASE_IN_OUT);
            break;
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  perfStartKeyframe() — arm one keyframe of the choreography player.
//  Starts tweens for gaze, sets aperture, optionally fires a blink, and
//  schedules perfPhaseEnd for the combined ease+hold window.
// ═══════════════════════════════════════════════════════════════════════════
void perfStartKeyframe(uint8_t i) {
    const Keyframe& k = perfSeq[i];
    int lrTarget = (k.lr < 0) ? (int)round(curAngle[CH_LR]) : (int)k.lr;
    int udTarget = (k.ud < 0) ? (int)round(curAngle[CH_UD]) : (int)k.ud;
    moveChannel(CH_LR, lrTarget, k.easeMs, (EaseType)k.ease);
    moveChannel(CH_UD, udTarget, k.easeMs, (EaseType)k.ease);
    gAperture = k.aperture / 100.0f;
    if (k.blink > 0 && !blinkBusy()) startBlink(k.blink - 1);
    perfPhaseEnd = millis() + k.easeMs + k.holdMs;
}

// ═══════════════════════════════════════════════════════════════════════════
//  enterMode() — single dispatch for all mode transitions.
//  Resets blink FSM, applies per-mode entry pose/flags, and sets currentMode.
// ═══════════════════════════════════════════════════════════════════════════
void enterMode(EyeMode m) {
    currentMode = m;
    blinkPhase  = BP_IDLE;
    blinkQueued = false;
    // L2: a mode change can abandon an in-flight tween (blink mid-close, or a
    // gaze saccade), leaving it active to fight the new mode's pose writes the
    // next tweenTick(). Cancel ALL six channels — lids AND gaze (LR/UD).
    for (uint8_t ch = 0; ch < 6; ch++) tweens[ch].active = false;
    // M3: clear any MANUAL trim adjustment so it doesn't leak into other modes.
    resetLidLimits();
    switch (m) {
        case MODE_SLEEP:
            enterSleep();   // center gaze + close lids; breathing loop handles the rest
            break;
        case MODE_ALIVE:
            aliveInit = false;   // ALIVE loop lazily inits gaze timers from current millis
            break;
        case MODE_MANUAL:
            controllerInitialized = false;   // MANUAL loop lazily calls initControllerMode()
            break;
        case MODE_CALIBRATION:
            calibrate();                     // pose once on entry; loop re-asserts on ~500 ms throttle
            controllerInitialized = false;
            break;
        case MODE_PERFORM:
            // blink FSM already reset above; start sequence from keyframe 0
            if (perfLen > 0) {
                perfIdx = 0;
                perfStartKeyframe(0);
            }
            // if perfLen==0 the loop block will bounce back to ALIVE immediately
            break;
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  setup()
// ═══════════════════════════════════════════════════════════════════════════
void setup() {
    Serial.begin(115200);

    nvsLoadCal();   // Phase C: restore saved calibration if present (keeps defaults otherwise)
    applyCal();     // populate limits[]/SAFE_* from cal[]

    // I2C to the PCA9685 — SDA=4, SCL=5 (matches the ESP32-S3 wiring)
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
    Wire.setClock(100000);

    // Initialise PCA9685
    pca.begin();
    pca.setPWMFreq(50);  // 50 Hz standard for hobby servos
    pca.setDebug(DEBUG_PWM);
    Serial.println(pca.isConnected()
        ? "PCA9685 detected on I2C (0x40) — commands will drive real servos"
        : "PCA9685 NOT found — running in SIM mode (commands logged only)");

    // No physical inputs to configure — mode and movement come from the web.

    // WiFi AP Setup
    WiFi.softAP("EyeMech-Controller", "eyemech123");
    MDNS.begin("eyemech");   // F9: reachable at eyemech.local on the AP
    Serial.print("AP IP address: ");
    Serial.println(WiFi.softAPIP());

    // WebServer Routes
    server.on("/update", []() {
        int lr = server.arg("lr").toInt();
        int ud = server.arg("ud").toInt();
        webProvider.update(lr, ud);
        lastActivityMs = millis();   // F3: stamp activity (no wake — joystick chatter shouldn't auto-wake)
        server.send(200, "text/plain", "OK");
    });

    // GET /lookat?x=<-1..1>&y=<-1..1> — aim the gaze. x:left..right, y:down..up.
    // Best seen in ALIVE (holds briefly, then resumes). 400 if x or y is missing.
    server.on("/lookat", []() {
        if (!server.hasArg("x") || !server.hasArg("y")) { server.send(400, "text/plain", "need x & y"); return; }
        float x = constrain(server.arg("x").toFloat(), -1.0f, 1.0f);
        float y = constrain(server.arg("y").toFloat(), -1.0f, 1.0f);
        lookAt(x, y);
        touchActivity();   // F3: wake SLEEP → ALIVE; stamp activity
        server.send(200, "text/plain", "OK");
    });

    // Momentary one-shot blink. While one is playing, a press queues one more.
    server.on("/blink", []() {
        touchActivity();   // F3: wake SLEEP → ALIVE; stamp activity
        if (!blinkBusy()) startBlink(0);
        else              blinkQueued = true;
        server.send(200, "text/plain", "OK");
    });

    server.on("/mood", []() {
        touchActivity();   // F3: wake SLEEP → ALIVE; stamp activity
        String m = server.arg("set");
        if      (m == "calm")     currentMood = MOOD_CALM;
        else if (m == "alert")    currentMood = MOOD_ALERT;
        else if (m == "curious")  currentMood = MOOD_CURIOUS;
        else if (m == "sleepy")   currentMood = MOOD_SLEEPY;
        else if (m == "skittish") currentMood = MOOD_SKITTISH;
        // R3: hold the user's explicit choice before drift resumes
        nextMoodDriftMs = millis() + (uint32_t)random(20000, 45001);
        server.send(200, "text/plain", "OK");
    });

    server.on("/mode", []() {
        String m = server.arg("set");
        if      (m == "sleep")       enterMode(MODE_SLEEP);
        else if (m == "alive")       enterMode(MODE_ALIVE);
        else if (m == "manual")      enterMode(MODE_MANUAL);
        else if (m == "calibration") enterMode(MODE_CALIBRATION);
        lastActivityMs = millis();   // F3: stamp activity (no wake — explicit mode set overrides sleep logic)
        server.send(200, "text/plain", "OK");
    });

    // Compact JSON telemetry snapshot — read-only, no state mutation.
    // Shape: {"m":<mode>,"mood":<mood>,"a":[lr,ud,tl,bl,tr,br],"bp":<blinkPhase>,"ap":<aperture%>,"perf":<cannedIdx|-1>}
    //   perf: canned index playing while in PERFORM (-1 otherwise) — twin shows "Choreo #N"
    server.on("/state", []() {
        char buf[128];
        snprintf(buf, sizeof(buf),
            "{\"m\":%d,\"mood\":%d,\"a\":[%d,%d,%d,%d,%d,%d],\"bp\":%d,\"ap\":%d,\"perf\":%d}",
            (int)currentMode,
            (int)currentMood,
            (int)curAngle[0], (int)curAngle[1],
            (int)curAngle[2], (int)curAngle[3],
            (int)curAngle[4], (int)curAngle[5],
            (int)blinkPhase,
            (int)round(gAperture * 100.0f),
            (currentMode == MODE_PERFORM) ? (int)playingCanned : -1);
        server.send(200, "application/json", buf);
    });

    // ── Choreography routes ─────────────────────────────────────────────────
    // POST /seq — load a sequence from compact CSV body.
    // Format: keyframes separated by ';', each 7 comma-separated ints:
    //   lr,ud,ap,blink,easeMs,holdMs,ease
    //   lr/ud: target angle 40..140, or -1 to HOLD current value
    //   ap: lid openness 0..100
    //   blink: 0=none, 1..5=trigger blink style (blink-1) at keyframe start
    //   easeMs/holdMs: milliseconds (capped at 60000)
    //   ease: 0=EASE_OUT, 1=EASE_IN_OUT, 2=EASE_LINEAR
    // Example: "-1,-1,100,0,1,800,0;120,70,100,0,300,500,1"
    server.on("/seq", HTTP_POST, []() {
        touchActivity();   // F3: editing/loading a sequence is engagement — stamp + wake
        String body = server.arg("plain");
        if (body.length() > SEQ_MAX_BODY) { server.send(413, "text/plain", "too large"); return; }  // M5
        uint8_t count = 0;
        int pos = 0;
        int bodyLen = body.length();
        while (pos <= bodyLen && count < MAX_KEYFRAMES) {
            // Find end of this keyframe token (';' or end of string)
            int sep = body.indexOf(';', pos);
            if (sep < 0) sep = bodyLen;
            String token = body.substring(pos, sep);
            pos = sep + 1;
            if (token.length() == 0) continue;
            // Parse 7 fields from token
            int fields[7] = { -1, -1, 100, 0, 300, 500, 0 };
            int fi = 0;
            int tp = 0;
            int tlen = token.length();
            while (tp <= tlen && fi < 7) {
                int cp = token.indexOf(',', tp);
                if (cp < 0) cp = tlen;
                String f = token.substring(tp, cp);
                if (f.length() > 0) fields[fi] = f.toInt();  // M4: empty field keeps its default (e.g. easeMs=300, not 0=snap)
                fi++;
                tp = cp + 1;
            }
            if (fi < 7) continue;   // malformed keyframe — skip
            Keyframe& k = perfSeq[count];
            // lr/ud: -1 = HOLD, otherwise clamp to [40,140]
            k.lr      = (fields[0] < 0) ? -1 : (int16_t)clamp(fields[0], 40, 140);
            k.ud      = (fields[1] < 0) ? -1 : (int16_t)clamp(fields[1], 40, 140);
            k.aperture= (uint8_t)clamp(fields[2], 0, 100);
            k.blink   = (uint8_t)clamp(fields[3], 0, 5);
            k.easeMs  = (uint16_t)clamp(fields[4], 0, 60000);
            k.holdMs  = (uint16_t)clamp(fields[5], 0, 60000);
            k.ease    = (uint8_t)clamp(fields[6], 0, 2);
            count++;
        }
        perfLen = count;
        playingCanned = -1;   // a custom/edited sequence — not one of the canned ones
        char resp[12];
        snprintf(resp, sizeof(resp), "OK %d", count);
        server.send(200, "text/plain", resp);
    });

    // GET /play?loop=1 — start playback (loop=1 to repeat)
    server.on("/play", []() {
        touchActivity();   // F3: wake SLEEP → ALIVE + stamp, so a non-looping show isn't cut by auto-sleep
        if (perfLen == 0) { server.send(400, "text/plain", "no sequence"); return; }  // L1: don't flip PERFORM->ALIVE + abort blink on empty
        perfLoop = server.arg("loop") == "1";
        perfIdx  = 0;
        enterMode(MODE_PERFORM);
        server.send(200, "text/plain", "OK");
    });

    // GET /stop — exit performance, return to ALIVE
    server.on("/stop", []() {
        touchActivity();   // F3: stamp activity on user stop
        enterMode(MODE_ALIVE);
        server.send(200, "text/plain", "OK");
    });

    // GET /canned?i=N — load a built-in sequence into perfSeq (does not auto-play)
    server.on("/canned", []() {
        touchActivity();   // F3: wake + stamp so a canned load while idle/asleep isn't ignored
        if (!server.hasArg("i")) { server.send(400, "text/plain", "missing i"); return; }
        int i = server.arg("i").toInt();
        if (i < 0 || i > 9) { server.send(400, "text/plain", "bad index"); return; }
        loadCanned(i);
        char resp[12]; snprintf(resp, sizeof(resp), "OK %d", (int)perfLen);
        server.send(200, "text/plain", resp);
    });

    // GET /express?name=wink_l|wink_r|squint|wide|skeptical
    server.on("/express", []() {
        String name = server.arg("name");
        if (name.length() == 0) { server.send(400, "text/plain", "need name"); return; }
        triggerExpression(name.c_str());
        touchActivity();
        server.send(200, "text/plain", "OK");
    });

    // GET /behavior?do=startle|greet|settle|track[&x=..&y=..]
    server.on("/behavior", []() {
        String d = server.arg("do");
        if (d.isEmpty()) { server.send(400, "text/plain", "missing do"); return; }
        touchActivity();   // F3: wake SLEEP → ALIVE; stamp activity
        if      (d == "startle") { triggerBehavior(BEH_STARTLE); }
        else if (d == "greet")   { triggerBehavior(BEH_GREET); }
        else if (d == "settle")  { triggerBehavior(BEH_SETTLE); }
        else if (d == "track") {
            if (!server.hasArg("x") || !server.hasArg("y")) {
                server.send(400, "text/plain", "need x & y"); return;
            }
            float x = constrain(server.arg("x").toFloat(), -1.0f, 1.0f);
            float y = constrain(server.arg("y").toFloat(), -1.0f, 1.0f);
            triggerBehavior(BEH_TRACK, x, y);
        }
        else { server.send(400, "text/plain", "unknown behavior"); return; }
        server.send(200, "text/plain", "OK");
    });

    // Serve the main dashboard at the root path.
    // send_P streams the ~67KB PROGMEM page in chunks (no single contiguous String
    // alloc, which fragmentation can fail under WiFi+WS, yielding a blank page).
    server.on("/", []() {
        server.send_P(200, "text/html", WEB_INTERFACE_HTML);
    });

    // GET /health — uptime, free heap, loop fps
    server.on("/health", []() {
        char buf[80];
        snprintf(buf, sizeof(buf),
            "{\"up\":%lu,\"heap\":%lu,\"fps\":%lu}",
            millis() / 1000UL,
            (unsigned long)ESP.getFreeHeap(),
            (unsigned long)loopHz);
        server.send(200, "application/json", buf);
    });

    // ── Calibration routes (Phase C) ────────────────────────────────────────
    // GET /cal -> working cal[] + live angles
    server.on("/cal", HTTP_GET, []() {
        String j = "{\"sel\":" + String(calSel) + ",\"cal\":" + calToJson() + ",\"ang\":[";
        for (int i = 0; i < 6; i++) { if (i) j += ","; j += (int)round(curAngle[i]); }
        server.send(200, "application/json", j + "]}");
    });

    // GET /cal/jog?ch=&d=&edge=0|1 -> nudge a channel, return new angle
    server.on("/cal/jog", []() {
        if (currentMode != MODE_CALIBRATION) { server.send(409, "text/plain", "not in calibration"); return; }
        int ch = server.arg("ch").toInt();
        if (ch < 0 || ch > 5) { server.send(400, "text/plain", "bad ch"); return; }
        int d = server.arg("d").toInt();
        bool edge = server.arg("edge").toInt() != 0;
        calSel = ch;
        calWrite((uint8_t)ch, (int)round(curAngle[ch]) + d, edge);
        server.send(200, "text/plain", String((int)round(curAngle[ch])));
    });

    // GET /cal/go?ch=&a= -> drive a channel to an ABSOLUTE angle (edge), return new angle. Used by the wizard to park non-active servos at a neutral pose.
    server.on("/cal/go", []() {
        if (currentMode != MODE_CALIBRATION) { server.send(409, "text/plain", "not in calibration"); return; }
        int ch = server.arg("ch").toInt();
        if (ch < 0 || ch > 5) { server.send(400, "text/plain", "bad ch"); return; }
        int a = server.arg("a").toInt();
        calSel = ch;
        calWrite((uint8_t)ch, a, true);
        server.send(200, "text/plain", String((int)round(curAngle[ch])));
    });

    // POST /cal/set?ch=&slot=center|enda|endb|safemin|safemax -> capture live angle
    server.on("/cal/set", HTTP_POST, []() {
        if (currentMode != MODE_CALIBRATION) { server.send(409, "text/plain", "not in calibration"); return; }
        int ch = server.arg("ch").toInt();
        if (ch < 0 || ch > 5) { server.send(400, "text/plain", "bad ch"); return; }
        String slot = server.arg("slot");
        int a = (int)round(curAngle[ch]);
        if      (slot == "center")  cal[ch].center  = a;
        else if (slot == "enda")    cal[ch].endA    = a;
        else if (slot == "endb")    cal[ch].endB    = a;
        else if (slot == "safemin") cal[ch].safeMin = a;
        else if (slot == "safemax") cal[ch].safeMax = a;
        else { server.send(400, "text/plain", "bad slot"); return; }
        applyCal();   // SAFE/limits live-update; gaze centre read live
        server.send(200, "text/plain", "OK");
    });

    // POST /cal/mirror -> seed right-eye lids from left, reflected (TR<-TL, BR<-BL)
    server.on("/cal/mirror", HTTP_POST, []() {
        if (currentMode != MODE_CALIBRATION) { server.send(409, "text/plain", "not in calibration"); return; }
        mirrorLid(CH_TL, CH_TR);
        mirrorLid(CH_BL, CH_BR);
        applyCal();
        server.send(200, "text/plain", "OK");
    });

    // POST /cal/autosafe -> set each channel's SAFE bounds to wrap its captured
    // points (+margin), so the functional range can never be clamped off. Called by
    // the wizard before saving.
    server.on("/cal/autosafe", HTTP_POST, []() {
        if (currentMode != MODE_CALIBRATION) { server.send(409, "text/plain", "not in calibration"); return; }
        const int M = 6;   // margin degrees
        for (int i = 0; i < 6; i++) {
            int lo = min(cal[i].endA, cal[i].endB);
            int hi = max(cal[i].endA, cal[i].endB);
            if (i == CH_LR || i == CH_UD) { lo = min(lo, cal[i].center); hi = max(hi, cal[i].center); }
            cal[i].safeMin = max(0,   lo - M);
            cal[i].safeMax = min(180, hi + M);
        }
        applyCal();
        server.send(200, "text/plain", "OK");
    });

    server.on("/cal/save",   HTTP_POST, []() { nvsSaveCal(); server.send(200, "text/plain", "OK"); });
    server.on("/cal/revert", HTTP_POST, []() { nvsLoadCal(); applyCal(); server.send(200, "text/plain", "OK"); });
    server.on("/cal/reset",  HTTP_POST, []() {
        if (currentMode != MODE_CALIBRATION) { server.send(409, "text/plain", "not in calibration"); return; }
        ServoCal d[6] = {
            {40,140,40,140,90},{40,140,40,140,90},
            {10,90,90,10,0},{10,90,10,90,0},{10,90,10,90,0},{10,90,90,10,0}
        };
        for (int i = 0; i < 6; i++) cal[i] = d[i];
        applyCal();
        server.send(200, "text/plain", "OK");
    });

    server.begin();
    webSocket.begin();       // F8: WebSocket server on port 81
    webSocket.onEvent(wsCb);

    Serial.println("Initialising servos...");
    calibrate();        // boot servo-settle; lands in the centered/closed pose
    delay(1000);
    enterMode(MODE_SLEEP);   // establishes boot state (poses once, resets blink FSM)
    lastActivityMs = millis();   // F3: start idle timer from boot
    Serial.println("System ready — asleep, awaiting web command");
}

// ═══════════════════════════════════════════════════════════════════════════
//  loop()
// ═══════════════════════════════════════════════════════════════════════════
// NOTE: compile loop() at -O1. The xtensa-esp32s3 GCC in ESP32 core 3.3.10
// crashes (internal compiler error in the code-hoisting pass) when this
// function is built at the default -Os. -O1 sidesteps the toolchain bug; the
// rest of the sketch still builds at -Os. Remove if a fixed core drops the ICE.
__attribute__((optimize("O1")))
void loop() {
    // F9: loop rate counter
    static uint32_t loopCnt = 0, loopHzTs = 0;
    uint32_t loopNow = millis();
    loopCnt++;
    if (loopNow - loopHzTs >= 1000) { loopHz = loopCnt; loopCnt = 0; loopHzTs = loopNow; }

    // These three run unconditionally every iteration — they are cheap, time-based,
    // and must not be throttled so HTTP responses and tween/blink transitions stay crisp.
    server.handleClient();
    webSocket.loop();   // F8: pump WebSocket connections
    tweenTick();   // advance all active tweens every cycle (no-op when idle)
    blinkTick();   // advance blink state machine every cycle (no-op when idle)

    // Per-mode logic runs on a 12 ms frame cadence so it does not block handleClient().
    const uint32_t FRAME_MS = 12;
    static uint32_t lastFrame = 0;
    uint32_t now = millis();
    if (now - lastFrame < FRAME_MS) return;
    lastFrame = now;

    // F3: auto-sleep after IDLE_SLEEP_MS of inactivity
    if (currentMode != MODE_SLEEP && currentMode != MODE_CALIBRATION &&
        !(currentMode == MODE_PERFORM && perfLoop)) {
        if ((int32_t)(now - lastActivityMs) >= (int32_t)IDLE_SLEEP_MS) {
            enterMode(MODE_SLEEP);
            return;
        }
    }

    // F8: push state to WebSocket clients if anything changed this frame
    wsBroadcastState();

    // ── SLEEP mode (boot default) — posed on entry by enterMode(); gentle lid breathing ──
    if (currentMode == MODE_SLEEP) {
        if (!blinkBusy()) {
            static uint32_t lastBreath = 0;
            if (now - lastBreath >= 40) {
                lastBreath = now;
                float ph = (float)(now % 4000) / 4000.0f * 6.2832f;   // 4 s period
                gAperture = (sinf(ph) * 0.5f + 0.5f) * 0.12f;          // 0 .. ~12% open
                updateLidsForUD(90);                                    // centered gaze, breathing lids
            }
        }
        return;
    }

    // ── CALIBRATION mode — HOLD: servos stay wherever the jog routes placed them ──
    //   calibrate() poses centre+closed once on entry (enterMode); after that we never
    //   re-assert, so live jog sticks. tweenTick lets any eased motion settle.
    if (currentMode == MODE_CALIBRATION) {
        tweenTick();
        return;
    }

    // ── ALIVE mode — mood-driven saccade gaze loop ─────────────────────────
    if (currentMode == MODE_ALIVE) {
        const MoodParams& md = MOODS[currentMood];

        // C3: detect mood change — engage new tempo immediately so the switch is felt at once
        if (currentMood != lastAppliedMood) {
            lastAppliedMood = currentMood;
            nextSaccadeMs = now + (uint32_t)random(150, 450);
            nextJitterMs  = now + (uint32_t)random(150, 401);
            nextBlinkMs   = now + blinkIntervalMs(md.blinksPerMin);
        }

        // C2: exponential smoothing — lids glide on mood change instead of popping (~580 ms @ 12 ms frame)
        gAperture += (md.aperture - gAperture) * APERTURE_SMOOTH;

        if (!aliveInit) {
            moveChannel(CH_LR, 90, 200, EASE_IN_OUT);
            moveChannel(CH_UD, 90, 200, EASE_IN_OUT);
            fixBaseLR = cal[CH_LR].center; fixBaseUD = cal[CH_UD].center;
            nextSaccadeMs   = now + (uint32_t)random(md.dwellMin, md.dwellMax + 1);
            nextJitterMs    = now + (uint32_t)random(150, 401);
            nextBlinkMs     = now + blinkIntervalMs(md.blinksPerMin);
            nextMoodDriftMs = now + (uint32_t)random(15000, 40001);
            nextAmbientChoreoMs = now + (uint32_t)random(60000, 120001);   // 1–2 min first fire
            aliveInit = true;
        }
        float half = (LIM_LR.mx - LIM_LR.mn) / 2.0f;   // half of LR travel
        if ((int32_t)(now - nextSaccadeMs) >= 0) {   // M2: wrap-safe
            // new saccade target per axis, center-biased
            float rLR = randRange(-1.0f, 1.0f) * md.ampFrac * half;
            if (randf() < md.centerBias) rLR *= CENTER_BIAS_MULT;
            float rUD = randRange(-1.0f, 1.0f) * md.ampFrac * half;
            if (randf() < md.centerBias) rUD *= CENTER_BIAS_MULT;
            int tLR = clamp((int)round(90 + rLR), LIM_LR.mn, LIM_LR.mx);
            int tUD = clamp((int)round(90 + rUD), LIM_UD.mn, LIM_UD.mx);
            // C4: amplitude-proportional duration; overshoot-settle ease on large moves
            int dLR = abs(tLR - (int)round(curAngle[CH_LR]));
            int dUD = abs(tUD - (int)round(curAngle[CH_UD]));
            int amp = (dLR > dUD) ? dLR : dUD;
            uint16_t sacDur = (uint16_t)clamp(SAC_DUR_BASE + (int)(amp * SAC_DUR_PER_DEG), SAC_DUR_MIN, SAC_DUR_MAX);
            EaseType sacEase = (amp > BIG_SACCADE_DEG) ? EASE_OUT_BACK : EASE_OUT;
            moveChannel(CH_LR, tLR, sacDur, sacEase);
            moveChannel(CH_UD, tUD, sacDur, sacEase);
            fixBaseLR = tLR; fixBaseUD = tUD;
            nextSaccadeMs = now + sacDur + (uint32_t)random(md.dwellMin, md.dwellMax + 1);
            nextJitterMs  = now + (uint32_t)random(150, 401);
            // C5: saccade-coupled blink — push spontaneous scheduler so blink rate stays within budget
            if (randf() < SACCADE_BLINK_PROB && !blinkBusy()) { startBlink(md.blinkStyle); nextBlinkMs = now + blinkIntervalMs(md.blinksPerMin); }
        } else if ((int32_t)(now - nextJitterMs) >= 0 && !tweens[CH_LR].active && !tweens[CH_UD].active) {   // M2: wrap-safe
            // microsaccade jitter around the fixation base
            moveChannel(CH_LR, clamp((int)round(fixBaseLR + randRange(-1,1)*md.jitterDeg), LIM_LR.mn, LIM_LR.mx), JITTER_DUR_MS, EASE_OUT);
            moveChannel(CH_UD, clamp((int)round(fixBaseUD + randRange(-1,1)*md.jitterDeg), LIM_UD.mn, LIM_UD.mx), JITTER_DUR_MS, EASE_OUT);
            nextJitterMs = now + (uint32_t)random(150, 401);
        }
        // Spontaneous blink on schedule
        if ((int32_t)(now - nextBlinkMs) >= 0) {   // M2: wrap-safe
            if (!blinkBusy()) startBlink(md.blinkStyle);
            nextBlinkMs = now + blinkIntervalMs(md.blinksPerMin);
        }
        // R3: autonomous mood drift — weighted Markov random-walk between moods
#if ALIVE_MOOD_DRIFT
        if ((int32_t)(now - nextMoodDriftMs) >= 0) {   // M2: wrap-safe
            const uint8_t* w = MOOD_DRIFT_W[currentMood];
            int total = w[0]+w[1]+w[2]+w[3]+w[4];
            int r = (int)random(0, total);
            int pick = 0; for (int i = 0; i < 5; i++) { if (r < w[i]) { pick = i; break; } r -= w[i]; }
            currentMood = (Mood)pick;          // C3 detection above handles the timer reschedule next frame
            nextMoodDriftMs = now + (uint32_t)random(15000, 40001);
        }
#endif
        // F7: ambient choreography — occasionally fire a random canned sequence, then return to ALIVE
#if ALIVE_AMBIENT_CHOREO
        if ((int32_t)(now - nextAmbientChoreoMs) >= 0) {
            loadCanned((int)random(0, 10));  // pick from all 10 canned sequences (0-9)
            perfLoop = false;
            enterMode(MODE_PERFORM);         // auto-returns to ALIVE when done
            nextAmbientChoreoMs = now + (uint32_t)random(60000, 120001);  // reset timer
            return;
        }
#endif
        // Lids follow eased gaze — gate during blinks/expressions so they own lid tweens
        if (!blinkBusy() && !expressionBusy()) updateLidsForUD((int)round(curAngle[CH_UD]));
        return;
    }

    // ── PERFORM mode — keyframe choreography player ───────────────────────────
    if (currentMode == MODE_PERFORM) {
        if (perfLen == 0) { enterMode(MODE_ALIVE); return; }   // nothing loaded -> bail
        if ((int32_t)(now - perfPhaseEnd) >= 0) {   // M2: wrap-safe
            perfIdx++;
            if (perfIdx >= perfLen) {
                if (perfLoop) { perfIdx = 0; }
                else { enterMode(MODE_ALIVE); return; }
            }
            perfStartKeyframe(perfIdx);
        }
        // eased gaze + aperture drive the lids; yield to blink controller / expressions
        if (!blinkBusy() && !expressionBusy()) updateLidsForUD((int)round(curAngle[CH_UD]));
        return;
    }

    // ── MANUAL mode — driven entirely by the dashboard virtual joystick ────
    if (!controllerInitialized) {
        initControllerMode();
        controllerInitialized = true;
    }

    int udValue   = webProvider.getUD();
    int lrValue   = webProvider.getLR();
    // Lid open/closed extents come from calibration (cal[] -> limits[]), not a trim slider.

    int targetLR = scalePotentiometer(lrValue, CH_LR);
    int targetUD = scalePotentiometer(udValue, CH_UD);

    // Retarget tween when joystick destination changes by more than 2°.
    // Continuous small updates simply re-aim the in-flight tween (pursuit following).
    float destLR = tweens[CH_LR].active ? tweens[CH_LR].to : curAngle[CH_LR];
    if (abs(targetLR - destLR) > 2.0f) {
        moveChannel(CH_LR, targetLR, 180, EASE_IN_OUT);
    }

    float destUD = tweens[CH_UD].active ? tweens[CH_UD].to : curAngle[CH_UD];
    if (abs(targetUD - destUD) > 2.0f) {
        moveChannel(CH_UD, targetUD, 180, EASE_IN_OUT);
    }

    // Drive eyelids from the current eased UD position — yield to blink controller / expressions
    if (!blinkBusy() && !expressionBusy()) {
        gAperture = 1.0f;   // manual openness controlled by trim slider, not mood
        updateLidsForUD((int)round(curAngle[CH_UD]));
    }
}
