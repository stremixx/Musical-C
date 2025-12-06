#ifndef VISUALIZER_NODE_H
#define VISUALIZER_NODE_H

#include "miniaudio.h"
#include <atomic>

const int FFT_SIZE = 512;
const int NUM_BARS = 32;

struct VisualizerNode {
  ma_node_base base;
  std::atomic<float> bars[NUM_BARS];

  // Audio Thread Local Storage
  float inputBuffer[FFT_SIZE];
  int writeIndex = 0;
};

// VTable for the visualizer node
extern ma_node_vtable g_visualizer_vtable;

#endif // VISUALIZER_NODE_H
