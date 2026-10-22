# Unified Makefile for building examples with arduino-cli

# Board/toolchain
FQBN        ?= esp32:esp32:esp32s3
PORT        ?= /dev/ttyACM0
BAUD        ?= 115200
ARDUINO_CLI ?= arduino-cli
PYTHON      ?= python3

VERSION_FILE        := $(CURDIR)/VERSION
VERSION_SYNC_SCRIPT := $(CURDIR)/tools/sync_version.py
UI_GEN_SCRIPT := $(CURDIR)/tools/gen_ui.py

# Examples
EXAMPLES_DIR   := $(CURDIR)/examples
EXAMPLE_SKETCHES := $(shell find $(EXAMPLES_DIR) -maxdepth 3 -mindepth 1 -name "*.ino")
EXAMPLES_LIST  := $(sort $(patsubst %/,%,$(patsubst $(EXAMPLES_DIR)/%,%,$(dir $(EXAMPLE_SKETCHES)))))
EXAMPLES_LIST  := $(filter-out __library_impl __library_impl/%,$(EXAMPLES_LIST))
DEFAULT_EXAMPLE := $(firstword $(EXAMPLES_LIST))
EXAMPLE        ?= $(DEFAULT_EXAMPLE)
BUILD_DIR_BASE := $(CURDIR)/.build

TEST_STUB_DIR := $(CURDIR)/tests/stubs
TEST_SOURCES := $(filter %.cpp,$(wildcard $(CURDIR)/tests/*.cpp))
TEST_EXTRA_SOURCES :=

# Common flags
EXTRA_FLAGS_COMMON := -DESP32S3 -DARDUINO_USB_MODE=1

# Example-specific extra flags (function)
example_flags = $(if $(filter LoopPeriodStress,$(notdir $(1))),-DPROBOT_CLM_NOLOG=1 -DPROBOT_SCHED_NOLOG=1,)

.PHONY: all build build-all _build_single upload clean boards libs serial list help test tests/control_tests ui

all: help

help:
	@echo "Usage: make build EXAMPLE=<Name|all>"
	@echo "Examples directory: $(EXAMPLES_DIR)"
	@echo "Targets: build, build-all, upload, serial, clean, list, ui"
	@echo "Build artifacts: $(BUILD_DIR_BASE)/<Example>"

list:
ifeq ($(EXAMPLES_LIST),)
	@echo "(no examples found)"
else
	@for ex in $(EXAMPLES_LIST); do echo $$ex; done
endif

build:
ifeq ($(EXAMPLE),all)
	@set -e; \
	for ex in $(EXAMPLES_LIST); do \
	  $(MAKE) --no-print-directory _build_single EXAMPLE=$$ex; \
	done
else
	@$(MAKE) --no-print-directory _build_single EXAMPLE=$(EXAMPLE)
endif

build-all:
	@$(MAKE) build EXAMPLE=all

_build_single:
	@echo "==> Building $(EXAMPLE)"
	$(PYTHON) $(VERSION_SYNC_SCRIPT)
	$(PYTHON) $(UI_GEN_SCRIPT)
	$(ARDUINO_CLI) compile --fqbn $(FQBN) --warnings all \
	  --library $(CURDIR) \
	  --build-path $(BUILD_DIR_BASE)/$(EXAMPLE) \
	  --build-property compiler.cpp.extra_flags="$(EXTRA_FLAGS_COMMON) $(call example_flags,$(EXAMPLE))" \
	  --build-property compiler.c.extra_flags="$(EXTRA_FLAGS_COMMON) $(call example_flags,$(EXAMPLE))" \
	  $(EXAMPLES_DIR)/$(EXAMPLE)

upload: build
	$(ARDUINO_CLI) upload -p $(PORT) --fqbn $(FQBN) $(EXAMPLES_DIR)/$(EXAMPLE)

serial:
	arduino-cli monitor -p $(PORT) --config $(BAUD)

clean:
	rm -rf $(BUILD_DIR_BASE)
	-$(ARDUINO_CLI) cache clean >/dev/null 2>&1 || true
	rm -f tests/control_tests

boards:
	arduino-cli board list

libs:
	arduino-cli lib install "Adafruit NeoPixel" 

version-sync:
	$(PYTHON) $(VERSION_SYNC_SCRIPT)

ui:
	$(PYTHON) $(UI_GEN_SCRIPT)

tests/control_tests: $(TEST_SOURCES)
	g++ -std=c++17 -Wall -Wextra -pedantic -I src -I $(TEST_STUB_DIR) -DPROBOT_CLM_NOLOG=1 -DPROBOT_SCHED_NOLOG=1 -DPROBOT_LOGGER_NO_SCHED_ATTACH=1 -DPROBOT_BUILTINLED_EXTERNAL=1 -o $@ $(TEST_SOURCES) $(TEST_EXTRA_SOURCES)

test: build tests/control_tests
	./tests/control_tests
