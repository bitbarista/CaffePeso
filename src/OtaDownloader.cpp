// OtaDownloader.cpp — MUST NOT include ESPAsyncWebServer (conflicts with http_parser.h)
#include "OtaDownloader.h"
#include <Update.h>
#include <esp_http_client.h>
#include <mbedtls/ssl.h>
#include <Arduino.h>

// Skip TLS certificate verification — equivalent to WiFiClientSecure::setInsecure().
// We trust GitHub Pages by URL, not by cert chain.
static esp_err_t noVerifyAttach(void* conf) {
    mbedtls_ssl_conf_authmode((mbedtls_ssl_config*)conf, MBEDTLS_SSL_VERIFY_NONE);
    return ESP_OK;
}

bool otaStreamUpdate(const String& url, int updateType, int* progress, String* message) {
    Serial.printf("otaStreamUpdate: url=%s heap=%u maxAlloc=%u\n",
                  url.c_str(), ESP.getFreeHeap(), ESP.getMaxAllocHeap());

    esp_http_client_config_t cfg = {};
    cfg.url                         = url.c_str();
    cfg.crt_bundle_attach           = noVerifyAttach; // skip cert verification (trusted URL)
    cfg.timeout_ms                  = 60000;
    cfg.buffer_size                 = 1024;
    cfg.buffer_size_tx              = 512;

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        *message = "HTTP client init failed";
        return false;
    }

    esp_err_t err = esp_http_client_open(client, 0);
    Serial.printf("otaStreamUpdate: open err=%d (%s)\n", err, esp_err_to_name(err));
    if (err != ESP_OK) {
        *message = "Connection failed: " + String(esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return false;
    }

    int total  = esp_http_client_fetch_headers(client);
    int status = esp_http_client_get_status_code(client);
    Serial.printf("otaStreamUpdate: HTTP %d, size=%d\n", status, total);

    if (status != 200) {
        *message = "Download failed (HTTP " + String(status) + ")";
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return false;
    }
    if (total <= 0) {
        *message = "Download failed (unknown size)";
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return false;
    }
    if (!Update.begin(total, updateType)) {
        *message = "Flash init failed";
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return false;
    }

    uint8_t buf[1024];
    int written = 0;
    unsigned long lastActivity = millis();

    while (written < total) {
        int n = esp_http_client_read(client, (char*)buf, sizeof(buf));
        if (n > 0) {
            if (Update.write(buf, n) != (size_t)n) {
                *message = "Flash write error";
                esp_http_client_close(client);
                esp_http_client_cleanup(client);
                return false;
            }
            written += n;
            *progress = (written * 100) / total;
            lastActivity = millis();
        } else if (n == 0) {
            if (millis() - lastActivity > 15000) {
                *message = "Download timed out";
                esp_http_client_close(client);
                esp_http_client_cleanup(client);
                return false;
            }
            delay(1);
        } else {
            *message = "Download read error";
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            return false;
        }
    }

    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    if (!Update.end(true)) {
        *message = "Flash verify failed";
        return false;
    }
    return true;
}
