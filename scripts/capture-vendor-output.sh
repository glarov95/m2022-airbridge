#!/bin/bash
#
# Capture ground truth from the vendor driver while it still runs (SPEC.md 8.2, M0 remainder).
#
# Method 1 (default, no printer and no root needed): run the exact filter chain cupsd would
# run for the Samsung queue, by hand:
#     PDF --cgpdftoraster--> CUPS raster --rastertosec--> SPL/QPDL
# and keep both the raster intermediate (what the vendor encoder consumed) and its output.
#
# Method 2 (--via-queue): prints the steps for capturing through a temporary CUPS queue with a
# file:// device URI, which records exactly what cupsd sends, PJL included. It needs root and a
# cupsd restart, so it is documented rather than executed.
#
# Outputs, per fixture page and option set, under fixtures/oracle/samsung/:
#     <name>.spl        vendor native output (the golden file)
#     <name>.ras.gz     CUPS raster fed to the vendor filter (gzip)
#     <name>.opts       exact option string used
# plus CAPTURE-INFO.md with tool versions, hashes and raster header summaries.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PPD="${M2022_PPD:-/etc/cups/ppd/Samsung_M2020_Series.ppd}"
FILTER="${M2022_VENDOR_FILTER:-/Library/Printers/Samsung/UPD/Filters/rastertosec}"
QUEUE="${M2022_QUEUE:-Samsung_M2020_Series}"
DEVICE_URI_VALUE="${M2022_DEVICE_URI:-usb://Samsung/M2020%20Series?serial=ZF45B8GF3C01YSD}"
PAGES="$ROOT/fixtures/pages"
OUT="$ROOT/fixtures/oracle/samsung"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

if [[ "${1:-}" == "--via-queue" ]]; then
    cat <<EOF
Method 2: capture through a temporary file:// queue (needs sudo, restarts cupsd).

  sudo sh -c 'grep -q "^FileDevice Yes" /etc/cups/cups-files.conf || echo "FileDevice Yes" >> /etc/cups/cups-files.conf'
  sudo launchctl kickstart -k system/org.cups.cupsd
  sudo lpadmin -p M2022_capture -E -v file:///tmp/m2022-capture.spl -P "$PPD"
  lp -d M2022_capture -o PageSize=A4 "$PAGES/small-text-a4.pdf"
  # wait for the job to complete, then:
  cp /tmp/m2022-capture.spl "$OUT/small-text-a4.viaqueue.spl"
  sudo lpadmin -x M2022_capture
  sudo sed -i '' '/^FileDevice Yes/d' /etc/cups/cups-files.conf
  sudo launchctl kickstart -k system/org.cups.cupsd

Compare with the method-1 file: cmp small-text-a4.spl small-text-a4.viaqueue.spl
EOF
    exit 0
fi

[[ -r "$PPD" ]] || { echo "PPD not found: $PPD" >&2; exit 1; }
[[ -x "$FILTER" ]] || { echo "vendor filter not found: $FILTER" >&2; exit 1; }
mkdir -p "$OUT"

# The vendor filter dereferences a NULL FILE* (fscanf in DoPageProcessCMS3) when manual duplex is
# selected and this cache directory is missing; cupsd normally creates it. /Library/Caches is
# sticky and admin-writable, so no privileges are needed.
if [[ ! -d /Library/Caches/com.sec.printer ]]; then
    mkdir -p /Library/Caches/com.sec.printer 2>/dev/null \
        || echo "warning: cannot create /Library/Caches/com.sec.printer; the manual-duplex variant will fail" >&2
fi

# Summarise a CUPS raster stream header (v2/v3, either endianness). Offsets are file offsets:
# 4-byte sync word, then cups_page_header2_t (1796 bytes).
rasinfo() {
    python3 - "$1" <<'PY'
import struct, sys, os
p = sys.argv[1]
with open(p, "rb") as f:
    hdr = f.read(1800)
size = os.path.getsize(p)
sync = hdr[:4]
if sync in (b"RaS2", b"RaS3"):
    e = ">"
elif sync in (b"2SaR", b"3SaR"):
    e = "<"
else:
    print(f"unknown sync {sync!r}, size {size}")
    sys.exit(0)
u = lambda off: struct.unpack(e + "I", hdr[off:off + 4])[0]
cstr = lambda off: hdr[off:off + 64].split(b"\0", 1)[0].decode("ascii", "replace")
info = dict(sync=sync.decode(), media_type=cstr(132), duplex=u(276), hwres=f"{u(280)}x{u(284)}",
            copies=u(344), pagesize_pt=f"{u(356)}x{u(360)}", width=u(376), height=u(380),
            bits_per_color=u(388), bits_per_pixel=u(392), bytes_per_line=u(396),
            color_order=u(400), color_space=u(404), compression=u(408), row_count=u(412),
            row_feed=u(416), row_step=u(420), page_size_name=cstr(1736), file_size=size)
if sync in (b"RaS2", b"2SaR"):
    page_bytes = 1800 + info["bytes_per_line"] * info["height"]
    info["pages"] = round(size / page_bytes, 3)
print(", ".join(f"{k}={v}" for k, v in info.items()))
PY
}

MANIFEST="$OUT/CAPTURE-INFO.md"
{
    echo "# Vendor output capture"
    echo
    echo "Captured: $(date -u +%Y-%m-%dT%H:%M:%SZ) on $(sw_vers -productName) $(sw_vers -productVersion) ($(uname -m)), CUPS $(ipptool --version 2>/dev/null || echo '?')"
    echo "Method: 1 (cupsfilter → cgpdftoraster → rastertosec by hand). See scripts/capture-vendor-output.sh."
    echo
    echo "Queue: $QUEUE"
    echo "PPD: $PPD (sha256 $(shasum -a 256 "$PPD" | cut -c1-64))"
    echo "Filter: $FILTER (sha256 $(shasum -a 256 "$FILTER" | cut -c1-64), $(file -b "$FILTER" | cut -c1-80))"
    echo "Driver package version: $(defaults read /Library/Printers/Samsung/UPD/Version.plist CFBundleShortVersionString 2>/dev/null || echo '?')"
    echo
    echo "| file | copies | options | raster header | spl bytes | spl sha256 |"
    echo "|---|---|---|---|---|---|"
} > "$MANIFEST"

# capture <name> <pdf> <copies> <option>...
capture() {
    local name="$1" pdf="$2" copies="$3"; shift 3
    local opts=("$@")
    local optstr="${opts[*]}"
    local cf_args=()
    for o in "${opts[@]}"; do cf_args+=(-o "$o"); done

    echo "== $name  (copies=$copies; $optstr)"
    mkdir -p "$(dirname "$TMP/$name")" "$(dirname "$OUT/$name")"
    cupsfilter -p "$PPD" -i application/pdf -m application/vnd.cups-raster -t fixture -n "$copies" "${cf_args[@]}" "$pdf" \
        > "$TMP/$name.ras" 2> "$TMP/$name.cupsfilter.log"

    # Run the vendor filter with the CUPS filter calling convention:
    #   argv: job-id user title copies options [file]
    # The binary is x86_64 and runs under Rosetta 2.
    PPD="$PPD" PRINTER="$QUEUE" PRINTER_INFO="$QUEUE" PRINTER_LOCATION="fixture" \
    DEVICE_URI="$DEVICE_URI_VALUE" CONTENT_TYPE=application/vnd.cups-raster \
    FINAL_CONTENT_TYPE="printer/$QUEUE" CHARSET=utf-8 LANG=en_US.UTF-8 \
    CUPS_SERVERROOT=/etc/cups CUPS_DATADIR=/usr/share/cups CUPS_SERVERBIN=/usr/libexec/cups \
    "$FILTER" 1 fixture fixture "$copies" "$optstr" "$TMP/$name.ras" \
        > "$OUT/$name.spl" 2> "$TMP/$name.filter.log" && rc=0 || rc=$?
    if [[ $rc -ne 0 ]]; then
        echo "   vendor filter FAILED (exit $rc); recorded in manifest, no .spl kept" >&2
        rm -f "$OUT/$name.spl"
        printf '%s\n' "$optstr" > "$OUT/$name.opts"
        echo "| $name | $copies | $optstr | (not captured: vendor filter exit $rc) | | |" >> "$MANIFEST"
        return 0
    fi
    [[ -f "$OUT/filter-stderr.example.log" ]] || cp "$TMP/$name.filter.log" "$OUT/filter-stderr.example.log"

    gzip -9 -c "$TMP/$name.ras" > "$OUT/$name.ras.gz"
    printf '%s\n' "$optstr" > "$OUT/$name.opts"
    local info; info="$(rasinfo "$TMP/$name.ras")"
    local sz; sz="$(stat -f %z "$OUT/$name.spl")"
    local sha; sha="$(shasum -a 256 "$OUT/$name.spl" | cut -c1-64)"
    echo "| $name | $copies | $optstr | $info | $sz | $sha |" >> "$MANIFEST"
    echo "   raster: $info"
    echo "   spl: $sz bytes"
}

# The unicode page is derived from text with Apple's text filter so it gets real glyphs.
for media in a4 letter; do
    ps=$([[ $media == a4 ]] && echo A4 || echo Letter)
    cupsfilter -p "$PPD" -i text/plain -m application/pdf -o PageSize=$ps -o cpi=12 -o lpi=7 \
        "$PAGES/unicode-text.txt" > "$PAGES/unicode-text-$media.pdf" 2> "$TMP/unicode-$media.log"
    head -c 4 "$PAGES/unicode-text-$media.pdf" | grep -q '%PDF' || { echo "text→PDF failed for $media" >&2; cat "$TMP/unicode-$media.log" >&2; exit 1; }
done

BASE=(SECResolution=Normal id_SaveMode=Off SECManualDuplexOption=None InputSlot=Auto MediaType=Default id_EdgeEnhance=Normal SkipBlankPages=False)

for pdf in "$PAGES"/*.pdf; do
    name="$(basename "$pdf" .pdf)"
    media="${name##*-}"
    ps=$([[ $media == a4 ]] && echo A4 || echo Letter)
    capture "$name" "$pdf" 1 "PageSize=$ps" "${BASE[@]}"
done

# Option variants on one page, to learn what each option changes in the vendor output.
P="$PAGES/small-text-a4.pdf"
capture small-text-a4.best        "$P" 1 PageSize=A4 SECResolution=Best   id_SaveMode=Off SECManualDuplexOption=None InputSlot=Auto MediaType=Default id_EdgeEnhance=Normal SkipBlankPages=False
capture small-text-a4.tonersave   "$P" 1 PageSize=A4 SECResolution=Normal id_SaveMode=On  SECManualDuplexOption=None InputSlot=Auto MediaType=Default id_EdgeEnhance=Normal SkipBlankPages=False
capture small-text-a4.duplex-long "$P" 1 PageSize=A4 SECResolution=Normal id_SaveMode=Off SECManualDuplexOption=LongEdge InputSlot=Auto MediaType=Default id_EdgeEnhance=Normal SkipBlankPages=False
capture small-text-a4.manualfeed  "$P" 1 PageSize=A4 SECResolution=Normal id_SaveMode=Off SECManualDuplexOption=None InputSlot=Manual MediaType=Thick id_EdgeEnhance=Normal SkipBlankPages=False
capture small-text-a4.edge-off    "$P" 1 PageSize=A4 SECResolution=Normal id_SaveMode=Off SECManualDuplexOption=None InputSlot=Auto MediaType=Default id_EdgeEnhance=Off SkipBlankPages=False
capture small-text-a4.edge-max    "$P" 1 PageSize=A4 SECResolution=Normal id_SaveMode=Off SECManualDuplexOption=None InputSlot=Auto MediaType=Default id_EdgeEnhance=Maximum SkipBlankPages=False
capture small-text-a4.copies2     "$P" 2 PageSize=A4 "${BASE[@]}"
capture blank-a4.skipblank        "$PAGES/blank-a4.pdf" 1 PageSize=A4 SECResolution=Normal id_SaveMode=Off SECManualDuplexOption=None InputSlot=Auto MediaType=Default id_EdgeEnhance=Normal SkipBlankPages=True

# Media sweep: one small job per PageSize the PPD offers, to derive the band-width rule.
python3 "$ROOT/scripts/gen-fixtures.py" --media-sweep "$PAGES/media" > /dev/null
for pdf in "$PAGES"/media/*.pdf; do
    size="$(basename "$pdf" .pdf)"
    capture "media/$size" "$pdf" 1 "PageSize=$size" "${BASE[@]}"
done

echo; echo "Done. Manifest: $MANIFEST"; du -sh "$OUT"
