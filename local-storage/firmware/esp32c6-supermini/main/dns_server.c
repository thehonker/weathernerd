/*
 * WeatherNerd — Minimal DNS server for captive portal
 *
 * Resolves all DNS A queries to the AP's IP address (192.168.4.1).
 * This hijacks the phone's captive portal detection probes
 * (connectivitycheck.gstatic.com, captive.apple.com, etc.)
 * so they hit our HTTP server, which redirects to the portal page.
 *
 * Uses lwIP raw UDP API (udp_new/udp_bind/udp_recv).
 */

#include "dns_server.h"

#include <string.h>
#include "esp_log.h"
#include "esp_netif.h"
#include "lwip/udp.h"
#include "lwip/pbuf.h"
#include "lwip/ip_addr.h"

static const char *TAG = "dns_server";

static struct udp_pcb *s_dns_pcb = NULL;
static uint32_t s_ap_ip_u32;  /* AP IP as raw u32 for DNS response */

/* ---- DNS response builder ----
 * Minimal DNS response: copy the query, set QR=1 (response),
 * add a single A record answer pointing to s_ap_ip. */

static void dns_recv_callback(void *arg, struct udp_pcb *pcb, struct pbuf *p,
                              const ip_addr_t *addr, u16_t port)
{
    if (!p || p->len < 12) {
        if (p) pbuf_free(p);
        return;
    }

    /* Parse the DNS header from the query */
    uint8_t *query = (uint8_t *)p->payload;
    uint16_t flags = (query[2] << 8) | query[3];
    uint16_t qdcount = (query[4] << 8) | query[5];

    /* Only handle standard queries (QR=0, opcode=0) */
    if ((flags & 0x8000) || (flags & 0x7800) || qdcount == 0) {
        pbuf_free(p);
        return;
    }

    /* Find the end of the question section to append our answer */
    uint8_t *qptr = query + 12;
    int qend = 12;

    /* Skip all questions (name + type + class) */
    for (int i = 0; i < qdcount; i++) {
        /* Skip name (label sequence ending with 0) */
        while (qptr < (uint8_t *)p->payload + p->len) {
            uint8_t label_len = *qptr;
            if (label_len == 0) {
                qptr++;
                qend++;
                break;
            }
            if ((label_len & 0xC0) == 0xC0) {
                /* Compression pointer — 2 bytes */
                qptr += 2;
                qend += 2;
                break;
            }
            qptr += 1 + label_len;
            qend += 1 + label_len;
        }
        /* Skip QTYPE (2 bytes) + QCLASS (2 bytes) */
        qptr += 4;
        qend += 4;
    }

    /* Check if this is an A query (type 1, class IN 1) */
    /* Re-parse the first question's type */
    uint8_t *type_ptr = query + 12;
    /* Skip name */
    while (type_ptr < (uint8_t *)p->payload + p->len) {
        uint8_t label_len = *type_ptr;
        if (label_len == 0) { type_ptr++; break; }
        if ((label_len & 0xC0) == 0xC0) { type_ptr += 2; break; }
        type_ptr += 1 + label_len;
    }
    uint16_t qtype = (type_ptr[0] << 8) | type_ptr[1];
    uint16_t qclass = (type_ptr[2] << 8) | type_ptr[3];

    /* Build response.
     * For A queries: copy query + add 16-byte answer (name ptr + type + class + ttl + rdlength + rdata)
     * For AAAA/other: copy query, set response with no answers */
    int answer_len = (qtype == 1 && qclass == 1) ? 16 : 0;
    int response_len = qend + answer_len;

    struct pbuf *resp = pbuf_alloc(PBUF_TRANSPORT, response_len, PBUF_RAM);
    if (!resp) {
        pbuf_free(p);
        return;
    }

    uint8_t *r = (uint8_t *)resp->payload;

    /* Copy the question section verbatim */
    memcpy(r, query, qend);

    /* Set response flags: QR=1, RD=1 (copied from query), RA=1 */
    r[2] = (query[2] & 0x01) | 0x80;  /* QR=1, opcode=0, AA=0, TC=0, RD=copied */
    r[3] = 0x80;                       /* RA=1, Z=0, RCODE=0 (no error) */

    /* ANCOUNT */
    r[6] = 0;
    r[7] = (answer_len > 0) ? 1 : 0;

    /* NSCOUNT + ARCOUNT already 0 (copied from query, but ensure) */
    r[8] = 0; r[9] = 0;
    r[10] = 0; r[11] = 0;

    if (answer_len > 0) {
        /* Answer record:
         *   Name: compression pointer to offset 12 (0xC00C)
         *   Type: A (0x0001)
         *   Class: IN (0x0001)
         *   TTL: 60 seconds (0x0000003C)
         *   RDLENGTH: 4 (0x0004)
         *   RDATA: 4 bytes of AP IP address */
        r[qend + 0] = 0xC0;  /* compression pointer */
        r[qend + 1] = 0x0C;  /* to offset 12 */
        r[qend + 2] = 0x00; r[qend + 3] = 0x01;  /* type A */
        r[qend + 4] = 0x00; r[qend + 5] = 0x01;  /* class IN */
        r[qend + 6] = 0x00; r[qend + 7] = 0x00;
        r[qend + 8] = 0x00; r[qend + 9] = 0x3C;  /* TTL = 60 */
        r[qend + 10] = 0x00; r[qend + 11] = 0x04; /* rdlength = 4 */
        r[qend + 12] = (s_ap_ip_u32 >> 0) & 0xFF;
        r[qend + 13] = (s_ap_ip_u32 >> 8) & 0xFF;
        r[qend + 14] = (s_ap_ip_u32 >> 16) & 0xFF;
        r[qend + 15] = (s_ap_ip_u32 >> 24) & 0xFF;
    }

    /* Send response */
    udp_sendto(pcb, resp, addr, port);
    pbuf_free(resp);
    pbuf_free(p);
}

/* ---- Public API ---- */

esp_err_t dns_server_start(void)
{
    if (s_dns_pcb) {
        ESP_LOGW(TAG, "DNS server already running");
        return ESP_OK;
    }

    /* Get AP IP address */
    esp_netif_t *ap_netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    if (!ap_netif) {
        ESP_LOGE(TAG, "AP netif not found — WiFi AP must be started first");
        return ESP_ERR_INVALID_STATE;
    }

    esp_netif_ip_info_t ip_info;
    esp_err_t ret = esp_netif_get_ip_info(ap_netif, &ip_info);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get AP IP: %s", esp_err_to_name(ret));
        return ret;
    }

    s_ap_ip_u32 = ip_info.ip.addr;  /* raw u32, network byte order */

    /* Create UDP PCB and bind to port 53 */
    s_dns_pcb = udp_new();
    if (!s_dns_pcb) {
        ESP_LOGE(TAG, "Failed to create UDP PCB");
        return ESP_ERR_NO_MEM;
    }

    /* Extract octets for IP_ADDR4 macro (expects individual bytes) */
    uint8_t ip0 = (s_ap_ip_u32 >> 0) & 0xFF;
    uint8_t ip1 = (s_ap_ip_u32 >> 8) & 0xFF;
    uint8_t ip2 = (s_ap_ip_u32 >> 16) & 0xFF;
    uint8_t ip3 = (s_ap_ip_u32 >> 24) & 0xFF;

    ip_addr_t bind_addr;
    IP_ADDR4(&bind_addr, ip0, ip1, ip2, ip3);

    err_t err = udp_bind(s_dns_pcb, &bind_addr, 53);
    if (err != ERR_OK) {
        ESP_LOGE(TAG, "Failed to bind UDP port 53: %d", err);
        udp_remove(s_dns_pcb);
        s_dns_pcb = NULL;
        return ESP_FAIL;
    }

    udp_recv(s_dns_pcb, dns_recv_callback, NULL);

    ESP_LOGI(TAG, "DNS server started on " IPSTR ":53 (captive portal hijack)",
             IP2STR(&ip_info.ip));
    return ESP_OK;
}

esp_err_t dns_server_stop(void)
{
    if (!s_dns_pcb) {
        return ESP_OK;
    }

    udp_remove(s_dns_pcb);
    s_dns_pcb = NULL;
    ESP_LOGI(TAG, "DNS server stopped");
    return ESP_OK;
}
