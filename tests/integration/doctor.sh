#!/bin/sh
# `doctor --no-service` against a server started by hand: the Bonjour and IPP checks must
# pass, so the doctor can tell a working server from a missing one. Usage: doctor.sh BIN WORK
set -eu
BIN=$1
WORK=$2
NAME="M2022 doctor test"

rm -rf "$WORK"
mkdir -p "$WORK/spool"
PORT=$((8100 + $$ % 900))
"$BIN" server --port "$PORT" --name "$NAME" --device "file://$WORK/device.out" \
    --spool "$WORK/spool" --log "$WORK/server.log" --no-tls > "$WORK/server.out" 2>&1 &
PID=$!
trap 'kill $PID 2>/dev/null || true; wait $PID 2>/dev/null || true' EXIT
sleep 2

# with the server up: bonjour and ipp ok (the usb check depends on the printer; ignore it)
"$BIN" doctor --no-service --port "$PORT" --name "$NAME" > "$WORK/doctor.txt" || true
cat "$WORK/doctor.txt"
grep -q '^ok   bonjour' "$WORK/doctor.txt" || { echo "bonjour check failed"; exit 1; }
grep -q '^ok   ipp' "$WORK/doctor.txt" || { echo "ipp check failed"; exit 1; }

# without it: both must fail and the exit status says so
kill $PID
wait $PID 2>/dev/null || true
if "$BIN" doctor --no-service --port "$PORT" --name "$NAME" > "$WORK/doctor2.txt"; then
    echo "doctor passed with no server running"
    cat "$WORK/doctor2.txt"
    exit 1
fi
grep -q '^FAIL ipp' "$WORK/doctor2.txt" || { echo "ipp failure not reported"; cat "$WORK/doctor2.txt"; exit 1; }
echo "doctor integration: OK"
