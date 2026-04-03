#!/bin/bash

PORT=8943
ROM="taf_l7"
BINARY="../../xpinmamed.x11"
DUMP1="nvram-initial.bin"
DUMP2="nvram-coins.bin"

echo "--------------------------------------------------"
echo "WPC NVRAM Coin Finder Tool"
echo "--------------------------------------------------"

# 1. Start and wipe
killall -9 xpinmamed.x11 2>/dev/null
echo "1. Starting PinMAME and clearing NVRAM..."
$BINARY -headless -startpaused -httpport $PORT -nosound $ROM > /dev/null 2>&1 &
PID=$!
sleep 5

echo "2. Resetting to Factory Defaults..."
curl -s "http://localhost:$PORT/api/debugger/nvram?cmd=clear" > /dev/null
curl -s "http://localhost:$PORT/api/debugger/control?cmd=resume" > /dev/null

echo "Waiting for initialization (10s)..."
sleep 10

echo "3. Pausing and dumping initial NVRAM..."
curl -s "http://localhost:$PORT/api/debugger/control?cmd=pause" > /dev/null
curl -s "http://localhost:$PORT/api/debugger/nvram/dump" > $DUMP1
echo "Initial dump saved to $DUMP1"

echo "4. Resuming and pulsing Coin 1 (x2)..."
curl -s "http://localhost:$PORT/api/debugger/control?cmd=resume" > /dev/null
sleep 2
echo "Pulse 1..."
curl -s "http://localhost:$PORT/api/input?sw=1&val=1" > /dev/null
sleep 0.5
curl -s "http://localhost:$PORT/api/input?sw=1&val=0" > /dev/null
sleep 2
echo "Pulse 2..."
curl -s "http://localhost:$PORT/api/input?sw=1&val=1" > /dev/null
sleep 0.5
curl -s "http://localhost:$PORT/api/input?sw=1&val=0" > /dev/null
sleep 2

echo "5. Pausing and dumping final NVRAM..."
curl -s "http://localhost:$PORT/api/debugger/control?cmd=pause" > /dev/null
curl -s "http://localhost:$PORT/api/debugger/nvram/dump" > $DUMP2
echo "Final dump saved to $DUMP2"

echo "6. Analyzing differences..."
# Binary diff using ruby
echo "Changes found:"
echo "OFFSET | OLD | NEW"
echo "-------------------"
ruby -e '
  f1 = File.open("nvram-initial.bin", "rb").read
  f2 = File.open("nvram-coins.bin", "rb").read
  f1.each_byte.with_index do |b1, i|
    b2 = f2[i].ord
    if b1 != b2
      printf("0x%04X | %02X  | %02X\n", i, b1, b2)
    end
  end
'

kill -9 $PID 2>/dev/null
echo "--------------------------------------------------"
echo "Test complete."
