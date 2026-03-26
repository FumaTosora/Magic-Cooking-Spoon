// spell_types.h
#pragma once
#include <Arduino.h>

// -------- LED helpers --------
struct LedSegment { uint16_t duration_ms; uint8_t r, g, b; };
struct LedEffect  { const LedSegment* segs; uint8_t count; bool loop; };

// -------- Spell registry --------
enum SpellId : uint8_t {
  SPELL_NONE = 0,
  SPELL_EXPELLIARMUS,
  SPELL_IDLE,
  SPELL_EXPECTO_PATRONUM,
  SPELL_REPARO,
  SPELL_ALOHOMORA,
  SPELL_INCENDIO
};

struct SpellDescriptor {
  SpellId     id;
  const char* gestureLabel;   // must match EI label exactly
  const LedEffect* effect;
};

// -------- Detected event --------
struct DetectedEvent {
  SpellId  id   = SPELL_NONE;
  float    conf = 0;
  uint32_t t_ms = 0;
};