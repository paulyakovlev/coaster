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

// GPS UART pins: TBD. The module isn't wired yet; the candidates are GP1 and
// GP2 on header J1 (GPIO 1 and 2). Set them from the actual wiring.
#error "GPS pins not set: fill in GPS_RX_PIN and GPS_TX_PIN, then delete this line"
#define GPS_RX_PIN  // board RX <- module TX
#define GPS_TX_PIN  // board TX -> module RX

// ATGM336H (GPS + BeiDou). Assumed: 9600 8N1, the usual factory default.
// Not checked against its data sheet; hwtest/gps_test.cpp confirms it.
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
