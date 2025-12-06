#ifndef TUI_H
#define TUI_H

#include <iostream>
#include <string>
#include <vector>

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

std::string formatTime(float seconds);
std::string drawProgressBar(float current, float total, int width);
std::string drawVolumeBar(float volume, int width);
void drawVisualizer(std::ostream &out, const std::vector<float> &bars,
                    int height);

#endif // TUI_H
