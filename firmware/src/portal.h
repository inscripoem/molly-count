// molly-count — first-run WiFi provisioning: soft AP "molly-count-Setup" +
// wildcard DNS (captive detection) + a single hand-rolled form page.
// Blocking; caller draws the setup screen first.
#pragma once

// Runs the portal for up to timeout_ms. Returns true when credentials were
// saved (caller may immediately try net_wake_cycle()).
bool portal_run(unsigned long timeout_ms);
