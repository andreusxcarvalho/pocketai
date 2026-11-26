# Host-side unit tests for firmware/lib/pocket_core (no hardware, no PlatformIO needed).
#   make -C firmware/test/host
CXX ?= g++
CXXFLAGS ?= -std=c++17 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined
CORE := ../../lib/pocket_core/src
SRCS := $(wildcard $(CORE)/*.cpp) test_audio.cpp test_logic.cpp

all: run

pocket_tests: $(SRCS) $(wildcard $(CORE)/*.h) minitest.h
	$(CXX) $(CXXFLAGS) -I$(CORE) -I. $(SRCS) -o $@ -lm

run: pocket_tests
	./pocket_tests

clean:
	rm -f pocket_tests

.PHONY: all run clean
