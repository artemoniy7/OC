CXX ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -pedantic
QEMU ?= qemu-system-i386
BUILD_DIR := build
IMAGE := $(BUILD_DIR)/os.img
BUILDER := $(BUILD_DIR)/build_image

.PHONY: all run clean

all: $(IMAGE)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILDER): src/build_image.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $< -o $@

$(IMAGE): $(BUILDER)
	$(BUILDER) $@

run: $(IMAGE)
	$(QEMU) -drive format=raw,file=$(IMAGE)

clean:
	rm -rf $(BUILD_DIR)
