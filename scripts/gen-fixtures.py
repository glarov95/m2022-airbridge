#!/usr/bin/env python3
"""Generate deterministic fixture pages as PDF using only the Python standard library.

Pages (SPEC.md 8.2): blank, black-square, horizontal-lines, vertical-lines, checkerboard,
gray-ramp, small-text, photo, test-sheet, each for A4 and Letter. The unicode-text page is
produced from unicode-text.txt by scripts/capture-vendor-output.sh (macOS text filter), so it
gets a font with non-Latin glyphs.

Geometry is expressed on the 600 dpi device grid (1 px = 0.12 pt) so that 1 px lines and
checkerboard cells land exactly on raster pixels and produce crisp reference rasters.
"""
import math
import os
import random
import sys
import zlib

DPI = 600
PX = 72.0 / DPI  # points per device pixel
MEDIA = {"a4": (595.276, 841.890), "letter": (612.0, 792.0)}
MARGIN_PX = 354  # ~15 mm; multiple of 1 px so images align to the device grid


class PDF:
    def __init__(self):
        self.objs = []

    def add(self, body: bytes) -> int:
        self.objs.append(body)
        return len(self.objs)

    def stream(self, entries: str, data: bytes) -> int:
        data = zlib.compress(data, 9)
        head = f"<< {entries} /Filter /FlateDecode /Length {len(data)} >>\nstream\n".encode()
        return self.add(head + data + b"\nendstream")

    def build(self, root: int) -> bytes:
        out = bytearray(b"%PDF-1.4\n%\xe2\xe3\xcf\xd3\n")
        offsets = []
        for i, body in enumerate(self.objs, 1):
            offsets.append(len(out))
            out += f"{i} 0 obj\n".encode() + body + b"\nendobj\n"
        xref = len(out)
        out += f"xref\n0 {len(self.objs) + 1}\n0000000000 65535 f \n".encode()
        for o in offsets:
            out += f"{o:010d} 00000 n \n".encode()
        out += (f"trailer\n<< /Size {len(self.objs) + 1} /Root {root} 0 R >>\n"
                f"startxref\n{xref}\n%%EOF\n").encode()
        return bytes(out)


def pdf_string(s: str) -> str:
    s = s.encode("cp1252", "replace").decode("latin-1")
    return "(" + s.replace("\\", "\\\\").replace("(", "\\(").replace(")", "\\)") + ")"


# ---- content helpers (all coordinates in points) ------------------------------------

def rect(x, y, w, h, gray=0.0):
    return f"{gray:.4f} g {x:.3f} {y:.3f} {w:.3f} {h:.3f} re f\n"


def px_rect(px_x, px_y, px_w, px_h, gray=0.0):
    """Rectangle in device pixels, aligned to the 600 dpi grid."""
    return rect(px_x * PX, px_y * PX, px_w * PX, px_h * PX, gray)


def stroke_line(x0, y0, x1, y1, width_pt):
    return f"0 G {width_pt:.4f} w {x0:.3f} {y0:.3f} m {x1:.3f} {y1:.3f} l S\n"


def text(font, size, x, y, s, gray=0.0):
    return f"BT {gray:.3f} g /{font} {size} Tf {x:.3f} {y:.3f} Td {pdf_string(s)} Tj ET\n"


def image_place(name, px_x, px_y, px_w, px_h):
    return f"q {px_w * PX:.4f} 0 0 {px_h * PX:.4f} {px_x * PX:.4f} {px_y * PX:.4f} cm /{name} Do Q\n"


# ---- image builders ----------------------------------------------------------------

def checkerboard_image(width_px, regions):
    """1-bit DeviceGray image: bands of checkerboards with the given cell sizes.
    regions: list of (cell_px, height_px). Rows are separated by a 24 px white gap."""
    rows = []
    for cell, height in regions:
        pat_a = ("0" * cell + "1" * cell) * (width_px // (2 * cell) + 1)
        pat_b = ("1" * cell + "0" * cell) * (width_px // (2 * cell) + 1)
        row_a = int(pat_a[:width_px].ljust((width_px + 7) // 8 * 8, "1"), 2).to_bytes((width_px + 7) // 8, "big")
        row_b = int(pat_b[:width_px].ljust((width_px + 7) // 8 * 8, "1"), 2).to_bytes((width_px + 7) // 8, "big")
        for y in range(height):
            rows.append(row_a if (y // cell) % 2 == 0 else row_b)
        white = b"\xff" * ((width_px + 7) // 8)
        rows.extend([white] * 24)
    data = b"".join(rows)
    return data, width_px, len(rows)


def ramp_image(width_px):
    """8-bit DeviceGray single row: 0 (black) to 255 (white) across the width."""
    return bytes(x * 255 // (width_px - 1) for x in range(width_px)), width_px, 1


def photo_image(w=1000, h=750, seed=20260902):
    """Procedural 8-bit gray 'photograph': gradients, shaded spheres, a sharp-edged block,
    a fine sinusoidal texture, a hairline grid, and low-amplitude deterministic noise."""
    rng = random.Random(seed)
    noise = [rng.randint(-3, 3) for _ in range(4096)]
    spheres = [(300, 420, 160, 0.95), (650, 300, 110, 0.75), (780, 560, 80, 0.55)]
    light = (-0.5, -0.6, 0.62)
    ln = math.sqrt(sum(c * c for c in light))
    light = tuple(c / ln for c in light)
    buf = bytearray(w * h)
    i = 0
    for y in range(h):
        base_row = 200 - 110 * y / (h - 1)  # sky-to-ground gradient
        for x in range(w):
            v = base_row
            if 60 <= x < 260 and 520 <= y < 700:  # sharp dark block
                v = 28.0
            elif 880 <= x < 980 and 100 <= y < 300:  # fine texture
                v = 128 + 100 * math.sin(x * 0.9) * math.sin(y * 0.9)
            elif 40 <= x < 240 and 60 <= y < 260 and (x % 20 == 0 or y % 20 == 0):
                v = 10.0  # hairline grid
            for (cx, cy, r, albedo) in spheres:
                dx, dy = x - cx, y - cy
                d2 = dx * dx + dy * dy
                if d2 < r * r:
                    nz = math.sqrt(r * r - d2) / r
                    nx, ny = dx / r, dy / r
                    lam = max(0.0, nx * light[0] + ny * light[1] + nz * light[2])
                    spec = max(0.0, lam) ** 40
                    v = 12 + 210 * albedo * lam + 60 * spec
                    break
            v += noise[i & 4095]
            buf[i] = 0 if v < 0 else 255 if v > 255 else int(v)
            i += 1
    return bytes(buf), w, h


# ---- pages ---------------------------------------------------------------------------

def page_blank(W, H, pdf, res):
    return ""


def page_black_square(W, H, pdf, res):
    side = 50 * 72 / 25.4
    return rect((W - side) / 2, (H - side) / 2, side, side)


def page_lines(W, H, pdf, res, horizontal=True):
    c = ""
    wpx, hpx = int(W / PX), int(H / PX)
    m = MARGIN_PX
    c += text("F1", 8, m * PX, (hpx - m - 60) * PX,
              ("Horizontal" if horizontal else "Vertical") + " lines: px widths 1,2,3,4,6,8; pt widths 0.25,0.5,1,2; then 1 px lines with 1,2,3,4 px gaps")
    pos = m + 200
    # pixel-exact rules
    for wpx_line in (1, 2, 3, 4, 6, 8):
        for k in range(5):
            if horizontal:
                c += px_rect(m, hpx - pos, wpx - 2 * m, wpx_line)
            else:
                c += px_rect(pos, m, wpx_line, hpx - 2 * m - 300)
            pos += 40
        pos += 80
    # stroked lines in points (exercise the rasteriser's stroke adjustment)
    for wpt in (0.25, 0.5, 1.0, 2.0):
        for k in range(3):
            if horizontal:
                y = (hpx - pos) * PX
                c += stroke_line(m * PX, y, (wpx - m) * PX, y, wpt)
            else:
                x = pos * PX
                c += stroke_line(x, m * PX, x, (hpx - 2 * m - 300) * PX, wpt)
            pos += 40
        pos += 80
    # resolution gratings: 1 px lines with 1..4 px gaps
    for gap in (1, 2, 3, 4):
        for k in range(24):
            if horizontal:
                c += px_rect(m, hpx - pos, wpx - 2 * m, 1)
            else:
                c += px_rect(pos, m, 1, hpx - 2 * m - 300)
            pos += 1 + gap
        pos += 100
    # diagonals
    if horizontal:
        c += stroke_line(m * PX, m * PX, (wpx - m) * PX, (m + 1200) * PX, 0.12)
        c += stroke_line(m * PX, (m + 200) * PX, (wpx - m) * PX, (m + 1400) * PX, 1.0)
    return c


def page_checkerboard(W, H, pdf, res):
    wpx, hpx = int(W / PX), int(H / PX)
    width = wpx - 2 * MARGIN_PX
    regions = [(1, 300), (2, 300), (3, 300), (4, 300), (6, 300), (8, 300), (16, 300), (32, 300), (64, 320)]
    data, iw, ih = checkerboard_image(width, regions)
    n = pdf.stream(f"/Type /XObject /Subtype /Image /Width {iw} /Height {ih} /ColorSpace /DeviceGray /BitsPerComponent 1", data)
    res["XObject"]["Im1"] = n
    top = hpx - MARGIN_PX - 100
    c = text("F1", 8, MARGIN_PX * PX, (top + 30) * PX, "Checkerboard cells: 1, 2, 3, 4, 6, 8, 16, 32, 64 px (top to bottom)")
    c += image_place("Im1", MARGIN_PX, top - ih, iw, ih)
    return c


def page_gray_ramp(W, H, pdf, res):
    wpx, hpx = int(W / PX), int(H / PX)
    width = wpx - 2 * MARGIN_PX
    c = text("F1", 8, MARGIN_PX * PX, (hpx - MARGIN_PX - 60) * PX, "Gray ramp: 16 steps (0 %..100 % black), 256 steps, continuous wedge")
    top = hpx - MARGIN_PX - 200
    step = width // 16
    for i in range(16):
        c += px_rect(MARGIN_PX + i * step, top - 400, step, 400, 1.0 - i / 15.0)
    top -= 500
    step = width // 256
    for i in range(256):
        c += px_rect(MARGIN_PX + i * step, top - 400, step, 400, 1.0 - i / 255.0)
    top -= 500
    data, iw, ih = ramp_image(width)
    n = pdf.stream(f"/Type /XObject /Subtype /Image /Width {iw} /Height {ih} /ColorSpace /DeviceGray /BitsPerComponent 8", data)
    res["XObject"]["Im1"] = n
    c += image_place("Im1", MARGIN_PX, top - 400, iw, 400)
    # small patches of specific tones for densitometry
    top -= 600
    for i, g in enumerate((0.9, 0.75, 0.5, 0.25, 0.1, 0.05)):
        c += px_rect(MARGIN_PX + i * 500, top - 400, 400, 400, g)
    return c


PANGRAM = "The quick brown fox jumps over the lazy dog 0123456789 !?%&"


def page_small_text(W, H, pdf, res):
    c = ""
    x = MARGIN_PX * PX
    y = H - MARGIN_PX * PX - 20
    for font in ("F1", "F2"):
        for size in (4, 5, 6, 7, 8, 9, 10, 12):
            y -= size * 1.6
            c += text(font, size, x, y, f"{size} pt {'Helvetica' if font == 'F1' else 'Times'}: {PANGRAM}")
        y -= 14
    # reversed white-on-black block
    y -= 20
    block_h = 90
    c += rect(x, y - block_h, W - 2 * x, block_h, 0.0)
    yy = y - 14
    for size in (5, 6, 8, 10):
        c += text("F1", size, x + 8, yy, f"{size} pt reversed: {PANGRAM}", gray=1.0)
        yy -= size * 1.7
    # accented Latin-1 line (WinAnsi)
    y -= block_h + 24
    c += text("F1", 9, x, y, "Latin-1: Ærø Çà côté naïve façade señor Über groß Ångström €")
    # paragraph of 8 pt text, ragged right
    y -= 30
    words = (PANGRAM + " ").split()
    line, lines = [], []
    for i in range(400):
        line.append(words[i % len(words)])
        if len(" ".join(line)) > 110:
            lines.append(" ".join(line))
            line = []
    for ln in lines[:30]:
        c += text("F2", 8, x, y, ln)
        y -= 10
    return c


def page_photo(W, H, pdf, res):
    data, iw, ih = photo_image()
    n = pdf.stream(f"/Type /XObject /Subtype /Image /Width {iw} /Height {ih} /ColorSpace /DeviceGray /BitsPerComponent 8", data)
    res["XObject"]["Im1"] = n
    wpx, hpx = int(W / PX), int(H / PX)
    # place at 200 image px per inch: 1 image px = 3 device px
    pw, ph = iw * 3, ih * 3
    x = (wpx - pw) // 2
    y = hpx - MARGIN_PX - 200 - ph
    c = text("F1", 8, MARGIN_PX * PX, (hpx - MARGIN_PX - 60) * PX, "Procedural photograph, 1000x750 px placed at 200 ppi (3 device px per image px)")
    c += image_place("Im1", x, y, pw, ph)
    return c


def page_test_sheet(W, H, pdf, res):
    wpx, hpx = int(W / PX), int(H / PX)
    m = MARGIN_PX
    c = text("F1", 11, m * PX, (hpx - m - 40) * PX, "M2022 AirBridge test sheet")
    y = H - (m + 120) * PX
    x = m * PX
    for size in (4, 6, 8, 10):
        c += text("F1", size, x, y, f"{size} pt Helvetica: {PANGRAM}")
        y -= size * 1.6
    for size in (4, 6, 8, 10):
        c += text("F2", size, x, y, f"{size} pt Times: {PANGRAM}")
        y -= size * 1.6
    # reversed
    y -= 10
    c += rect(x, y - 40, W - 2 * x, 40, 0.0)
    c += text("F1", 6, x + 8, y - 14, "6 pt reversed white on black: " + PANGRAM, gray=1.0)
    c += text("F1", 8, x + 8, y - 30, "8 pt reversed white on black: " + PANGRAM, gray=1.0)
    y -= 70
    # lines block
    ypx = int(y / PX)
    for wl in (1, 2, 4):
        c += px_rect(m, ypx, wpx - 2 * m, wl)
        ypx -= 30
    for gap in (1, 2, 4):
        for k in range(16):
            c += px_rect(m, ypx, 1200, 1)
            ypx -= 1 + gap
        ypx -= 40
    c += stroke_line((m + 1400) * PX, (ypx + 200) * PX, (wpx - m) * PX, (ypx + 20) * PX, 0.12)
    c += stroke_line((m + 1400) * PX, (ypx + 220) * PX, (wpx - m) * PX, (ypx + 40) * PX, 1.0)
    ypx -= 80
    # gray steps
    width = wpx - 2 * m
    step = width // 16
    for i in range(16):
        c += px_rect(m + i * step, ypx - 250, step, 250, 1.0 - i / 15.0)
    ypx -= 300
    # checkerboard patch and photo side by side
    data, iw, ih = checkerboard_image(1800, [(1, 150), (2, 150), (4, 150), (8, 150)])
    n = pdf.stream(f"/Type /XObject /Subtype /Image /Width {iw} /Height {ih} /ColorSpace /DeviceGray /BitsPerComponent 1", data)
    res["XObject"]["Im1"] = n
    c += image_place("Im1", m, ypx - ih, iw, ih)
    data, pw_, ph_ = photo_image(w=600, h=450)
    n2 = pdf.stream(f"/Type /XObject /Subtype /Image /Width {pw_} /Height {ph_} /ColorSpace /DeviceGray /BitsPerComponent 8", data)
    res["XObject"]["Im2"] = n2
    c += image_place("Im2", m + 2000, ypx - ph_ * 2, pw_ * 2, ph_ * 2)
    return c


PAGES = {
    "blank": page_blank,
    "black-square": page_black_square,
    "horizontal-lines": lambda W, H, p, r: page_lines(W, H, p, r, True),
    "vertical-lines": lambda W, H, p, r: page_lines(W, H, p, r, False),
    "checkerboard": page_checkerboard,
    "gray-ramp": page_gray_ramp,
    "small-text": page_small_text,
    "photo": page_photo,
    "test-sheet": page_test_sheet,
}

UNICODE_TEXT = """M2022 AirBridge unicode text fixture
English: The quick brown fox jumps over the lazy dog.
Bulgarian: Ах, чудна българска земьо, полюшвай цъфтящи жита!
Greek: Ξεσκεπάζω τὴν ψυχοφθόρα βδελυγμία.
German: Falsches Üben von Xylophonmusik quält jeden größeren Zwerg.
French: Voix ambiguë d'un cœur qui, au zéphyr, préfère les jattes de kiwis.
Polish: Pchnąć w tę łódź jeża lub ośm skrzyń fig.
Czech: Příliš žluťoučký kůň úpěl ďábelské ódy.
Turkish: Pijamalı hasta yağız şoföre çabucak güvendi.
Symbols: € £ ¥ © ® ™ ½ ¾ ± × ÷ → ← ↑ ↓ ∞ ≈ ≠ ≤ ≥ • … — –
Digits: 0123456789  Box: ┌─┬─┐ │ ├─┼─┤ └─┴─┘
"""


# Every PageSize the vendor PPD offers (points), used by --media-sweep to capture one small job
# per size so the band-width rule can be derived for all of them (SPEC.md 6.4).
PPD_SIZES = {
    "A4": (595, 842), "A5": (420, 595), "B5-JIS": (516, 729), "B5-ISO": (499, 709),
    "Letter": (612, 792), "Legal": (612, 1008), "Executive": (522, 756), "US-Folio": (612, 936),
    "Oficio": (612, 972), "Postcard_S": (288, 432), "EnvC5": (459, 649), "EnvDL": (312, 624),
    "Env10": (297, 684), "EnvMonarch": (279, 540),
}


def page_media_probe(W, H, pdf, res):
    """30 mm black square centred plus a 1 px rule 20 mm from the top: fits every size."""
    side = 30 * 72 / 25.4
    c = rect((W - side) / 2, (H - side) / 2, side, side)
    ypx = int((H - 20 * 72 / 25.4) / PX)
    c += px_rect(MARGIN_PX, ypx, int(W / PX) - 2 * MARGIN_PX, 1)
    return c


def build_media_probe(name):
    W, H = PPD_SIZES[name]
    pdf = PDF()
    f1 = pdf.add(b"<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding /WinAnsiEncoding >>")
    content = page_media_probe(W, H, pdf, {"XObject": {}})
    cs = pdf.stream("", content.encode("latin-1"))
    page = pdf.add((f"<< /Type /Page /Parent {len(pdf.objs) + 2} 0 R /MediaBox [0 0 {W} {H}] "
                    f"/Contents {cs} 0 R /Resources << /Font << /F1 {f1} 0 R >> >> >>").encode())
    pages = pdf.add(f"<< /Type /Pages /Kids [{page} 0 R] /Count 1 >>".encode())
    root = pdf.add(f"<< /Type /Catalog /Pages {pages} 0 R >>".encode())
    return pdf.build(root)


def build_page(name, media):
    W, H = MEDIA[media]
    pdf = PDF()
    f1 = pdf.add(b"<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding /WinAnsiEncoding >>")
    f2 = pdf.add(b"<< /Type /Font /Subtype /Type1 /BaseFont /Times-Roman /Encoding /WinAnsiEncoding >>")
    res = {"XObject": {}}
    content = PAGES[name](W, H, pdf, res)
    cs = pdf.stream("", content.encode("latin-1"))
    xobj = "".join(f"/{k} {v} 0 R " for k, v in res["XObject"].items())
    xobj_dict = f"/XObject << {xobj}>>" if xobj else ""
    page = pdf.add((f"<< /Type /Page /Parent {len(pdf.objs) + 2} 0 R /MediaBox [0 0 {W:.3f} {H:.3f}] "
                    f"/Contents {cs} 0 R /Resources << /Font << /F1 {f1} 0 R /F2 {f2} 0 R >> {xobj_dict} >> >>").encode())
    pages = pdf.add(f"<< /Type /Pages /Kids [{page} 0 R] /Count 1 >>".encode())
    root = pdf.add(f"<< /Type /Catalog /Pages {pages} 0 R >>".encode())
    return pdf.build(root)


def main():
    args = sys.argv[1:]
    if args and args[0] == "--media-sweep":
        out_dir = args[1] if len(args) > 1 else os.path.join(os.path.dirname(__file__), "..", "fixtures", "pages", "media")
        os.makedirs(out_dir, exist_ok=True)
        for name in PPD_SIZES:
            path = os.path.join(out_dir, f"{name}.pdf")
            with open(path, "wb") as f:
                f.write(build_media_probe(name))
            print(f"{path}: {os.path.getsize(path)} bytes")
        return
    out_dir = args[0] if args else os.path.join(os.path.dirname(__file__), "..", "fixtures", "pages")
    os.makedirs(out_dir, exist_ok=True)
    with open(os.path.join(out_dir, "unicode-text.txt"), "w", encoding="utf-8") as f:
        f.write(UNICODE_TEXT)
    for name in PAGES:
        for media in MEDIA:
            path = os.path.join(out_dir, f"{name}-{media}.pdf")
            with open(path, "wb") as f:
                f.write(build_page(name, media))
            print(f"{path}: {os.path.getsize(path)} bytes")


if __name__ == "__main__":
    main()
