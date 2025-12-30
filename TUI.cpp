#include "TUI.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

// Use extern for NUM_BARS if we want to share it, or redefine.
// VisualizerNode.h has it. Best to include VisualizerNode.h for consistency?
// Or just TUI shouldn't care about max bars constant if implementation is
// dynamic? Implementation uses NUM_BARS inside `drawVisualizer` in main.cpp:
// `out << "--";` loop for NUM_BARS.
// Let's rely on VisualizerNode.h for that constant to keep it single source of
// truth.
#include "VisualizerNode.h"

std::string formatTime(float seconds) {
  int m = static_cast<int>(seconds) / 60;
  int s = static_cast<int>(seconds) % 60;
  char buffer[16];
  snprintf(buffer, sizeof(buffer), "%02d:%02d", m, s);
  return std::string(buffer);
}

std::string drawProgressBar(float current, float total, int width) {
  if (total <= 0.0f)
    return std::string(" [") + std::string(width, ' ') + "] 00:00 / 00:00";

  float progress = std::min(std::max(current / total, 0.0f), 1.0f);
  int pos = static_cast<int>(progress * width);

  std::string bar = "[";
  bar += COLOR_CYAN;
  for (int i = 0; i < width; ++i) {
    if (i < pos)
      bar += "\u2588"; // Full Block
    else if (i == pos)
      bar += "\u2592"; // Medium Shade
    else
      bar += " ";
  }
  bar += COLOR_RESET;
  bar += "] " + formatTime(current) + " / " + formatTime(total);
  return bar;
}

std::string drawVolumeBar(float volume, int width) {
  int pos = static_cast<int>(volume * width);
  std::string bar = "[";
  bar += COLOR_GREEN;
  for (int i = 0; i < width; ++i) {
    if (i < pos)
      bar += "\u2588";
    else
      bar += " ";
  }
  bar += COLOR_RESET;
  bar += "] " + std::to_string(static_cast<int>(volume * 100)) + "%";
  return bar;
}

void drawVisualizer(std::ostream &out, const std::vector<float> &bars,
                    int height) {
  // out << "\r\n"; // Removed spacer

  for (int h = height; h > 0; --h) {
    out << "  "; // Margin
    for (float val : bars) {
      // Normalized height approx.
      int barHeight = static_cast<int>(std::min(val * height, (float)height));

      if (h <= barHeight) {
        out << COLOR_GREEN << "\u2588 " << COLOR_RESET;
      } else {
        out << "  ";
      }
    }
    out << "\r\n";
  }
  // Bottom line
  out << "  ";
  for (int i = 0; i < NUM_BARS; ++i)
    out << "--";
  out << "\r\n\r\n";
}
