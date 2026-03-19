#include "audio_wrapper.h"

// TODO: Diesen Include auf den Header deiner Audio-EI-Library ändern.
// Beispiel: #include <my_audio_project_inferencing.h>
#include <audio_model_inferencing.h>

static const int16_t* g_pcm_ptr = nullptr;

static int audio_get_data(size_t offset, size_t length, float* out_ptr) {
  // Int16 PCM [-32768, 32767] -> Float [-1, 1]
  const int16_t* p = g_pcm_ptr + offset;
  for (size_t i = 0; i < length; i++) {
    out_ptr[i] = (float)p[i] / 32768.0f;
  }
  return 0; // 0 = OK für Edge Impulse
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

  g_pcm_ptr = pcm;
  EI_IMPULSE_ERROR err = run_classifier(&sig, &res, false);
  g_pcm_ptr = nullptr;

  if (err != EI_IMPULSE_OK) return false;

  // Beste Klasse finden
  float best = 0.0f;
  const char* bestLabel = nullptr;
  for (size_t i = 0; i < EI_CLASSIFIER_LABEL_COUNT; i++) {
    if (res.classification[i].value > best) {
      best = res.classification[i].value;
      bestLabel = res.classification[i].label;
    }
  }
  if (out_label) *out_label = bestLabel;
  if (out_conf)  *out_conf  = best;
  return (bestLabel != nullptr);
}