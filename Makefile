# Makefile for cardputer_asv_mp3 (M5Stack Cardputer-ADV MP3/WAV player)
# Wraps PlatformIO commands. Override the launcher if system Python is 3.14+:
#   make build PIO="python3.12 -m platformio"

.PHONY: all build upload flash flash-nostub flash-bootloader clean monitor test test-native size help

# Prefer a PlatformIO-supported Python when plain `pio` is on 3.14+
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

build: ## Build device firmware (cardputer-adv)
	$(PIO) run -e $(ENV_DEVICE)

upload: ## Build and flash firmware
	$(PIO) run -e $(ENV_DEVICE) --target upload

flash: upload ## Alias for upload

flash-nostub: ## Flash without ROM stub (if ACM drops after "Stub running")
	$(PIO) run -e cardputer-adv-nostub --target upload

flash-bootloader: ## Flash after manual download mode (see help)
	@echo "1) Power OFF"
	@echo "2) Hold G0, power ON, release G0"
	@echo "3) Confirm /dev/ttyACM0 exists, then Enter"
	@read _
	$(PIO) run -e $(ENV_DEVICE) --target upload --upload-port /dev/ttyACM0

clean: ## Clean PlatformIO build artifacts
	$(PIO) run -e $(ENV_DEVICE) --target clean

monitor: ## Open serial monitor (115200)
	$(PIO) device monitor -b 115200

test: test-native ## Run host unit tests

test-native: ## Run native (host) Unity tests
	$(PIO) test -e $(ENV_NATIVE)

size: ## Show firmware size
	$(PIO) run -e $(ENV_DEVICE) --target size

help: ## Show this help
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
