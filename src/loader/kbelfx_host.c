// kbelf platform callbacks for the x86-64 emulator.
//
// Segments are allocated out of the guest memory region (see guest_mem.h) so
// that `laddr` is a real, dereferenceable host pointer the app's own code
// can also address as a 32-bit guest pointer. Everything else (metadata
// allocation, file I/O) is plain host malloc/stdio.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define KBELF_REVEAL_PRIVATE
#include "guest_mem.h"
#include "kbelf.h"
#include "trap.h"

// ==== kbelf/string.h ===========================================================

size_t kbelfq_strlen(char const *str) {
    return strlen(str);
}
void kbelfq_strcpy(char *dst, char const *src) {
    strcpy(dst, src);
}
char const *kbelfq_strrchr(char const *str, char c) {
    return strrchr(str, c);
}
bool kbelfq_streq(char const *a, char const *b) {
    return !strcmp(a, b);
}
void kbelfq_memcpy(void *dst, void const *src, size_t nmemb) {
    memcpy(dst, src, nmemb);
}
void kbelfq_memset(void *dst, uint8_t c, size_t nmemb) {
    memset(dst, c, nmemb);
}
bool kbelfq_memeq(void const *a, void const *b, size_t nmemb) {
    return !memcmp(a, b, nmemb);
}

// ==== kbelf/port.h: metadata allocator =========================================

void *kbelfx_malloc(size_t len) {
    return malloc(len);
}
void *kbelfx_realloc(void *mem, size_t len) {
    return realloc(mem, len);
}
void kbelfx_free(void *mem) {
    free(mem);
}

// ==== kbelf/port.h: segment allocator (lives in guest memory) ==================

bool kbelfx_seg_alloc(kbelf_inst inst, size_t segs_len, kbelf_segment *segs) {
    (void)inst;
    size_t min_va = SIZE_MAX, max_va = 0, min_align = 16;
    for (size_t i = 0; i < segs_len; i++) {
        if (min_align < segs[i].alignment) min_align = segs[i].alignment;
        if (min_va > segs[i].vaddr_req) min_va = segs[i].vaddr_req;
        if (max_va < segs[i].vaddr_req + segs[i].size) max_va = segs[i].vaddr_req + segs[i].size;
    }

    size_t    alloc_size = max_va - min_va;
    uintptr_t base       = guest_mem_alloc(alloc_size, min_align);

    segs[0].alloc_cookie = (void *)base;
    size_t offset         = base - min_va;
    for (size_t i = 0; i < segs_len; i++) {
        segs[i].laddr = segs[i].vaddr_real = segs[i].vaddr_req + offset;
    }
    return true;
}

void kbelfx_seg_free(kbelf_inst inst, size_t segs_len, kbelf_segment *segs) {
    // The guest region is a bump allocator for the process lifetime; nothing
    // to free (graceloader's own kbelfx.c does the same for the ESP32
    // target's aligned_alloc-backed segments -- apps aren't unloaded until
    // the process exits).
    (void)inst;
    (void)segs_len;
    (void)segs;
}

// ==== kbelf/port.h: file I/O ====================================================

void *kbelfx_open(char const *path) {
    return fopen(path, "rb");
}
void kbelfx_close(void *fd) {
    fclose((FILE *)fd);
}
long kbelfx_read(void *fd, void *buf, long buf_len) {
    return (long)fread(buf, 1, (size_t)buf_len, (FILE *)fd);
}
long kbelfx_load(kbelf_inst inst, void *fd, kbelf_laddr laddr, kbelf_laddr file_size, kbelf_laddr mem_size) {
    (void)inst;
    if (mem_size > file_size) {
        memset((void *)(laddr + file_size), 0, mem_size - file_size);
    }
    return (long)fread((void *)laddr, 1, (size_t)file_size, (FILE *)fd);
}
int kbelfx_seek(void *fd, long pos) {
    return fseek((FILE *)fd, pos, SEEK_SET);
}

void kbelfx_cache_sync(kbelf_laddr addr, size_t size) {
    // x86-64 has coherent I/D caches; nothing to do.
    (void)addr;
    (void)size;
}

bool kbelfx_copy_from_user(kbelf_inst inst, void *buf, kbelf_laddr laddr, size_t len) {
    (void)inst;
    memcpy(buf, (void const *)laddr, len);
    return true;
}
bool kbelfx_copy_to_user(kbelf_inst inst, kbelf_laddr laddr, void *buf, size_t len) {
    (void)inst;
    memcpy((void *)laddr, buf, len);
    return true;
}
ptrdiff_t kbelfx_strlen_from_user(kbelf_inst inst, kbelf_laddr laddr) {
    (void)inst;
    return (ptrdiff_t)strlen((char const *)laddr);
}

// ==== kbelf/port.h: library resolution ==========================================
// app.so declares a single NEEDED entry ("liball.so") that stands for "look
// everything up in the builtin table" -- there are no real shared-library
// files to find.
kbelf_file kbelfx_find_lib(char const *needed) {
    (void)needed;
    return NULL;
}

kbelf_builtin_lib const *kbelfx_builtin_libs[1];
size_t                   kbelfx_builtin_libs_len = 1;

void kbelfx_host_init(void) {
    kbelfx_builtin_libs[0] = trap_builtin_lib();
}
