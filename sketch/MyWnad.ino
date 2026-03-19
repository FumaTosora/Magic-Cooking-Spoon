#include <Arduino.h>
#include <PDM.h>

// Choose your IMU lib (uncomment the one for your board):
#include <Arduino_LSM9DS1.h>          // Nano 33 BLE Sense Rev1
// #include <Arduino_BMI270_BMM150.h> // Nano 33 BLE Sense Rev2

#include "audio_wrapper.h"
#include "gesture_wrapper.h"

// ---------------- Config you may adjust ----------------

// Number of axes expected by your gesture model per sample.
// 3 = accelerometer only, 6 = accelerometer + gyroscope
#ifndef GESTURE_AXES
#define GESTURE_AXES 6
#endif

// Confidence thresholds and fusion window
static const float AUDIO_CONF_THRESH = 0.7f;
static const float GEST_CONF_THRESH  = 0.7f;
static const uint32_t FUSION_TIME_WINDOW_MS = 800;

// ---------------- LED effect engine (data-driven) ----------------
#ifndef LEDR
#define LEDR LED_BUILTIN
#define LEDG LED_BUILTIN
#define LEDB LED_BUILTIN
#endif

static inline void ledOffPins() {
  digitalWrite(LEDR, HIGH); digitalWrite(LEDG, HIGH); digitalWrite(LEDB, HIGH);
}
static inline void ledSetRGB(uint8_t r, uint8_t g, uint8_t b) {
  // Active-low RGB on Nano 33 BLE
  digitalWrite(LEDR, r ? HIGH : LOW);
  digitalWrite(LEDG, g ? HIGH : LOW);
  digitalWrite(LEDB, b ? HIGH : LOW);
}

struct LedSegment { uint16_t duration_ms; uint8_t r,g,b; };
struct LedEffect  { const LedSegment* segs; uint8_t count; bool loop; };

struct LedEffectPlayer {
  const LedEffect* eff = nullptr;
  uint8_t idx = 0; uint32_t endAt = 0; bool playing = false;
  void begin() { pinMode(LEDR, OUTPUT); pinMode(LEDG, OUTPUT); pinMode(LEDB, OUTPUT); ledOffPins(); }
  void play(const LedEffect* e) {
    eff = e; idx = 0; playing = (e && e->count > 0);
    if (playing) { apply(e->segs[0]); endAt = millis() + e->segs[0].duration_ms; } else ledOffPins();
  }
  void stop() { playing = false; eff = nullptr; ledOffPins(); }
  void update() {
    if (!playing || !eff) return;
    uint32_t now = millis();
    if (now >= endAt) {
      idx++;
      if (idx >= eff->count) { if (eff->loop) idx = 0; else { stop(); return; } }
      apply(eff->segs[idx]); endAt = now + eff->segs[idx].duration_ms;
    }
  }
  void apply(const LedSegment& s) { ledSetRGB(s.r,s.g,s.b); }
} ledPlayer;

// Generic effects (success, mismatch)
const LedSegment EFFECT_MATCH_GENERAL_SEGS[] = {
  {120, 0,255,0}, {120, 0,0,0}, {120, 0,255,0}, {120, 0,0,0}, {120, 0,255,0}, {150, 0,0,0}
};
const LedEffect EFFECT_MATCH_GENERAL = { EFFECT_MATCH_GENERAL_SEGS, (uint8_t)(sizeof(EFFECT_MATCH_GENERAL_SEGS)/sizeof(LedSegment)), false };

const LedSegment EFFECT_MISMATCH_SEGS[] = {
  {150, 255,0,0}, {150, 0,0,0}, {150, 255,0,0}, {150, 0,0,0}, {150, 255,0,0}, {180, 0,0,0}
};
const LedEffect EFFECT_MISMATCH = { EFFECT_MISMATCH_SEGS, (uint8_t)(sizeof(EFFECT_MISMATCH_SEGS)/sizeof(LedSegment)), false };

// Spell-specific effects (you can add more later)
const LedSegment EFFECT_LUMOS_SEGS[] = {
  {80,  0,255,0}, {70, 0,0,0}, {100, 0,255,0}, {100, 0,0,0}, {200, 0,255,0}, {150, 0,0,0}
};
const LedEffect EFFECT_LUMOS = { EFFECT_LUMOS_SEGS, (uint8_t)(sizeof(EFFECT_LUMOS_SEGS)/sizeof(LedSegment)), false };

const LedSegment EFFECT_NOX_SEGS[] = {
  {100, 255,255,255}, {120, 0,0,0}, {80, 255,255,255}, {300, 0,0,0}
};
const LedEffect EFFECT_NOX = { EFFECT_NOX_SEGS, (uint8_t)(sizeof(EFFECT_NOX_SEGS)/sizeof(LedSegment)), false };

// ---------------- Spell registry ----------------
enum SpellId : uint8_t { SPELL_NONE=0, SPELL_LUMOS, SPELL_NOX /* add new here */ };

struct SpellDescriptor {
  SpellId id;
  const char* audioLabel;    // label in Audio EI model
  const char* gestureLabel;  // label in Gesture EI model
  const LedEffect* successEffect;
};

const SpellDescriptor SPELLS[] = {
  { SPELL_LUMOS, "lumos", "lumos_gesture", &EFFECT_LUMOS },
  { SPELL_NOX,   "nox",   "nox_gesture",   &EFFECT_NOX   }
};
const size_t NUM_SPELLS = sizeof(SPELLS)/sizeof(SpellDescriptor);

SpellId spellFromLabel(const char* label, bool isAudio) {
  if (!label) return SPELL_NONE;
  for (size_t i=0;i<NUM_SPELLS;i++) {
    if ((isAudio && strcmp(label, SPELLS[i].audioLabel)==0) ||
        (!isAudio && strcmp(label, SPELLS[i].gestureLabel)==0)) return SPELLS[i].id;
  }
  return SPELL_NONE;
}
const SpellDescriptor* spellById(SpellId id) {
  for (size_t i=0;i<NUM_SPELLS;i++) if (SPELLS[i].id==id) return &SPELLS[i];
  return nullptr;
}
void onSpellMatch(SpellId id) {
  const SpellDescriptor* sd = spellById(id);
  if (sd && sd->successEffect) ledPlayer.play(sd->successEffect);
  else ledPlayer.play(&EFFECT_MATCH_GENERAL);
}
void onSpellMismatch() { ledPlayer.play(&EFFECT_MISMATCH); }

// ---------------- Audio capture (PDM) ----------------
volatile int16_t* g_audioBuf = nullptr;
volatile size_t   g_audioSamplesNeeded = 0;
volatile size_t   g_audioWriteIdx = 0;
volatile bool     g_audioWindowReady = false;
volatile bool     g_audioHold = false; // stop filling during inference

void onPDMdata() {
  if (!g_audioBuf || g_audioSamplesNeeded == 0) { int b=PDM.available(); while(b>0){int16_t dump[32]; int r=min(b,(int)sizeof(dump)); PDM.read(dump,r); b-=r;} return; }
  int bytes = PDM.available();
  while (bytes >= 2) {
    if (g_audioHold) { int16_t dump; PDM.read(&dump, 2); bytes -= 2; continue; }
    int16_t sample;
    int r = PDM.read(&sample, sizeof(sample));
    if (r != 2) break;
    bytes -= 2;
    g_audioBuf[g_audioWriteIdx++] = sample;
    if (g_audioWriteIdx >= g_audioSamplesNeeded) {
      g_audioWriteIdx = 0;
      g_audioWindowReady = true;
      g_audioHold = true;
      break;
    }
  }
}

// ---------------- IMU frame buffer ----------------
float* g_gestureFrame = nullptr;           // flattened [t0 axes..., t1 axes..., ...]
size_t g_gestureFrameLen = 0;              // total values per window
size_t g_gestureWriteIndex = 0;
uint32_t g_imuPeriodUs = 0;
uint32_t g_nextImuSampleDue = 0;

// ---------------- Fusion state ----------------
struct DetectedEvent {
  SpellId id = SPELL_NONE;
  float conf = 0.0f;
  uint32_t t_ms = 0;
};
DetectedEvent lastAudio, lastGesture;

void checkFusion() {
  if (lastAudio.id == SPELL_NONE || lastGesture.id == SPELL_NONE) return;
  uint32_t dt = (lastAudio.t_ms > lastGesture.t_ms) ? (lastAudio.t_ms - lastGesture.t_ms) : (lastGesture.t_ms - lastAudio.t_ms);
  if (dt <= FUSION_TIME_WINDOW_MS) {
    if (lastAudio.id == lastGesture.id) onSpellMatch(lastAudio.id);
    else onSpellMismatch();
    lastAudio = DetectedEvent();
    lastGesture = DetectedEvent();
  }
}

// ---------------- Setup ----------------
void setup() {
  Serial.begin(115200);
  while (!Serial) {}

  ledPlayer.begin();

  // IMU init
  if (!IMU.begin()) {
    Serial.println("IMU start failed.");
    while (1) delay(500);
  }

  // Query model requirements via wrappers (compile-time constants inside each model)
  const int audioSamples = audio_get_required_samples();
  const int audioRate    = audio_get_sample_rate_hz();

  g_audioSamplesNeeded = audioSamples;
  g_audioBuf = (int16_t*)malloc(sizeof(int16_t) * g_audioSamplesNeeded);
  if (!g_audioBuf) { Serial.println("Audio buffer alloc failed"); while (1) delay(500); }

  g_gestureFrameLen = (size_t)gesture_get_frame_length();
  g_gestureFrame = (float*)malloc(sizeof(float) * g_gestureFrameLen);
  if (!g_gestureFrame) { Serial.println("Gesture frame alloc failed"); while (1) delay(500); }

  const float gestureIntervalMs = gesture_get_interval_ms();
  g_imuPeriodUs = (uint32_t)(gestureIntervalMs * 1000.0f);
  g_nextImuSampleDue = micros();

  // PDM init
  if (!PDM.begin(1, audioRate)) {
    Serial.println("PDM start failed.");
    while (1) delay(500);
  }
  PDM.setBufferSize(2048);
  PDM.onReceive(onPDMdata);

  Serial.println("Wand ready.");
}

// ---------------- Loop ----------------
void loop() {
  ledPlayer.update();

  // IMU sampling at the model's interval
  uint32_t nowUs = micros();
  if ((int32_t)(nowUs - g_nextImuSampleDue) >= 0) {
    g_nextImuSampleDue += g_imuPeriodUs;

    float ax=0, ay=0, az=0, gx=0, gy=0, gz=0;
    if (IMU.accelerationAvailable()) IMU.readAcceleration(ax, ay, az);
    if (GESTURE_AXES == 6) {
      if (IMU.gyroscopeAvailable()) IMU.readGyroscope(gx, gy, gz);
    }

    // Write to gesture frame in expected interleaved order
    if (g_gestureFrame && g_gestureFrameLen >= GESTURE_AXES) {
      if (g_gestureWriteIndex + GESTURE_AXES <= g_gestureFrameLen) {
        g_gestureFrame[g_gestureWriteIndex++] = ax;
        g_gestureFrame[g_gestureWriteIndex++] = ay;
        g_gestureFrame[g_gestureWriteIndex++] = az;
        if (GESTURE_AXES == 6) {
          g_gestureFrame[g_gestureWriteIndex++] = gx;
          g_gestureFrame[g_gestureWriteIndex++] = gy;
          g_gestureFrame[g_gestureWriteIndex++] = gz;
        }
      }
      // If frame full -> run gesture inference
      if (g_gestureWriteIndex >= g_gestureFrameLen) {
        g_gestureWriteIndex = 0;
        const char* label = nullptr; float conf = 0.0f;
        if (gesture_infer_from_frame(g_gestureFrame, g_gestureFrameLen, &label, &conf)) {
          SpellId id = spellFromLabel(label, false);
          if (id != SPELL_NONE && conf >= GEST_CONF_THRESH) {
            lastGesture.id = id; lastGesture.conf = conf; lastGesture.t_ms = millis();
            checkFusion();
          }
        }
      }
    }
  }

  // Audio inference when window is ready
  if (g_audioWindowReady) {
    g_audioWindowReady = false; // consume
    const char* label = nullptr; float conf = 0.0f;
    bool ok = audio_infer_from_int16((const int16_t*)g_audioBuf, (size_t)g_audioSamplesNeeded, &label, &conf);
    g_audioHold = false; // re-enable filling
    if (ok) {
      SpellId id = spellFromLabel(label, true);
      if (id != SPELL_NONE && conf >= AUDIO_CONF_THRESH) {
        lastAudio.id = id; lastAudio.conf = conf; lastAudio.t_ms = millis();
        checkFusion();
      }
    }
  }
}