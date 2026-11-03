#include "png.h"

#include <stdio.h>
#include <vector>

// Minimal PNG writer: 8-bit grayscale, uncompressed deflate blocks. No
// dependencies; files are larger than a real encoder's but open anywhere.

static uint32_t crc_table[256];

static void crc_init() {
    for (uint32_t n = 0; n < 256; n++) {
        uint32_t c = n;
        for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        crc_table[n] = c;
    }
}

static uint32_t crc(const uint8_t *buf, size_t len, uint32_t c = 0xFFFFFFFFu) {
    for (size_t i = 0; i < len; i++) c = crc_table[(c ^ buf[i]) & 0xFF] ^ (c >> 8);
    return c;
}

static void put32(std::vector<uint8_t> &v, uint32_t x) {
    v.push_back(x >> 24);
    v.push_back(x >> 16);
    v.push_back(x >> 8);
    v.push_back(x);
}

static void chunk(FILE *f, const char *type, const std::vector<uint8_t> &data) {
    std::vector<uint8_t> out;
    put32(out, data.size());
    out.insert(out.end(), type, type + 4);
    out.insert(out.end(), data.begin(), data.end());
    put32(out, crc(out.data() + 4, out.size() - 4) ^ 0xFFFFFFFFu);
    fwrite(out.data(), 1, out.size(), f);
}

bool png_write_gray(const char *path, const uint8_t *pixels, int width, int height) {
    crc_init();

    // Raw image data: each row starts with filter type 0 (none)
    std::vector<uint8_t> raw;
    raw.reserve((size_t)(width + 1) * height);
    for (int y = 0; y < height; y++) {
        raw.push_back(0);
        raw.insert(raw.end(), pixels + (size_t)y * width, pixels + (size_t)(y + 1) * width);
    }

    // zlib stream made of stored (uncompressed) deflate blocks
    std::vector<uint8_t> z = {0x78, 0x01};
    uint32_t a = 1, b = 0;
    for (uint8_t byte : raw) {
        a = (a + byte) % 65521;
        b = (b + a) % 65521;
    }
    for (size_t pos = 0; pos < raw.size() || pos == 0;) {
        size_t len = raw.size() - pos;
        if (len > 65535) len = 65535;
        bool last = pos + len == raw.size();
        z.push_back(last ? 1 : 0);
        z.push_back(len & 0xFF);
        z.push_back(len >> 8);
        z.push_back(~len & 0xFF);
        z.push_back((~len >> 8) & 0xFF);
        z.insert(z.end(), raw.begin() + pos, raw.begin() + pos + len);
        pos += len;
        if (last) break;
    }
    put32(z, (b << 16) | a);

    FILE *f = fopen(path, "wb");
    if (!f) return false;

    static const uint8_t signature[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    fwrite(signature, 1, 8, f);

    std::vector<uint8_t> ihdr;
    put32(ihdr, width);
    put32(ihdr, height);
    ihdr.insert(ihdr.end(), {8, 0, 0, 0, 0});  // 8-bit, grayscale, no interlace
    chunk(f, "IHDR", ihdr);
    chunk(f, "IDAT", z);
    chunk(f, "IEND", {});

    return fclose(f) == 0;
}
