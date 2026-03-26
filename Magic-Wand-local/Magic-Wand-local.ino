#include <Expelliarmus_inferencing.h>
#include <Arduino_LSM9DS1.h>
#include "spell_types.h"

// ================== Config ==================
static const float GESTURE_CONF_THRESH = 0.75f;

#ifndef EI_CLASSIFIER_RAW_SAMPLES_PER_FRAME
#error "EI_CLASSIFIER_RAW_SAMPLES_PER_FRAME not defined"
#endif

static const int AXES = EI_CLASSIFIER_RAW_SAMPLES_PER_FRAME;
static SpellId lastSpell = SPELL_NONE;

// ================== LED Pins ==================
static const int LED_R_PIN = LEDR;
static const int LED_G_PIN = LEDG;
static const int LED_B_PIN = LEDB;

static const bool LED_INVERT = true;

// ================== LED Core ==================
static inline uint8_t applyInvert(uint8_t v) {
  return LED_INVERT ? (255 - v) : v;
}

static inline void ledSet(uint8_t r, uint8_t g, uint8_t b) {
  analogWrite(LED_R_PIN, applyInvert(r));
  analogWrite(LED_G_PIN, applyInvert(g));
  analogWrite(LED_B_PIN, applyInvert(b));
}

static inline void ledOff() {
  ledSet(0, 0, 0);
}

static inline void ledInit() {
  pinMode(LED_R_PIN, OUTPUT);
  pinMode(LED_G_PIN, OUTPUT);
  pinMode(LED_B_PIN, OUTPUT);
#if defined(analogWriteResolution)
  analogWriteResolution(8);
#endif
  ledOff();
}

// ================== LED Effects ==================
const LedSegment EFF_EXPELLIARMUS_SEG[] = {
  {90,255,0,0}, {90,0,0,0},
  {90,255,0,0}, {90,0,0,0},
  {90,255,0,0}, {180,0,0,0}
};
const LedEffect EFF_EXPELLIARMUS = { EFF_EXPELLIARMUS_SEG, 6, false };

const LedSegment EFF_INCENDIO_SEG[] = {
  {120,220,110,0}, {70,170,80,0}, {90,255,130,0},
  {60,200,90,0}, {80,240,120,0}, {70,180,85,0},
  {50,230,115,0}, {90,210,95,0}, {60,255,140,0},
  {80,190,90,0}, {70,220,105,0}, {50,255,135,0},
  {90,200,85,0}, {60,235,120,0}, {80,175,80,0},
  {60,245,130,0}, {70,205,95,0}, {50,250,135,0},
  {80,185,85,0}, {100,215,100,0},
  {70,235,110,0}, {60,195,90,0}, {90,225,105,0},
  {60,205,95,0}, {120,0,0,0}
};
const LedEffect EFF_INCENDIO = { EFF_INCENDIO_SEG, 25, false };

const LedSegment EFF_EXPECTO_PATRONUM_SEG[] = {
  {2000,0,0,255}, {100,0,0,0}
};
const LedEffect EFF_EXPECTO_PATRONUM = { EFF_EXPECTO_PATRONUM_SEG, 2, false };

const LedSegment EFF_REPARO_SEG[] = {
  {80,0,255,0}, {60,0,0,0}, {70,0,255,0}, {50,0,0,0},
  {90,0,255,0}, {60,0,0,0}, {80,0,255,0}, {50,0,0,0},
  {70,0,255,0}, {60,0,0,0}, {80,0,255,0}, {50,0,0,0},
  {90,0,255,0}, {60,0,0,0}, {2000,0,255,0}
};
const LedEffect EFF_REPARO = { EFF_REPARO_SEG, 15, false };

const LedSegment EFF_ALOHOMORA_SEG[] = {
  {80,255,255,255}, {40,0,0,0}, {3000,255,255,255}, {100,0,0,0}
};
const LedEffect EFF_ALOHOMORA = { EFF_ALOHOMORA_SEG, 4, false };

// ================== LED Player ==================
struct LedPlayer {
  const LedEffect* eff = nullptr;
  uint8_t idx = 0;
  uint32_t endAt = 0;
  bool playing = false;

  void begin() { ledInit(); }

  void play(const LedEffect* e) {
    eff = e; idx = 0; playing = (e && e->count > 0);
    if (playing) { 
      ledSet(e->segs[0].r,e->segs[0].g,e->segs[0].b);
      endAt = millis() + e->segs[0].duration_ms;
    } else { ledOff(); }
  }

  void stop() { playing = false; eff = nullptr; ledOff(); }

  void update() {
    if (!playing || !eff) return;
    if ((int32_t)(millis()-endAt) >= 0) {
      idx++;
      if (idx >= eff->count) { if (eff->loop) idx=0; else { stop(); return; } }
      ledSet(eff->segs[idx].r, eff->segs[idx].g, eff->segs[idx].b);
      endAt = millis() + eff->segs[idx].duration_ms;
    }
  }
};
LedPlayer ledPlayer;

// ================== Spell Mapping ==================
const SpellDescriptor SPELLS[] = {
  { SPELL_EXPELLIARMUS,"Expelliarmus",&EFF_EXPELLIARMUS },
  { SPELL_IDLE,"Idle",nullptr },
  { SPELL_EXPECTO_PATRONUM,"Expecto_Patronum",&EFF_EXPECTO_PATRONUM },
  { SPELL_REPARO,"Reparo",&EFF_REPARO },
  { SPELL_ALOHOMORA,"Alohomora",&EFF_ALOHOMORA },
  { SPELL_INCENDIO,"Incendio",&EFF_INCENDIO }
};
const size_t NUM_SPELLS = sizeof(SPELLS)/sizeof(SPELLS[0]);

SpellId spellFromLabel(const char* label) {
  for (size_t i=0;i<NUM_SPELLS;i++) if(strcmp(label,SPELLS[i].gestureLabel)==0) return SPELLS[i].id;
  return SPELL_NONE;
}

const SpellDescriptor* spellById(SpellId id) {
  for (size_t i=0;i<NUM_SPELLS;i++) if(SPELLS[i].id==id) return &SPELLS[i];
  return nullptr;
}

// ================== IMU ==================
static float imuBuffer[EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE];
static size_t imuWriteIndex = 0;
static const uint32_t SAMPLE_INTERVAL_US = 1000000UL / EI_CLASSIFIER_FREQUENCY;
static uint32_t nextSampleTime = 0;

static bool sampleIMU() {
  float ax,ay,az,gx,gy,gz;

  if (!IMU.accelerationAvailable()) return false;
  IMU.readAcceleration(ax,ay,az);

  if (AXES>=6) {
    if (!IMU.gyroscopeAvailable()) return false;
    IMU.readGyroscope(gx,gy,gz);
    if (fabs(gx)<0.5f) gx=0;
    if (fabs(gy)<0.5f) gy=0;
    if (fabs(gz)<0.5f) gz=0;
  }

  imuBuffer[imuWriteIndex++] = ax;
  imuBuffer[imuWriteIndex++] = ay;
  imuBuffer[imuWriteIndex++] = az;
  if (AXES>=6) { imuBuffer[imuWriteIndex++] = gx; imuBuffer[imuWriteIndex++] = gy; imuBuffer[imuWriteIndex++] = gz; }

  return true;
}

// ================== EI Callback ==================
static int getSignalData(size_t offset,size_t length,float* out) {
  memcpy(out,imuBuffer+offset,length*sizeof(float));
  return 0;
}

// ================== Setup ==================
void setup() {
  Serial.begin(115200);
  for (uint32_t t=millis();!Serial && millis()-t<3000;){}
  if(!IMU.begin()) while(1) delay(100);
  ledPlayer.begin();
  nextSampleTime = micros();
}

// ================== Loop ==================
void loop() {
  ledPlayer.update();
  imuWriteIndex = 0;

  // IMU Daten sammeln
  while(imuWriteIndex<EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE){
    while(!sampleIMU()){}
    delayMicroseconds(SAMPLE_INTERVAL_US);
  }

  // Motion berechnen
  float motion=0;
  for(size_t i=0;i<EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE;i++) motion+=fabs(imuBuffer[i]);
  motion/=EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE;

  const float MOTION_THRESH=1.0f;
  if(motion<MOTION_THRESH){
    lastSpell=SPELL_NONE;
    ledPlayer.stop();
    Serial.print("Idle due to low motion: ");
    Serial.println(motion,4);
    return;
  }

  // Klassifikation
  ei_impulse_result_t result={0};
  signal_t signal;
  signal.total_length=EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE;
  signal.get_data=&getSignalData;

  if(run_classifier(&signal,&result,false)!=EI_IMPULSE_OK) return;

  int bestIdx=-1;
  float bestConf=0;
  for(int i=0;i<EI_CLASSIFIER_LABEL_COUNT;i++){
    Serial.print(result.classification[i].label);
    Serial.print(": ");
    Serial.println(result.classification[i].value,4);
    if(result.classification[i].value>bestConf){bestConf=result.classification[i].value;bestIdx=i;}
  }

  if(bestIdx>=0 && bestConf>=GESTURE_CONF_THRESH){
    const char* label=result.classification[bestIdx].label;
    SpellId id=spellFromLabel(label);
    Serial.print("Detected spell: ");
    Serial.print(label);
    Serial.print(" (conf=");
    Serial.print(bestConf,4);
    Serial.print("), motion=");
    Serial.println(motion,4);

    if(id!=SPELL_NONE && id!=lastSpell){
      const SpellDescriptor* sd=spellById(id);
      if(sd && sd->effect) ledPlayer.play(sd->effect);
      lastSpell=id;
    }
  } else lastSpell=SPELL_NONE;

  delay(3);
}