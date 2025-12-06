CXX = c++
CXXFLAGS = -std=c++17 -Wall

# Detect OS
UNAME_S := $(shell uname -s)

# Linux Flags
ifeq ($(UNAME_S),Linux)
    LDFLAGS += -ldl -lpthread -lm
endif

# macOS Flags
ifeq ($(UNAME_S),Darwin)
    # Miniaudio usually handles this via pragmas, but explicit is safe
    LDFLAGS += -framework CoreAudio -framework AudioToolbox -framework CoreFoundation
endif

TARGET = music_player
SRC = main.cpp FftUtils.cpp TerminalUtils.cpp VisualizerNode.cpp TUI.cpp MusicPlayer.cpp

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) $(SRC) -o $(TARGET) $(LDFLAGS)

clean:
	rm -f $(TARGET)

.PHONY: clean
