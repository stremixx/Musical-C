#define MINIAUDIO_IMPLEMENTATION
#include "MusicPlayer.h"
#include <algorithm>
#include <iostream>

TermMusicPlayer::TermMusicPlayer() {
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

TermMusicPlayer::~TermMusicPlayer() {
  if (soundLoaded)
    ma_sound_uninit(&sound);

  if (initialized) {
    ma_node_uninit(&visNode.base, NULL);
    ma_engine_uninit(&engine);
  }
}

bool TermMusicPlayer::play(const std::string &path) {
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

void TermMusicPlayer::stop() {
  if (soundLoaded) {
    ma_sound_stop(&sound);
    ma_sound_seek_to_pcm_frame(&sound, 0);
  }
}

void TermMusicPlayer::togglePause() {
  if (soundLoaded) {
    if (ma_sound_is_playing(&sound)) {
      ma_sound_stop(&sound);
    } else {
      ma_sound_start(&sound);
    }
  }
}

void TermMusicPlayer::changeVolume(float delta) {
  if (!initialized)
    return;
  currentVolume += delta;
  if (currentVolume < 0.0f)
    currentVolume = 0.0f;
  if (currentVolume > 1.0f)
    currentVolume = 1.0f;
  ma_engine_set_volume(&engine, currentVolume);
}

void TermMusicPlayer::seekBy(float delta) {
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

bool TermMusicPlayer::isInit() const { return initialized; }

bool TermMusicPlayer::isLoaded() const { return soundLoaded; }

bool TermMusicPlayer::isPlaying() const {
  return soundLoaded && ma_sound_is_playing(&sound);
}

std::string TermMusicPlayer::getCurrentTitle() const {
  return currentFile.empty() ? "None" : currentFile;
}

float TermMusicPlayer::getVolume() const { return currentVolume; }

float TermMusicPlayer::getCursor() {
  float cursor = 0.0f;
  if (soundLoaded)
    ma_sound_get_cursor_in_seconds(&sound, &cursor);
  return cursor;
}

float TermMusicPlayer::getLength() {
  float length = 0.0f;
  if (soundLoaded)
    ma_sound_get_length_in_seconds(&sound, &length);
  return length;
}

void TermMusicPlayer::getVisData(std::vector<float> &outBars) {
  outBars.resize(NUM_BARS);
  for (int i = 0; i < NUM_BARS; ++i) {
    outBars[i] = visNode.bars[i].load(std::memory_order_relaxed);
  }
}
