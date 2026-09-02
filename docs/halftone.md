# Halftoning

Module `src/halftone/`, header `include/m2022/halftone.h`. Pure, one line at a time, so a
band-based encoder never needs the whole page in memory. Input is ink coverage 0..255 from
`raster.md`; output is packed rows, 1 = black, MSB first.

## Why it matters

The M2022 puts toner down or not; there is no gray dot. Everything that looks like gray on paper
is a pattern of black dots, and the pattern is decided here. The vendor driver's quality
settings, toner save and edge enhancement all live in this stage (`spl-qpdl.md` section 4), so
this is exactly the part of the product where we can do better than the original.

## Methods

| method | idea | character |
|---|---|---|
| `threshold` | black when ink > T | crisp text and lines; no tones |
| `bayer4`, `bayer8` | ordered dither with the dispersed Bayer matrix (16 or 64 levels) | fast, regular texture, fine detail survives |
| `clustered` | ordered dither with a 45° two-centre dot screen, cell 4..16 | the classic laser screen, and what the vendor uses; robust on toner engines because dots are clusters |
| `blue-noise` | 64×64 void-and-cluster mask (`scripts/gen-bluenoise.py`) | tone without visible structure, isolated dots |
| `floyd-steinberg` | error diffusion, serpentine scan | best tonal fidelity for photos, worm artefacts at some tones |

Ordered methods compare ink against a threshold from a matrix indexed by (x mod cell, y mod
cell). Ranks are scaled to 2..254 so that solid black and pure white are exact for every method,
and a constant patch produces exactly the expected number of dots (tested).

The clustered screen is generated, not typed in: two dot centres per cell on a checkerboard,
cells ranked by distance to the nearest centre with the two dots growing in lockstep. At 600 dpi
an 8-pixel cell gives 106 lines per inch at 45°, close to the vendor's screen seen in decoded
pages.

Floyd–Steinberg keeps two rows of error and alternates scan direction (serpentine), which
removes the diagonal bias of one-directional diffusion. Errors are integers; the code is
deterministic, and determinism is tested.

## Presets (SPEC.md 7.1)

| preset | method | tone | intent |
|---|---|---|---|
| draft | bayer8 | dot gain 0.10, coverage 0.75 | fast, light, toner save |
| normal | blue-noise | dot gain 0.15 | documents with graphics |
| text | threshold at ink 96 (≈ sGray 190) | no gain | keeps anti-aliased edges and light hairlines; iOS renders plans with hairlines that a 50 % threshold would erase |
| photo | floyd-steinberg | dot gain 0.20 | photographs |
| vendor | clustered 8 | dot gain 0.15 | the vendor's kind of screen, for comparison |

## Measured against the vendor

`tests/unit/test_halftone_vendor.c` runs the vendor's own input raster through our pipeline and
compares with the vendor's output bitmap decoded from the matching job:

| fixture | preset | pixel agreement | recall | precision |
|---|---|---|---|---|
| small-text-a4 | text | 99.99 % | 1.000 | 0.999 |
| black-square-a4 | normal | 100 % | | |
| gray-ramp-a4 | normal | ink 12.3 % vs vendor 10.7 % | | |

So the vendor thresholds text almost exactly where our Text preset does, and our tone curve
currently lays down a little more ink in the tones than the vendor's screen. That gap is what
M10 calibrates with scans.

## Pictures

`m2022-airbridge render fixtures/oracle/samsung/photo-a4.ras.gz --method M --out x.pbm` for each
method, and `render artifacts/capture/job-004-p1.pgm --preset text` on the page the iPhone sent;
crops under `artifacts/halftone/` (local, not committed).

## What this teaches

Halftoning is signal processing with a one-bit output: ordered dither trades tone for
resolution in a fixed pattern, error diffusion pushes the quantisation error to neighbours so the
local average is right, and blue noise is the spectral shape you want that error to have. The
engine's physics decide which trade is right: isolated dots print unreliably on toner, which is
why laser printers used clustered screens for decades.
