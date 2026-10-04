// ripscan: find rip-relative (disp32) and call/jmp rel32 references to given virtual addresses in a memory image.
// Usage: ripscan <image.bin> <image_base_hex> <scan_file_off_hex> <scan_len_hex> <target_va_hex>...
// Prints: target_va ref_pos_va tail   (ref_pos_va = address of the disp32; tail = bytes after disp32 before the
// instruction ends: 0, 1 or 4). The image is streamed in 32 MB chunks (never loaded whole).
// Build: gcc -O2 -o ripscan.exe ripscan.c
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

static uint64_t* targets;
static int nt;
static uint8_t bm_lo[1 << 16];  // bitmap on bits 0..15 of the target (1 byte per entry: fast, 64 KB)
static uint8_t bm_hi[1 << 16];  // bitmap on bits 16..31

static inline void check(uint64_t dst, uint64_t va, int tail) {
    if (!bm_lo[dst & 0xFFFF] || !bm_hi[(dst >> 16) & 0xFFFF]) return;
    for (int k = 0; k < nt; ++k)
        if (dst == targets[k]) printf("%llx %llx %d\n", (unsigned long long)dst, (unsigned long long)va, tail);
}

int main(int argc, char** argv) {
    if (argc < 6) { fprintf(stderr, "usage: ripscan image base off len target...\n"); return 2; }
    const char* path = argv[1];
    uint64_t base = strtoull(argv[2], 0, 16);
    uint64_t off = strtoull(argv[3], 0, 16);
    uint64_t len = strtoull(argv[4], 0, 16);
    nt = argc - 5;
    targets = (uint64_t*)malloc(sizeof(uint64_t) * nt);
    for (int i = 0; i < nt; ++i) {
        targets[i] = strtoull(argv[5 + i], 0, 16);
        bm_lo[targets[i] & 0xFFFF] = 1;
        bm_hi[(targets[i] >> 16) & 0xFFFF] = 1;
    }
    FILE* f = fopen(path, "rb");
    if (!f) { perror("open"); return 1; }
    const size_t CH = 32u << 20;
    uint8_t* buf = (uint8_t*)malloc(CH + 16);
    uint64_t pos = off, end = off + len;
    while (pos < end) {
        size_t want = (size_t)((end - pos) < CH + 8 ? (end - pos) : CH + 8);
        if (_fseeki64(f, (long long)pos, SEEK_SET) != 0) break;
        size_t got = fread(buf, 1, want, f);
        if (got <= 8) break;
        size_t n = got - 8;
        for (size_t i = 0; i < n; ++i) {
            int32_t d;
            memcpy(&d, buf + i, 4);
            uint64_t va = base + pos + i;
            uint64_t dst = va + 4 + (int64_t)d;
            check(dst, va, 0);
            check(dst + 1, va, 1);
            check(dst + 4, va, 4);
        }
        if (got < want) break;
        pos += n;
    }
    fclose(f);
    return 0;
}
