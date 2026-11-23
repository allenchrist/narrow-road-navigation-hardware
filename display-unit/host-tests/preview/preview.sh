#!/bin/bash
# Draws the car screen on your laptop for each recorded server message, so
# you can see layout changes without the board. Needs g++ and the libraries
# PlatformIO downloads: build the display unit in PlatformIO once first.
#
#   cd display-unit/host-tests/preview && ./preview.sh
# Pictures go to ./out (PPM; most image viewers open them).
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
UNIT=$HERE/../..
LIBS=${LIBS:-$UNIT/.pio/libdeps/esp32-8048s050}
[ -d "$LIBS/lvgl" ] || { echo "No $LIBS/lvgl: build the display unit in PlatformIO first, or set LIBS"; exit 1; }
INC="-I$HERE/stubs -I$UNIT/include -I$UNIT/src -I$LIBS/lvgl -I$LIBS/lvgl/src -I$LIBS/ArduinoJson/src -DLV_CONF_INCLUDE_SIMPLE"
mkdir -p "$HERE/build" "$HERE/out"
echo "Compiling LVGL (first time takes a minute)..."
find "$LIBS/lvgl/src" -name "*.c" | while read -r f; do
  o="$HERE/build/$(echo "$f" | md5sum | cut -c1-12).o"
  [ -f "$o" ] || gcc -c -O1 -w $INC "$f" -o "$o"
done
g++ -std=c++17 -O1 -w $INC "$HERE/render.cpp" "$UNIT/src/ui.cpp" "$UNIT/src/fleet.cpp" \
  "$UNIT/src/alert_text.cpp" "$HERE"/build/*.o -o "$HERE/build/render"
"$HERE/build/render" "$HERE/../fixtures/server_updates.json" "$HERE/out"
echo "Done: $(ls "$HERE/out" | wc -l) pictures in $HERE/out"
