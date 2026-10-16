// PAX graphics library passthroughs. pax-gfx is portable C linked directly
// into this emulator (see emulator/components/pax-gfx); guest pointers are
// real host pointers, so pax_buf_t*, pixel buffers, and point arrays can all
// be passed straight through with no marshalling beyond picking the right
// argument registers (int/pointer args from a0-a7, float args from fa0-fa7,
// tracked independently per the RISC-V calling convention).

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cpu.h"
#include "guest_mem.h"
#include "handle_table.h"
#include "pax_codecs.h"
#include "pax_gfx.h"
#include "trap.h"

void trap_pax_buf_init(cpu_t *cpu) {
    int   width = arg_i32(cpu, 2), height = arg_i32(cpu, 3);
    pax_buf_type_t type = (pax_buf_type_t)arg_i32(cpu, 4);
    void          *mem  = arg_ptr(cpu, 1);
    if (mem == NULL) {
        // pax_buf_init() self-mallocs the backing buffer when `mem` is
        // NULL, using the host heap -- same 32-bit-guest-pointer problem as
        // trap_heap_caps_malloc() (see esp_shim.c), except this pointer
        // comes back out through pax_buf_get_pixels() rather than as a
        // direct return value. Pre-allocate it from guest memory ourselves
        // instead of letting pax-gfx self-allocate.
        mem = guest_mem_calloc(pax_buf_calc_size_dynamic(width, height, type), 8);
    }
    ret_u32(cpu, (uint32_t)pax_buf_init((pax_buf_t *)arg_ptr(cpu, 0), mem, width, height, type));
}
void trap_pax_buf_destroy(cpu_t *cpu) {
    pax_buf_destroy((pax_buf_t *)arg_ptr(cpu, 0));
}
void trap_pax_buf_reversed(cpu_t *cpu) {
    pax_buf_reversed((pax_buf_t *)arg_ptr(cpu, 0), arg_u32(cpu, 1) != 0);
}
void trap_pax_buf_get_width(cpu_t *cpu) {
    ret_u32(cpu, (uint32_t)pax_buf_get_width((pax_buf_t const *)arg_ptr(cpu, 0)));
}
void trap_pax_buf_get_height(cpu_t *cpu) {
    ret_u32(cpu, (uint32_t)pax_buf_get_height((pax_buf_t const *)arg_ptr(cpu, 0)));
}
void trap_pax_buf_get_width_raw(cpu_t *cpu) {
    ret_u32(cpu, (uint32_t)pax_buf_get_width_raw((pax_buf_t const *)arg_ptr(cpu, 0)));
}
void trap_pax_buf_get_height_raw(cpu_t *cpu) {
    ret_u32(cpu, (uint32_t)pax_buf_get_height_raw((pax_buf_t const *)arg_ptr(cpu, 0)));
}
void trap_pax_buf_get_pixels(cpu_t *cpu) {
    ret_ptr(cpu, (void *)pax_buf_get_pixels((pax_buf_t const *)arg_ptr(cpu, 0)));
}
void trap_pax_buf_get_pixels_rw(cpu_t *cpu) {
    ret_ptr(cpu, pax_buf_get_pixels_rw((pax_buf_t *)arg_ptr(cpu, 0)));
}
void trap_pax_buf_get_size(cpu_t *cpu) {
    ret_u32(cpu, (uint32_t)pax_buf_get_size((pax_buf_t const *)arg_ptr(cpu, 0)));
}
void trap_pax_buf_set_orientation(cpu_t *cpu) {
    pax_buf_set_orientation((pax_buf_t *)arg_ptr(cpu, 0), (pax_orientation_t)arg_i32(cpu, 1));
}
void trap_pax_buf_get_orientation(cpu_t *cpu) {
    ret_u32(cpu, (uint32_t)pax_buf_get_orientation((pax_buf_t const *)arg_ptr(cpu, 0)));
}
void trap_pax_buf_type_info(cpu_t *cpu) {
    pax_buf_type_info_t info = pax_buf_type_info((pax_buf_type_t)arg_i32(cpu, 0));
    uint64_t             packed;
    memcpy(&packed, &info, sizeof(info));
    ret_u64(cpu, packed);
}

void trap_pax_background(cpu_t *cpu) {
    pax_background((pax_buf_t *)arg_ptr(cpu, 0), (pax_col_t)arg_u32(cpu, 1));
}
void trap_pax_draw_image(cpu_t *cpu) {
    pax_draw_image(
        (pax_buf_t *)arg_ptr(cpu, 0), (pax_buf_t const *)arg_ptr(cpu, 1), arg_f32(cpu, 0), arg_f32(cpu, 1)
    );
}
void trap_pax_simple_rect(cpu_t *cpu) {
    pax_simple_rect(
        (pax_buf_t *)arg_ptr(cpu, 0), (pax_col_t)arg_u32(cpu, 1), arg_f32(cpu, 0), arg_f32(cpu, 1), arg_f32(cpu, 2),
        arg_f32(cpu, 3)
    );
}
void trap_pax_simple_tri(cpu_t *cpu) {
    pax_simple_tri(
        (pax_buf_t *)arg_ptr(cpu, 0), (pax_col_t)arg_u32(cpu, 1), arg_f32(cpu, 0), arg_f32(cpu, 1), arg_f32(cpu, 2),
        arg_f32(cpu, 3), arg_f32(cpu, 4), arg_f32(cpu, 5)
    );
}
void trap_pax_simple_line(cpu_t *cpu) {
    pax_simple_line(
        (pax_buf_t *)arg_ptr(cpu, 0), (pax_col_t)arg_u32(cpu, 1), arg_f32(cpu, 0), arg_f32(cpu, 1), arg_f32(cpu, 2),
        arg_f32(cpu, 3)
    );
}
void trap_pax_draw_shape(cpu_t *cpu) {
    pax_draw_shape(
        (pax_buf_t *)arg_ptr(cpu, 0), (pax_col_t)arg_u32(cpu, 1), arg_u32(cpu, 2),
        (pax_vec2f const *)arg_ptr(cpu, 3)
    );
}
void trap_pax_draw_shape_triang(cpu_t *cpu) {
    pax_draw_shape_triang(
        (pax_buf_t *)arg_ptr(cpu, 0), (pax_col_t)arg_u32(cpu, 1), arg_u32(cpu, 2),
        (pax_vec2f const *)arg_ptr(cpu, 3), arg_u32(cpu, 4), (size_t const *)arg_ptr(cpu, 5)
    );
}
void trap_pax_triang_concave(cpu_t *cpu) {
    // Unlike pax_buf_init(), there's no way to hand pax_triang_concave() a
    // caller-supplied buffer -- it always self-allocates *output via the
    // host heap internally (see pax_shapes.c). Let it do that onto a local
    // host variable, then copy the result into guest memory before handing
    // the guest a pointer to it; see the comment on trap_heap_caps_malloc()
    // in esp_shim.c for why a raw host pointer can't go directly into a
    // guest register.
    size_t     *host_output = NULL;
    size_t      num_points  = arg_u32(cpu, 1);
    pax_vec2f const *points = (pax_vec2f const *)arg_ptr(cpu, 2);
    size_t      count       = pax_triang_concave(&host_output, num_points, points);

    // `count` is the number of *triangles*; the array itself holds 3 indices
    // per triangle (see pax_shapes.c's `calloc(n_tris, sizeof(size_t) * 3)`).
    size_t **out = (size_t **)arg_ptr(cpu, 0);
    if (out != NULL) {
        if (count > 0) {
            size_t bytes          = count * 3 * sizeof(size_t);
            size_t *guest_output = guest_mem_calloc(bytes, sizeof(size_t));
            memcpy(guest_output, host_output, bytes);
            *out = guest_output;
        } else {
            *out = NULL;
        }
    }
    free(host_output);
    ret_u32(cpu, (uint32_t)count);
}

void trap_pax_insert_png_fd(cpu_t *cpu) {
    // `fd` was handed to the guest by our fopen/fastopen shim as a
    // handle_table index (see handle_table.h), not a real FILE* -- those
    // aren't guaranteed to fit in a 32-bit guest register. Translate back
    // before calling the real (host) pax-codecs implementation, which does
    // need a real FILE*.
    FILE *fd = (FILE *)handle_get(arg_u32(cpu, 1));
    ret_u32(
        cpu, (uint32_t)pax_insert_png_fd(
                 (pax_buf_t *)arg_ptr(cpu, 0), fd, arg_i32(cpu, 2), arg_i32(cpu, 3), arg_i32(cpu, 4)
             )
    );
}

void trap_pax_buf_get_type(cpu_t *cpu) {
    ret_u32(cpu, (uint32_t)pax_buf_get_type((pax_buf_t const *)arg_ptr(cpu, 0)));
}
void trap_pax_draw_rect(cpu_t *cpu) {
    pax_draw_rect(
        (pax_buf_t *)arg_ptr(cpu, 0), (pax_col_t)arg_u32(cpu, 1), arg_f32(cpu, 0), arg_f32(cpu, 1), arg_f32(cpu, 2),
        arg_f32(cpu, 3)
    );
}
void trap_pax_get_pixel(cpu_t *cpu) {
    ret_u32(cpu, (uint32_t)pax_get_pixel((pax_buf_t const *)arg_ptr(cpu, 0), arg_i32(cpu, 1), arg_i32(cpu, 2)));
}
void trap_pax_draw_line(cpu_t *cpu) {
    pax_draw_line(
        (pax_buf_t *)arg_ptr(cpu, 0), (pax_col_t)arg_u32(cpu, 1), arg_f32(cpu, 0), arg_f32(cpu, 1), arg_f32(cpu, 2),
        arg_f32(cpu, 3)
    );
}
void trap_pax_draw_tri(cpu_t *cpu) {
    pax_draw_tri(
        (pax_buf_t *)arg_ptr(cpu, 0), (pax_col_t)arg_u32(cpu, 1), arg_f32(cpu, 0), arg_f32(cpu, 1), arg_f32(cpu, 2),
        arg_f32(cpu, 3), arg_f32(cpu, 4), arg_f32(cpu, 5)
    );
}
void trap_pax_outline_rect(cpu_t *cpu) {
    pax_outline_rect(
        (pax_buf_t *)arg_ptr(cpu, 0), (pax_col_t)arg_u32(cpu, 1), arg_f32(cpu, 0), arg_f32(cpu, 1), arg_f32(cpu, 2),
        arg_f32(cpu, 3)
    );
}
void trap_pax_simple_circle(cpu_t *cpu) {
    pax_simple_circle(
        (pax_buf_t *)arg_ptr(cpu, 0), (pax_col_t)arg_u32(cpu, 1), arg_f32(cpu, 0), arg_f32(cpu, 1), arg_f32(cpu, 2)
    );
}
