#include "wifi_setup.h"

#include <DNSServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>
#include <string.h>
#include "qrcode.h"  // Espressif's qrcode component, part of the Arduino core
#include "strava.h"
#include "sync.h"

#define AP_SSID   "Coaster"
// Wi-Fi QR format: phone cameras offer to join. T:nopass = open network.
#define AP_QR     "WIFI:T:nopass;S:" AP_SSID ";;"
// Strava sends the phone back here, see STRAVA_CALLBACK in src/sync.cpp
#define MDNS_NAME "coaster"

// "WIFI:..." and "http://192.168.100.200/" need version 2 (25x25); 4 leaves room
#define QR_MAX_VERSION 4
#define QR_MAX_SIZE    (17 + 4 * QR_MAX_VERSION)

#define JOIN_TIMEOUT_MS 15000
// Let the phone get the last page before Wi-Fi changes under it
#define PAGE_GRACE_MS   1000

// Home network, read by sync. NVS partition, see partitions.csv.
#define PREFS_NS  "wifi"

enum Step { STEP_AP, STEP_JOIN, STEP_SIGNIN };

static DNSServer dns;
static WebServer server(80);
static Step      step;
static uint32_t  step_since;
static bool      changed;    // new screen to show
static uint32_t  saved_at;   // home network saved (step 1 done), 0 = not yet
static uint32_t  linked_at;  // signed in (step 2 done), 0 = not yet
static char      state[17];  // sign-in nonce, checked on the callback

static uint8_t     qr[QR_MAX_SIZE * QR_MAX_SIZE];
static uint8_t     qr_size;
static const char *caption;

// esp_qrcode_generate hands the code to a callback; copy it out
static void copy_qr(esp_qrcode_handle_t code) {
    int size = esp_qrcode_get_size(code);
    if (size > QR_MAX_SIZE) return;
    for (int y = 0; y < size; y++) {
        for (int x = 0; x < size; x++) qr[y * size + x] = esp_qrcode_get_module(code, x, y);
    }
    qr_size = size;
}

static void show(const char *text, const char *cap) {
    qr_size = 0;
    if (text) {
        esp_qrcode_config_t cfg = {copy_qr, QR_MAX_VERSION, ESP_QRCODE_ECC_MED};
        if (esp_qrcode_generate(&cfg, text) != ESP_OK) Serial.println("# setup: QR code failed");
    }
    caption = cap;
    changed = true;
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

// ---- Step 1: home Wi-Fi, on the device's own network -------------------------

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
    Preferences prefs;
    if (!prefs.begin(PREFS_NS, false) || !prefs.putString("ssid", ssid) ||
        prefs.putString("pass", pass) != pass.length()) {
        prefs.end();
        send_page(500, "<p>Couldn't save. Try again.</p><p><a href=/>Back</a></p>");
        return;
    }
    prefs.end();
    Serial.printf("# setup: saved \"%s\"\n", ssid.c_str());
    send_page(200, "<h2>Saved</h2><p>Next: Strava. Look at the Coaster.</p>");
    saved_at = millis() | 1;
}

// ---- Step 2: Strava, on home Wi-Fi --------------------------------------------

static void handle_signin() {
    Serial.println("# setup: phone opened the sign-in page");
    char url[320];
    strava_authorize_url(url, sizeof(url), state);
    server.sendHeader("Location", url);
    server.send(302, "text/plain", "");
}

static void handle_callback() {
    // Logged so a sign-in that stalls shows whether the phone got back here
    Serial.println("# setup: Strava sent the phone back");
    // Phones sometimes load the page twice; the code works only once
    if (linked_at) {
        send_page(200, "<h2>Connected</h2><p>Rides upload when the Coaster is home.</p>");
        return;
    }
    // Strava adds error=access_denied when the rider says no
    if (server.arg("state") != state || server.hasArg("error") || !server.hasArg("code")) {
        send_page(400, "<p>Not connected.</p><p><a href=/>Try again</a></p>");
        return;
    }
    if (!strava_scope_ok(server.arg("scope").c_str())) {
        send_page(400, "<p>Coaster needs permission to upload your rides.</p>"
                       "<p><a href=/>Try again</a></p>");
        return;
    }
    if (!strava_link(server.arg("code").c_str())) {
        send_page(502, "<p>Couldn't reach Strava.</p><p><a href=/>Try again</a></p>");
        return;
    }
    Serial.println("# setup: signed in to Strava");
    send_page(200, "<h2>Connected</h2><p>Rides upload when the Coaster is home.</p>");
    linked_at = millis() | 1;
}

// Step 1: phones probe a known URL to find out whether they're online (Apple:
// /hotspot-detect.html, Android: /generate_204). Anything other than the
// expected answer makes them open the portal, so every unknown URL redirects
// to the form.
static void handle_other() {
    if (step != STEP_AP) {
        server.send(404, "text/plain", "");
        return;
    }
    server.sendHeader("Location", "http://" + WiFi.softAPIP().toString() + "/");
    server.send(302, "text/plain", "");
}

static void set_step(Step s) {
    step       = s;
    step_since = millis();
}

static void open_ap() {
    // Wi-Fi first: the network stack must be up before any socket call
    if (!WiFi.mode(WIFI_AP) || !WiFi.softAP(AP_SSID)) {
        Serial.println("# setup: access point failed");
    }
    MDNS.end();
    // Every name resolves to the device, so the phone's probe reaches it
    dns.start(53, "*", WiFi.softAPIP());
    Serial.printf("# setup: \"%s\" open at %s\n", AP_SSID, WiFi.softAPIP().toString().c_str());
    set_step(STEP_AP);
    show(AP_QR, "scan to set up");
}

static bool join() {
    Preferences prefs;
    if (!prefs.begin(PREFS_NS, true) || !prefs.isKey("ssid")) {
        prefs.end();
        return false;
    }
    String ssid = prefs.getString("ssid"), pass = prefs.getString("pass");
    prefs.end();
    WiFi.mode(WIFI_STA);
    dns.stop();
    WiFi.begin(ssid.c_str(), pass.c_str());
    Serial.printf("# setup: joining \"%s\"\n", ssid.c_str());
    set_step(STEP_JOIN);
    show(nullptr, "joining wifi");
    return true;
}

static void open_signin() {
    MDNS.begin(MDNS_NAME);
    snprintf(state, sizeof(state), "%08lx%08lx", (unsigned long)esp_random(),
             (unsigned long)esp_random());
    static char url[32];
    snprintf(url, sizeof(url), "http://%s/", WiFi.localIP().toString().c_str());
    Serial.printf("# setup: Strava sign-in at %s\n", url);
    set_step(STEP_SIGNIN);
    show(url, "scan for Strava");
}

void wifi_setup_start() {
    saved_at = linked_at = 0;
    static bool routes = false;  // WebServer keeps handlers across begin/stop
    if (!routes) {
        routes = true;
            server.on("/", HTTP_GET, [] {
            if (step == STEP_SIGNIN) {
                handle_signin();
            } else {
                send_page(200, FORM);
            }
        });
        server.on("/save", HTTP_POST, handle_save);
        server.on("/callback", HTTP_GET, handle_callback);
        server.onNotFound(handle_other);
    }
    if (!join()) open_ap();
    server.begin();  // after Wi-Fi is on, or it crashes (no network stack yet)
}

SetupEvent wifi_setup_poll() {
    if (step == STEP_AP) dns.processNextRequest();
    server.handleClient();

    if (step == STEP_AP && saved_at && millis() - saved_at >= PAGE_GRACE_MS) {
        saved_at = 0;
        join();
    }
    if (step == STEP_JOIN) {
        if (WiFi.status() == WL_CONNECTED) {
            open_signin();
        } else if (millis() - step_since >= JOIN_TIMEOUT_MS) {
            Serial.println("# setup: couldn't join home Wi-Fi");
            open_ap();
        }
    }
    if (linked_at && millis() - linked_at >= PAGE_GRACE_MS) return SETUP_DONE;

    if (!changed) return SETUP_NONE;
    changed = false;
    return SETUP_SCREEN;
}

void wifi_setup_screen(const uint8_t **qr_out, uint8_t *size, const char **caption_out) {
    *qr_out      = qr_size ? qr : nullptr;
    *size        = qr_size;
    *caption_out = caption;
}

void wifi_setup_stop() {
    server.stop();
    dns.stop();
    MDNS.end();
    // Takes the access point down too; softAPdisconnect(true) before it
    // logged ESP_ERR_WIFI_STOP_STATE (Wi-Fi switched off twice)
    WiFi.mode(WIFI_OFF);
    Serial.println("# setup: closed");
}
