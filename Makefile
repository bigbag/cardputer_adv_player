# Build the M5Stack Cardputer-ADV MP3/WAV player with PlatformIO.
# Select another launcher if the system uses Python 3.14 or later.
# Example: make build PIO="python3.12 -m platformio"

.PHONY: all build upload flash flash-nostub flash-bootloader clean monitor test test-native size help

# Select a Python version that PlatformIO supports.
PIO ?= $(shell \
  if command -v pio >/dev/null 2>&1 && pio system info 2>/dev/null | grep -qE 'Python[[:space:]]+3\.(1[0-3])\.'; then \
    echo pio; \
  elif command -v python3.12 >/dev/null 2>&1; then \
    echo "python3.12 -m platformio"; \
  elif command -v python3.11 >/dev/null 2>&1; then \
    echo "python3.11 -m platformio"; \
  elif command -v python3.13 >/dev/null 2>&1; then \
    echo "python3.13 -m platformio"; \
  else \
    echo pio; \
  fi)

ENV_DEVICE ?= cardputer-adv
ENV_NATIVE ?= native

all: help

build: ## Build device firmware for cardputer-adv.
	$(PIO) run -e $(ENV_DEVICE)

upload: ## Build firmware and upload it to the device.
	$(PIO) run -e $(ENV_DEVICE) --target upload

flash: upload ## Run the upload target.

# Use this target if USB-JTAG disconnects after "Stub running".
flash-nostub: ## Upload without the esptool stub.
	$(PIO) run -e cardputer-adv-nostub --target upload

flash-bootloader: ## Upload after you manually select download mode.
	@echo "1) Power OFF"
	@echo "2) Hold G0, power ON, release G0"
	@echo "3) Confirm /dev/ttyACM0 exists, then Enter"
	@read _
	$(PIO) run -e $(ENV_DEVICE) --target upload --upload-port /dev/ttyACM0

clean: ## Remove PlatformIO build output.
	$(PIO) run -e $(ENV_DEVICE) --target clean

monitor: ## Open the serial monitor at 115200 baud.
	$(PIO) device monitor -b 115200

test: test-native ## Run the host unit tests.

test-native: ## Run the native Unity tests.
	$(PIO) test -e $(ENV_NATIVE)

size: ## Show firmware size.
	$(PIO) run -e $(ENV_DEVICE) --target size

help: ## Show command help.
	@echo "cardputer_asv_mp3 — Cardputer-ADV MP3/WAV player"
	@echo "  PIO launcher: $(PIO)"
	@echo "  Device env:   $(ENV_DEVICE)"
	@echo ""
	@echo "Usage: make [target] [PIO=\"python3.12 -m platformio\"]"
	@echo ""
	@awk 'BEGIN {FS = ":.*##"} /^[a-zA-Z_-]+:.*##/ { printf "  \033[36m%-12s\033[0m %s\n", $$1, $$2 }' $(MAKEFILE_LIST)
	@echo ""
	@echo "Download mode (if upload fails): power OFF → hold G0 → power ON → release → make flash"
	@echo ""
