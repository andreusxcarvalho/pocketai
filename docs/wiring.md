# Wiring

All 11 XIAO pins are used. The mic, SD card and camera are on the Sense board.

```
                         ┌──────────────────────────────┐
                         │   XIAO ESP32S3 (+ Sense)     │
   Touch pad REC ────────┤ D0  (GPIO1)          GND     ├──┬── GND bus
   Touch pad MODE ───────┤ D1  (GPIO2)          3V3     ├──┼── 3V3 bus
   LDR divider ──────────┤ D2  (GPIO3)     D10 (GPIO9)  ├──┼── MOSI ── EPD SDA
   Battery divider ──────┤ D3  (GPIO4)     D9  (GPIO8)  ├──┼── MISO (SD only)
   EPD CS ───────────────┤ D4  (GPIO5)     D8  (GPIO7)  ├──┼── SCK  ── EPD SCL
   EPD RES ──────────────┤ D5  (GPIO6)     D7  (GPIO44) ├──┼── EPD BUSY
   EPD D/C ──────────────┤ D6  (GPIO43)                 │  │
                         │  [U.FL] ── antenna           │  │
                         │  BAT+ pad   BAT– pad (under) │  │
                         └────┬───────────┬─────────────┘  │
   LiPo (+) ──[slide switch]──┘           └── LiPo (–) ────┘
```

| WeAct 2.13" e-ink | XIAO |
| --- | --- |
| VCC | 3V3 |
| GND | GND |
| SDA | D10 |
| SCL | D8 |
| CS | D4 |
| D/C | D6 |
| RES | D5 |
| BUSY | D7 |

```
Battery sense (D3)                    LDR (D2)
BAT+ ─[220k]─┬─[220k]─ GND            3V3 ─[GL5528]─┬─[47k]─ GND
             D3                                     D2
             └─[100nF]─ GND                         └─[100nF]─ GND
```

- Battery voltage = reading × 2. The divider draws about 9 µA.
- LDR: about 0.15 V in darkness, 2.5 V at 10 lux, 3.2 V in daylight. Calibrate with `tools/ldr_calibrate.py`.
- Touch pads: about 10 × 10 mm of copper tape inside the case wall (≤1.5 mm plastic), one short wire each, no resistor.
- GPIO3 is a JTAG strapping pin; harmless with default eFuses.
- D6/D7 are the hardware UART pins; logs go over USB instead.
- The user LED shares GPIO21 with the SD chip-select, so v01 only blinks it when the card is idle.
