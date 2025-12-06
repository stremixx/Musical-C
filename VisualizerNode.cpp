#include "VisualizerNode.h"
#include "FftUtils.h"
#include <cmath>
#include <cstring> // for memcpy

static void node_process_pcm_frames(ma_node *pNode, const float **ppFramesIn,
                                    ma_uint32 *pFrameCountIn,
                                    float **ppFramesOut,
                                    ma_uint32 *pFrameCountOut) {
  VisualizerNode *pVis = (VisualizerNode *)pNode;
  const float *pFrames = ppFramesIn[0];
  ma_uint32 frameCount = *pFrameCountIn;

  // Pass-through
  if (ppFramesOut != NULL && ppFramesOut[0] != NULL) {
    memcpy(ppFramesOut[0], ppFramesIn[0],
           frameCount * ma_node_get_output_channels(pNode, 0) * sizeof(float));
  }
  *pFrameCountOut = frameCount;

  int channels = ma_node_get_input_channels(pNode, 0);

  for (ma_uint32 i = 0; i < frameCount; ++i) {
    // Downmix to mono
    float sample = 0.0f;
    for (int c = 0; c < channels; ++c) {
      sample += pFrames[i * channels + c];
    }
    sample /= channels;

    // Fill buffer
    pVis->inputBuffer[pVis->writeIndex++] = sample;

    if (pVis->writeIndex >= FFT_SIZE) {
      // Process FFT
      CArray data(FFT_SIZE);
      for (int j = 0; j < FFT_SIZE; ++j) {
        // Hanning Window
        float window = 0.5f * (1.0f - cos(2.0f * PI * j / (FFT_SIZE - 1)));
        data[j] = Complex(pVis->inputBuffer[j] * window, 0);
      }

      fft(data);

      // Map to bars (Linear mapping for simplicity first, or simple grouping)
      // FFT_SIZE/2 bins (0 to Nyquist).
      // We have 256 useful bins. We want 32 bars.
      // 256 / 32 = 8 bins per bar.

      int binsPerBar = (FFT_SIZE / 2) / NUM_BARS;

      for (int b = 0; b < NUM_BARS; ++b) {
        float magnitude = 0.0f;
        for (int k = 0; k < binsPerBar; ++k) {
          int binIdx = b * binsPerBar + k;
          if (binIdx < FFT_SIZE / 2) {
            magnitude += std::abs(data[binIdx]);
          }
        }
        magnitude /= binsPerBar;

        float val = magnitude * 2.0f; // Gain
        pVis->bars[b].store(val, std::memory_order_relaxed);
      }

      pVis->writeIndex = 0;
    }
  }
}

ma_node_vtable g_visualizer_vtable = {node_process_pcm_frames, NULL,
                                      1, // 1 input bus
                                      1, // 1 output bus
                                      MA_NODE_FLAG_PASSTHROUGH};
