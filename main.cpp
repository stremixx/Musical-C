#include "MusicPlayer.h"
#include "TUI.h"
#include "TerminalUtils.h"
#include "VisualizerNode.h" // For NUM_BARS constant if needed, or rely on TUI

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <thread>
#include <thread>
#include <unistd.h>
#include <csignal>
#include <atomic>

std::atomic<bool> resizeRequest(false);

void handleResize(int sig) {
    resizeRequest = true;
}


namespace fs = std::filesystem;

// Helper to execute command and get output
#include <cstdio>
#include <memory>
#include <array>
#include <stdexcept>

std::string exec(const char* cmd) {
    std::array<char, 128> buffer;
    std::string result;
    std::shared_ptr<FILE> pipe(popen(cmd, "r"), pclose);
    if (!pipe) return ""; // failed
    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
        result += buffer.data();
    }
    // Remove trailing newline
    if (!result.empty() && result.back() == '\n') {
        result.pop_back();
    }
    return result;
}

enum AppMode {
    MODE_LOCAL,
    MODE_YOUTUBE
};


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
  // Register Signal Handler
  signal(SIGWINCH, handleResize);

  bool running = true;
  bool dirty = true;
  AppMode currentMode = MODE_LOCAL;
  std::string ytTitle = "No Audio Loaded";


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
        } else if (c == 'n' && currentMode == MODE_LOCAL) {
          currentIndex = (currentIndex + 1) % files.size();
          player.play(files[currentIndex]);
        } else if (c == 'p' && currentMode == MODE_LOCAL) {
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
        } else if (c == 'y') {
          if (currentMode == MODE_LOCAL) {
              // Switch TO YouTube Mode
              player.stop();
              currentMode = MODE_YOUTUBE;
              disableRawMode();
              
              std::cout << "\033[2J\033[H"; // Manual Clear
              std::cout << "=== YouTube Mode ===\r\n";
              std::cout << "Enter YouTube URL (or empty to cancel): ";
              std::cout.flush();

              std::string url;
              std::getline(std::cin, url);

              if (!url.empty()) {
                  std::cout << "Fetching title...\r\n";
                  // Get Title
                  // Redirect stderr to null to avoid warnings breaking TUI
                  std::string titleCmd = "./yt-dlp --no-warnings --print title \"" + url + "\" 2>/dev/null";
                  ytTitle = exec(titleCmd.c_str());
                  if (ytTitle.empty()) ytTitle = "Unknown Title";

                  std::cout << "Downloading: " << ytTitle << "...\r\n";

                  if (fs::exists("playing.mp3")) fs::remove("playing.mp3");

                  // Download
                  // Added --no-warnings just in case
                  std::string cmd = "./yt-dlp --no-warnings --ffmpeg-location ./bin/ffmpeg -x --audio-format mp3 -o \"playing.mp3\" \"" + url + "\" > /dev/null 2>&1";
                  int ret = system(cmd.c_str());
                  
                  if (ret == 0 && fs::exists("playing.mp3")) {
                      player.play("playing.mp3");
                  } else {
                      // If it failed, it might be due to network or severe error. 
                      // Since we silence output, we can't see why, but we keep UI clean.
                      std::cout << "Download failed.\r\n";
                      ytTitle = "Error Loading Video";
                      std::this_thread::sleep_for(std::chrono::seconds(1));
                  }
              } else {
                  // If canceled, stay in empty YouTube mode? Or go back?
                  // Sticking to mode prevents confusion.
                  ytTitle = "No Audio Loaded";
              }
              enableRawMode();
              clearScreen();
              dirty = true;
          } else {
              // Switch FROM YouTube Mode (Back to Local)
              player.stop();
              currentMode = MODE_LOCAL;
              // Clear screen completely to prevent ghosts
              std::cout << "\033[2J\033[H";
              std::cout.flush();
              dirty = true;
          }
        } else if (c == 'u' && currentMode == MODE_YOUTUBE) {
             // Optional: Allow entering new URL without exiting mode?
             player.stop();
             disableRawMode();
             std::cout << "\033[2J\033[H";
             std::cout << "=== YouTube Mode ===\r\n";
             std::cout << "Enter New YouTube URL: ";
             std::cout.flush();
             std::string url;
             std::getline(std::cin, url);
             if (!url.empty()) {
                  std::cout << "Fetching title...\r\n";
                  std::string titleCmd = "./yt-dlp --no-warnings --print title \"" + url + "\" 2>/dev/null";
                  ytTitle = exec(titleCmd.c_str());
                  std::cout << "Downloading: " << ytTitle << "...\r\n";
                  if (fs::exists("playing.mp3")) fs::remove("playing.mp3");
                  std::string cmd = "./yt-dlp --no-warnings --ffmpeg-location ./bin/ffmpeg -x --audio-format mp3 -o \"playing.mp3\" \"" + url + "\" > /dev/null 2>&1";
                  int ret = system(cmd.c_str());
                  if (ret == 0 && fs::exists("playing.mp3")) {
                      player.play("playing.mp3");
                  } else {
                      std::cout << "Download failed.\r\n";
                      ytTitle = "Error Loading Video";
                      std::this_thread::sleep_for(std::chrono::seconds(1));
                  }
             }
             enableRawMode();
             clearScreen();
             dirty = true;
        }
      }
    }

    // Update dirty check: if playing, we need to redraw progress bar every so
    // often
    // Update dirty check: if playing, we need to redraw progress bar every so
    // often
    if (player.isPlaying())
      dirty = true;
    
    // Handle Window Resize
    if (resizeRequest) {
        resizeRequest = false;
        dirty = true;
    }

    // Render UI
    if (dirty) {
      int rows, cols;
      getTermSize(rows, cols);

      if (rows < 25 || cols < 40) {
          // Terminal too small - show warning instead of broken UI
          std::cout << "\033[H\033[2J"; // Home and Clear
          std::cout << "Terminal too small.\r\n";
          std::cout << "Please resize to at least 25x40.\r\n";
          std::cout << "Current: " << rows << "x" << cols << "\r\n";
          std::cout << "Press 'q' to quit.\r\n";
          std::cout.flush();
          dirty = false; 
          // Sleep to avoid busy loop if just resizing
          std::this_thread::sleep_for(std::chrono::milliseconds(100));
          continue;
      }

      // Calculate available height for visualizer
      // Header: 1 line
      // Vis: visHeight lines + 2 extra spacer lines (top/bottom)
      // Vis Separator: 1 line ("--...--")
      // Content Separator: 1 line ("-------")
      // Now Playing: 1 line
      // Status: 1 line
      // Vol: 1 line
      // Prog: 1 line
      // Empty: 1 line
      // Playlist Header + Items + Spacer: 9-ish lines 
      // Controls: 1 line
      // Empty: 1 line (at end)
      
      // Total approx 20-22 lines of fixed content.
      int reservedHeight = 24; 
      int visHeight = std::max(2, rows - reservedHeight);

      // Widths
      int totalWidth = std::max(40, cols - 4);      // Margin
      int barWidth = std::max(10, totalWidth - 25); // Room for timestamps

      std::stringstream buffer;
      // \033[2J = Clear entire screen
      // \033[3J = Clear scrollback buffer (pushes everything up/gone)
      // \033[H  = Move cursor to home
      buffer << "\033[2J\033[3J\033[H";

      buffer << COLOR_BOLD << COLOR_MAGENTA
             << "=== Terminal Music Player ===" << COLOR_RESET << "\r\n";

      // Visualizer Area
      std::vector<float> bars;
      player.getVisData(bars);
      drawVisualizer(buffer, bars, visHeight);

      buffer << "-----------------------------" << "\r\n";
      buffer << "Now Playing: " << COLOR_CYAN << (currentMode == MODE_LOCAL ? player.getCurrentTitle() : ytTitle)
             << COLOR_RESET << "\r\n";
      buffer << "Status: [" << (currentMode == MODE_LOCAL ? "LOCAL" : "YOUTUBE") << "] "
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
      
      if (currentMode == MODE_LOCAL) {
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
      } else {
          // YouTube Mode UI
          buffer << COLOR_BOLD << COLOR_RED << "   YOUTUBE PLAYER   " << COLOR_RESET << "\r\n";
          buffer << "   Playing from dynamic stream.\r\n";
          buffer << "   Title: " << ytTitle << "\r\n";
          buffer << "   (Files hidden in this mode)\r\n";
          buffer << "\r\n\r\n";
      }

      buffer << "\r\n";
      buffer << "\r\n";
      if (currentMode == MODE_LOCAL) {
        buffer << "Controls: [Space] Pause | [n] Next | [p] Prev | [+/-] Vol | [f/b] Seek | [y] YouTube | [q] Quit\r\n";
      } else {
        buffer << "Controls: [Space] Pause | [u] New URL | [+/-] Vol | [f/b] Seek | [y] Back to Local | [q] Quit\r\n";
      }

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
