// zlib checksum passthroughs (zlib is linked in via pax-codecs' vendored
// copy). Plain value-in/value-out, no pointer-safety concerns beyond the
// input buffer, which is guest memory (== a real host pointer).

#include <zlib.h>

#include "cpu.h"
#include "trap.h"

void trap_adler32(cpu_t *cpu) {
    ret_u32(cpu, (uint32_t)adler32((uLong)arg_u32(cpu, 0), (Bytef const *)arg_ptr(cpu, 1), arg_u32(cpu, 2)));
}
void trap_crc32(cpu_t *cpu) {
    ret_u32(cpu, (uint32_t)crc32((uLong)arg_u32(cpu, 0), (Bytef const *)arg_ptr(cpu, 1), arg_u32(cpu, 2)));
}
