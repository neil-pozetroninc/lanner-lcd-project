CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra
LDFLAGS ?=

SRC_DIR := programs
BUILD_DIR := build

BINARIES := $(BUILD_DIR)/lcd_vitals $(BUILD_DIR)/lcd_button_daemon

TARGET ?=
# Default deployment directory (override with TARGET_DIR=/your/path when deploying)
TARGET_DIR ?= ~/lcd_vitals_deploy
SCP ?= scp
SSH ?= ssh

.PHONY: all deploy remote-install update clean driver

all: $(BINARIES)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/lcd_vitals: $(SRC_DIR)/lcd_vitals_multistate.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $<

$(BUILD_DIR)/lcd_button_daemon: $(SRC_DIR)/lcd_daemon_multistate.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $<

deploy: all
	@if [ -z "$(TARGET)" ]; then echo "Set TARGET=user@host (or IP) for deploy"; exit 1; fi
	$(SSH) $(TARGET) "mkdir -p $(TARGET_DIR)"
	$(SCP) $(BINARIES) $(TARGET):$(TARGET_DIR)/

remote-install: deploy
	$(SSH) $(TARGET) "sudo install -m 0755 $(TARGET_DIR)/lcd_vitals /usr/local/bin/lcd_vitals && sudo install -m 0755 $(TARGET_DIR)/lcd_button_daemon /usr/local/bin/lcd_button_daemon"

update: deploy
	@echo "Stopping LCD daemon on $(TARGET)..."
	$(SSH) $(TARGET) "sudo systemctl stop lcd-button-daemon.service"
	@echo "Installing updated binaries..."
	$(SSH) $(TARGET) "sudo install -m 0755 $(TARGET_DIR)/lcd_vitals /usr/local/bin/lcd_vitals && sudo install -m 0755 $(TARGET_DIR)/lcd_button_daemon /usr/local/bin/lcd_button_daemon"
	@echo "Starting LCD daemon..."
	$(SSH) $(TARGET) "sudo systemctl start lcd-button-daemon.service"
	@echo "Waiting for daemon to initialize..."
	@sleep 2
	@echo "Verifying daemon status..."
	$(SSH) $(TARGET) "sudo systemctl status lcd-button-daemon.service --no-pager"

driver:
	$(MAKE) -C driver boot

clean:
	rm -rf $(BUILD_DIR)
