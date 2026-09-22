/*
 * WeatherNerd — Minimal DNS server for captive portal
 *
 * Resolves all DNS queries to the AP's IP address (192.168.4.1).
 * This hijacks the phone's captive portal detection probes
 * (connectivitycheck.gstatic.com, captive.apple.com, etc.)
 * so they hit our HTTP server, which redirects to the portal page.
 *
 * A queries → respond with AP IPv4 address
 * AAAA queries → respond with empty answer (force IPv4 fallback)
 * Other queries → respond with empty answer
 */

#pragma once

#include "esp_err.h"

/* Start DNS server on port 53.
 * Must be called after WiFi AP is up (needs AP IP address).
 * Resolves all A queries to the AP's IPv4 address. */
esp_err_t dns_server_start(void);

/* Stop the DNS server. Call before wifi_deinit_ap(). */
esp_err_t dns_server_stop(void);
