#pragma once

// Brings up Wi-Fi station mode using CONFIG_MAXHEADRON_WIFI_SSID/PASSWORD
// and blocks until an IP address is obtained. Retries indefinitely on
// failure (logging as it goes) rather than giving up - this runs once,
// early in app_main, before the animation loop starts.
void wifi_connect_blocking(void);
