#pragma once

// Brings up the W5500 SPI Ethernet link (same wiring/config as the ktech
// boards - see ~/workspace/ktech/src/ktech/main/eth.c) and blocks until an
// IP address is obtained via DHCP. Only compiled when
// CONFIG_MAXHEADRON_USE_ETHERNET is set; call once, early in app_main,
// instead of wifi_connect_blocking().
void eth_connect_blocking(void);
