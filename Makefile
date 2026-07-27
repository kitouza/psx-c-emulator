.PHONY: all configure build run clean

BUILD_DIR := build
TARGET := $(BUILD_DIR)/psx-emulator

all: build

configure:
	cmake -S . -B $(BUILD_DIR)

build: configure
	cmake --build $(BUILD_DIR)

run: build
	./$(TARGET)

clean:
	cmake -E remove_directory $(BUILD_DIR)
