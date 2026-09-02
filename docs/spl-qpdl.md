# SPL/QPDL as the Samsung M2020 Series speaks it

Working notes for the encoder and decoder (SPEC.md 6.4, 6.5). Everything here is derived from
the vendor output captured on 2026-09-02 (`fixtures/oracle/samsung/`) and from the unofficial
SpliX SPL2 document (`OpenPrinting/splix`, branch `doc`, `specs-en/*.tex`). No vendor or SpliX
code was read for this; see ADR-010.

Survey tool: `scripts/spl-survey.py`. All multi-byte integers in records are big-endian unless
stated otherwise.

## 1. Job envelope

```text
1B 25 2D 31 32 33 34 35 58          <ESC>%-12345X   universal exit language (UEL)
@PJL DEFAULT SERVICEDATE=20260902\r\n              the only non-deterministic line
@PJL COMMENT OS information : 26.6.2\r\n
@PJL COMMENT Model Name : Samsung M2020 Series\r\n
@PJL COMMENT PPD Version : 3.92\r\n
@PJL COMMENT Model path : /Library/Printers/Samsung/UPD/Filters/\r\n
@PJL SET XIGNOREFF=OFF\r\n                         ON when "Skip blank pages" is selected
\r\n
@PJL SET RESOLUTION = 600\r\n                      1200 in Best mode
@PJL SET BITSPERPIXEL = 1\r\n                      absent in Best mode
@PJL SET PAPERTYPE = OFF\r\n                       THICK, THIN, BOND, ... per MediaType
@PJL SET DUPLEX = OFF\r\n                          MANUAL for manual duplex, then
                                                   @PJL SET BINDING = LONGEDGE | SHORTEDGE
@PJL ENTER LANGUAGE=QPDL\r\n
<records>
09                                                 end of job
1B 25 2D 31 32 33 34 35 58                         UEL, end of file (no @PJL EOJ)
```

The vendor filter carries the `PAPERTYPE` value into the job through the CUPS raster header:
the PPD's `MediaType` choices put the literal PJL string into the header's `MediaType` field.

## 2. Records

Only four record types occur in 41 captured jobs.

### 2.1 Page header, type 0x00, 17 bytes

A4, 600 dpi, auto feeder, one copy (every A4 job in the fixtures):

```text
offset  bytes        meaning
0x00    00           record type: begin page
0x01    06           y resolution / 100          (0C at 1200 dpi)
0x02    00 01        copies (BE16)               always 1 from the vendor filter
0x04    02           paper code                  see table below
0x05    09 AF        paper width  in 1/300 in    2479 = 210 mm  (1/150 in at 1200 dpi: 04 D8)
0x07    0D B4        paper height in 1/300 in    3508 = 297 mm  (1/150 in at 1200 dpi: 06 DA)
0x09    01           feeder: 1 auto, 2 manual    (SpliX: 3 multi, 4 top, 5 bottom, 6 envelopes)
0x0A    00           fixed 0
0x0B    00           duplex                      stays 0 even for manual duplex
0x0C    00           duplex tumble               stays 0
0x0D    00           fixed 0
0x0E    03           QPDL version                3 for this printer
0x0F    01           fixed 1
0x10    06           x resolution / 100          (0C at 1200 dpi)
```

Paper codes observed (SpliX names in parentheses where they differ):

| code | size | code | size |
|---|---|---|---|
| 0x00 | Letter | 0x09 | EnvDL |
| 0x01 | Legal | 0x0B | B5-JIS (JB5) |
| 0x02 | A4 | 0x0C | B5-ISO (B5) |
| 0x03 | Executive | 0x0D | Postcard_S ("not listed" in SpliX) |
| 0x06 | Env10 (Com10) | 0x10 | A5 |
| 0x07 | EnvMonarch | 0x18 | US-Folio (Folio) |
| 0x08 | EnvC5 | 0x1C | Oficio (not in SpliX) |

Paper width/height per size, in 1/300 in: A4 2479×3508, A5 1750×2479, B5-ISO 2079×2954,
B5-JIS 2150×3038, Letter 2550×3300, Legal 2550×4200, Executive 2175×3150, US-Folio 2550×3900,
Oficio 2550×4050, Postcard_S 1200×1800, EnvC5 1913×2704, EnvDL 1300×2600, Env10 1238×2850,
EnvMonarch 1163×2250. These are floor(points × 300 / 72) of the PPD `PaperDimension`.

### 2.2 Band, type 0x0C, 11-byte header + payload

First band of `black-square-a4.spl`:

```text
0C           record type: band
15           band number (21): bands of 128 lines counted from the top of the printable area;
             blank bands are simply omitted
13 00        band width in pixels (4864)
00 80        band height in lines (128)
11           compression type 0x11
00 00 02 4C  payload length (588), checksum included
EF CD AB 09  ... payload, see section 3
```

Band width rule, verified on all 14 sizes: `ceil(raster_width / 256) * 256`, i.e. 32-byte
alignment of the packed 1-bit line. Raster widths come from the printable area at 600 dpi
(page minus 12.5 pt margins on each side): A4 4750 → 4864, Letter 4896 → 5120, A5 3292 → 3328,
B5-ISO 3936 → 4096, B5-JIS 4092 → 4352, Executive 4128 → 4352, Legal/Oficio/US-Folio 4892 → 5120,
Env10 2263 → 2304, EnvC5 3616 → 3840, EnvDL 2368 → 2560, EnvMonarch 2113 → 2304,
Postcard_S 2192 → 2304. At 1200 dpi the A4 band width is 9728 (= 2 × 4864).

Band height is 128 in every band, including the last one on the page.

### 2.3 End of page, type 0x01, 3 bytes

`01 00 01`: copies (BE16). The vendor filter always writes 1 and lets CUPS repeat pages.

### 2.4 End of job, type 0x09, 1 byte

Immediately followed by the UEL.

## 3. Band payload, compression 0x11

Per the SpliX document, confirmed by the fixtures:

```text
offset  size        meaning
0x00    4           magic 0x09ABCDEF, written in host order (little-endian on this Mac:
                    EF CD AB 09). The printer autodetects endianness from it and applies
                    it to the next two fields.
0x04    4           raw length N (≤ 128)
0x08    64 × 2      offset table: 64 unsigned 16-bit back-references (absolute values)
0x88    N           N raw bytes copied to the start of the output
0x88+N  ...         token stream
end     4           checksum, big-endian: sum of all bytes from the band record header
                    (the 0x0C byte) up to but not including the checksum
```

Offset table of the first black-square band (little-endian entries): 0x0080, 0x0100, 0x0180,
0x0200, 0x0280, 0x0300, 0x0380, 0x0400, 0x0001, 0x0181, 0x01FF, 0x0081, 0x0201, 0x027F, ...
Multiples of 128 point at the same byte position in previous lines' columns; 1 and the odd
values point at vertical and diagonal neighbours. The text page's first band starts its table
with 0x0001, 0x0002, 0x0301, 0x0380, ... instead: the vendor tunes the table per band.

Token stream:

```c
if (b1 & 0x80) {                       /* match */
    length = (b1 & 0x7F) + ((b2 & 0xC0) << 1) + 3;   /* 3 .. 514 */
    index  =  b2 & 0x3F;                              /* 0 .. 63 into the table */
    copy `length` bytes from out_pos - table[index]   /* overlapping copies allowed */
} else {                               /* literals */
    length = b1 + 1;                                  /* 1 .. 64 (b1 may be 0..63 or up to 0x7F?) */
    copy the next `length` input bytes
}
```

Established by decoding all 867 vendor bands (`m2022-airbridge decode`, `tests/unit/test_qpdl_fixtures.c`):

- table indices are 0-based (a 1-based reading fails on the first band);
- the checksum is the sum of the payload bytes only, the 11-byte band header excluded;
- every band decodes to exactly width/8 × 128 bytes;
- literal runs use the full range 1..128 (the SpliX document's "64" is not a vendor limit);
- the raw length is usually 128 or 64 but other values occur;
- the offset table changes **per band** within a job (9 distinct tables in the 35-band text
  page, 64 in the 67-band 1200 dpi page), so the printer accepts arbitrary tables. The black
  square, printed from the captured job, used 3 different tables.

The vendor halftone, seen in the decoded pages (`decode --pbm`), is a clustered-dot ordered
screen at roughly 45°, the classic laser screen. Text is thresholded, not dithered.

Band data layout before compression: the band is a 128-line × width-px 1-bit image with 1 = white
(bits inverted relative to PBM), stored column-major by byte: byte k of line 0, byte k of line 1,
..., byte k of line 127, then byte k+1 of line 0, and so on. This puts vertically adjacent
bytes next to each other, which is why offsets of 1 and 128 dominate the table.

## 4. Effect of options (small-text-a4 variants)

| variant | PJL change | header change | band data |
|---|---|---|---|
| Best | `RESOLUTION = 1200`, no `BITSPERPIXEL` | res 0C/0C, paper dims halved, width 9728, 2× bands | 1200 dpi halftone from the 600 dpi gray raster |
| Toner save | none | none | different (lighter) halftone, +18 % bytes |
| Edge enhancement Off | none | none | slightly different (−157 bytes) |
| Edge enhancement Maximum | none | none | identical to Normal |
| Manual feed | none | feeder 02 | identical |
| Media type Thick | `PAPERTYPE = THICK` | none | identical |
| Manual duplex long edge | `DUPLEX = MANUAL`, `BINDING = LONGEDGE` | none | identical |
| Copies 2 | none | none | second page record, identical bands |
| Skip blank pages | `XIGNOREFF=ON` | none | (blank page: zero bands either way) |

So: quality, toner save and edge enhancement live entirely in the vendor's halftoning, which is
exactly the part we replace with our own.

## 5. Sizes for reference

| job | bands | payload bytes |
|---|---|---|
| blank A4 | 0 | 0 |
| black square A4 (50 mm) | 11 | 5 940 |
| small text A4 | 35 | 287 582 |
| small text A4, Best (1200 dpi) | 67 | 521 127 |
| photo A4 | 20 | 109 131 |
| checkerboard A4 | 24 | 50 703 |

## 6. Still unknown (hardware questions, SPEC.md 16)

- Which PJL `SET` lines the printer requires.
- Whether the printer honours the page-footer copies field.
- Whether a different offset table is accepted.
- What `PacketSize 512` in SpliX refers to; nothing in the stream is 512-aligned.
- Whether 1200 dpi prints cleanly.
