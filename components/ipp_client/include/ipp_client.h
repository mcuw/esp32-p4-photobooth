#pragma once

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Optionale Laufzeit-Konfiguration. Alle Felder sind optional:
 * NULL bzw. 0 -> Wert aus menuconfig (IPP Client) wird verwendet.
 */
typedef struct {
    const char *host;      /**< IP oder Hostname des Druckers */
    uint16_t    port;      /**< IPP-Port, Standard 631 */
    const char *path;      /**< IPP-Pfad, z. B. "/ipp/print" */
    const char *media;     /**< IPP "media"-Keyword; "" = Drucker-Standard, NULL = menuconfig */
    uint32_t    timeout_ms;/**< HTTP-Timeout, 0 -> 60000 */
} ipp_config_t;

/**
 * Fragt Status und Faehigkeiten des Druckers ab (Get-Printer-Attributes)
 * und loggt lesbare Teile der Antwort. Gut zum Testen der Verbindung.
 */
esp_err_t ipp_get_printer_attributes(const ipp_config_t *cfg);

/**
 * Sendet ein JPEG als Print-Job (document-format image/jpeg).
 * Das JPEG muss komplett im Speicher liegen (PSRAM ist ok).
 * Empfehlung: Baseline-JPEG (nicht progressiv), Seitenverhaeltnis passend zum Papier.
 */
esp_err_t ipp_print_jpeg(const ipp_config_t *cfg, const uint8_t *jpeg, size_t len,
                         const char *job_name);

#ifdef __cplusplus
}
#endif
