#!/bin/sh
# Print N copies of the smallest useful page (the black square, 6 KB, 11 bands) through the
# installed daemon over IPP, one job each, and check that every job completes. Uses paper
# and a little toner: run on purpose. Usage: soak.sh BIN FIXTURE_DIR [PAGES] [PORT]
set -eu
BIN=$1
FIXTURES=$2
PAGES=${3:-2}
PORT=${4:-8000}
WORK=${TMPDIR:-/tmp}/m2022-soak.$$
mkdir -p "$WORK"
trap 'rm -rf "$WORK"' EXIT
gunzip -c "$FIXTURES/black-square-a4.ras.gz" > "$WORK/page.pwg"

"$BIN" doctor --no-service --port "$PORT" > "$WORK/doctor.txt" ||
    { cat "$WORK/doctor.txt"; exit 1; }
i=0
while [ $i -lt "$PAGES" ]; do
    i=$((i + 1))
    ipptool -q -f "$WORK/page.pwg" -d filetype=image/pwg-raster "ipp://localhost:$PORT/ipp/print" \
        /usr/share/cups/ipptool/print-job.test
    echo "job $i submitted"
done
# wait until no job is pending or processing
i=0
while ipptool -c "ipp://localhost:$PORT/ipp/print" /usr/share/cups/ipptool/get-jobs.test 2>/dev/null |
    grep -qE ',(pending|processing),'; do
    i=$((i + 1))
    [ $i -le 240 ] || { echo "jobs still active after 2 minutes"; exit 1; }
    sleep 0.5
done
completed=$(ipptool -c "ipp://localhost:$PORT/ipp/print" "$(dirname "$0")/get-jobs-completed.test" \
    2>/dev/null | grep -c ',completed' || true)
echo "soak: $PAGES jobs submitted, $completed completed jobs in the printer's history"
