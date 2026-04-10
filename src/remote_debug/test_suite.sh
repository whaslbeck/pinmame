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
    if [[ "$1" == *"$2"* ]]; then echo "  [PASS] $3"; else echo "  [FAIL] $3 (Expected '$2')"; echo "  Body: $1"; kill -9 $PID 2>/dev/null; exit 1; fi
}

echo "1. Verifying Info & Core Connectivity..."
INFO=$(curl -s "http://localhost:$PORT/api/info")
assert_contains "$INFO" "taf_l7" "Game Name"
assert_contains "$INFO" '"wpc_bank":' "WPC Bank Field"
assert_contains "$INFO" '"segments":' "Alphanumeric Segments Field"

echo "2. Verifying UI Endpoint (Embedded)..."
UI=$(curl -s "http://localhost:$PORT/ui")
assert_contains "$UI" "<title>PinMAME Advanced Debugger</title>" "Web UI Loader"

echo "3. Verifying Screenshot API Consistency..."
S_INFO=$(curl -s "http://localhost:$PORT/api/screenshot/info")
assert_contains "$S_INFO" '"width":' "Screenshot Info"
S_RAW_SIZE=$(curl -s "http://localhost:$PORT/api/screenshot/raw" | wc -c)
if [ $S_RAW_SIZE -gt 1000 ]; then echo "  [PASS] Screenshot Raw Data ($S_RAW_SIZE bytes)"; else echo "  [FAIL] Screenshot Raw too small"; kill -9 $PID 2>/dev/null; exit 1; fi
S_PNM_HEAD=$(curl -s "http://localhost:$PORT/api/screenshot/pnm" | head -n 1)
assert_contains "$S_PNM_HEAD" "P6" "Screenshot PNM Header"

echo "4. Resuming Execution..."
curl -s "http://localhost:$PORT/api/debugger/control?cmd=resume" > /dev/null
echo "  Waiting for execution (10s)..."
sleep 10
echo "  Setting Breakpoint at 0x8CC1..."
curl -s "http://localhost:$PORT/api/debugger/breakpoints?cmd=add&addr=0x8CC1" > /dev/null
echo "  Waiting for hit (5s)..."
sleep 5

echo "5. Verifying DMD API Consistency (Post-Init)..."
DMD_INFO=$(curl -s "http://localhost:$PORT/api/dmd/info")
if [[ "$DMD_INFO" == *'"width":128'* ]]; then
    echo "  [PASS] DMD Info"
    DMD_RAW_SIZE=$(curl -s "http://localhost:$PORT/api/dmd/raw" | wc -c)
    if [ $DMD_RAW_SIZE -eq 4096 ]; then echo "  [PASS] DMD Raw Data (4096 bytes)"; else echo "  [FAIL] DMD Raw Size ($DMD_RAW_SIZE)"; kill -9 $PID 2>/dev/null; exit 1; fi
    DMD_PNM_HEAD=$(curl -s "http://localhost:$PORT/api/dmd/pnm" | head -n 1)
    assert_contains "$DMD_PNM_HEAD" "P5" "DMD PNM Header"
else
    echo "  [INFO] DMD not yet initialized by game (skipped consistency check)"
fi

echo "6. Verifying Memory Operations..."
curl -s "http://localhost:$PORT/api/debugger/memory/fill?addr=0x0100&size=16&val=0xAA&cpu=0" > /dev/null
FIND=$(curl -s "http://localhost:$PORT/api/debugger/memory/find?addr=0x0000&pattern=AAAAAAAA&cpu=0" | tr -d ' ')
assert_contains "$FIND" '"found":256' "Memory Fill & Find"

echo "7. Verifying Register Write API..."
# Pause first to ensure the value doesn't get changed by the game immediately
curl -s "http://localhost:$PORT/api/debugger/control?cmd=pause" > /dev/null
# Test 1: Index-based (M6809_B is register 5)
curl -s "http://localhost:$PORT/api/debugger/state/write?reg=5&val=0x42" > /dev/null
STATE=$(curl -s "http://localhost:$PORT/api/debugger/state" | tr -d ' ')
assert_contains "$STATE" '"b":66' "Register Write Index (B=0x42)"
# Test 2: Name-based (Accumulator A)
curl -s "http://localhost:$PORT/api/debugger/state/write?reg=A&val=0x13" > /dev/null
STATE=$(curl -s "http://localhost:$PORT/api/debugger/state" | tr -d ' ')
assert_contains "$STATE" '"a":19' "Register Write Name (A=0x13)"
curl -s "http://localhost:$PORT/api/debugger/control?cmd=resume" > /dev/null

echo "8. Verifying Cabinet Inputs..."
curl -s "http://localhost:$PORT/api/input?sw=13&val=1" > /dev/null
INFO_SW=$(curl -s "http://localhost:$PORT/api/info")
assert_contains "$INFO_SW" "taf_l7" "Input API Connectivity"

echo "9. Verifying Enhanced Callstack API..."
STACK=$(curl -s "http://localhost:$PORT/api/debugger/callstack")
assert_contains "$STACK" '"stack":' "Callstack API Format"
assert_contains "$STACK" '"bank":' "Callstack Banking Info"
assert_contains "$STACK" '"pc":' "Callstack Register Context (PC)"
assert_contains "$STACK" '"u":' "Callstack Register Context (U)"

echo "10. Verifying Live Register Context Execution..."
# Pause
curl -s "http://localhost:$PORT/api/debugger/control?cmd=pause" > /dev/null
# Set PC=FE3C
curl -s "http://localhost:$PORT/api/debugger/state/write?reg=PC&val=FE3C" > /dev/null
# Set A=01
curl -s "http://localhost:$PORT/api/debugger/state/write?reg=A&val=01" > /dev/null
# Step
curl -s "http://localhost:$PORT/api/debugger/control?cmd=step" > /dev/null
# Wait a bit for timeslice to finish
sleep 1
# Check PC
STATE_STEP=$(curl -s "http://localhost:$PORT/api/debugger/state" | tr -d ' ')
# If PC is still FE3C (65084), it failed. We expect it to have moved (e.g. to FE3F = 65087)
if [[ "$STATE_STEP" == *'"pc":65084'* ]]; then
    echo "  [FAIL] Live Register Context: PC stayed at FE3C"
    kill -9 $PID 2>/dev/null
    exit 1
else
    echo "  [PASS] Live Register Context: PC moved from FE3C"
fi

echo "=================================================="
echo "ALL TESTS PASSED"
echo "=================================================="

kill -9 $PID 2>/dev/null
exit 0
