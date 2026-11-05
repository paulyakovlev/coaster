#pragma once

#include <stdint.h>

// Wi-Fi setup: the device opens its own network, "Coaster" (open, no
// password), and serves a captive portal where the rider types in the home
// network. Phones open the portal by themselves after joining.

// Open the network and the portal. Returns the QR code to join it
// (*size x *size modules, 1 = black), or nullptr if it failed.
const uint8_t *wifi_setup_start(uint8_t *size);

// Call often while setup is open. True once the home network is saved.
bool wifi_setup_poll();

// Close the portal and switch Wi-Fi off
void wifi_setup_stop();
