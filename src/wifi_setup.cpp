#include "wifi_setup.h"

#include <DNSServer.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>
#include <string.h>
#include "qrcode.h"  // Espressif's qrcode component, part of the Arduino core

#define AP_SSID   "Coaster"
// Wi-Fi QR format: phone cameras offer to join. T:nopass = open network.
#define AP_QR     "WIFI:T:nopass;S:" AP_SSID ";;"

// The text above needs version 2 (25x25); 4 leaves room
#define QR_MAX_VERSION 4
#define QR_MAX_SIZE    (17 + 4 * QR_MAX_VERSION)

// Home network, read by sync later. NVS partition, see partitions.csv.
#define PREFS_NS  "wifi"

static DNSServer dns;
static WebServer server(80);
static bool      saved;

static uint8_t qr[QR_MAX_SIZE * QR_MAX_SIZE];
static uint8_t qr_size;

// esp_qrcode_generate hands the code to a callback; copy it out
static void copy_qr(esp_qrcode_handle_t code) {
    int size = esp_qrcode_get_size(code);
    if (size > QR_MAX_SIZE) return;
    for (int y = 0; y < size; y++) {
        for (int x = 0; x < size; x++) qr[y * size + x] = esp_qrcode_get_module(code, x, y);
    }
    qr_size = size;
}

static const char PAGE_HEAD[] =
    "<!doctype html><meta name=viewport content='width=device-width'>"
    "<title>Coaster</title><body style='font:18px sans-serif;margin:24px'>";

static const char FORM[] =
    "<h2>Home Wi-Fi</h2><form method=post action=/save>"
    "<p>Network name<br><input name=ssid maxlength=32 autocapitalize=off required></p>"
    "<p>Password<br><input name=pass type=password maxlength=63></p>"
    "<p><button>Save</button></p></form>";

static void send_page(int code, const char *body) {
    String html = PAGE_HEAD;
    html += body;
    server.send(code, "text/html", html);
}

static void handle_save() {
    String ssid = server.arg("ssid");
    String pass = server.arg("pass");
    // WPA2 passwords are 8-63 characters; empty = an open home network
    if (ssid.length() < 1 || ssid.length() > 32 || (pass.length() > 0 && pass.length() < 8) ||
        pass.length() > 63) {
        send_page(400, "<p>The password needs 8 to 63 characters.</p>"
                       "<p><a href=/>Back</a></p>");
        return;
    }
    // shortcut: saved without trying to join, so a typo shows up only at sync;
    // check the network once sync joins it
    Preferences prefs;
    if (!prefs.begin(PREFS_NS, false) || !prefs.putString("ssid", ssid) ||
        prefs.putString("pass", pass) != pass.length()) {
        prefs.end();
        send_page(500, "<p>Couldn't save. Try again.</p><p><a href=/>Back</a></p>");
        return;
    }
    prefs.end();
    Serial.printf("# wifi setup: saved \"%s\"\n", ssid.c_str());
    send_page(200, "<h2>Saved</h2><p>You can close this page.</p>");
    saved = true;
}

// Phones probe a known URL to find out whether they're online (Apple:
// /hotspot-detect.html, Android: /generate_204). Anything other than the
// expected answer makes them open the portal, so every unknown URL redirects
// to the form.
static void handle_other() {
    server.sendHeader("Location", "http://" + WiFi.softAPIP().toString() + "/");
    server.send(302, "text/plain", "");
}

const uint8_t *wifi_setup_start(uint8_t *size) {
    esp_qrcode_config_t cfg = {copy_qr, QR_MAX_VERSION, ESP_QRCODE_ECC_MED};
    qr_size = 0;
    if (esp_qrcode_generate(&cfg, AP_QR) != ESP_OK || qr_size == 0) {
        Serial.println("# wifi setup: QR code failed");
        return nullptr;
    }

    if (!WiFi.mode(WIFI_AP) || !WiFi.softAP(AP_SSID)) {
        Serial.println("# wifi setup: access point failed");
        WiFi.mode(WIFI_OFF);
        return nullptr;
    }
    // Every name resolves to the device, so the phone's probe reaches it
    dns.start(53, "*", WiFi.softAPIP());
    server.on("/", HTTP_GET, [] { send_page(200, FORM); });
    server.on("/save", HTTP_POST, handle_save);
    server.onNotFound(handle_other);
    server.begin();
    saved = false;
    Serial.printf("# wifi setup: \"%s\" open at %s\n", AP_SSID,
                  WiFi.softAPIP().toString().c_str());

    *size = qr_size;
    return qr;
}

bool wifi_setup_poll() {
    dns.processNextRequest();
    server.handleClient();
    return saved;
}

void wifi_setup_stop() {
    server.stop();
    dns.stop();
    // Takes the access point down too; softAPdisconnect(true) before it
    // logged ESP_ERR_WIFI_STOP_STATE (Wi-Fi switched off twice)
    WiFi.mode(WIFI_OFF);
    Serial.println("# wifi setup: closed");
}
