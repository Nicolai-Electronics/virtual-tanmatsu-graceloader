// libspng passthroughs, for apps that decode PNGs directly rather than
// through pax_insert_png_fd(). spng_ctx* is fully opaque to callers (they
// never dereference its fields, just pass it back into other spng_*
// calls), so -- like FILE* -- it goes through the handle table rather than
// being exposed as a raw host pointer (see handle_table.h and the comment
// on trap_heap_caps_malloc() in esp_shim.c for why).
//
// struct spng_ihdr is plain fixed-width fields (no pointers/longs), so it's
// safe to memcpy directly between host and guest -- verified in spng.h.

#include <stdio.h>
#include <string.h>

#include "cpu.h"
#include "handle_table.h"
#include "spng.h"
#include "trap.h"

void trap_spng_ctx_new(cpu_t *cpu) {
    spng_ctx *ctx = spng_ctx_new(arg_i32(cpu, 0));
    ret_u32(cpu, ctx ? handle_alloc(ctx) : 0);
}
void trap_spng_ctx_free(cpu_t *cpu) {
    uint32_t  h   = arg_u32(cpu, 0);
    spng_ctx *ctx = handle_get(h);
    if (ctx != NULL) {
        spng_ctx_free(ctx);
    }
    handle_free(h);
}
void trap_spng_set_png_file(cpu_t *cpu) {
    spng_ctx *ctx  = handle_get(arg_u32(cpu, 0));
    FILE     *file = handle_get(arg_u32(cpu, 1));
    ret_u32(cpu, (uint32_t)(ctx && file ? spng_set_png_file(ctx, file) : -1));
}
void trap_spng_set_image_limits(cpu_t *cpu) {
    spng_ctx *ctx = handle_get(arg_u32(cpu, 0));
    ret_u32(cpu, (uint32_t)(ctx ? spng_set_image_limits(ctx, arg_u32(cpu, 1), arg_u32(cpu, 2)) : -1));
}
void trap_spng_decoded_image_size(cpu_t *cpu) {
    spng_ctx *ctx = handle_get(arg_u32(cpu, 0));
    size_t    len = 0;
    int       rc  = ctx ? spng_decoded_image_size(ctx, arg_i32(cpu, 1), &len) : -1;
    size_t   *out = (size_t *)arg_ptr(cpu, 2);
    if (out != NULL) {
        *out = len;
    }
    ret_u32(cpu, (uint32_t)rc);
}
void trap_spng_decode_image(cpu_t *cpu) {
    spng_ctx *ctx = handle_get(arg_u32(cpu, 0));
    ret_u32(
        cpu, (uint32_t)(ctx ? spng_decode_image(ctx, arg_ptr(cpu, 1), arg_u32(cpu, 2), arg_i32(cpu, 3),
                                                arg_i32(cpu, 4))
                            : -1)
    );
}
void trap_spng_get_ihdr(cpu_t *cpu) {
    spng_ctx          *ctx  = handle_get(arg_u32(cpu, 0));
    struct spng_ihdr    ihdr;
    int                 rc   = ctx ? spng_get_ihdr(ctx, &ihdr) : -1;
    struct spng_ihdr   *out  = (struct spng_ihdr *)arg_ptr(cpu, 1);
    if (rc == 0 && out != NULL) {
        memcpy(out, &ihdr, sizeof(ihdr));
    }
    ret_u32(cpu, (uint32_t)rc);
}
void trap_spng_strerror(cpu_t *cpu) {
    ret_ptr(cpu, (void *)spng_strerror(arg_i32(cpu, 0)));
}
