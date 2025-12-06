#ifndef MUSIC_PLAYER_H
#define MUSIC_PLAYER_H

#include "VisualizerNode.h"
#include "miniaudio.h"
#include <string>
#include <vector>

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
  TermMusicPlayer();
  ~TermMusicPlayer();

  bool play(const std::string &path);
  void stop();
  void togglePause();
  void changeVolume(float delta);
  void seekBy(float delta);

  bool isInit() const;
  bool isLoaded() const;
  bool isPlaying() const;
  std::string getCurrentTitle() const;
  float getVolume() const;
  float getCursor();
  float getLength();

  // Vis Data
  void getVisData(std::vector<float> &outBars);
};

#endif // MUSIC_PLAYER_H
