#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

// Connects to the broker configured via Kconfig and subscribes to the
// "express" topic. Non-blocking - the client runs in its own task and
// reconnects on its own; call once, after Wi-Fi is up.
void mqtt_link_start(void);

// If an externally-published message is currently active, copies it
// (NUL-terminated) into `out` and returns true.
bool mqtt_get_external_message(char *out, size_t out_len, uint32_t now_ms);

// Edge-triggered: returns true at most once per incoming spark request.
bool mqtt_consume_spark_request(void);
