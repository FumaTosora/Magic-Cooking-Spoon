#pragma once
#include <stddef.h>

// Returns total number of float values per window expected by the Gesture model
int   gesture_get_frame_length();

// Returns sampling interval in milliseconds (1/frequency) for the Gesture model
float gesture_get_interval_ms();

// Runs inference on a flattened IMU frame of length gesture_get_frame_length().
// The frame must be interleaved per time step: ax,ay,az,(gx,gy,gz) depending on your model axes.
// Returns true on success and outputs top label + confidence.
bool gesture_infer_from_frame(const float* frame, size_t value_count, const char** out_label, float* out_conf);
File: gesture_wrapper.cpp Replace the include below with your Gesture EI header — the one you modified to expose run_classifier_gesture.

#include "gesture_wrapper.h"

// Replace with your Gesture EI library header (modified to have run_classifier_gesture):
#include <gesture_model_inferencing.h>  // <-- change to your gesture model's inferencing header

// Forward declaration for the renamed function (if the header does not already declare it):
extern "C" EI_IMPULSE_ERROR run_classifier_gesture(signal_t* signal, ei_impulse_result_t* result, bool debug);

static const float* g_frame_ptr = nullptr;

static int gesture_get_data(size_t offset, size_t length, float* out_ptr) {
  const float* p = g_frame_ptr + offset;
  for (size_t i = 0; i < length; i++) out_ptr[i] = p[i];
  return 0;
}

int gesture_get_frame_length() {
  return (int)EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE;
}

float gesture_get_interval_ms() {
  return (float)EI_CLASSIFIER_INTERVAL_MS;
}

bool gesture_infer_from_frame(const float* frame, size_t value_count, const char** out_label, float* out_conf) {
  if (!frame || value_count < EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE) return false;

  signal_t sig;
  sig.total_length = EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE;
  sig.get_data = &gesture_get_data;

  ei_impulse_result_t res = { 0 };

  g_frame_ptr = frame;
  EI_IMPULSE_ERROR err = run_classifier_gesture(&sig, &res, false);
  g_frame_ptr = nullptr;

  if (err != EI_IMPULSE_OK) return false;

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