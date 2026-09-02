#!/bin/sh
# Throughput and memory of the whole pipeline (SPEC.md 7.4): a multi-page job made from a
# vendor input raster is printed through a hand-started server into a file device; the
# server's peak RSS and the job's wall time come out. No printer, no paper.
# Usage: scripts/measure-throughput.sh [PAGES] [RASTER.ras.gz] [PORT]
set -eu
PAGES=${1:-20}
RASTER=${2:-fixtures/oracle/samsung/small-text-a4.ras.gz}
PORT=${3:-8001}
BIN=./build/src/m2022-airbridge
WORK=build/measure
rm -rf "$WORK"
mkdir -p "$WORK/spool"

# CUPS raster: one sync word, then header+data per page; repeat the page
gunzip -c "$RASTER" > "$WORK/page.ras"
head -c 4 "$WORK/page.ras" > "$WORK/job.pwg"
tail -c +5 "$WORK/page.ras" > "$WORK/page.body"
i=0
while [ $i -lt "$PAGES" ]; do
    cat "$WORK/page.body" >> "$WORK/job.pwg"
    i=$((i + 1))
done

"$BIN" server --port "$PORT" --name "M2022 measure" --device "file://$PWD/$WORK/device.out" \
    --spool "$WORK/spool" --log "$WORK/server.log" --no-tls > "$WORK/server.out" 2>&1 &
PID=$!
trap 'kill $PID 2>/dev/null || true; wait $PID 2>/dev/null || true' EXIT
sleep 2

# peak RSS sampler
( peak=0; while kill -0 $PID 2>/dev/null; do
      rss=$(ps -o rss= -p $PID 2>/dev/null | tr -d ' ')
      [ -n "$rss" ] && [ "$rss" -gt "$peak" ] && peak=$rss
      echo "$peak" > "$WORK/peak_rss_kb"; sleep 0.2; done ) &
SAMPLER=$!

ipptool -q -f "$WORK/job.pwg" -d filetype=image/pwg-raster "ipp://localhost:$PORT/ipp/print" \
    /usr/share/cups/ipptool/print-job.test
i=0
until grep -q 'end of job' "$WORK/server.log"; do
    i=$((i + 1)); [ $i -gt 600 ] || sleep 0.5
    [ $i -le 600 ] || { echo "job did not finish"; exit 1; }
done
kill $PID; wait $PID 2>/dev/null || true
wait $SAMPLER 2>/dev/null || true # it ends by itself once the server is gone

grep -E 'start:|first band|end of job' "$WORK/server.log" | sed -E 's/^.*\[Job [0-9]+\] //' 
start=$(grep 'start:' "$WORK/server.log" | tail -1 | sed -E 's/^.*T([0-9:.]+)Z.*/\1/')
end=$(grep 'end of job' "$WORK/server.log" | tail -1 | sed -E 's/^.*T([0-9:.]+)Z.*/\1/')
secs=$(python3 -c "
import sys
def s(t): h,m,sec=t.split(':'); return int(h)*3600+int(m)*60+float(sec)
print('%.2f' % (s('$end')-s('$start')))")
ppm=$(python3 -c "print('%.1f' % ($PAGES*60/max(float('$secs'),0.001)))")
echo "pages: $PAGES, job time: $secs s ($ppm pages/min), peak RSS: $(cat "$WORK/peak_rss_kb") KB," \
    "output: $(wc -c < "$WORK/device.out") bytes"
