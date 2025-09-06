# ESP32-S3 - Advanced Makefile
# =========================================
# Usage: make <target> BOARD=<board> [options]
# Example: make build BOARD=n16r8cam

# Configuration
BOARD ?= freenove
BAUD ?= 115200

# Auto-detect serial ports if PORT not specified
ifeq ($(PORT),)
    # Try to detect available serial ports (exclude Bluetooth and debug)
    DETECTED_PORTS := $(shell ls /dev/tty.* /dev/ttyUSB* /dev/ttyACM* 2>/dev/null | grep -v -i bluetooth | grep -v debug-console | grep -E '(usbserial|usbmodem|USB|ACM)')
    PORT_COUNT := $(words $(DETECTED_PORTS))
    
    ifeq ($(PORT_COUNT),0)
        PORT := AUTO_DETECT_FAILED  # Will show helpful message
    else ifeq ($(PORT_COUNT),1)
        PORT := $(DETECTED_PORTS)  # Use the only available port
    else
        # Multiple ports detected, will prompt user
        PORT := ASK
    endif
else
    # User specified PORT explicitly
    PORT := $(PORT)
endif

# Validate board selection
VALID_BOARDS := freenove n16r8cam
ifeq ($(filter $(BOARD),$(VALID_BOARDS)),)
    $(error Invalid BOARD='$(BOARD)'. Valid options: $(VALID_BOARDS))
endif

# Map short names to full config names
ifeq ($(BOARD),freenove)
    BOARD_CONFIG := freenove
    BOARD_FULL_NAME := FREENOVE_ESP32_S3_WROOM
else ifeq ($(BOARD),n16r8cam)
    BOARD_CONFIG := n16r8cam
    BOARD_FULL_NAME := ESP32_S3_N16R8_CAM

endif

# Build configuration chains
CONFIG_BASE := sdkconfig.defaults;sdkconfig.defaults.esp32s3;sdkconfig.defaults.esp32s3.$(BOARD_CONFIG)
CONFIG_PROD := $(CONFIG_BASE);sdkconfig.production

# Colors for output (disable with NO_COLOR=1)
ifndef NO_COLOR
    RED := \033[0;31m
    GREEN := \033[0;32m
    YELLOW := \033[1;33m
    BLUE := \033[0;34m
    MAGENTA := \033[0;35m
    CYAN := \033[0;36m
    NC := \033[0m
endif

# Default target
.DEFAULT_GOAL := help

# ==============================================================================
# HELP & DOCUMENTATION
# ==============================================================================
.PHONY: help
help:
	@echo "$(CYAN)╔══════════════════════════════════════════════════════════╗$(NC)"
	@echo "$(CYAN)║         ESP32-S3 - Build System              ║$(NC)"
	@echo "$(CYAN)╚══════════════════════════════════════════════════════════╝$(NC)"
	@echo ""
	@echo "$(GREEN)USAGE:$(NC)"
	@echo "  make <target> BOARD=<board> [PORT=<port>]"
	@echo ""
	@echo "$(GREEN)MAIN TARGETS:$(NC)"
	@echo "  $(YELLOW)build$(NC)        Build firmware (development mode)"
	@echo "  $(YELLOW)prod$(NC)         Build firmware (production mode)"
	@echo "  $(YELLOW)flash$(NC)        Flash firmware to device"
	@echo "  $(YELLOW)monitor$(NC)      Open serial monitor"
	@echo "  $(YELLOW)all$(NC)          Build + Flash + Monitor"
	@echo ""
	@echo "$(GREEN)CONFIGURATION:$(NC)"
	@echo "  $(YELLOW)menuconfig$(NC)   Open configuration menu"
	@echo "  $(YELLOW)info$(NC)         Show current configuration"
	@echo ""
	@echo "$(GREEN)MAINTENANCE:$(NC)"
	@echo "  $(YELLOW)clean$(NC)        Clean build files"
	@echo "  $(YELLOW)erase$(NC)        Erase entire flash"
	@echo "  $(YELLOW)size$(NC)         Show binary size analysis"
	@echo "  $(YELLOW)ports$(NC)        List available serial ports"
	@echo ""
	@echo "$(GREEN)BOARDS:$(NC)"
	@echo "  $(CYAN)freenove$(NC)     FREENOVE_ESP32_S3_WROOM (8MB Flash, Octal PSRAM)"
	@echo "  $(CYAN)n16r8cam$(NC)     ESP32_S3_N16R8_CAM (16MB Flash, Quad PSRAM)"
	@echo ""
	@echo "$(GREEN)OPTIONS:$(NC)"
	@echo "  $(CYAN)BOARD$(NC)        Target board [$(VALID_BOARDS)] (default: $(BOARD))"
	@echo "  $(CYAN)PORT$(NC)         Serial port (default: $(PORT))"
	@echo "  $(CYAN)BAUD$(NC)         Baud rate (default: $(BAUD))"
	@echo "  $(CYAN)NO_COLOR$(NC)     Disable colored output (NO_COLOR=1)"
	@echo ""
	@echo "$(GREEN)EXAMPLES:$(NC)"
	@echo "  make build BOARD=freenove"
	@echo "  make prod BOARD=n16r8cam"
	@echo "  make flash PORT=/dev/ttyACM0"
	@echo "  make all BOARD=n16r8cam PORT=/dev/ttyUSB1"

# ==============================================================================
# BUILD TARGETS
# ==============================================================================

# Development build
.PHONY: build
build: _print_board_info
	@echo "$(GREEN)► Building $(BOARD_FULL_NAME) [DEVELOPMENT]$(NC)"
	@echo "$(BLUE)  Config chain: base → esp32s3 → $(BOARD_CONFIG)$(NC)"
	@idf.py -DSDKCONFIG_DEFAULTS="$(CONFIG_BASE)" build

# Production build
.PHONY: prod production
prod production: _check_production_config _print_board_info
	@echo "$(YELLOW)► Building $(BOARD_FULL_NAME) [PRODUCTION]$(NC)"
	@echo "$(MAGENTA)  ⚠️  Using production optimizations$(NC)"
	@echo "$(BLUE)  Config chain: base → esp32s3 → $(BOARD_CONFIG) → production$(NC)"
	@echo "$(BLUE)  Partition: $(PARTITION_TABLE)$(NC)"
	@idf.py -DSDKCONFIG_DEFAULTS="$(CONFIG_PROD)" build
	@echo "$(GREEN)✓ Production build complete$(NC)"

# ==============================================================================
# FLASH & MONITOR TARGETS
# ==============================================================================

# Flash firmware
.PHONY: flash
flash: _select_port
	@echo "$(GREEN)► Flashing to $(SELECTED_PORT)$(NC)"
	@idf.py -p $(SELECTED_PORT) flash

# Monitor serial output
.PHONY: monitor
monitor: _select_port
	@echo "$(GREEN)► Opening monitor on $(SELECTED_PORT) @ $(BAUD) baud$(NC)"
	@idf.py -p $(SELECTED_PORT) -b $(BAUD) monitor

# Flash and monitor
.PHONY: flash-monitor fm
flash-monitor fm: flash monitor

# Complete development cycle
.PHONY: all
all: build flash monitor

# ==============================================================================
# CONFIGURATION TARGETS
# ==============================================================================

# Open menuconfig
.PHONY: menuconfig config
menuconfig config:
	@echo "$(BLUE)► Opening configuration menu for $(BOARD_FULL_NAME)$(NC)"
	@idf.py menuconfig

# Show current configuration
.PHONY: info
info: _print_board_info
	@echo "$(CYAN)Current Configuration:$(NC)"
	@echo "  Board:       $(BOARD_FULL_NAME)"
	@echo "  Config:      $(BOARD_CONFIG)"
	@echo "  Port:        $(PORT)"
	@echo "  Partition:   $(PARTITION_TABLE)"
	@echo ""
	@echo "$(CYAN)Build Configs:$(NC)"
	@echo "  Development: $(CONFIG_BASE)"
	@echo "  Production:  $(CONFIG_PROD)"

# ==============================================================================
# MAINTENANCE TARGETS
# ==============================================================================

# Clean build
.PHONY: clean
clean:
	@echo "$(YELLOW)► Cleaning build files$(NC)"
	@idf.py fullclean
	@rm -f .port_selected
	@echo "$(GREEN)✓ Clean complete$(NC)"

# Erase flash
.PHONY: erase erase-flash
erase erase-flash: _select_port
	@echo "$(RED)⚠️  ERASING ENTIRE FLASH on $(SELECTED_PORT)$(NC)"
	@echo "$(RED)   This will delete all data including WiFi credentials$(NC)"
	@read -p "   Continue? [y/N]: " confirm && [ "$$confirm" = "y" ] || exit 1
	@idf.py -p $(SELECTED_PORT) erase-flash
	@echo "$(GREEN)✓ Flash erased$(NC)"

# Show size analysis
.PHONY: size
size:
	@echo "$(CYAN)► Binary Size Analysis$(NC)"
	@idf.py size
	@echo ""
	@idf.py size-components

# ==============================================================================
# ADVANCED TARGETS
# ==============================================================================

# Create production release
.PHONY: release
release: prod
	@echo "$(MAGENTA)► Creating release package$(NC)"
	@mkdir -p releases
	@cp build/*.bin releases/$(BOARD)_$(shell date +%Y%m%d_%H%M%S).bin
	@echo "$(GREEN)✓ Release created in releases/$(NC)"

# Run unit tests
.PHONY: test
test:
	@echo "$(CYAN)► Running unit tests$(NC)"
	@idf.py -T all build

# Open documentation
.PHONY: docs
docs:
	@echo "$(CYAN)► Opening documentation$(NC)"
	@python -m webbrowser "https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/"

# ==============================================================================
# CI/CD TARGETS (no colors, parseable output)
# ==============================================================================

.PHONY: ci-build
ci-build:
	@echo "CI: Building $(BOARD_FULL_NAME)"
	@idf.py -DSDKCONFIG_DEFAULTS="$(CONFIG_BASE)" build

.PHONY: ci-prod
ci-prod:
	@echo "CI: Production build for $(BOARD_FULL_NAME)"
	@idf.py -DSDKCONFIG_DEFAULTS="$(CONFIG_PROD)" build

# ==============================================================================
# INTERNAL HELPERS (prefix with _)
# ==============================================================================

.PHONY: _print_board_info
_print_board_info:
	@echo "$(CYAN)╭─────────────────────────────────────╮$(NC)"
	@echo "$(CYAN)│ Board: $(BOARD_FULL_NAME)$(NC)"
	@echo "$(CYAN)╰─────────────────────────────────────╯$(NC)"

.PHONY: _check_production_config
_check_production_config:
	@if grep -q "PRODUCTION_WIFI_SSID" sdkconfig.production 2>/dev/null; then \
		echo "$(RED)⚠️  WARNING: WiFi credentials not updated in sdkconfig.production$(NC)"; \
		echo "$(YELLOW)   Update CONFIG_AG_WIFI_SSID and CONFIG_AG_WIFI_PASSWORD$(NC)"; \
		read -p "   Continue anyway? [y/N]: " confirm && [ "$$confirm" = "y" ] || exit 1; \
	fi

# Port selection helper
.PHONY: _select_port
_select_port:
ifeq ($(PORT),ASK)
	@echo "$(CYAN)► Multiple serial ports detected:$(NC)"
	@echo ""
	@n=1; \
	for port in $(DETECTED_PORTS); do \
		echo "  $$n) $$port"; \
		n=$$((n+1)); \
	done; \
	echo ""; \
	read -p "Select port number [1-$(PORT_COUNT)]: " port_num; \
	if [ -z "$$port_num" ]; then \
		port_num=1; \
	fi; \
	selected=$$(echo "$(DETECTED_PORTS)" | tr ' ' '\n' | sed -n "$${port_num}p"); \
	if [ -z "$$selected" ]; then \
		echo "$(RED)Invalid selection$(NC)"; \
		exit 1; \
	fi; \
	echo "$(GREEN)✓ Selected: $$selected$(NC)"; \
	echo "SELECTED_PORT=$$selected" > .port_selected; \
	$(eval SELECTED_PORT := $$(shell cat .port_selected | cut -d= -f2))
else ifeq ($(PORT),AUTO_DETECT_FAILED)
	@echo "$(YELLOW)⚠️  No ESP32 serial ports detected$(NC)"
	@echo ""
	@echo "$(CYAN)Common port names:$(NC)"
	@echo "  • /dev/ttyUSB0 (Linux)"
	@echo "  • /dev/ttyACM0 (Linux alternative)"
	@echo "  • /dev/tty.usbserial-XXXXX (macOS)"
	@echo "  • /dev/tty.wchusbserialXXXX (macOS CH340)"
	@echo "  • COM3, COM4... (Windows)"
	@echo ""
	@echo "$(YELLOW)Please specify the port manually:$(NC)"
	@echo "  make flash PORT=/dev/your_port"
	@exit 1
else
	$(eval SELECTED_PORT := $(PORT))
endif

# List available ports
.PHONY: ports
ports:
	@echo "$(CYAN)► Available serial ports:$(NC)"
	@if [ -z "$(DETECTED_PORTS)" ]; then \
		echo "  $(YELLOW)No ports detected$(NC)"; \
		echo ""; \
		echo "  Possible ports to try:"; \
		echo "    - /dev/ttyUSB0 (Linux)"; \
		echo "    - /dev/ttyACM0 (Linux alternative)"; \
		echo "    - /dev/tty.usbserial-* (macOS)"; \
		echo "    - COM3, COM4... (Windows)"; \
	else \
		for port in $(DETECTED_PORTS); do \
			echo "  • $$port"; \
		done; \
	fi

# ==============================================================================
# SHORTHAND ALIASES
# ==============================================================================

.PHONY: b f m c
b: build      # Shorthand for build
f: flash      # Shorthand for flash
m: monitor    # Shorthand for monitor
c: clean      # Shorthand for clean

# Catch-all for invalid targets
%:
	@echo "$(RED)Error: Unknown target '$@'$(NC)"
	@echo "Run 'make help' to see available targets"
	@exit 1