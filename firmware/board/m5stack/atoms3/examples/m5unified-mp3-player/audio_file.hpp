// SPDX-FileCopyrightText: 2021 M5Stack
// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {
uint16_t le16(const uint8_t* p) { return p[0] | unsigned(p[1]) << 8; }
uint32_t le32(const uint8_t* p) { return le16(p) | uint32_t(le16(p + 2)) << 16; }

struct Wav {
    uint32_t rate = 0;
    uint32_t bytes = 0;
    uint16_t channels = 0;
    uint16_t bits = 0;
    uint16_t align = 0;
};

bool parseWav(FILE* file, Wav& wav)
{
    if (std::fseek(file, 0, SEEK_END) != 0) return false;
    const long fileSize = std::ftell(file);
    if (fileSize < 12 || std::fseek(file, 0, SEEK_SET) != 0) return false;
    uint8_t header[12];
    if (std::fread(header, 1, sizeof(header), file) != sizeof(header)
        || std::memcmp(header, "RIFF", 4) || std::memcmp(header + 8, "WAVE", 4)) return false;
    const uint64_t end = uint64_t(le32(header + 4)) + 8;
    if (end > uint64_t(fileSize)) return false;

    for (uint64_t pos = 12; pos + 8 <= end;) {
        uint8_t chunk[8];
        if (std::fread(chunk, 1, sizeof(chunk), file) != sizeof(chunk)) return false;
        const auto len = le32(chunk + 4);
        const auto next = pos + 8 + len + (len & 1u);
        if (next > end) return false;
        if (!std::memcmp(chunk, "fmt ", 4) && !wav.rate) {
            uint8_t fmt[16];
            if (len < sizeof(fmt) || std::fread(fmt, 1, sizeof(fmt), file) != sizeof(fmt)) return false;
            wav.channels = le16(fmt + 2);
            wav.rate = le32(fmt + 4);
            wav.align = le16(fmt + 12);
            wav.bits = le16(fmt + 14);
            if (le16(fmt) != 1 || wav.channels < 1 || wav.channels > 2
                || (wav.bits != 8 && wav.bits != 16) || wav.align != wav.channels * (wav.bits / 8)
                || wav.rate < 1000 || wav.rate > 192000 || le32(fmt + 8) != wav.rate * wav.align)
                return false;
        } else if (!std::memcmp(chunk, "data", 4)) {
            wav.bytes = len;
            return wav.rate && len && len % wav.align == 0;
        }
        if (std::fseek(file, long(next), SEEK_SET) != 0) return false;
        pos = next;
    }
    return false;
}


uint32_t id3Size(const uint8_t* p, bool syncsafe)
{
    const auto shift = syncsafe ? 7u : 8u;
    uint32_t value = 0;
    for (unsigned i = 0; i < 4; ++i) {
        if (syncsafe && (p[i] & 128)) return UINT32_MAX;
        value = (value << shift) | p[i];
    }
    return value;
}

void id3Text(char (&out)[72], const uint8_t* bytes, size_t len)
{
    if (!len) return;
    const unsigned encoding = bytes[0];
    if (encoding > 3) return;
    unsigned i = 1;
    size_t used = 0;
    bool littleEndian = false;
    if (encoding == 1) {
        if (len < 3) return;
        if (bytes[1] == 0xff && bytes[2] == 0xfe) littleEndian = true;
        else if (bytes[1] != 0xfe || bytes[2] != 0xff) return;
        i += 2;
    }
    while (i < len && used < sizeof(out) - 1) {
        unsigned c = bytes[i++];
        if (encoding == 1 || encoding == 2) {
            if (i == len) break;
            c = littleEndian ? c | (unsigned(bytes[i]) << 8) : (c << 8) | bytes[i];
            ++i;
        }
        if (!c) break;
        if (c < 32) c = ' ';
        if (encoding == 3 || c < 128) out[used++] = char(c);
        else if (c < 2048 && used + 2 < sizeof(out)) {
            out[used++] = char(0xc0 | (c >> 6));
            out[used++] = char(0x80 | (c & 63));
        } else if (c >= 2048 && used + 3 < sizeof(out) && (c < 0xd800 || c > 0xdfff)) {
            out[used++] = char(0xe0 | (c >> 12));
            out[used++] = char(0x80 | ((c >> 6) & 63));
            out[used++] = char(0x80 | (c & 63));
        }
    }
    out[used] = 0;
}

struct Tags {
    char title[72]{};
    char performer[72]{};
};

Tags readTags(FILE* file)
{
    Tags tags;
    std::fseek(file, 0, SEEK_END);
    const long length = std::ftell(file);
    std::rewind(file);
    uint8_t header[10];
    long audioStart = 0;
    if (std::fread(header, 1, sizeof(header), file) == sizeof(header) &&
        std::memcmp(header, "ID3", 3) == 0) {
        const auto bytes = id3Size(header + 6, true);
        const uint64_t end = uint64_t(bytes) + 10;
        if (length >= 10 && end <= uint64_t(length)) {
            const unsigned version = header[3];
            const uint64_t footer = version == 4 && (header[5] & 0x10) ? 10 : 0;
            if (end + footer <= uint64_t(length)) audioStart = long(end + footer);
            // Read plain v2.3/v2.4 frames. Skip unsynchronized, compressed,
            // encrypted or extended headers without guessing their contents.
            if ((version == 3 || version == 4) && !(header[5] & 0xc0)) {
                for (uint64_t pos = 10; pos + 10 <= end;) {
                    uint8_t frame[10];
                    if (std::fread(frame, 1, sizeof(frame), file) != sizeof(frame) || !frame[0]) break;
                    const auto frameSize = id3Size(frame + 4, version == 4);
                    const auto next = pos + 10 + frameSize;
                    if (!frameSize || next > end) break;
                    const bool title = std::memcmp(frame, "TIT2", 4) == 0;
                    const bool performer = std::memcmp(frame, "TPE1", 4) == 0;
                    if ((title || performer) && !frame[9]) {
                        uint8_t text[144];
                        const size_t read = std::fread(text, 1, frameSize < sizeof(text) ? frameSize : sizeof(text), file);
                        id3Text(title ? tags.title : tags.performer, text, read);
                    }
                    if (std::fseek(file, long(next), SEEK_SET) != 0) break;
                    pos = next;
                }
            }
        }
    }
    if ((!tags.title[0] || !tags.performer[0]) && length >= 128 &&
        std::fseek(file, length - 128, SEEK_SET) == 0) {
        uint8_t tail[128];
        if (std::fread(tail, 1, sizeof(tail), file) == sizeof(tail) && !std::memcmp(tail, "TAG", 3)) {
            for (unsigned field = 0; field < 2; ++field) {
                auto& dest = field ? tags.performer : tags.title;
                if (dest[0]) continue;
                uint8_t text[31]{};
                std::memcpy(text + 1, tail + 3 + 30 * field, 30);
                size_t end = sizeof(text);
                while (end > 1 && text[end - 1] == ' ') --end;
                id3Text(dest, text, end);
            }
        }
    }
    std::fseek(file, audioStart, SEEK_SET);
    return tags;
}
}
