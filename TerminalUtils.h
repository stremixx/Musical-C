#ifndef TERMINAL_UTILS_H
#define TERMINAL_UTILS_H

void enableRawMode();
void disableRawMode();
void clearScreen();
int kbhit();
void getTermSize(int &rows, int &cols);

#endif // TERMINAL_UTILS_H
