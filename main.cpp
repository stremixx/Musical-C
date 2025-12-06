#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <complex>
#include <fcntl.h>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <sys/ioctl.h>
#include <termios.h>
#include <thread>
#include <unistd.h>
#include <valarray>
#include <vector>

namespace fs = std::filesystem;

// --- Colors & Styles ---
#define COLOR_RESET "\033[0m"
#define COLOR_BOLD "\033[1m"
#define COLOR_RED "\033[31m"
#define COLOR_GREEN "\033[32m"
#define COLOR_YELLOW "\033[33m"
#define COLOR_BLUE "\033[34m"
#define COLOR_MAGENTA "\033[35m"
#define COLOR_CYAN "\033[36m"
#define COLOR_WHITE "\033[37m"

// --- FFT Utils ---
using Complex = std::complex<float>;
using CArray = std::valarray<Complex>;
const float PI = 3.141592653589793238460f;

void fft(CArray &x) {
  const size_t N = x.size();
  if (N <= 1)
    return;

  // Divide
  CArray even = x[std::slice(0, N / 2, 2)];
  CArray odd = x[std::slice(1, N / 2, 2)];

  // Conquer
  fft(even);
  fft(odd);

  // Combine
  for (size_t k = 0; k < N / 2; ++k) {
    Complex t = std::polar(1.0f, -2 * PI * k / N) * odd[k];
    x[k] = even[k] + t;
    x[k + N / 2] = even[k] - t;
  }
}

// --- Visualizer Node ---

const int FFT_SIZE = 512;
const int NUM_BARS = 32;

struct VisualizerNode {
  ma_node_base base;
  std::atomic<float> bars[NUM_BARS];

  // Audio Thread Local Storage
  float inputBuffer[FFT_SIZE];
  int writeIndex = 0;
};

static void node_process_pcm_frames(ma_node *pNode, const float **ppFramesIn,
                                    ma_uint32 *pFrameCountIn,
                                    float **ppFramesOut,
                                    ma_uint32 *pFrameCountOut) {
  VisualizerNode *pVis = (VisualizerNode *)pNode;
  const float *pFrames = ppFramesIn[0];
  ma_uint32 frameCount = *pFrameCountIn;

  // Pass-through
  if (ppFramesOut != NULL && ppFramesOut[0] != NULL) {
    MA_COPY_MEMORY(ppFramesOut[0], ppFramesIn[0],
                   frameCount * ma_node_get_output_channels(pNode, 0) *
                       sizeof(float));
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

        // Smoothing / update atomic
        // Simple decay or direct set? Direct set for responsiveness, user sees
        // frame rate. Logarithmic scaling for display usually looks better: 20
        // * log10(mag) But for raw 0-1 bars, let's just boost it.

        float val = magnitude * 2.0f; // Gain
        // Smooth falloff could be done here, but doing it in UI thread is
        // easier for purely visual smoothing Actually, let's just store the raw
        // peak for this frame.
        pVis->bars[b].store(val, std::memory_order_relaxed);
      }

      pVis->writeIndex = 0;
    }
  }
}

static ma_node_vtable g_visualizer_vtable = {node_process_pcm_frames, NULL,
                                             1, // 1 input bus
                                             1, // 1 output bus
                                             MA_NODE_FLAG_PASSTHROUGH};

// --- Terminal Utils ---

struct termios orig_termios;

void disableRawMode() {
  tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
  std::cout << "\033[?25h"; // Show cursor
}

void enableRawMode() {
  tcgetattr(STDIN_FILENO, &orig_termios);
  atexit(disableRawMode);

  struct termios raw = orig_termios;
  raw.c_lflag &= ~(ECHO | ICANON); // Disable echo and canonical mode
  raw.c_cc[VMIN] = 0;              // read() returns immediately
  raw.c_cc[VTIME] = 0;

  tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
  std::cout << "\033[?25l"; // Hide cursor
}

int kbhit() {
  struct timeval tv = {0L, 0L};
  fd_set fds;
  FD_ZERO(&fds);
  FD_SET(STDIN_FILENO, &fds);
  return select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv);
}

void getTermSize(int &rows, int &cols) {
  struct winsize w;
  ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
  rows = w.ws_row;
  cols = w.ws_col;
}

// --- Music Player ---

class TermMusicPlayer {
  ma_engine engine;
  ma_sound sound;

  // Visualization
  VisualizerNode visNode;

  bool initialized = false;
  bool soundLoaded = false;
  std::string currentFile;
  float currentVolume = 1.0f;

public:
  TermMusicPlayer() {
    ma_result result;
    // Zero init array
    for (int i = 0; i < NUM_BARS; ++i)
      visNode.bars[i] = 0.0f;

    if ((result = ma_engine_init(NULL, &engine)) == MA_SUCCESS) {
      // Init Visualizer Node
      ma_node_config nodeConfig = ma_node_config_init();
      nodeConfig.vtable = &g_visualizer_vtable;

      // We must specify channel counts for the buses
      ma_uint32 channels = ma_engine_get_channels(&engine);
      nodeConfig.pInputChannels = &channels;
      nodeConfig.pOutputChannels = &channels;

      ma_node_graph *pGraph = &engine.nodeGraph;

      if ((result = ma_node_init(pGraph, &nodeConfig, NULL, &visNode.base)) ==
          MA_SUCCESS) {
        // Attach VisNode output to Engine Endpoint
        ma_node_attach_output_bus(&visNode.base, 0,
                                  ma_node_graph_get_endpoint(pGraph), 0);
        initialized = true;
      } else {
        std::cerr << "Visualizer node init failed with error: " << result
                  << std::endl;
        initialized = false;
      }

      if (initialized)
        ma_engine_set_volume(&engine, currentVolume);
    } else {
      std::cerr << "Engine init failed with error: " << result << std::endl;
    }
  }

  ~TermMusicPlayer() {
    if (soundLoaded)
      ma_sound_uninit(&sound);

    if (initialized) {
      ma_node_uninit(&visNode.base, NULL);
      ma_engine_uninit(&engine);
    }
  }

  bool play(const std::string &path) {
    if (!initialized)
      return false;

    if (soundLoaded) {
      ma_sound_stop(&sound);
      ma_sound_uninit(&sound);
      soundLoaded = false;
    }

    // MA_SOUND_FLAG_NO_DEFAULT_ATTACHMENT because we want to attach to our
    // custom node manually
    if (ma_sound_init_from_file(&engine, path.c_str(),
                                MA_SOUND_FLAG_NO_DEFAULT_ATTACHMENT, NULL, NULL,
                                &sound) == MA_SUCCESS) {

      // Attach Sound -> Visualizer Node
      ma_node_attach_output_bus(&sound, 0, &visNode.base, 0);

      ma_sound_start(&sound);
      soundLoaded = true;
      currentFile = path;
      return true;
    }
    return false;
  }

  void stop() {
    if (soundLoaded) {
      ma_sound_stop(&sound);
      ma_sound_seek_to_pcm_frame(&sound, 0);
    }
  }

  void togglePause() {
    if (soundLoaded) {
      if (ma_sound_is_playing(&sound)) {
        ma_sound_stop(&sound);
      } else {
        ma_sound_start(&sound);
      }
    }
  }

  void changeVolume(float delta) {
    if (!initialized)
      return;
    currentVolume += delta;
    if (currentVolume < 0.0f)
      currentVolume = 0.0f;
    if (currentVolume > 1.0f)
      currentVolume = 1.0f;
    ma_engine_set_volume(&engine, currentVolume);
  }

  void seekBy(float delta) {
    if (!soundLoaded)
      return;

    bool wasPlaying = ma_sound_is_playing(&sound);
    if (wasPlaying) {
      ma_sound_stop(&sound);
    }

    float current = getCursor();
    float length = getLength();
    float target = std::min(std::max(current + delta, 0.0f), length);

    ma_uint32 sampleRate;
    if (ma_sound_get_data_format(&sound, NULL, NULL, &sampleRate, NULL, 0) ==
        MA_SUCCESS) {
      ma_uint64 frame = (ma_uint64)(target * sampleRate);
      ma_sound_seek_to_pcm_frame(&sound, frame);
    }

    if (wasPlaying) {
      ma_sound_start(&sound);
    }
  }

  bool isInit() const { return initialized; }

  // Status getters
  bool isLoaded() const { return soundLoaded; }
  bool isPlaying() const { return soundLoaded && ma_sound_is_playing(&sound); }
  std::string getCurrentTitle() const {
    return currentFile.empty() ? "None" : currentFile;
  }

  float getVolume() const { return currentVolume; }

  float getCursor() {
    float cursor = 0.0f;
    if (soundLoaded)
      ma_sound_get_cursor_in_seconds(&sound, &cursor);
    return cursor;
  }

  float getLength() {
    float length = 0.0f;
    if (soundLoaded)
      ma_sound_get_length_in_seconds(&sound, &length);
    return length;
  }

  // Vis Data
  void getVisData(std::vector<float> &outBars) {
    outBars.resize(NUM_BARS);
    for (int i = 0; i < NUM_BARS; ++i) {
      outBars[i] = visNode.bars[i].load(std::memory_order_relaxed);
    }
  }
};

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
  out << "\r\n";

  // We have 32 bars.
  // Display them. We can probably fit them all or do every other one if width
  // is tight. Let's try to fit 32 bars. " " (space) + bar char. = 64 chars
  // wide. Should fit.

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

// --- Main Loop ---

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
      // Let's dynamically size the playlist if we want, but task is mainly
      // about "program screen" (visualizer + width). Let's prioritize
      // Visualizer height.

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

  disableRawMode();
  return 0;
}
