CXX = c++
CXXFLAGS = -std=c++17 -Wall

TARGET = music_player
SRC = main.cpp

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) $(SRC) -o $(TARGET)

clean:
	rm -f $(TARGET)

.PHONY: clean
