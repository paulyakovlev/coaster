#include "sync.h"

#include <Arduino.h>
#include <LittleFS.h>
#include <NetworkClientSecure.h>
#include <Preferences.h>
#include <WiFi.h>
#include <string.h>
#include "strava.h"

#if __has_include("strava_secrets.h")
#include "strava_secrets.h"
#else
#error "Copy src/strava_secrets.example.h to src/strava_secrets.h and paste the client secret"
#endif

// Strava API app "Coaster" (strava.com/settings/api). The ID isn't secret.
#define STRAVA_CLIENT_ID "294274"
#define STRAVA_HOST      "www.strava.com"
// Must be inside the app's Authorization Callback Domain
#define STRAVA_CALLBACK  "http%3A%2F%2Fcoaster.local%2Fcallback"

#define JOIN_TIMEOUT_MS  15000
// Strava processes an upload in a few seconds; ask this often, this many times.
// Still processing after that: the next sync uploads it again, and Strava
// answers "duplicate", which counts as done.
#define POLL_EVERY_MS    2000
#define POLL_TRIES       10
// shortcut: at most this many rides per sync; the rest go next time
#define MAX_RIDES        16

#define BOUNDARY "coaster7f3a9c1e"

static volatile bool running;
static volatile bool cancel;

// Replies: the token reply carries the athlete profile, about 1 KB.
// One user at a time: setup and sync never run together.
static char reply[3072];

// One HTTPS request to Strava. The body is head, then the file (if any), then
// tail. HTTP/1.0 so the reply isn't chunked, except for uploads (see below).
// The reply body lands in reply[], cut to fit. Returns the status code, or -1
// if the request didn't get through (logged with the step that failed).
static int fail(NetworkClientSecure &client, const char *path, const char *step) {
    char err[100] = "";
    client.lastError(err, sizeof(err));
    Serial.printf("# strava: %s: %s failed (%s)\n", path, step, err);
    return -1;
}

// "HTTP/1.1 201 Created" -> 201, or -1
static int read_status(NetworkClientSecure &client) {
    if (!client.find("HTTP/1.")) return -1;
    client.read();  // minor version digit
    return client.parseInt();
}

static int request(const char *method, const char *path, const char *token, const char *type,
                   const char *head, File *file, const char *tail) {
    reply[0] = '\0';  // never log a previous reply: the token reply holds secrets
    NetworkClientSecure client;
    client.useBuiltinCACertBundle();  // checks Strava's certificate
    client.setTimeout(20000);         // an upload's reply comes once Strava has the whole file
    if (!client.connect(STRAVA_HOST, 443)) return fail(client, path, "connect");

    // Uploads ask before sending the file (Expect: 100-continue, HTTP/1.1 only).
    // A refusal then arrives while the connection is still open; sent with
    // the file, it closed the connection mid-file and the answer was lost.
    // The reply may come chunked: the key finder in lib/strava doesn't mind.
    size_t length = strlen(head) + (file ? file->size() : 0) + strlen(tail);
    client.printf("%s %s HTTP/1.%c\r\nHost: " STRAVA_HOST "\r\n", method, path, file ? '1' : '0');
    if (file) client.print("Connection: close\r\nExpect: 100-continue\r\n");
    if (token) client.printf("Authorization: Bearer %s\r\n", token);
    if (type) client.printf("Content-Type: %s\r\n", type);
    client.printf("Content-Length: %u\r\n\r\n", (unsigned)length);

    int status = 100;
    if (file) {
        // No answer in a few seconds: send anyway, as HTTP allows
        uint32_t t0 = millis();
        while (!client.available() && client.connected() && millis() - t0 < 3000) delay(10);
        if (client.available()) {
            status = read_status(client);
            if (status == 100 && !client.find("\r\n\r\n")) status = -1;
        }
        if (status != 100) Serial.printf("# strava: %s: answered before the file: %d\n", path, status);
    }
    if (status == 100) {
        client.print(head);
        if (file) {
            static uint8_t chunk[1024];
            size_t n, sent = 0;
            while ((n = file->read(chunk, sizeof(chunk))) > 0) {
                if (client.write(chunk, n) != n) break;
                sent += n;
            }
            if (sent != file->size()) {
                Serial.printf("# strava: sent %u of %u bytes\n", (unsigned)sent,
                              (unsigned)file->size());
                return fail(client, path, "sending the file");
            }
        }
        client.print(tail);
        status = read_status(client);
    }

    if (status < 0) return fail(client, path, "waiting for the reply");
    if (!client.find("\r\n\r\n")) return fail(client, path, "reading the reply headers");
    // Until Strava closes the connection (readBytes would wait out the timeout)
    size_t   got = 0;
    uint32_t t0  = millis();
    while ((client.connected() || client.available()) && millis() - t0 < 20000) {
        int c = client.read();
        if (c < 0) {
            delay(5);
        } else if (got < sizeof(reply) - 1) {
            reply[got++] = (char)c;
        }
    }
    reply[got] = '\0';
    client.stop();
    return status;
}

static bool save_refresh(const char *token) {
    Preferences prefs;
    if (!prefs.begin("strava", false)) return false;
    bool ok = prefs.putString("refresh", token) == strlen(token);
    prefs.end();
    return ok;
}

// POST /oauth/token. Saves the refresh token Strava returns: it can change on
// every call, and the old one stops working at once, so it's saved before the
// access token is used. A failed save leaves the device signed out.
static bool token_request(const char *grant, const char *field, const char *value,
                          char *access, size_t access_size) {
    char body[256];
    snprintf(body, sizeof(body),
             "client_id=" STRAVA_CLIENT_ID "&client_secret=" STRAVA_CLIENT_SECRET
             "&grant_type=%s&%s=%s",
             grant, field, value);
    int status = request("POST", "/oauth/token", nullptr, "application/x-www-form-urlencoded",
                         body, nullptr, "");
    char refresh[80];
    if (status != 200) {
        // An error reply carries no tokens, so it's safe to log
        Serial.printf("# strava: token request failed (%d): %.200s\n", status, reply);
        return false;
    }
    if (!strava_json_str(reply, "refresh_token", refresh, sizeof(refresh)) ||
        !strava_json_str(reply, "access_token", access, access_size)) {
        Serial.println("# strava: token reply without tokens");  // not logged: may hold one
        return false;
    }
    if (!save_refresh(refresh)) {
        Serial.println("# strava: couldn't save the refresh token; sign in again");
        return false;
    }
    return true;
}

void strava_authorize_url(char *out, size_t size, const char *state) {
    snprintf(out, size,
             "https://" STRAVA_HOST "/oauth/authorize?client_id=" STRAVA_CLIENT_ID
             "&redirect_uri=%s&response_type=code&approval_prompt=auto"
             "&scope=activity:write&state=%s",
             STRAVA_CALLBACK, state);
}

bool strava_link(const char *code) {
    char access[80];
    return token_request("authorization_code", "code", code, access, sizeof(access));
}

// Upload one ride and wait for Strava to process it. False: stop syncing
// (network, signed out, rate limit); the ride stays queued.
static bool upload(const char *name, const char *token) {
    char path[48];
    snprintf(path, sizeof(path), "/%s", name);
    File f = LittleFS.open(path, "r");
    if (!f) return true;  // gone since the listing: nothing to do

    // external_id lets Strava tell rides apart; it's the file name
    char head[512];
    snprintf(head, sizeof(head),
             "--" BOUNDARY "\r\nContent-Disposition: form-data; name=\"data_type\"\r\n\r\ngpx\r\n"
             "--" BOUNDARY "\r\nContent-Disposition: form-data; name=\"external_id\"\r\n\r\n%s\r\n"
             "--" BOUNDARY "\r\nContent-Disposition: form-data; name=\"file\"; filename=\"%s\"\r\n"
             "Content-Type: application/gpx+xml\r\n\r\n",
             name, name);
    int status = request("POST", "/api/v3/uploads", token,
                         "multipart/form-data; boundary=" BOUNDARY, head, &f,
                         "\r\n--" BOUNDARY "--\r\n");
    f.close();
    if (status != 201) {
        Serial.printf("# sync: %s upload failed (%d): %.200s\n", name, status, reply);
        return false;
    }

    long long id;
    if (!strava_json_int(reply, "id", &id)) return false;
    StravaUpload result = strava_upload_status(reply);
    char poll[48];
    snprintf(poll, sizeof(poll), "/api/v3/uploads/%lld", id);
    for (int i = 0; i < POLL_TRIES && result == STRAVA_UP_PROCESSING; i++) {
        delay(POLL_EVERY_MS);
        if (request("GET", poll, token, nullptr, "", nullptr, "") != 200) return false;
        result = strava_upload_status(reply);
    }

    // Uploaded rides are kept, renamed; deleting them waits for a full flash
    const char *ext = nullptr;
    switch (result) {
    case STRAVA_UP_DONE:       ext = "sent"; break;
    case STRAVA_UP_DUPLICATE:  ext = "sent"; break;
    case STRAVA_UP_REJECTED:   ext = "rejected"; break;
    case STRAVA_UP_PROCESSING: break;  // next sync
    }
    Serial.printf("# sync: %s %s: %.200s\n", name, ext ? ext : "still processing", reply);
    if (ext) {
        char to[48];
        snprintf(to, sizeof(to), "/%.*s.%s", (int)strlen(name) - 4, name, ext);
        if (!LittleFS.rename(path, to)) Serial.printf("# sync: couldn't rename %s\n", name);
    }
    return true;
}

// Finished rides waiting for upload, up to MAX_RIDES. names may be nullptr
// to only count them.
static int queued(char names[][32]) {
    int n = 0;
    File root = LittleFS.open("/");
    for (File f = root.openNextFile(); f && n < MAX_RIDES; f = root.openNextFile()) {
        const char *name = f.name();
        size_t len = strlen(name);
        if (len > 4 && len < 32 && strcmp(name + len - 4, ".gpx") == 0) {
            if (names) strcpy(names[n], name);
            n++;
        }
    }
    return n;
}

static void sync_task(void *) {
    static char names[MAX_RIDES][32];
    int n = queued(names);

    Preferences prefs;
    prefs.begin("wifi", true);
    String ssid = prefs.getString("ssid"), pass = prefs.getString("pass");
    prefs.end();
    prefs.begin("strava", true);
    String refresh = prefs.getString("refresh");
    prefs.end();

    Serial.printf("# sync: %d ride(s), joining \"%s\"\n", n, ssid.c_str());
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), pass.c_str());
    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED && !cancel && millis() - t0 < JOIN_TIMEOUT_MS) delay(100);

    char access[80];
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println(cancel ? "# sync: cancelled" : "# sync: home Wi-Fi not found");
    } else if (token_request("refresh_token", "refresh_token", refresh.c_str(), access,
                             sizeof(access))) {
        for (int i = 0; i < n && !cancel; i++) {
            if (!upload(names[i], access)) break;
        }
    }

    WiFi.mode(WIFI_OFF);
    Serial.println("# sync: done");
    running = false;
    vTaskDelete(nullptr);
}

void sync_start() {
#ifdef FAKE_RIDE
    return;  // made-up rides stay on the device; Strava flags them as fake
#endif
    if (running) return;
    Preferences prefs;
    bool ready = prefs.begin("wifi", true) && prefs.isKey("ssid");
    prefs.end();
    ready = ready && prefs.begin("strava", true) && prefs.isKey("refresh");
    prefs.end();
    if (!ready || queued(nullptr) == 0) return;

    running = true;
    cancel  = false;
    // TLS needs a large stack
    if (xTaskCreate(sync_task, "sync", 12288, nullptr, 1, nullptr) != pdPASS) running = false;
}

bool sync_running() {
    return running;
}

void sync_cancel() {
    cancel = true;
}
