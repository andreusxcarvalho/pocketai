// Pocket AI recorder firmware. The product version (v01..v10) is chosen by the PlatformIO
// environment; see include/pocket_features.h and docs/versions.md.
#include <Arduino.h>

#include "app.h"

void setup() { app.setup(); }
void loop() { app.loop(); }
