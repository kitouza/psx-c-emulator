.PHONY: all configure build run debug clean

BUILD_DIR := build
TARGET := $(BUILD_DIR)/psx-emulator

all: build

configure:
	cmake -S . -B $(BUILD_DIR)

build: configure
	cmake --build $(BUILD_DIR)

run: build
	./$(TARGET) $(ARGS)

debug: build
	./$(TARGET) --debug-ui

clean:
	cmake -E remove_directory $(BUILD_DIR)
