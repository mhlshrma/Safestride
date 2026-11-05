CXX = clang++
CXXFLAGS = -std=c++17 -Wall -Wextra -O2
TARGET = hit2help_sim
SRC = hit2help_sim.cpp

all: $(TARGET)

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(SRC)

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)

.PHONY: all run clean
