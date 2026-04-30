#include "esp_http_server.h"
#include "esp_log.h"

#include "ArduinoJson.h"

#include "psram_allocator.h"
#include "nvs_config.h"
#include "http_cors.h"
#include "http_utils.h"

static const char* TAG = "http_mqtt";

esp_err_t GET_mqtt_info(httpd_req_t *req)
{
    ConGuard g(http_server, req);

    if (is_network_allowed(req) != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Unauthorized");
    }

    httpd_resp_set_type(req, "application/json");

    if (set_cors_headers(req) != ESP_OK) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    char *uri    = Config::getMqttURI();
    char *user   = Config::getMqttUser();
    char *prefix = Config::getMqttTopicPrefix();

    PSRAMAllocator allocator;
    JsonDocument doc(&allocator);

    doc["mqttEnable"]      = Config::isMqttEnabled() ? 1 : 0;
    doc["mqttURI"]         = uri;
    doc["mqttUser"]        = user;
    doc["mqttTopicPrefix"] = prefix;
    doc["mqttInterval"]    = Config::getMqttPublishInterval();
    doc["mqttDiscovery"]   = Config::isMqttDiscoveryEnabled() ? 1 : 0;

    esp_err_t ret = sendJsonResponse(req, doc);
    doc.clear();

    free(uri);
    free(user);
    free(prefix);
    return ret;
}

esp_err_t PATCH_update_mqtt(httpd_req_t *req)
{
    ConGuard g(http_server, req);

    if (is_network_allowed(req) != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Unauthorized");
    }

    if (set_cors_headers(req) != ESP_OK) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    if (validateOTP(req) != ESP_OK) {
        return ESP_FAIL;
    }

    PSRAMAllocator allocator;
    JsonDocument doc(&allocator);

    esp_err_t err = getJsonData(req, doc);
    if (err != ESP_OK) {
        return err;
    }

    if (doc["mqttEnable"].is<bool>()) {
        Config::setMqttEnabled(doc["mqttEnable"].as<bool>());
    }
    if (doc["mqttURI"].is<const char*>()) {
        Config::setMqttURI(doc["mqttURI"].as<const char*>());
    }
    if (doc["mqttUser"].is<const char*>()) {
        Config::setMqttUser(doc["mqttUser"].as<const char*>());
    }
    if (doc["mqttPass"].is<const char*>()) {
        Config::setMqttPass(doc["mqttPass"].as<const char*>());
    }
    if (doc["mqttTopicPrefix"].is<const char*>()) {
        Config::setMqttTopicPrefix(doc["mqttTopicPrefix"].as<const char*>());
    }
    if (doc["mqttInterval"].is<uint16_t>()) {
        Config::setMqttPublishInterval(doc["mqttInterval"].as<uint16_t>());
    }
    if (doc["mqttDiscovery"].is<bool>()) {
        Config::setMqttDiscoveryEnabled(doc["mqttDiscovery"].as<bool>());
    }

    doc.clear();
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}
