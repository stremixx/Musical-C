#include "TerminalUtils.h"

#include <iostream>
#include <stdlib.h> // for atexit
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

struct termios orig_termios;

void disableRawMode() {
  tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
  std::cout << "\033[?25h"; // Show cursor
}

void clearScreen() { std::cout << "\033[2J\033[H"; }

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
