# Builds ./openabyss from src/: C11 and SDL 3 (pkg-config sdl3).

CC      ?= cc
# GNU Make can mistake Emscripten's cmake/ directory on PATH for a program.
# Let the shell resolve the executable and use its full path in both steps.
CMAKE   ?= $(shell command -v cmake)
CFLAGS  ?= -std=c11 -O2 -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wno-sign-conversion
SDL3_CFLAGS := $(shell pkg-config --cflags sdl3 2>/dev/null)
SDL3_LIBS   := $(shell pkg-config --libs sdl3 2>/dev/null)
SRC     := $(wildcard src/uw_*.c) src/tools/uwshell.c
HEADERS := $(wildcard src/*.h) src/tools/uwshell.h
NAMES   := -DUW_PROGRAM='"openabyss"' -DUW_WINDOW_TITLE='"OpenAbyss"' -DUW_PREF_DIR='"openabyss"'

openabyss: $(SRC) $(HEADERS)
	@if [ -z "$(SDL3_LIBS)" ]; then echo "make: pkg-config finds no sdl3"; exit 1; fi
	$(CC) $(CFLAGS) -DUW_NO_HARNESS -DUW_NO_ACCOUNTING $(NAMES) $(SDL3_CFLAGS) $(SRC) -lm $(SDL3_LIBS) -o $@

clean:
	rm -f openabyss

# Requires an activated Emscripten SDK (emcmake on PATH).
wasm:
	emcmake "$(CMAKE)" -S . -B build/wasm -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
	"$(CMAKE)" --build build/wasm --target wasm --parallel

serve-wasm:
	python3 -m http.server 8080 --directory dist/web --bind 127.0.0.1

.PHONY: clean wasm serve-wasm
