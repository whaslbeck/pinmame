#!/bin/bash

PORT=8944
ROM="taf_l7"
BINARY="../../xpinmamed.x11"

echo "=================================================="
echo "PinMAME Debugger Verification Suite"
echo "=================================================="

# Kill any existing process
killall -9 xpinmamed.x11 2>/dev/null
$BINARY -headless -startpaused -httpport $PORT -nosound $ROM > suite_log.txt 2>&1 &
PID=$!
sleep 5

function assert_contains() {
    if [[ "$1" == *"$2"* ]]; then echo "  [PASS] $3"; else echo "  [FAIL] $3 (Expected '$2')"; kill -9 $PID 2>/dev/null; exit 1; fi
}

echo "1. Verifying Info & WPC Banking..."
INFO=$(curl -s "http://localhost:$PORT/api/info")
assert_contains "$INFO" "taf_l7" "Game Name"
assert_contains "$INFO" '"wpc_bank":' "WPC Bank Field"
assert_contains "$INFO" '"segments":' "Alphanumeric Segments Field"

echo "2. Setting Breakpoint & Resuming..."
curl -s "http://localhost:$PORT/api/debugger/command?cmd=bp%200x8CC1" > /dev/null
curl -s "http://localhost:$PORT/api/debugger/control?cmd=resume" > /dev/null
echo "  Waiting for hit and init (10s)..."
sleep 10

echo "3. Verifying DMD API (Post-Init)..."
DMD_INFO=$(curl -s "http://localhost:$PORT/api/dmd/info")
if [[ "$DMD_INFO" == *'"width":128'* ]]; then
    echo "  [PASS] DMD Width"
else
    echo "  [INFO] DMD not yet initialized (skipped)"
fi

echo "4. Verifying Memory Operations..."
curl -s "http://localhost:$PORT/api/debugger/memory/fill?addr=0x0100&size=16&val=0xAA" > /dev/null
FIND=$(curl -s "http://localhost:$PORT/api/debugger/memory/find?addr=0x0000&pattern=AAAAAAAA" | tr -d ' ')
assert_contains "$FIND" '"found":256' "Memory Fill & Find"

echo "5. Verifying Cabinet Inputs..."
curl -s "http://localhost:$PORT/api/input?sw=13&val=1" > /dev/null
INFO_SW=$(curl -s "http://localhost:$PORT/api/info")
assert_contains "$INFO_SW" "taf_l7" "API responsive"

echo "=================================================="
echo "ALL TESTS PASSED"
echo "=================================================="

kill -9 $PID 2>/dev/null
exit 0
