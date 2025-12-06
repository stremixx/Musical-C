#include "MusicPlayer.h"
#include "TUI.h"
#include "TerminalUtils.h"
#include "VisualizerNode.h" // For NUM_BARS constant if needed, or rely on TUI

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <thread>
#include <unistd.h>

namespace fs = std::filesystem;

std::vector<std::string> getAudioFiles(const std::string &path) {
  std::vector<std::string> files;
  for (const auto &entry : fs::directory_iterator(path)) {
    if (entry.is_regular_file()) {
      std::string ext = entry.path().extension().string();
      if (ext == ".mp3" || ext == ".wav" || ext == ".flac" || ext == ".ogg") {
        files.push_back(entry.path().filename().string());
      }
    }
  }
  std::sort(files.begin(), files.end());
  return files;
}

int main() {
  TermMusicPlayer player;
  if (!player.isInit()) {
    std::cerr << "Failed to initialize audio engine." << std::endl;
    return 1;
  }

  std::vector<std::string> files = getAudioFiles(".");
  int currentIndex = 0;

  if (files.empty()) {
    std::cout << "No audio files found in current directory." << std::endl;
    return 0;
  }

  enableRawMode();
  clearScreen();

  bool running = true;
  bool dirty = true;

  while (running) {
    // Handle Input
    if (kbhit()) {
      char c;
      if (read(STDIN_FILENO, &c, 1) == 1) {
        dirty = true;
        if (c == 'q') {
          running = false;
        } else if (c == ' ') {
          player.togglePause();
        } else if (c == 'n') {
          currentIndex = (currentIndex + 1) % files.size();
          player.play(files[currentIndex]);
        } else if (c == 'p') {
          currentIndex = (currentIndex - 1 + files.size()) % files.size();
          player.play(files[currentIndex]);
        } else if (c == '=' || c == '+') {
          player.changeVolume(0.05f);
        } else if (c == '-' || c == '_') {
          player.changeVolume(-0.05f);
        } else if (c == 'f') {
          player.seekBy(5.0f);
        } else if (c == 'b') {
          player.seekBy(-5.0f);
        }
      }
    }

    // Update dirty check: if playing, we need to redraw progress bar every so
    // often
    if (player.isPlaying())
      dirty = true;

    // Render UI
    if (dirty) {
      int rows, cols;
      getTermSize(rows, cols);

      // Calculate available height for visualizer
      // Header: 2 lines
      // Playing/Status/Vol/Prog: 4 lines
      // Playlist Header + Separation: 2 lines
      // Playlist items: 7 lines
      // Controls: 2 lines
      // Bottom margin: 1 line
      // Total fixed usage approx: 18 lines.

      int reservedHeight = 18;
      int visHeight = std::max(5, rows - reservedHeight);

      // Widths
      int totalWidth = std::max(40, cols - 4);      // Margin
      int barWidth = std::max(10, totalWidth - 25); // Room for timestamps

      std::stringstream buffer;
      buffer << "\033[H"; // Move cursor to home

      buffer << COLOR_BOLD << COLOR_MAGENTA
             << "=== Terminal Music Player ===" << COLOR_RESET << "\r\n";

      // Visualizer Area
      std::vector<float> bars;
      player.getVisData(bars);
      drawVisualizer(buffer, bars, visHeight);

      buffer << "-----------------------------" << "\r\n";
      buffer << "Now Playing: " << COLOR_CYAN << player.getCurrentTitle()
             << COLOR_RESET << "\r\n";
      buffer << "Status: "
             << (player.isPlaying()
                     ? (std::string(COLOR_GREEN) + "[PLAYING]" + COLOR_RESET)
                     : (std::string(COLOR_YELLOW) + "[PAUSED]" + COLOR_RESET))
             << "\r\n";
      buffer << "Volume: "
             << drawVolumeBar(player.getVolume(), std::min(20, totalWidth / 2))
             << "\r\n";
      buffer << "Progress: "
             << drawProgressBar(player.getCursor(), player.getLength(),
                                barWidth)
             << "\r\n";

      buffer << "\r\n";
      // Truncate playlist to show fewer items to save space
      int start = std::max(0, currentIndex - 3);
      int end = std::min((int)files.size(), start + 7);

      buffer << "Playlist:\r\n";
      for (int i = start; i < end; ++i) {
        if (i == currentIndex) {
          buffer << COLOR_BOLD << COLOR_GREEN << " > " << files[i]
                 << COLOR_RESET << "\r\n";
        } else {
          buffer << "   " << files[i] << "\r\n";
        }
      }

      buffer << "\r\n";
      buffer << "Controls: [Space] Pause | [n] Next | [p] Prev | [+/-] Vol "
                "| [f/b] Seek | [q] Quit"
             << "\r\n";

      // Clear from cursor to end of screen
      buffer << "\033[J";

      std::cout << buffer.str();
      std::cout.flush();
      dirty = false;
    }

    std::this_thread::sleep_for(
        std::chrono::milliseconds(100)); // slightly slower refresh
  }

  clearScreen();
  disableRawMode();
  return 0;
}
