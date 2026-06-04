#!/usr/bin/env python3
"""U8G2 中文字库子集化工具 — 从 efontCN_12 提取仅源码中使用的汉字"""
import os, sys, glob

def extract_used_chars(src_dir="src"):
    chars = set()
    for ext in ['*.c', '*.cpp', '*.h']:
        for f in glob.glob(f'{src_dir}/**/{ext}', recursive=True):
            try:
                with open(f, 'r', encoding='utf-8', errors='ignore') as fh:
                    for ch in fh.read():
                        cp = ord(ch)
                        if 0x4E00 <= cp <= 0x9FFF or 0x3400 <= cp <= 0x4DBF:
                            chars.add(cp)
            except: pass
    return chars

def main():
    tmpfile = "/tmp/efont_cn_12.bin"
    
    # Extract raw binary if not already done
    if not os.path.exists(tmpfile):
        objfile = ".pio/build/m5stick-c/lib94c/M5GFX/lgfx/Fonts/efont/lgfx_efont_cn.c.o"
        tc = os.path.expanduser("~/.platformio/packages/toolchain-xtensa-esp32/bin")
        ret = os.system(f'"{tc}/xtensa-esp32-elf-objcopy" -O binary -j ".rodata.lgfx_efont_cn_12" "{objfile}" "{tmpfile}" 2>/dev/null')
        if ret != 0:
            print("Error: build first with 'pio run -e m5stick-c'")
            sys.exit(1)

    data = open(tmpfile, "rb").read()
    print(f"Original font: {len(data)} bytes ({len(data)/1024:.1f} KB)")

    needed = extract_used_chars("src")
    needed.add(0x20)  # Always include space
    print(f"Needed Chinese chars: {len(needed)}")

    hdr = data[:23]
    unicode_start = (hdr[21] << 8) | hdr[22]
    upper_a_start = (hdr[17] << 8) | hdr[18]
    lower_a_start = (hdr[19] << 8) | hdr[20]

    # ── Parse ASCII glyphs ──
    def parse_ascii_section(pos, end):
        glyphs = []
        while pos + 2 <= end:
            enc, sz = data[pos], data[pos+1]  # sz includes 2-byte header
            if sz == 0: break
            glyphs.append((enc, pos + 2, sz - 2))  # data_start, data_size
            pos += sz  # sz INCLUDES header!
        return glyphs, pos

    sec1, _ = parse_ascii_section(23, 23 + upper_a_start)
    sec2, _ = parse_ascii_section(23 + upper_a_start, 23 + lower_a_start)
    sec3, _ = parse_ascii_section(23 + lower_a_start, 23 + unicode_start)
    ascii_glyphs = sec1 + sec2 + sec3

    # ── Parse Unicode LUT ──
    base = 23 + unicode_start
    entries = []
    lut_ptr = base
    while lut_ptr + 4 <= len(data):
        offset = (data[lut_ptr] << 8) | data[lut_ptr+1]
        max_cp = (data[lut_ptr+2] << 8) | data[lut_ptr+3]
        if offset > 50000 or (offset == 0 and max_cp == 0 and len(entries) > 5):
            break
        entries.append((offset, max_cp))
        lut_ptr += 4

    # ── Parse Unicode glyphs (block size = next offset) ──
    font_ptr = base
    unicode_glyphs = []
    for i, (offset, max_cp) in enumerate(entries):
        font_ptr += offset
        block_end = font_ptr + entries[i+1][0] if i+1 < len(entries) else len(data)
        
        gpos = font_ptr
        while gpos + 3 <= block_end and gpos + 3 <= len(data):
            cp = (data[gpos] << 8) | data[gpos+1]
            gs = data[gpos+2]      # total_size = 3 (header) + data_size
            if cp == 0 or gs == 0: break
            if gpos + gs > block_end or gpos + gs > len(data): break
            unicode_glyphs.append((cp, gpos + 3, gs - 3))  # data_start, data_size
            gpos += gs             # size INCLUDES 3-byte header!

    print(f"Font: {len(ascii_glyphs)} ASCII + {len(unicode_glyphs)} Unicode glyphs")

    # ── Build subset ASCII data ──
    new_ascii = bytearray()
    offset_map = {}
    for enc, orig_data_off, data_sz in ascii_glyphs:
        total_sz = 2 + data_sz  # header (1 enc + 1 size) + data
        offset_map[enc] = len(new_ascii)
        new_ascii.append(enc & 0xFF)
        new_ascii.append(total_sz & 0xFF)
        new_ascii.extend(data[orig_data_off : orig_data_off + data_sz])

    # ── Filter Unicode glyphs ──
    kept = sorted(
        [(cp, off, sz) for cp, off, sz in unicode_glyphs if cp in needed],
        key=lambda x: x[0]
    )
    print(f"Kept: {len(kept)}/{len(unicode_glyphs)} Unicode glyphs")

    # ── Build new Unicode glyph data ──
    new_glyph = bytearray()
    for cp, orig_off, data_sz in kept:
        total_sz = 3 + data_sz  # header (2 cp + 1 size) + data
        new_glyph.append((cp >> 8) & 0xFF)
        new_glyph.append(cp & 0xFF)
        new_glyph.append(total_sz & 0xFF)
        new_glyph.extend(data[orig_off : orig_off + data_sz])

    # LUT: single block. Offset = LUT byte size (4), since font pointer must
    # jump past the LUT to reach glyph data. In the original font, the first
    # LUT offset equals total LUT size (e.g., 74 entries × 4 = 296 bytes).
    max_cp = max(cp for cp, _, _ in kept) if kept else 0x4E00
    new_lut = bytes([0x00, 0x04, (max_cp >> 8) & 0xFF, max_cp & 0xFF])

    # ── Rebuild header ──
    new_hdr = bytearray(hdr)
    new_unicode_start = len(new_ascii)
    new_hdr[21] = (new_unicode_start >> 8) & 0xFF
    new_hdr[22] = new_unicode_start & 0xFF

    for enc_val, (hi, lo) in [(ord('A'), (17, 18)), (ord('a'), (19, 20))]:
        off = offset_map.get(enc_val)
        if off is not None:
            new_hdr[hi] = (off >> 8) & 0xFF
            new_hdr[lo] = off & 0xFF

    result = bytes(new_hdr) + bytes(new_ascii) + new_lut + bytes(new_glyph)
    print(f"Subset: {len(result)} bytes ({len(result)/1024:.1f} KB)")
    print(f"Saved:  {len(data)-len(result)} bytes ({(len(data)-len(result))/1024:.1f} KB)")

    # ── Write files ──
    with open("/tmp/font_subset.bin", "wb") as f:
        f.write(result)

    os.makedirs("src/fonts", exist_ok=True)
    lines = [
        '/* Auto-generated Chinese font subset */',
        f'/* {len(kept)} Chinese + {len(ascii_glyphs)} ASCII glyphs, {len(result)} bytes */',
        '#include <stdint.h>',
        '#ifndef PROGMEM',
        '#define PROGMEM',
        '#endif',
        f'PROGMEM const uint8_t lgfx_cn_font_subset[{len(result)}] = {{',
    ]
    for i in range(0, len(result), 16):
        chunk = result[i:i+16]
        h = ', '.join(f'0x{b:02X}' for b in chunk)
        lines.append(f'  {h}{"," if i+16 < len(result) else ""}')
    lines.extend(['};', ''])

    with open("src/fonts/cn_font_subset.c", "w") as f:
        f.write('\n'.join(lines))

    with open("src/fonts/cn_font_subset.h", "w") as f:
        f.write('''#ifndef CN_FONT_SUBSET_H
#define CN_FONT_SUBSET_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
extern const uint8_t lgfx_cn_font_subset[];
#ifdef __cplusplus
}
#endif
#endif
''')

    print("Generated: src/fonts/cn_font_subset.c + .h")
    print("Now update hal_display.cpp to use lgfx_cn_font_subset instead of efontCN_12")

if __name__ == '__main__':
    main()
