# Raster ingest and tone

Module `src/raster/`, header `include/m2022/raster.h`. Pure: no I/O, no allocation.

## What arrives

Clients render the page themselves and send it as Apple Raster or PWG Raster (measured in
`ipp-airprint.md`): for A4 at 600 dpi that is 4960×7015 pixels of **sGray**, 8 bits per pixel,
0 = black, 255 = white, encoded with the sRGB transfer curve. PAPPL decodes the container and
hands us one line at a time. The vendor's own filter consumed the same geometry from CUPS as
**K** 8-bit, 255 = black, and our fixtures keep those rasters (`fixtures/oracle/samsung/*.ras.gz`),
so the pipeline can be run on exactly the input the vendor had.

`m2022_raster_line_to_gray()` normalises every supported line format to sGray:

| format | source | conversion |
|---|---|---|
| `SGRAY_8` | PWG sgray_8, Apple W8 | copied |
| `BLACK_8` | CUPS colour space K | inverted |
| `BLACK_1` | PWG black_1 | unpacked to 0/255 |
| `SRGB_24` | PWG srgb_8, Apple SRGB24 | linearise, Rec. 709 luminance, re-encode as sGray |

We do not advertise colour raster types, so the sRGB path exists for robustness only.

## Tone curve: from gray to ink

The halftoners want **ink coverage** (0 = no toner, 255 = solid). A halftone patch with coverage
c reflects about (1 − c) of the light (Murray-Davies), so to reproduce the luminance Y encoded by
an sGray value we need c = 1 − Y **in linear light**. That is the whole reason for the sRGB
transfer function here: sGray is perceptual, coverage is physical.

Toner dots spread when fused, so a nominal 50 % patch prints darker: dot gain. We model the
printed coverage as p(c) = c + 4g·c·(1 − c), peaking at +g at 50 %, and invert it so the
requested darkness is what lands on paper. `m2022_tone_build()` bakes gamma (a taste control on
top), dot-gain compensation, a coverage scale (toner save) and black/white clipping points into a
256-entry LUT; `m2022_tone_apply()` is then one table lookup per pixel.

Default: gamma 1.0, dot gain 0.15, scale 1.0. Calibrating the dot gain against scans of the
vendor's ramp is M10 work; the first measurement says our Normal preset puts about 16 % more ink
on the gray ramp than the vendor's screen does.

## Geometry

- `m2022_raster_fit_line()` copies a window of a line with fill outside the source: the encoder
  uses it to place the client's full-page raster into the printer's printable area (12.5 pt
  margins, `include/m2022/media.h`) and to pad to the band width.
- `m2022_raster_upscale2x_line()` doubles a 300 dpi line with linear interpolation, in case a
  client ever sends 300 dpi despite `RS600`.

## CUPS raster header

`m2022_cupsraster_parse_header()` reads the v2/v3 stream header (sync word, then the 1796-byte
`cups_page_header2_t`, integers in the byte order the sync word announces) and
`m2022_cupsraster_pixel_format()` maps it to the formats above. Only uncompressed v3 pages are
consumed; the fixtures are v3 and PAPPL never hands us the container.

## What this teaches

Why "gray" is not one thing: an 8-bit value can be perceptual (sGray), linear (luminance),
device darkness (K) or coverage (ink), and every conversion between them is a transfer function
you have to name. Getting tone right is applying those conversions in the right order, once,
in a table.
