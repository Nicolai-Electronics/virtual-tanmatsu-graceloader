# Virtual graceloader

Tanmatsu apps, on a Linux PC.

`tanmatsu-graceloader` is the loader that runs `app.so` on the Tanmatsu badge.
This is a stand-in for it — and for the badge — that runs the same `app.so`
files, unmodified, on a desktop Linux box: an SDL2 window for the display, the
keyboard for the buttons, the host sound card for the speaker.

Nothing is recompiled for this. An app's own `make build` produces
`build/app.so`, a 32-bit RISC-V shared object built with the ESP-IDF toolchain,
and this emulator loads and runs exactly that file.

## How it works

**Loading is the real thing.** `src/main.c` links against the same `kbelf`
loader the on-device graceloader uses (`lib/kbelf`, its own git submodule),
so `app.so` is relocated, init-constructed and entered the way the device
does it.

**Execution is a from-scratch RV32IMAFC interpreter** (`src/riscv/cpu.c`) — no
JIT, no translation to x86. Fast enough for these apps, not fast enough to be a
general ESP32-P4 emulator.

**Guest memory is identity-mapped.** The whole guest address space is one
`mmap()` below the 2 GiB mark (`MAP_32BIT`, `src/loader/guest_mem.h`), so a
32-bit guest address and the host pointer to that byte are numerically the same
number. A buffer an app passes to a host-implemented function is used with a
plain cast; there is no address translation anywhere in this project. Imported
symbols become trampolines in a reserved range above the guest heap.

**Host APIs are traps.** `app.so` has an undefined symbol for everything it
calls outside itself: ESP-IDF, the Tanmatsu BSP, pax-gfx, libc.
`src/shims/trap_list.h` lists all of them (284 entries) and
`src/shims/trap_table.c` maps each to a C function in `src/shims/` and
`src/bsp/`. That is the whole boundary between the app and this machine, and
almost all of this emulator's behaviour lives there.

**The display and audio backends are real SDL2**, and pax-gfx is compiled from
source beside the emulator (`components/pax-gfx`, `components/pax-codecs`)
rather than shimmed, so an app's drawing code runs the same code it runs on the
device.

**Everything is built `-m32`.** `app.so` is ILP32, and several pax-gfx structs
it shares with this emulator embed raw pointer fields. Compiled LP64, those
fields would be 8 bytes instead of 4 and every field after the first pointer
would shift, so the app would read garbage out of its own structs. Building the
emulator 32-bit makes the layouts match exactly — the same idea as the
identity-mapped guest memory: stop bridging two ABIs and make them the same ABI.
That is why the 32-bit multilib copies of the dependencies are required, not the
usual 64-bit ones.

## Requirements

Host packages (Debian/Ubuntu):

```
sudo apt install build-essential cmake git python3 \
                 gcc-multilib g++-multilib \
                 libsdl2-dev:i386 zlib1g-dev:i386
```

Building the apps also needs the ESP-IDF `riscv32-esp-elf` toolchain. The
Makefile looks for it in `$IDF_TOOLS_PATH`, then `~/.espressif`, `~/idf` and
`~/esp-idf-tools`; override it per invocation with
`make synthracer IDF_TOOLS_PATH=~/idf/tools-6.0`.

## Layout

Relative paths throughout, so a side-by-side checkout works for anyone:

```
virtual-tanmatsu-graceloader/     this emulator (lib/kbelf is its own submodule)
tanmatsu-ballz-grace/             the apps, one repo each
tanmatsu-synthracer-grace/
...
```

## Build and run

```
git submodule update --init  # fetch lib/kbelf, first time only
make build                   # configure (first time) and build build/graceloader-emulator
make ballz                   # build Ballz, install it to the virtual card, run it
```

`make <name>` does all three for any of `synthminer synthracer portals showreel
pogn paperclips floppybird ballz musicalkeyboard`. `make help` lists everything.

- `make build` / `make clean` — the emulator itself.
- `make <name>-build` — run that app's own Makefile, producing its `build/app.so`.
- `make <name>-install` — stage that app onto the virtual card (see below).
- `make <name>-run` — build, install, run.
- `make run APP=<name>` — same as `make <name>`.
- `make apps` / `make apps-clean` — every app, or each app's own `clean`.
- `make submodules` — fill in the synthengine3D git submodule the 3D apps need.
- `make unstage` — take the staged apps back off the virtual card.

Running the emulator directly:

```
build/graceloader-emulator [--int DIR] [--sd DIR] [--card DIR] <guest path to app.so>
```

## The virtual card

The emulator exports two host directories, `int/` and `sd/`, as the app's
`/int` and `/sd`. FatFs calls go straight to host `opendir`/`readdir`/`fopen`, so
an app can write a save file and you can look at it with `cat`.

An app finds its own assets by appending to
`graceloader_get_install_basepath()`, which the real graceloader sets to the
directory it loaded `app.so` from — `/sd/apps/at.cavac.synthminer`, with
`textures/` and `music/` beside it. Running `build/app.so` straight out of an
app's checkout instead reports that checkout's `build/`, which holds a binary and
nothing else, so every asset path misses and the app comes up with flat colours
and no music. `make <name>-install` is this emulator's version of the app's own
`make install`: `tools/stage-app.py` reads the app's `metadata.json` for what
belongs in an install directory and puts it where a launcher would.

`card/` is the part of a card image that ships with this emulator — the
launcher's shared `/int/icons` pack, which three apps read by absolute path and
no app repo contains. It is copied into `int/` and `sd/` before an app starts,
never overwriting what is already there. See `card/README.md`.

NVS is a flat file under `$HOME/.config/graceloader-emulator`, so settings and
high scores survive between runs.

## Keyboard

SDL2 key events become `bsp_input` navigation keys: arrows, `Esc`, `Enter`,
`Tab`, `Backspace`, `Home`, `End`, `PgUp`, `PgDn`, `Menu`, `Super`, `Space`,
`F1`–`F12`, and the media volume keys. Printable keys go through SDL2's
text-input events, so a layout other than US still types the right characters.

## What is stood in for

| On the device | Here |
| --- | --- |
| MIPI-DSI panel, 480x800 portrait | SDL2 texture at the panel's own size and colour format, rotated to a landscape window |
| I2S + ES8156 codec | SDL2 queued audio device, paced by a stream clock (see `src/bsp/bsp_audio.c`) |
| pax-gfx, pax-codecs, PNG decode | the same components, compiled from source |
| PPA hardware 2D blitter | CPU-side implementation of the jobs `se_ppa.c` actually submits |
| `/int` and `/sd` FAT volumes | host directories |
| NVS | a flat file |
| FreeRTOS tasks | POSIX threads, each with its own `cpu_t` and guest stack, sharing one address space |
| `esp_timer` | `CLOCK_MONOTONIC`; one tick is one millisecond |

Deliberately absent, because apps already have to cope with them being missing:
the I2C bus (every `i2c_master_*` call returns `ESP_ERR_NOT_SUPPORTED`), the
environmental sensors, the gyroscope and accelerometer, USB Serial/JTAG, and the
hardware H.264 encoder. The status LEDs are simulated in memory but have no
window.

## Notes and limits

- **Audio pacing is the emulator's job.** None of these apps has a
  `vTaskDelay()`, a timer or any other sleep in its audio loop; the blocking
  `i2s_channel_write()` is the only thing that paces their mixer tasks. So the
  emulator models the DMA transmission rather than a buffer, and an app whose
  interpreted mixer task cannot keep up with the stream will glitch — on the
  device the same app would simply have been starved of CPU.
- **Interpreter speed varies a lot between apps.** Frame rate is not comparable
  to the device, and an app that is smooth on hardware may not be here.
- **A trap that does not exist yet shows up as an unresolved symbol** at load
  time, naming the symbol. Adding one means a function in the right
  `src/shims/*.c` and one `X(...)` line in `src/shims/trap_list.h`.
- **`ESP_ERROR_CHECK` failures abort** the way they do on the device, with the
  failing expression and source line, which is usually the fastest clue about a
  missing or half-implemented API.

## License

This emulator's own code is MIT-licensed (see `LICENSE`). The libraries it links
into `graceloader-emulator` are each under their own license, listed below — all
permissive (MIT/BSD/zlib), so none of them constrain this choice. `lib/kbelf` and
`components/pax-*` are compiled from source alongside the emulator (see
`CMakeLists.txt`); SDL2 and zlib are the host's own (multilib) system packages,
linked but not vendored.

| Library | Role here | License | Source |
| --- | --- | --- | --- |
| [kbelf](https://github.com/nullislandspace/KiloElfLoader) | ELF loader/relocator that loads `app.so` | MIT | `lib/kbelf` (its own submodule, `graceloader` branch) |
| [pax-gfx](https://github.com/robotman2412/pax-graphics) | 2D graphics library apps draw with | MIT | `components/pax-gfx` |
| [pax-codecs](https://github.com/robotman2412/pax-codecs) | PNG decode wrapper around pax-gfx | MIT | `components/pax-codecs` |
| [libspng](https://libspng.org/) | PNG decoder, used by pax-codecs | BSD-2-Clause | `components/pax-codecs/libspng` |
| [SDL2](https://www.libsdl.org/) | Window, framebuffer blit, keyboard input, queued audio output | zlib License | system package (`libsdl2-dev:i386`) |
| [zlib](https://zlib.net/) | DEFLATE decompression, used by libspng | zlib License | system package (`zlib1g-dev:i386`) |

All are permissive (MIT/BSD/zlib): no copyleft obligations, attribution is the
only requirement, which is what this section is for.
