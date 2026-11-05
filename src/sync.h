#pragma once

#include <stddef.h>

// Strava: sign-in and uploading finished rides over home Wi-Fi.
//
// Sign-in (from Wi-Fi setup): the phone goes to strava_authorize_url(),
// Strava sends it back to http://coaster.local/callback with a code, and
// strava_link() swaps that for a refresh token, kept in NVS.
//
// Sync: a background task joins home Wi-Fi, gets a fresh access token, and
// uploads every finished ride (*.gpx). Uploaded rides are renamed *.sent, never
// deleted; rides Strava refuses become *.rejected. Wi-Fi is off afterwards.

// Strava's sign-in page for this device. state comes back in the callback.
void strava_authorize_url(char *out, size_t size, const char *state);

// Swap the code from the callback for tokens and keep the refresh token.
// Blocks for a few seconds. Needs a network connection.
bool strava_link(const char *code);

// Home Wi-Fi saved and signed in to Strava, with rides waiting: start a sync
void sync_start();
bool sync_running();
// A ride is starting or setup is opening: stop after the current upload
void sync_cancel();
