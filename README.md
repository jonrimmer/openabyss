# OpenAbyss

A reimplementation of *Ultima Underworld: The Stygian Abyss* (1992) in
portable C11: the title, the menu, character generation, the whole game,
its cutscenes, music and saves, played the way the original plays them.

**This repository contains no part of the game.** You need your own copy.
OpenAbyss is made and tested against **the GOG release** of Ultima
Underworld; other releases may work, and are untested -- the program says
so when it meets one, and a report of how it went is welcome.

## Getting it

**Windows, Linux and macOS builds** are on the releases page: a zip for Windows
(unpack it anywhere and run `openabyss.exe`) and an AppImage for Linux
(make it executable and run it), and a zip containing `OpenAbyss.app` for
macOS 11 or later, on Intel and Apple Silicon. Unzip the macOS build and
drag the app to Applications. The app carries SDL with it. It is not
signed with an Apple Developer ID or notarized; if macOS blocks opening
it, allow it in System Settings > Privacy & Security after trying to open it.

**From source** you need a C11 compiler and CMake 3.16 or later; SDL 3 is
used if it is installed, and otherwise downloaded and built with the
program:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build                # builds build/openabyss
./build/openabyss
```

On macOS, CMake builds `build/OpenAbyss.app`; run its executable with
`./build/OpenAbyss.app/Contents/MacOS/OpenAbyss`. To make a standalone app
with SDL included, run `cmake --install build --prefix "$PWD/package"`.
After installing, ad-hoc sign it with
`codesign --force --deep --sign - package/OpenAbyss.app`.
For a universal build, add `'-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64'` and
`-DOPENABYSS_VENDORED_SDL=ON` to the configure command.

On Linux with SDL 3 installed (and `pkg-config`), `make` builds `./openabyss`
as well. SDL 3.4 or later writes screenshots as PNG; 3.2 writes them as BMP.

## Finding the game

GOG's release holds the game as a CD image, `game.gog`, in the folder GOG
installs it to. The first time it runs, OpenAbyss looks for that folder
where GOG, GOG Galaxy, Heroic and Wine put it; if it finds nothing it asks
you for the folder -- choose the one GOG installed the game to. It then
copies the game out of `game.gog` once (about 12 MB), into its per-user
folder (`~/.local/share/openabyss/` on Linux, `%APPDATA%\openabyss\` on
Windows, `~/Library/Application Support/openabyss/` on macOS), and remembers
it in `openabyss.cfg` there; `./openabyss
--locate` asks again.

A folder already holding the game's files works as well: the one with
`UW.EXE` and the folders `DATA`, `CRIT` and `CUTS`, or the folder above it
that holds it in `UW`.

### Without installing: the offline installer

GOG's offline installer can be unpacked without running it, on Linux or
Windows, with [innoextract](https://constexpr.org/innoextract/):

1. On gog.com, open Ultima Underworld in your library and download the
   **offline backup game installer** (`setup_ultima_underworld_....exe`).
   The small "GOG Galaxy" installer is only a downloader and holds no game
   files.
2. Take `game.gog` out of it, into a folder of your choosing:

   ```sh
   mkdir -p ~/Games/UltimaUnderworld && cd ~/Games/UltimaUnderworld
   innoextract -I game.gog /path/to/setup_ultima_underworld_*.exe
   ```

3. Run OpenAbyss. A folder in `Games` in your home folder whose name holds
   "underworld" is found by itself; anywhere else, choose it when
   OpenAbyss asks, or give it once as `--dir`.

`game.gog` is an ordinary ISO image under another name, so it can also be
opened with 7-Zip or, renamed `game.iso`, with a file manager, and its `UW`
folder used directly.

A folder can also be given every time: `--dir /path/to/UW`, or the
`UW_DATA` environment variable, or running OpenAbyss from inside it. The
case of the files' names does not matter: a copy extracted in lower case
works as well.

## Playing

**Sound plays by default** -- the music through an AdLib model (the game's
own ADLIB.ADV voice layer and an OPL2) and the cutscenes' voices through a
digital one. `--nosound` silences it; `--sound` keeps it on even for a
saved game that had it switched off.

**The window** shows the 320 x 200 picture at 4:3, as a VGA monitor did
(`--square` for square pixels), resizable; `--scale N` sets its first size,
`--fullscreen` or Alt-Enter full screen, `--no-vsync` and `--pace MS` the
timing. The game's own keys are the original's: the letters walk and turn,
the keypad glides the cursor, F1..F10 and the Ctrl keys open panels, Alt-q
writes a screenshot (`uwpicNNN.png` in the working directory), Alt-x quits.

**Tab** toggles a view that fills the window and hides the interface. The
3D renderer doubles its resolution to 344 × 226 in this mode. The camera
matches the window's aspect ratio, showing more horizontally in wide windows
without stretching or cropping the view. Combat is automatic: move the mouse to look around (up and down stay
within the original limits), hold the left mouse button to charge an attack
and release it to strike. Right-click uses the object or door at the centre
of the screen, or starts a conversation with an NPC. WASD moves forward, left, backward and
right; strafing uses 75% of forward speed. Diagonal movement is capped at
forward speed. Panel shortcuts still work. Conversations and dialogs show their usual UI;
closing them resumes the full-window view. Alt-Enter still controls whether
the window itself is full screen.

**Saves** go where the original keeps them, `SAVE1`..`SAVE4` beside the
game's files, when that directory is writable, and otherwise to SDL's
per-user data directory; `--saves DIR` puts them elsewhere. They are the
original's format: a save made here loads in the original, and the other
way round.

`--cutscene N` plays one cutscene on its own (the introduction is 1).

## The code

`src/` is the game as a library of C modules, named for the routines of
the original they reimplement; `src/tools/uwshell.c` is the program around
it -- the window, the clock, input, sound and the game's modes. The
library draws nothing itself: it computes into the same memory layout the
original used, and the program presents it.

## Licence and trademarks

The code is under the MIT licence (`LICENSE`). Ultima and Ultima Underworld
are trademarks of their owners; this project is not affiliated with or
endorsed by them, and it distributes none of the game's files.
