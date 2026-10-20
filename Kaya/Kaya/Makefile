CC ?= cc
CFLAGS ?= -Wall -Wextra -O2 -std=c11
SRC_DIR = src
BUILD_DIR = build
PREFIX ?= /usr/local

COMMON = $(SRC_DIR)/json.c $(SRC_DIR)/tar.c $(SRC_DIR)/manifest.c $(SRC_DIR)/setup.c

.PHONY: all clean install uninstall

all: $(BUILD_DIR)/kaya $(BUILD_DIR)/mkkay

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/kaya: $(SRC_DIR)/kaya.c $(SRC_DIR)/sandbox.c $(COMMON) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $^

$(BUILD_DIR)/mkkay: $(SRC_DIR)/mkkay.c $(COMMON) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $^

install: all
	mkdir -p $(PREFIX)/bin
	cp $(BUILD_DIR)/kaya $(PREFIX)/bin/kaya
	cp $(BUILD_DIR)/mkkay $(PREFIX)/bin/mkkay
	@echo "Kuruldu: $(PREFIX)/bin/kaya, $(PREFIX)/bin/mkkay"

uninstall:
	rm -f $(PREFIX)/bin/kaya $(PREFIX)/bin/mkkay

clean:
	rm -rf $(BUILD_DIR)
