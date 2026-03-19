#pragma once
#include <stddef.h>
#include <stdint.h>

// Returns the number of PCM samples required by the Audio model (e.g., 16000 for 1 s @ 16 kHz)
int audio_get_required_samples();

// Returns the sample rate required by the Audio model (e.g., 16000)
int audio_get_sample_rate_hz();

// Runs inference on a single audio window (int16 PCM). Returns true on success.
// out_label points to the top predicted class label inside the EI model.
// out_conf is the corresponding confidence/probability.
bool audio_infer_from_int16(const int16_t* pcm, size_t sample_count, const char** out_label, float* out_conf);
File: audio_wrapper.cpp Replace the include below with your actual Audio EI header (e.g., my_audio_model_inferencing.h).

#include "audio_wrapper.h"

// Replace with your Audio EI library header:
#include <audio_model_inferencing.h>  // <-- change this to your audio model's inferencing header

// This translation unit sees only the Audio model's macros
extern "C" {
  // The EI SDK defines signal_t, ei_impulse_result_t, EI_IMPULSE_ERROR, etc.
}

static const int16_t* g_pcm_ptr = nullptr;

static int audio_get_data(size_t offset, size_t length, float* out_ptr) {
  // Convert int16 [-32768, 32767] to float [-1, 1]
  const int16_t* p = g_pcm_ptr + offset;
  for (size_t i = 0; i < length; i++) out_ptr[i] = (float)p[i] / 32768.0f;
  return 0;
}

int audio_get_required_samples() {
  return (int)EI_CLASSIFIER_RAW_SAMPLE_COUNT;
}

int audio_get_sample_rate_hz() {
  return (int)EI_CLASSIFIER_FREQUENCY;
}

bool audio_infer_from_int16(const int16_t* pcm, size_t sample_count, const char** out_label, float* out_conf) {
  if (!pcm || sample_count < EI_CLASSIFIER_RAW_SAMPLE_COUNT) return false;

  signal_t sig;
  sig.total_length = EI_CLASSIFIER_RAW_SAMPLE_COUNT;
  sig.get_data = &audio_get_data;

  ei_impulse_result_t res = { 0 };
  EI_IMPULSE_ERROR err;

  g_pcm_ptr = pcm;
  err = run_classifier(&sig, &res, false);
  g_pcm_ptr = nullptr;

  if (err != EI_IMPULSE_OK) return false;

  // Pick best class
  float best = 0.0f; const char* bestLabel = nullptr;
  for (size_t i=0; i<EI_CLASSIFIER_LABEL_COUNT; i++) {
    if (res.classification[i].value > best) {
      best = res.classification[i].value;
      bestLabel = res.classification[i].label;
    }
  }
  if (out_label) *out_label = bestLabel;
  if (out_conf)  *out_conf  = best;
  return (bestLabel != nullptr);
}