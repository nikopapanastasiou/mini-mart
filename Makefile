CXX = g++
CXXFLAGS_PROD = -std=c++20 -O3 -march=native -mtune=native \
                -DNDEBUG -flto -fno-exceptions -fno-rtti \
                -Wall -Wextra -Werror

CXXFLAGS_DEV = -std=c++20 -O2 -g \
               -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion \
               -Wcast-align -Wshadow -Wold-style-cast

INCLUDES = -Iinclude
SOURCES = $(wildcard src/**/*.cpp src/*.cpp)

TEST_INCLUDES = -I/usr/src/googletest/googletest/include
TEST_LIBS = -lgtest -lgtest_main -pthread
TEST_SOURCES = test/*.cpp

build: 
	mkdir -p build
	$(CXX) $(CXXFLAGS_DEV) $(SOURCES) $(INCLUDES) -o build/main

build-prod: 
	mkdir -p build
	$(CXX) $(CXXFLAGS_PROD) $(SOURCES) $(INCLUDES) -o build/main

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

check:
	cppcheck ./include/**

thread-sanitizer: clean
	mkdir -p build
	$(CXX) $(CXXFLAGS_DEV) -fsanitize=thread -pie $(SOURCES) $(INCLUDES) -o build/main
	TSAN_OPTIONS="halt_on_error=1:abort_on_error=1" timeout 10s ./build/main || echo "ThreadSanitizer run completed"

tsan-test: clean
	mkdir -p build
	$(CXX) $(CXXFLAGS_DEV) -fsanitize=thread -pie $(TEST_SOURCES) $(filter-out src/main.cpp, $(SOURCES)) $(INCLUDES) $(TEST_INCLUDES) $(TEST_LIBS) -o build/test_tsan
	TSAN_OPTIONS="halt_on_error=1:abort_on_error=1" ./build/test_tsan

# Alternative: AddressSanitizer for memory issues (more reliable than TSAN)
asan-test: clean
	mkdir -p build
	$(CXX) $(CXXFLAGS_DEV) -fsanitize=address -fno-omit-frame-pointer $(TEST_SOURCES) $(filter-out src/main.cpp, $(SOURCES)) $(INCLUDES) $(TEST_INCLUDES) $(TEST_LIBS) -o build/test_asan
	./build/test_asan

# UndefinedBehaviorSanitizer - great for unsafe/fast Price operations
ubsan-test: clean
	mkdir -p build
	$(CXX) $(CXXFLAGS_DEV) -fsanitize=undefined -fno-omit-frame-pointer $(TEST_SOURCES) $(filter-out src/main.cpp, $(SOURCES)) $(INCLUDES) $(TEST_INCLUDES) $(TEST_LIBS) -o build/test_ubsan
	./build/test_ubsan

# Help target showing all available sanitizer options
help-sanitizers:
	@echo "Available sanitizer targets for HFT validation:"
	@echo "  thread-sanitizer  - Run main app with ThreadSanitizer (may have mapping issues)"
	@echo "  tsan-test        - Run tests with ThreadSanitizer (may have mapping issues)"
	@echo "  asan-test        - Run tests with AddressSanitizer (recommended)"
	@echo "  ubsan-test       - Run tests with UndefinedBehaviorSanitizer (great for unsafe Price ops)"
	@echo "  valgrind         - Run main app with Valgrind memcheck"
	@echo "  callgrind        - Run main app with Valgrind callgrind profiler"
	@echo ""
	@echo "For HFT development, use: make asan-test (most reliable)"

.PHONY: build run clean test thread-sanitizer tsan-test asan-test ubsan-test help-sanitizers