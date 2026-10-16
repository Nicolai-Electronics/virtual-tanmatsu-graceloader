# Virtual graceloader -- the Linux-PC emulator for Tanmatsu graceloader apps.
#
# Every path below is relative, so a plain side-by-side checkout works for
# anyone. Layout assumed:
#
#   ../tanmatsu-graceloader/      the on-device graceloader; supplies lib/kbelf
#   ../lib                        -> tanmatsu-graceloader/lib  (`make setup`)
#   ./                            this emulator
#   ../tanmatsu-<name>-grace/     the apps
#
# Host packages for the emulator. It is built -m32 because app.so is ILP32 and
# some pax-gfx structs are shared with the apps directly rather than through
# accessor functions (see the comment at the top of CMakeLists.txt), so the
# 32-bit multilib copies of the dependencies are required, not the 64-bit ones:
#
#   sudo apt install build-essential cmake gcc-multilib g++-multilib \
#                    libsdl2-dev:i386 zlib1g-dev:i386
#
# Building the apps needs the ESP-IDF riscv32-esp-elf toolchain. It is looked
# for in $IDF_TOOLS_PATH first, then in the usual install locations; override
# it per invocation with e.g.
#
#   make synthracer-build IDF_TOOLS_PATH=~/idf/tools-6.0

APPS := synthminer synthracer portals showreel pogn paperclips floppybird ballz musicalkeyboard

APP_DIR_synthminer      := ../tanmatsu-synthminer-grace
APP_DIR_synthracer      := ../tanmatsu-synthracer-grace
APP_DIR_portals         := ../tanmatsu-portals-grace
APP_DIR_showreel        := ../tanmatsu-showreel-grace
APP_DIR_pogn            := ../tanmatsu-pogn-grace
APP_DIR_paperclips      := ../tanmatsu-paperclips-grace
APP_DIR_floppybird      := ../tanmatsu-floppybird-grace
APP_DIR_ballz           := ../tanmatsu-ballz-grace
APP_DIR_musicalkeyboard := ../tanmatsu-musicalkeyboard-grace
APP_DIRS                := $(foreach name,$(APPS),$(APP_DIR_$(name)))

# Which app `make run` starts.
APP ?= synthracer

BUILD_DIR  ?= build
EMU        := $(BUILD_DIR)/graceloader-emulator
CMAKE      ?= cmake
JOBS       ?= $(shell nproc)
BUILD_TYPE ?= RelWithDebInfo

# The virtual card. The emulator exports these two directories to every app as
# "/int" and "/sd", and `make <name>-install` puts an app where the launcher on
# a real card would put it: <int|sd>/apps/<slug>/, with its textures and music
# beside app.so. That is what makes graceloader_get_install_basepath() answer
# the same thing here as it does on the device, which is what an app needs to
# find its own assets at all.
INT_DIR ?= int
SD_DIR  ?= sd
STAGE   := tools/stage-app.py
STAGED  := $(BUILD_DIR)/staged
PYTHON  ?= python3

# CMakeLists.txt reads its kbelf sources from ../lib/kbelf.
GRACELOADER_DIR ?= ../tanmatsu-graceloader
KBELF_LINK      := ../lib

# Toolchain location for the app builds; empty leaves it to the apps' own
# Makefiles (which fall back to ./esp-idf-tools or $PATH).
IDF_TOOLS_PATH ?= $(shell \
	for root in $${IDF_TOOLS_PATH} $(HOME)/.espressif $(HOME)/idf $(HOME)/esp-idf-tools; do \
		for dir in $$root $$(ls -d $$root/tools-* 2>/dev/null | sort -r); do \
			ls $$dir/tools/riscv32-esp-elf/*/riscv32-esp-elf/bin/riscv32-esp-elf-gcc >/dev/null 2>&1 \
				&& { echo $$dir; exit; }; \
		done; \
	done)
ifneq ($(IDF_TOOLS_PATH),)
APP_MAKE_VARS := IDF_TOOLS_PATH=$(IDF_TOOLS_PATH)
endif

.PHONY: all build configure clean run apps apps-clean setup help
.PHONY: %-build %-install %-run

all: build

# -------------------------------------------------------------------- emulator

build: configure
	$(CMAKE) --build $(BUILD_DIR) -j$(JOBS)

configure: $(BUILD_DIR)/CMakeCache.txt

$(BUILD_DIR)/CMakeCache.txt: CMakeLists.txt
	@if [ ! -d "$(KBELF_LINK)/kbelf" ]; then \
	  echo "error: $(KBELF_LINK)/kbelf is missing, and CMakeLists.txt takes its"; \
	  echo "       kbelf sources from there. Run 'make setup' first."; exit 1; fi
	mkdir -p $(BUILD_DIR)
	$(CMAKE) -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=$(BUILD_TYPE)

clean:
	rm -rf $(BUILD_DIR)

setup:
	@if [ -e "$(KBELF_LINK)" ]; then \
	  echo "$(KBELF_LINK) already exists, nothing to do"; \
	elif [ -d "$(GRACELOADER_DIR)/lib" ]; then \
	  ln -s "$(notdir $(GRACELOADER_DIR))/lib" "$(KBELF_LINK)" && \
	  echo "linked $(KBELF_LINK) -> $(notdir $(GRACELOADER_DIR))/lib"; \
	else \
	  echo "error: no $(GRACELOADER_DIR)/lib to link $(KBELF_LINK) to."; \
	  echo "       Put a tanmatsu-graceloader checkout next to this emulator, or"; \
	  echo "       point $(KBELF_LINK) at that checkout's lib/ directory yourself."; exit 1; fi

# ------------------------------------------------------------------------ apps

# `make <name>-build` compiles ../tanmatsu-<name>-grace/build/app.so with that
# app's own Makefile, so each app keeps its own build rules and this file only
# has to know where the repos live. `make <name>` and `make <name>-run` build
# first, then run it here.
%-build:
	@dir="$(APP_DIR_$*)"; \
	 if [ -z "$$dir" ]; then echo "error: unknown app '$*'. Known apps: $(APPS)"; exit 1; fi; \
	 if [ ! -d "$$dir" ]; then echo "error: '$*' should be in $$dir, which does not exist"; exit 1; fi; \
	 echo "=== Building $* ($$dir) ==="; \
	 $(MAKE) -C "$$dir" build $(APP_MAKE_VARS)

# The emulator's version of the app's own `make install`, onto the virtual card
# instead of onto a badge: tools/stage-app.py reads the app's metadata.json for
# which files belong in the install directory, and its Makefile for which
# directory that is, and prints the guest path the emulator should be given.
%-install: %-build
	@dir="$(APP_DIR_$*)"; \
	 if [ ! -f "$$dir/build/app.so" ]; then echo "error: $$dir/build/app.so was not produced"; exit 1; fi; \
	 out="$$($(PYTHON) $(STAGE) "$$dir" --int $(INT_DIR) --sd $(SD_DIR))" || exit 1; \
	 echo "$$out" | sed '$$d'; \
	 mkdir -p $(STAGED); \
	 echo "$$out" | tail -n1 > $(STAGED)/$*.path

%-run: build %-install
	@echo "=== Running $* ==="; \
	 $(EMU) --int $(INT_DIR) --sd $(SD_DIR) "$$(cat $(STAGED)/$*.path)"

$(foreach name,$(APPS),$(eval $(name): $(name)-run))
.PHONY: $(APPS)

apps: $(addsuffix -build,$(APPS))

apps-clean:
	@for dir in $(APP_DIRS); do \
	  if [ -d "$$dir" ]; then $(MAKE) -C "$$dir" clean; fi; \
	done

# Take the staged apps off the virtual card. Saves and settings in $(INT_DIR)
# and $(SD_DIR) are left where they are.
.PHONY: unstage
unstage:
	rm -rf $(INT_DIR)/apps $(SD_DIR)/apps $(STAGED)

# The synthengine3D apps keep their 3D engine as a git submodule; a plain
# clone leaves it empty and the app then fails on a missing synthengine3d.h.
.PHONY: submodules
submodules:
	@for dir in $(APP_DIRS); do \
	  if [ -f "$$dir/.gitmodules" ]; then \
	    echo "=== $${dir} ==="; \
	    git -C "$$dir" submodule update --init --recursive; \
	  fi; \
	done

run: $(APP)-run

# ------------------------------------------------------------------------ misc

help:
	@echo "Emulator:"
	@echo "  make build         configure (if needed) and build $(EMU)"
	@echo "  make configure     re-run the CMake configure step only"
	@echo "  make clean         remove $(BUILD_DIR)"
	@echo "  make setup         create $(KBELF_LINK) -> $(GRACELOADER_DIR)/lib"
	@echo "  make run           build and run $(APP) (change it with APP=<name>)"
	@echo
	@echo "Apps ($(APPS)):"
	@echo "  make <name>-build  build ../tanmatsu-<name>-grace/build/app.so"
	@echo "  make <name>-install  stage that app into $(INT_DIR)/ or $(SD_DIR)/ under apps/<slug>,"
	@echo "                     the way a launcher installs it on a card (from its metadata.json)"
	@echo "  make <name>-run    build the emulator and that app, install it, then run it"
	@echo "  make <name>        same as <name>-run"
	@echo "  make apps          build every app"
	@echo "  make apps-clean    run each app's own 'make clean'"
	@echo "  make submodules    fill in each app's git submodules (fresh clones)"
	@echo "  make unstage       remove the staged apps from $(INT_DIR)/apps and $(SD_DIR)/apps"
	@echo
	@echo "Useful variables:"
	@echo "  APP=<name>              app for 'make run' (default: $(APP))"
	@echo "  IDF_TOOLS_PATH=<dir>    ESP-IDF tools for the app builds (detected: $(if $(IDF_TOOLS_PATH),$(IDF_TOOLS_PATH),none))"
	@echo "  BUILD_TYPE=<type>       CMake build type (default: $(BUILD_TYPE))"
	@echo "  JOBS=<n>                parallel build jobs (default: $(JOBS))"
	@echo "  INT_DIR=<dir>           host directory the apps see as \"/int\" (default: $(INT_DIR))"
	@echo "  SD_DIR=<dir>            host directory the apps see as \"/sd\" (default: $(SD_DIR))"
