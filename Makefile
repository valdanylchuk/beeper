# beeper — subtractive synth TUI for BreezyBox.
# Targets: host (Mac/SDL2), web (wasm + xterm.js, for itch.io),
# ESP32 ELFs via ./buildelf.sh. Sources stay plain C99 for all three.
CC      ?= cc
CFLAGS  ?= -std=c99 -Os -Wall -Wextra
AR      ?= ar

BUILD    := build
TUI_SRCS := $(wildcard tuilib/*.c)
APP_SRCS := $(wildcard src/*.c)
HDRS     := $(wildcard tuilib/*.h src/*.h)
HOST_SDL := host/host_sdl.c

SDL_CFLAGS := $(shell sdl2-config --cflags 2>/dev/null)
SDL_LIBS   := $(shell sdl2-config --libs 2>/dev/null)

.PHONY: all run web serve itch elf clean

all: $(BUILD)/beeper

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/beeper: $(TUI_SRCS) $(APP_SRCS) $(HDRS) $(HOST_SDL) | $(BUILD)
	$(CC) $(CFLAGS) -Ituilib -Isrc $(SDL_CFLAGS) \
	    $(TUI_SRCS) $(APP_SRCS) $(HOST_SDL) $(SDL_LIBS) -o $@

run: $(BUILD)/beeper
	./$(BUILD)/beeper

# --- wasm build for itch.io (needs emsdk's emcc on PATH) ---
# Output is fully self-contained: xterm.js is vendored, no CDN.
WEB_DIR := $(BUILD)/web

web: $(WEB_DIR)/index.html

$(WEB_DIR)/index.html: $(TUI_SRCS) $(APP_SRCS) $(HDRS) $(HOST_SDL) \
                       web/index.html
	mkdir -p $(WEB_DIR)/vendor
	emcc -std=c99 -O2 -Ituilib -Isrc \
	    $(TUI_SRCS) $(APP_SRCS) $(HOST_SDL) \
	    -sUSE_SDL=2 -sENVIRONMENT=web -sALLOW_MEMORY_GROWTH \
	    -sEXPORTED_FUNCTIONS=_main,_tui_web_push_byte \
	    -o $(WEB_DIR)/beeper.js
	cp web/index.html $(WEB_DIR)/
	cp web/vendor/xterm.min.js web/vendor/xterm.min.css $(WEB_DIR)/vendor/

serve: web
	cd $(WEB_DIR) && python3 -m http.server 8000

itch: web
	cd $(WEB_DIR) && rm -f ../beeper-web.zip && \
	    zip -r ../beeper-web.zip index.html beeper.js beeper.wasm vendor
	@echo "upload $(BUILD)/beeper-web.zip to itch.io (HTML5 project)"

# --- ESP32-S3 / P4 loadable ELFs ---
elf:
	./buildelf.sh

clean:
	rm -rf $(BUILD)
