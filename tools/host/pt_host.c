#include "pt_host.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t crc_table[256];

static uint32_t crc(uint32_t c, const uint8_t *p, size_t n)
{
    if (!crc_table[1]) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t k = i;
            for (int j = 0; j < 8; j++) {
                k = (k & 1) ? 0xEDB88320u ^ (k >> 1) : k >> 1;
            }
            crc_table[i] = k;
        }
    }
    c = ~c;
    while (n--) {
        c = crc_table[(c ^ *p++) & 0xFF] ^ (c >> 8);
    }
    return ~c;
}

static void put32(FILE *f, uint32_t v)
{
    const uint8_t b[4] = {(uint8_t)(v >> 24), (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v};
    fwrite(b, 1, 4, f);
}

static void chunk(FILE *f, const char *type, const uint8_t *data, uint32_t len)
{
    put32(f, len);
    fwrite(type, 1, 4, f);
    if (len) {
        fwrite(data, 1, len, f);
    }
    uint32_t c = crc(0, (const uint8_t *)type, 4);
    c = crc(c, data, len);
    put32(f, c);
}

// A zlib stream of stored blocks: big files, no dependency.
int pt_host_write_png_sized(const char *path, const uint16_t *px, int W, int H)
{
    const size_t row = 1 + (size_t)W * 3;
    const size_t raw_len = row * (size_t)H;
    uint8_t *raw = (uint8_t *)malloc(raw_len);
    uint8_t *z = (uint8_t *)malloc(raw_len + raw_len / 65535 * 5 + 64);
    if (!raw || !z) {
        free(raw);
        free(z);
        return 0;
    }
    for (int y = 0; y < H; y++) {
        uint8_t *o = raw + (size_t)y * row;
        *o++ = 0;
        for (int x = 0; x < W; x++) {
            const uint16_t v = px[y * W + x];
            const int r = (v >> 11) & 0x1F, g = (v >> 5) & 0x3F, b = v & 0x1F;
            *o++ = (uint8_t)((r << 3) | (r >> 2));
            *o++ = (uint8_t)((g << 2) | (g >> 4));
            *o++ = (uint8_t)((b << 3) | (b >> 2));
        }
    }
    const size_t blocks = (raw_len + 65534) / 65535;
    size_t zn = 0;
    z[zn++] = 0x78;
    z[zn++] = 0x01;
    uint32_t a = 1, bsum = 0;
    for (size_t k = 0; k < blocks; k++) {
        const size_t off = k * 65535;
        const size_t len = raw_len - off < 65535 ? raw_len - off : 65535;
        z[zn++] = (k == blocks - 1) ? 1 : 0;
        z[zn++] = len & 0xFF;
        z[zn++] = (len >> 8) & 0xFF;
        z[zn++] = ~len & 0xFF;
        z[zn++] = (~len >> 8) & 0xFF;
        memcpy(z + zn, raw + off, len);
        zn += len;
    }
    for (size_t i = 0; i < raw_len; i++) {
        a = (a + raw[i]) % 65521;
        bsum = (bsum + a) % 65521;
    }
    const uint32_t adler = (bsum << 16) | a;
    z[zn++] = adler >> 24;
    z[zn++] = adler >> 16;
    z[zn++] = adler >> 8;
    z[zn++] = adler;
    free(raw);

    FILE *f = fopen(path, "wb");
    if (!f) {
        free(z);
        return 0;
    }
    static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    fwrite(sig, 1, 8, f);
    uint8_t ihdr[13] = {0};
    ihdr[0] = W >> 24, ihdr[1] = W >> 16, ihdr[2] = W >> 8, ihdr[3] = W & 0xFF;
    ihdr[4] = H >> 24, ihdr[5] = H >> 16, ihdr[6] = H >> 8, ihdr[7] = H & 0xFF;
    ihdr[8] = 8;
    ihdr[9] = 2;
    chunk(f, "IHDR", ihdr, 13);
    chunk(f, "IDAT", z, (uint32_t)zn);
    chunk(f, "IEND", NULL, 0);
    fclose(f);
    free(z);
    return 1;
}
