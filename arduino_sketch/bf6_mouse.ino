// ─────────────────────────────────────────────────────
//  BF6 DMA — Arduino / Teensy / MAKCU Mouse Sketch
//  Compatible with: Leonardo, Pro Micro, Teensy, MAKCU
//  Reads serial commands from attack PC, moves HID mouse
//
//  Protocol: "M <dx> <dy>\n"
//  Example:  "M -5 3\n" → move mouse left 5, down 3
// ─────────────────────────────────────────────────────

#include <Mouse.h>

void setup() {
  Serial.begin(115200);
  Mouse.begin();
}

void loop() {
  if (Serial.available() > 0) {
    String line = Serial.readStringUntil('\n');
    line.trim();

    if (line.startsWith("M ")) {
      // Parse: "M dx dy"
      int spaceIdx = line.indexOf(' ', 2);
      if (spaceIdx > 0) {
        int dx = line.substring(2, spaceIdx).toInt();
        int dy = line.substring(spaceIdx + 1).toInt();

        // Clamp to valid HID range [-127, 127]
        dx = constrain(dx, -127, 127);
        dy = constrain(dy, -127, 127);

        Mouse.move(dx, dy, 0);
      }
    }
  }
}
