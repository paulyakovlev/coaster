// GPS test: reads the ATGM336H over UART, echoes the raw NMEA sentences and
// prints each parsed fix as CSV over serial. Proves the wiring and baud rate
// before the main firmware relies on it. Indoors the module may take minutes
// to get a fix (it needs its antenna facing the sky), or none at all; try by
// a window or outside. Garbled text instead of "$GNRMC,..." lines means the
// baud rate is wrong.
//
// Flash: pio run -e gps-test -t upload && pio device monitor
//
// Serial commands (type a letter, then Enter):
//   r   raw NMEA echo on/off

#include <Arduino.h>
#include "ride.h"

// GPS UART: module TX -> GP1 on header J1 (GPIO 1, per the schematic in
// vendor/.../04_Hardware/Schematics). Nothing goes back to the module: the
// firmware only listens, and GP2 stays free for a GPS power switch.
// -1 leaves TX unassigned; the core picks its default TX1 (GPIO 16, audio
// I2S) only when both pins are -1.
#define GPS_RX_PIN  1   // board RX <- module TX
#define GPS_TX_PIN  -1  // not connected

// ATGM336H (GPS + BeiDou): 9600 8N1 by default, per the ATGM336H-5N user
// manual (section 2.6), tinytronics.nl/product_files/002176_ATGM336H.pdf
#define GPS_BAUD    9600

static NmeaParser nmea;
static bool       raw       = true;
static uint32_t   bytes     = 0;
static uint32_t   last_note = 0;

void setup() {
    Serial.begin(115200);
    Serial1.begin(GPS_BAUD, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
    nmea_init(&nmea);
    Serial.println("# time,valid,lat,lon,speed_kmh,ele_m,sats,hdop");
}

void loop() {
    while (Serial.available()) {
        if (Serial.read() == 'r') raw = !raw;
    }

    while (Serial1.available()) {
        char c = Serial1.read();
        bytes++;
        if (raw) Serial.write(c);
        GpsFix f;
        if (nmea_feed(&nmea, c, &f)) {
            Serial.printf("%lu,%d,%.6f,%.6f,%.1f,%.1f,%u,%.1f\n", (unsigned long)f.time, f.valid,
                          f.lat, f.lon, f.speed_mps * 3.6f, f.ele_m, f.sats, f.hdop);
        }
    }

    // Nothing at all from the module points at wiring (TX/RX swapped?) or power
    if (millis() - last_note >= 5000) {
        last_note = millis();
        if (bytes == 0) Serial.println("# no data from the GPS: check power, and TX/RX crossed");
    }
}
