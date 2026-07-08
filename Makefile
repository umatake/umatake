# Umatake USI Shogi Engine — Makefile

CXX      ?= clang++
TARGET    = umatake
SRCDIR    = src
SOURCES   = $(wildcard $(SRCDIR)/*.cpp)
OBJECTS   = $(SOURCES:.cpp=.o)

ARCH := $(shell uname -m)
ifeq ($(ARCH),arm64)
  ARCHFLAGS = -mcpu=native
else
  ARCHFLAGS = -march=native
endif

CXXFLAGS  = -std=c++17 -O3 -DNDEBUG -flto -Wall -Wextra -Wno-unused-parameter $(ARCHFLAGS)
LDFLAGS   = -flto -pthread

.PHONY: all clean debug perft bench

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CXX) $(OBJECTS) -o $@ $(LDFLAGS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

debug: CXXFLAGS = -std=c++17 -O1 -g -fsanitize=address,undefined -Wall -Wextra -Wno-unused-parameter
debug: LDFLAGS  = -fsanitize=address,undefined -pthread
debug: clean $(TARGET)

perft: all
	./$(TARGET) perft

bench: all
	./$(TARGET) bench

clean:
	rm -f $(SRCDIR)/*.o $(TARGET)
