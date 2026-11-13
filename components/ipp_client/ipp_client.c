#include "ipp_client.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "esp_http_client.h"
#include "esp_log.h"

static const char *TAG = "ipp";

#define IPP_HDR_MAX   768
#define IPP_RESP_MAX  4096

/* IPP Operationen */
#define IPP_OP_PRINT_JOB             0x0002
#define IPP_OP_GET_PRINTER_ATTRS     0x000B

/* IPP Tags */
#define TAG_OPERATION_ATTRS  0x01
#define TAG_JOB_ATTRS        0x02
#define TAG_END_OF_ATTRS     0x03
#define TAG_NAME_NO_LANG     0x42
#define TAG_KEYWORD          0x44
#define TAG_URI              0x45
#define TAG_CHARSET          0x47
#define TAG_NATURAL_LANG     0x48
#define TAG_MIME_TYPE        0x49

/* ---------- einfacher Schreibpuffer ---------- */

typedef struct {
    uint8_t *p;
    size_t   len;
    size_t   cap;
    bool     overflow;
} wbuf_t;

static void w8(wbuf_t *b, uint8_t v)
{
    if (b->len + 1 > b->cap) { b->overflow = true; return; }
    b->p[b->len++] = v;
}

static void w16(wbuf_t *b, uint16_t v) { w8(b, v >> 8); w8(b, v & 0xFF); }

static void w32(wbuf_t *b, uint32_t v)
{
    w8(b, v >> 24); w8(b, v >> 16); w8(b, v >> 8); w8(b, v);
}

static void wbytes(wbuf_t *b, const void *d, size_t n)
{
    if (b->len + n > b->cap) { b->overflow = true; return; }
    memcpy(b->p + b->len, d, n);
    b->len += n;
}

/* Attribut: tag, name-length, name, value-length, value.
 * name == "" -> zusaetzlicher Wert zum vorherigen Attribut (1setOf). */
static void wattr(wbuf_t *b, uint8_t tag, const char *name, const char *val)
{
    w8(b, tag);
    w16(b, (uint16_t)strlen(name));
    wbytes(b, name, strlen(name));
    w16(b, (uint16_t)strlen(val));
    wbytes(b, val, strlen(val));
}

/* ---------- Konfiguration aufloesen ---------- */

static void resolve_cfg(const ipp_config_t *in, ipp_config_t *out)
{
    out->host = (in && in->host && in->host[0]) ? in->host : CONFIG_IPP_PRINTER_HOST;
    out->port = (in && in->port) ? in->port : CONFIG_IPP_PRINTER_PORT;
    out->path = (in && in->path && in->path[0]) ? in->path : CONFIG_IPP_PRINTER_PATH;
    out->media = (in && in->media) ? in->media : CONFIG_IPP_MEDIA;
    out->timeout_ms = (in && in->timeout_ms) ? in->timeout_ms : 60000;
}

/* ---------- Request-Header (bis inkl. end-of-attributes) ---------- */

static void begin_request(wbuf_t *b, uint16_t op, const ipp_config_t *c)
{
    static uint32_t request_id = 1;
    char uri[160];
    snprintf(uri, sizeof(uri), "ipp://%s:%u%s", c->host, (unsigned)c->port, c->path);

    w16(b, 0x0200);            /* IPP 2.0 */
    w16(b, op);
    w32(b, request_id++);
    w8(b, TAG_OPERATION_ATTRS);
    wattr(b, TAG_CHARSET,      "attributes-charset", "utf-8");
    wattr(b, TAG_NATURAL_LANG, "attributes-natural-language", "en");
    wattr(b, TAG_URI,          "printer-uri", uri);
    wattr(b, TAG_NAME_NO_LANG, "requesting-user-name", "esp32");
}

/* ---------- HTTP-Transport ---------- */

static esp_err_t do_request(const ipp_config_t *c,
                            const uint8_t *hdr, size_t hdr_len,
                            const uint8_t *body, size_t body_len,
                            uint8_t *resp, size_t resp_cap, size_t *resp_len,
                            uint16_t *ipp_status)
{
    *resp_len = 0;
    *ipp_status = 0xFFFF;

    esp_http_client_config_t hc = {
        .host = c->host,
        .port = c->port,
        .path = c->path,
        .transport_type = HTTP_TRANSPORT_OVER_TCP,
        .method = HTTP_METHOD_POST,
        .timeout_ms = (int)c->timeout_ms,
        .buffer_size = 2048,
        .buffer_size_tx = 1024,
    };
    esp_http_client_handle_t cl = esp_http_client_init(&hc);
    if (!cl) return ESP_FAIL;

    esp_http_client_set_header(cl, "Content-Type", "application/ipp");
    esp_http_client_set_header(cl, "Accept", "application/ipp");

    esp_err_t err = esp_http_client_open(cl, (int)(hdr_len + body_len));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Verbindung zu %s:%u fehlgeschlagen: %s",
                 c->host, (unsigned)c->port, esp_err_to_name(err));
        goto out;
    }

    /* Header + Body senden (Teilschreibvorgaenge abfangen) */
    for (int part = 0; part < 2; part++) {
        const uint8_t *d = part == 0 ? hdr : body;
        size_t n = part == 0 ? hdr_len : body_len;
        size_t off = 0;
        while (off < n) {
            size_t chunk = n - off > 4096 ? 4096 : n - off;
            int w = esp_http_client_write(cl, (const char *)d + off, (int)chunk);
            if (w <= 0) {
                ESP_LOGE(TAG, "Senden abgebrochen bei %u/%u Bytes", (unsigned)off, (unsigned)n);
                err = ESP_FAIL;
                goto out;
            }
            off += (size_t)w;
        }
    }

    esp_http_client_fetch_headers(cl);
    int http_status = esp_http_client_get_status_code(cl);
    ESP_LOGI(TAG, "HTTP-Status: %d", http_status);

    int r;
    while (*resp_len < resp_cap &&
           (r = esp_http_client_read(cl, (char *)resp + *resp_len,
                                     (int)(resp_cap - *resp_len))) > 0) {
        *resp_len += (size_t)r;
    }

    if (http_status != 200) {
        err = ESP_FAIL;
    } else if (*resp_len >= 4) {
        *ipp_status = ((uint16_t)resp[2] << 8) | resp[3];
    } else {
        err = ESP_FAIL;
    }

out:
    esp_http_client_close(cl);
    esp_http_client_cleanup(cl);
    return err;
}

/* Gibt lesbare ASCII-Folgen der IPP-Antwort aus (reicht zum Debuggen). */
static void log_printable(const uint8_t *d, size_t n)
{
    char line[96];
    size_t l = 0;
    for (size_t i = 0; i <= n; i++) {
        bool pr = i < n && d[i] >= 0x20 && d[i] < 0x7F;
        if (pr && l < sizeof(line) - 1) {
            line[l++] = (char)d[i];
        } else {
            if (l >= 4) { line[l] = 0; ESP_LOGI(TAG, "  %s", line); }
            l = 0;
        }
    }
}

/* ---------- oeffentliche API ---------- */

esp_err_t ipp_get_printer_attributes(const ipp_config_t *cfg)
{
    ipp_config_t c;
    resolve_cfg(cfg, &c);

    uint8_t hdr[IPP_HDR_MAX];
    wbuf_t b = { .p = hdr, .cap = sizeof(hdr) };
    begin_request(&b, IPP_OP_GET_PRINTER_ATTRS, &c);
    wattr(&b, TAG_KEYWORD, "requested-attributes", "printer-make-and-model");
    wattr(&b, TAG_KEYWORD, "", "printer-state");
    wattr(&b, TAG_KEYWORD, "", "printer-state-reasons");
    wattr(&b, TAG_KEYWORD, "", "document-format-supported");
    wattr(&b, TAG_KEYWORD, "", "media-supported");
    wattr(&b, TAG_KEYWORD, "", "media-default");
    w8(&b, TAG_END_OF_ATTRS);
    if (b.overflow) return ESP_ERR_NO_MEM;

    static uint8_t resp[IPP_RESP_MAX];
    size_t rlen;
    uint16_t st;
    esp_err_t err = do_request(&c, hdr, b.len, NULL, 0, resp, sizeof(resp), &rlen, &st);
    if (err != ESP_OK) return err;

    ESP_LOGI(TAG, "IPP-Status 0x%04X, %u Bytes Antwort", st, (unsigned)rlen);
    log_printable(resp, rlen);
    return st < 0x0100 ? ESP_OK : ESP_FAIL;
}

esp_err_t ipp_print_jpeg(const ipp_config_t *cfg, const uint8_t *jpeg, size_t len,
                         const char *job_name)
{
    if (!jpeg || !len) return ESP_ERR_INVALID_ARG;

    ipp_config_t c;
    resolve_cfg(cfg, &c);

    uint8_t hdr[IPP_HDR_MAX];
    wbuf_t b = { .p = hdr, .cap = sizeof(hdr) };
    begin_request(&b, IPP_OP_PRINT_JOB, &c);
    wattr(&b, TAG_NAME_NO_LANG, "job-name", job_name ? job_name : "photobooth");
    wattr(&b, TAG_MIME_TYPE,    "document-format", "image/jpeg");
    if (c.media && c.media[0]) {
        w8(&b, TAG_JOB_ATTRS);
        wattr(&b, TAG_KEYWORD, "media", c.media);
    }
    w8(&b, TAG_END_OF_ATTRS);
    if (b.overflow) return ESP_ERR_NO_MEM;

    uint8_t resp[512];
    size_t rlen;
    uint16_t st;
    ESP_LOGI(TAG, "Sende %u Bytes JPEG an %s ...", (unsigned)len, c.host);
    esp_err_t err = do_request(&c, hdr, b.len, jpeg, len, resp, sizeof(resp), &rlen, &st);
    if (err != ESP_OK) return err;

    ESP_LOGI(TAG, "IPP-Status 0x%04X", st);
    if (st >= 0x0100) {
        log_printable(resp, rlen);
        return ESP_FAIL;
    }
    return ESP_OK;
}
