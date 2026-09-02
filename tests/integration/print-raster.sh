#!/bin/sh
# Print the vendor's own input raster through the whole Printer Application (IPP Print-Job
# with ipptool -> PAPPL -> our pipeline -> job encoder) into a file device, then hold the
# produced job against the vendor's for the same page: same band records, same page header.
# No hardware involved. Usage: print-raster.sh BIN FIXTURE_DIR WORK_DIR
set -eu
BIN=$1
FIXTURES=$2
WORK=$3
IPPTOOL_DIR=/usr/share/cups/ipptool

rm -rf "$WORK"
mkdir -p "$WORK/spool"
PORT=$((8100 + $$ % 900))
gunzip -c "$FIXTURES/black-square-a4.ras.gz" > "$WORK/black-square-a4.pwg"

"$BIN" server --port "$PORT" --device "file://$WORK/device.out" --spool "$WORK/spool" \
    --log "$WORK/server.log" --no-tls > "$WORK/server.out" 2>&1 &
PID=$!
trap 'kill $PID 2>/dev/null || true; wait $PID 2>/dev/null || true' EXIT

i=0
until ipptool -q "ipp://localhost:$PORT/ipp/print" "$IPPTOOL_DIR/get-printer-attributes.test" \
    > /dev/null 2>&1; do
    i=$((i + 1))
    if [ $i -gt 50 ]; then
        echo "server did not come up on port $PORT"
        cat "$WORK/server.out"
        exit 1
    fi
    sleep 0.2
done

ipptool -t -f "$WORK/black-square-a4.pwg" -d filetype=image/pwg-raster \
    "ipp://localhost:$PORT/ipp/print" "$IPPTOOL_DIR/print-job.test"

i=0
until "$BIN" decode "$WORK/device.out" --quiet > "$WORK/decode.txt" 2>&1 &&
    grep -q '1 page(s), 0 error(s)' "$WORK/decode.txt"; do
    i=$((i + 1))
    if [ $i -gt 150 ]; then
        echo "no complete job in the file device after 30 s"
        tail -20 "$WORK/server.log"
        exit 1
    fi
    sleep 0.2
done

"$BIN" decode "$WORK/device.out" > "$WORK/ours.txt"
"$BIN" decode "$FIXTURES/black-square-a4.spl" > "$WORK/vendor.txt"
ours=$(grep 'BAND' "$WORK/ours.txt" | awk '{print $3, $4}' | tr '\n' ' ')
vendor=$(grep 'BAND' "$WORK/vendor.txt" | awk '{print $3, $4}' | tr '\n' ' ')
echo "ours:   $ours"
echo "vendor: $vendor"
if [ "$ours" != "$vendor" ]; then
    echo "band structure differs from the vendor's"
    exit 1
fi
grep -q 'PAGE  1: 600x600 dpi, copies 1, paper 0x02 (A4) 2479x3508/300in, feeder 1' "$WORK/ours.txt" ||
    { echo "page header differs"; grep PAGE "$WORK/ours.txt"; exit 1; }
grep 'end of job' "$WORK/server.log" | tail -1
echo "integration: OK"
