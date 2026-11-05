#pragma once

#include <stdint.h>

// Setup, in two steps, each with a QR code on screen:
//
// 1. Home Wi-Fi: the device opens its own network, "Coaster" (open, no
//    password), and serves a captive portal where the rider types in the
//    home network. Phones open the portal by themselves after joining.
// 2. Strava: the device joins home Wi-Fi and shows the address of its sign-in
//    page there. That page sends the phone to Strava, which sends it back to
//    http://coaster.local/callback (src/sync.cpp).
//
// With a home network already saved, setup goes straight to step 2; if it
// can't join, back to step 1.
// shortcut: changing the network while the saved one is in range needs a
// re-flash or out of range; add a "forget Wi-Fi" when it matters

// Start setup. Call wifi_setup_poll() in the same loop pass for the first screen.
void wifi_setup_start();

enum SetupEvent {
    SETUP_NONE,
    SETUP_SCREEN,  // new screen: wifi_setup_screen()
    SETUP_DONE,    // signed in to Strava
};

// Call often while setup is open
SetupEvent wifi_setup_poll();

// What setup shows now: a QR code (nullptr while joining) and a caption
void wifi_setup_screen(const uint8_t **qr, uint8_t *size, const char **caption);

// Close the portal and switch Wi-Fi off
void wifi_setup_stop();
