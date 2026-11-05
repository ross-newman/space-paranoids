# A front for CMake, which does the real work in build/
BUILD = build
JOBS = $(shell nproc)

all: $(BUILD)/Makefile
	cmake --build $(BUILD) -j$(JOBS)

$(BUILD)/Makefile: CMakeLists.txt engine/CMakeLists.txt
	cmake -S . -B $(BUILD)

run: all
	./$(BUILD)/space-paranoids

# Every check runs without a display, and its pictures stay in the build directory
test: all
	SDL_VIDEODRIVER=dummy ./$(BUILD)/pad_test
	./$(BUILD)/maze_preview $(BUILD)/maze_check.png
	./$(BUILD)/terminal_preview $(BUILD)/terminal_check.png
	./$(BUILD)/vehicle_preview $(BUILD)/vehicle_check.png
	./$(BUILD)/score_check $(BUILD)

clean:
	rm -rf $(BUILD)

.PHONY: all run test clean
