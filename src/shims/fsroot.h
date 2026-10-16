#pragma once

#include <stddef.h>

// Real graceloader apps use absolute paths like "/int/..." and "/sd/..."
// (the internal-flash and SD-card mount points main.c sets up on real
// hardware). This emulator remaps those two prefixes onto ordinary host
// directories; every other path (relative, or some other absolute path) is
// passed through untranslated.

// Called once by main.c.
void fsroot_init(char const *int_dir, char const *sd_dir);

// Writes the translated path into `buf` (size `buflen`) and returns it.
char const *fsroot_translate(char const *guest_path, char *buf, size_t buflen);
