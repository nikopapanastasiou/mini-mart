CXX = g++
CXXFLAGS = -std=c++20

INCLUDES = -Iinclude
SOURCES = src/*.cpp

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
	$(CXX) $(CXXFLAGS) $(TEST_SOURCES) $(INCLUDES) $(TEST_INCLUDES) $(TEST_LIBS) -o build/test
	./build/test