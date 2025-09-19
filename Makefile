CXX = g++
CXXFLAGS = -std=c++20 -Wall -Wextra -O2

INCLUDES = -Iinclude
SOURCES = $(wildcard src/**/*.cpp src/*.cpp)

TEST_INCLUDES = -I/usr/src/googletest/googletest/include
TEST_LIBS = -lgtest -lgtest_main -pthread
TEST_SOURCES = test/*.cpp

build: 
	mkdir -p build
	$(CXX) $(CXXFLAGS) $(SOURCES) $(INCLUDES) -o build/main

run: build
	./build/main

clean:
	rm -rf build

test: clean 
	mkdir -p build
	$(CXX) $(CXXFLAGS) $(TEST_SOURCES) $(filter-out src/main.cpp, $(SOURCES)) $(INCLUDES) $(TEST_INCLUDES) $(TEST_LIBS) -o build/test
	./build/test

valgrind: clean
	mkdir -p build
	$(CXX) $(CXXFLAGS) $(SOURCES) $(INCLUDES) -o build/main
	timeout 5s valgrind --tool=memcheck --leak-check=full ./build/main

callgrind: clean
	mkdir -p build
	$(CXX) $(CXXFLAGS) $(SOURCES) $(INCLUDES) -o build/main
	timeout 5s valgrind --tool=callgrind ./build/main

.PHONY: build run clean test