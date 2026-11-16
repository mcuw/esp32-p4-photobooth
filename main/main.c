#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "nvs_flash.h"

#include "ipp_client.h"

static const char *TAG = "main";

extern const uint8_t test_jpg_start[] asm("_binary_test_jpg_start");
extern const uint8_t test_jpg_end[]   asm("_binary_test_jpg_end");

#define GOT_IP_BIT        BIT0
#define FAIL_BIT          BIT1
#define MAX_RETRIES       10   /* allgemeine Verbindungsfehler */
#define MAX_RETRIES_AUTH  3    /* Passwortproblem: nicht endlos probieren */

static EventGroupHandle_t s_events;
static int s_retries;
static int s_last_reason;

/* Gruende, die fast immer an einem falschen Passwort liegen */
static bool is_password_problem(int reason)
{
    switch (reason) {
    case WIFI_REASON_AUTH_FAIL:
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_HANDSHAKE_TIMEOUT:
        return true;
    default:
        return false;
    }
}

static const char *reason_hint(int reason)
{
    switch (reason) {
    case WIFI_REASON_NO_AP_FOUND:
        return "Netz nicht gefunden: SSID pruefen (Gross-/Kleinschreibung), "
               "nur 2,4 GHz moeglich, Signal zu schwach?";
    case WIFI_REASON_AUTH_FAIL:
        return "Anmeldung abgelehnt: Passwort falsch? (oder Router nur WPA3)";
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_HANDSHAKE_TIMEOUT:
        return "Schluesselaustausch fehlgeschlagen: Passwort falsch?";
    case WIFI_REASON_AUTH_EXPIRE:
        return "Anmeldung abgelaufen: Passwort oder Empfang pruefen";
    case WIFI_REASON_ASSOC_FAIL:
        return "Router lehnt Verbindung ab (MAC-Filter, zu viele Geraete?)";
    case WIFI_REASON_CONNECTION_FAIL:
        return "Verbindungsaufbau fehlgeschlagen: meist schwacher Empfang";
    case WIFI_REASON_BEACON_TIMEOUT:
        return "Verbindung verloren: Empfang zu schwach oder AP weg";
    default:
        return "unbekannter Grund";
    }
}

static void on_wifi(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "Verbinde mit '%s' ...", CONFIG_PHOTOBOOTH_WIFI_SSID);
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_CONNECTED) {
        ESP_LOGI(TAG, "Mit AP verbunden, warte auf IP (DHCP) ...");
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *d = data;
        int reason = d ? d->reason : -1;
        int rssi = d ? d->rssi : 0;
        s_last_reason = reason;

        ESP_LOGW(TAG, "WLAN getrennt (reason=%d): %s", reason, reason_hint(reason));
        if (rssi < -80 && rssi > -128) {
            ESP_LOGW(TAG, "Signal sehr schwach (%d dBm), naeher an den Router", rssi);
        }

        int max = is_password_problem(reason) ? MAX_RETRIES_AUTH : MAX_RETRIES;
        if (s_retries < max) {
            s_retries++;
            ESP_LOGI(TAG, "Neuer Versuch %d/%d", s_retries, max);
            esp_wifi_connect();
        } else {
            ESP_LOGE(TAG, "WLAN-Verbindung fehlgeschlagen. Letzte Ursache: %s",
                     reason_hint(reason));
            xEventGroupSetBits(s_events, FAIL_BIT);
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *e = data;
        ESP_LOGI(TAG, "IP: " IPSTR, IP2STR(&e->ip_info.ip));
        s_retries = 0;
        xEventGroupSetBits(s_events, GOT_IP_BIT);
    }
}

static bool wifi_start(void)
{
    size_t ssid_len = strlen(CONFIG_PHOTOBOOTH_WIFI_SSID);
    size_t pw_len = strlen(CONFIG_PHOTOBOOTH_WIFI_PASSWORD);

    if (ssid_len == 0) {
        ESP_LOGE(TAG, "Keine WLAN-SSID konfiguriert (menuconfig -> Photobooth oder sdkconfig.local)");
        return false;
    }
    if (pw_len > 0 && pw_len < 8) {
        ESP_LOGE(TAG, "WLAN-Passwort hat nur %u Zeichen, WPA verlangt mindestens 8", (unsigned)pw_len);
        return false;
    }
    ESP_LOGI(TAG, "WLAN-Start: SSID='%s' (%u Zeichen), Passwort %u Zeichen",
             CONFIG_PHOTOBOOTH_WIFI_SSID, (unsigned)ssid_len, (unsigned)pw_len);

    s_events = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_wifi, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_wifi, NULL));

    wifi_config_t wc = { 0 };
    strlcpy((char *)wc.sta.ssid, CONFIG_PHOTOBOOTH_WIFI_SSID, sizeof(wc.sta.ssid));
    strlcpy((char *)wc.sta.password, CONFIG_PHOTOBOOTH_WIFI_PASSWORD, sizeof(wc.sta.password));
    wc.sta.threshold.authmode = pw_len ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    wc.sta.pmf_cfg.capable = true;
    wc.sta.pmf_cfg.required = false;
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wc));
    ESP_ERROR_CHECK(esp_wifi_start());

    EventBits_t bits = xEventGroupWaitBits(s_events, GOT_IP_BIT | FAIL_BIT,
                                           pdFALSE, pdFALSE, pdMS_TO_TICKS(60000));
    if (bits == 0) {
        ESP_LOGE(TAG, "Timeout: nach 60 s weder IP noch Fehler vom WLAN-Treiber");
    }
    return (bits & GOT_IP_BIT) != 0;
}

void app_main(void)
{
    ESP_LOGI(TAG, "app_main gestartet, Drucker-IP: %s", CONFIG_IPP_PRINTER_HOST);

    esp_err_t r = nvs_flash_init();
    if (r == ESP_ERR_NVS_NO_FREE_PAGES || r == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        r = nvs_flash_init();
    }
    ESP_ERROR_CHECK(r);

    if (!wifi_start()) {
        ESP_LOGE(TAG, "Kein WLAN, Abbruch");
        return;
    }

    /* 1) Verbindung / Faehigkeiten pruefen (IP: menuconfig -> Photobooth -> Drucker (IPP)) */
    esp_err_t e = ipp_get_printer_attributes(NULL);
    ESP_LOGI(TAG, "Get-Printer-Attributes: %s", esp_err_to_name(e));

#if CONFIG_PHOTOBOOTH_PRINT_TEST
    /* 2) Testdruck. Zur Laufzeit andere IP: ipp_config_t c = { .host = "192.168.50.201" }; */
    e = ipp_print_jpeg(NULL, test_jpg_start, test_jpg_end - test_jpg_start, "esp32-test");
    ESP_LOGI(TAG, "Print-Job: %s", esp_err_to_name(e));

    /* Nach kurzer Zeit Druckerstatus erneut abfragen (printer-state-reasons) */
    vTaskDelay(pdMS_TO_TICKS(5000));
    ESP_LOGI(TAG, "Druckerstatus nach dem Auftrag:");
    ipp_get_printer_attributes(NULL);
#endif
}
