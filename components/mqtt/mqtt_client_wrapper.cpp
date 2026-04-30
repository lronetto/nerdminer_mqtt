#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "esp_event.h"
#include "mqtt_client.h"

#include "ArduinoJson.h"
#include "psram_allocator.h"

#include "mqtt_client_wrapper.h"

static const char *TAG = "MQTT";

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    MqttPublisher *self = (MqttPublisher *) handler_args;
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t) event_data;

    switch ((esp_mqtt_event_id_t) event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "broker connected");
            self->on_connected();
            break;
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "broker disconnected");
            self->on_disconnected();
            break;
        case MQTT_EVENT_ERROR:
            if (event && event->error_handle) {
                ESP_LOGE(TAG, "mqtt error: type=%d", event->error_handle->error_type);
            }
            break;
        default:
            break;
    }
}

MqttPublisher::MqttPublisher()
    : m_uri(nullptr), m_user(nullptr), m_pass(nullptr),
      m_client_id(nullptr), m_topic_prefix(nullptr), m_node_id(nullptr),
      m_handle(nullptr), m_connected(false),
      m_discovery_enabled(false), m_discovery_published(false)
{
    pthread_mutex_init(&m_lock, nullptr);
    memset(&m_stats, 0, sizeof(m_stats));
}

MqttPublisher::~MqttPublisher()
{
    if (m_handle) {
        esp_mqtt_client_stop((esp_mqtt_client_handle_t) m_handle);
        esp_mqtt_client_destroy((esp_mqtt_client_handle_t) m_handle);
    }
    free(m_uri);
    free(m_user);
    free(m_pass);
    free(m_client_id);
    free(m_topic_prefix);
    free(m_node_id);
    pthread_mutex_destroy(&m_lock);
}

static char *dup_or_empty(const char *s) {
    return strdup(s ? s : "");
}

bool MqttPublisher::init(const char *uri, const char *user, const char *pass,
                         const char *client_id, const char *topic_prefix,
                         const char *node_id, bool discovery_enabled)
{
    if (!uri || !*uri) {
        ESP_LOGE(TAG, "empty broker URI");
        return false;
    }

    m_uri = dup_or_empty(uri);
    m_user = dup_or_empty(user);
    m_pass = dup_or_empty(pass);
    m_client_id = dup_or_empty(client_id);
    m_topic_prefix = dup_or_empty(topic_prefix);
    m_node_id = dup_or_empty(node_id);
    m_discovery_enabled = discovery_enabled;

    char will_topic[128];
    snprintf(will_topic, sizeof(will_topic), "%s/%s/availability", m_topic_prefix, m_node_id);

    esp_mqtt_client_config_t cfg = {};
    cfg.broker.address.uri = m_uri;
    if (m_user && *m_user) cfg.credentials.username = m_user;
    if (m_pass && *m_pass) cfg.credentials.authentication.password = m_pass;
    if (m_client_id && *m_client_id) cfg.credentials.client_id = m_client_id;
    cfg.session.last_will.topic = will_topic;
    cfg.session.last_will.msg = "offline";
    cfg.session.last_will.msg_len = 7;
    cfg.session.last_will.qos = 1;
    cfg.session.last_will.retain = 1;
    cfg.session.keepalive = 60;
    cfg.network.reconnect_timeout_ms = 10000;

    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&cfg);
    if (!client) {
        ESP_LOGE(TAG, "esp_mqtt_client_init failed");
        return false;
    }
    m_handle = client;

    esp_mqtt_client_register_event(client, MQTT_EVENT_ANY, mqtt_event_handler, this);

    ESP_LOGI(TAG, "configured: uri=%s prefix=%s node=%s discovery=%d",
             m_uri, m_topic_prefix, m_node_id, m_discovery_enabled ? 1 : 0);
    return true;
}

bool MqttPublisher::start()
{
    if (!m_handle) return false;
    esp_err_t err = esp_mqtt_client_start((esp_mqtt_client_handle_t) m_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_mqtt_client_start failed: %s", esp_err_to_name(err));
        return false;
    }
    return true;
}

void MqttPublisher::on_connected()
{
    m_connected = true;

    char topic[128];
    snprintf(topic, sizeof(topic), "%s/%s/availability", m_topic_prefix, m_node_id);
    esp_mqtt_client_publish((esp_mqtt_client_handle_t) m_handle, topic, "online", 0, 1, 1);

    if (m_discovery_enabled && !m_discovery_published) {
        publish_discovery();
        m_discovery_published = true;
    }
}

void MqttPublisher::on_disconnected()
{
    m_connected = false;
}

void MqttPublisher::publish_json(const char *topic, const char *payload, int qos, bool retain)
{
    if (!m_handle || !m_connected) return;
    esp_mqtt_client_publish((esp_mqtt_client_handle_t) m_handle, topic, payload, 0, qos, retain ? 1 : 0);
}

void MqttPublisher::publish_state()
{
    if (!m_connected) return;

    PSRAMAllocator allocator;
    JsonDocument doc(&allocator);

    pthread_mutex_lock(&m_lock);
    doc["hashrate"]            = m_stats.hashrate;
    doc["hashrate_1m"]         = m_stats.hashrate_1m;
    doc["temp"]                = m_stats.temp;
    doc["temp2"]               = m_stats.temp2;
    doc["power"]               = m_stats.pwr_pin;
    doc["voltage_in"]          = m_stats.pwr_vin;
    doc["current_in"]          = m_stats.pwr_iin;
    doc["voltage_out"]         = m_stats.pwr_vout;
    doc["current_out"]         = m_stats.pwr_iout;
    doc["power_out"]           = m_stats.pwr_pout;
    doc["fan_rpm_0"]           = m_stats.fan_rpm_0;
    doc["fan_rpm_1"]           = m_stats.fan_rpm_1;
    doc["fan_pwm_0"]           = m_stats.fan_pwm_0;
    doc["fan_pwm_1"]           = m_stats.fan_pwm_1;
    doc["best_difficulty"]     = m_stats.best_difficulty;
    doc["total_best_diff"]     = m_stats.total_best_difficulty;
    doc["shares_accepted"]     = m_stats.shares_accepted;
    doc["shares_rejected"]     = m_stats.shares_rejected;
    doc["pool_difficulty"]     = m_stats.pool_difficulty;
    doc["pool_errors"]         = m_stats.pool_errors;
    doc["uptime"]              = m_stats.uptime;
    doc["total_uptime"]        = m_stats.total_uptime;
    doc["blocks_found"]        = m_stats.blocks_found;
    doc["total_blocks_found"]  = m_stats.total_blocks_found;
    pthread_mutex_unlock(&m_lock);

    char topic[128];
    snprintf(topic, sizeof(topic), "%s/%s/state", m_topic_prefix, m_node_id);

    char buf[1024];
    size_t n = serializeJson(doc, buf, sizeof(buf));
    if (n > 0) {
        publish_json(topic, buf, 0, true);
    }
}

// ---- Home Assistant Discovery ------------------------------------------------
//
// Publishes one config entry per sensor under the
// `homeassistant/sensor/<node>_<key>/config` topic.  All sensors share a single
// state topic and use value_template to extract their value from the JSON body.
//
// HA expects retained discovery messages so it can rebuild entities on restart.

namespace {

struct SensorDef {
    const char *key;
    const char *name;
    const char *unit;
    const char *device_class;
    const char *state_class;
    const char *icon;
};

constexpr SensorDef kSensors[] = {
    {"hashrate",            "Hashrate",          "GH/s", nullptr,        "measurement", "mdi:speedometer"},
    {"hashrate_1m",         "Hashrate (1m)",     "GH/s", nullptr,        "measurement", "mdi:speedometer-medium"},
    {"temp",                "Temperature",       "°C",   "temperature",  "measurement", nullptr},
    {"temp2",               "VR Temperature",    "°C",   "temperature",  "measurement", nullptr},
    {"power",               "Power",             "W",    "power",        "measurement", nullptr},
    {"voltage_in",          "Voltage In",        "V",    "voltage",      "measurement", nullptr},
    {"current_in",          "Current In",        "A",    "current",      "measurement", nullptr},
    {"voltage_out",         "Voltage Out",       "V",    "voltage",      "measurement", nullptr},
    {"current_out",         "Current Out",       "A",    "current",      "measurement", nullptr},
    {"fan_rpm_0",           "Fan 0 RPM",         "rpm",  nullptr,        "measurement", "mdi:fan"},
    {"fan_rpm_1",           "Fan 1 RPM",         "rpm",  nullptr,        "measurement", "mdi:fan"},
    {"best_difficulty",     "Best Difficulty",   nullptr, nullptr,       "measurement", "mdi:trophy"},
    {"total_best_diff",     "All-time Best Diff",nullptr, nullptr,       "measurement", "mdi:trophy-outline"},
    {"shares_accepted",     "Shares Accepted",   nullptr, nullptr,       "total_increasing", "mdi:check-circle"},
    {"shares_rejected",     "Shares Rejected",   nullptr, nullptr,       "total_increasing", "mdi:close-circle"},
    {"pool_difficulty",     "Pool Difficulty",   nullptr, nullptr,       "measurement", "mdi:pickaxe"},
    {"pool_errors",         "Pool Errors",       nullptr, nullptr,       "total_increasing", "mdi:alert"},
    {"uptime",              "Uptime",            "s",    "duration",     "total_increasing", nullptr},
    {"total_uptime",        "Total Uptime",      "s",    "duration",     "total_increasing", nullptr},
    {"blocks_found",        "Blocks Found",      nullptr, nullptr,       "total_increasing", "mdi:cube"},
    {"total_blocks_found",  "Total Blocks Found",nullptr, nullptr,       "total_increasing", "mdi:cube-outline"},
};

} // namespace

void MqttPublisher::publish_discovery()
{
    char state_topic[128];
    char avail_topic[128];
    snprintf(state_topic, sizeof(state_topic), "%s/%s/state", m_topic_prefix, m_node_id);
    snprintf(avail_topic, sizeof(avail_topic), "%s/%s/availability", m_topic_prefix, m_node_id);

    for (const SensorDef &s : kSensors) {
        PSRAMAllocator allocator;
        JsonDocument doc(&allocator);

        char unique_id[96];
        snprintf(unique_id, sizeof(unique_id), "%s_%s", m_node_id, s.key);

        char object_id[96];
        snprintf(object_id, sizeof(object_id), "%s_%s", m_node_id, s.key);

        char value_tpl[64];
        snprintf(value_tpl, sizeof(value_tpl), "{{ value_json.%s }}", s.key);

        char display_name[96];
        snprintf(display_name, sizeof(display_name), "NerdQAxe %s", s.name);

        doc["name"]               = display_name;
        doc["unique_id"]          = unique_id;
        doc["object_id"]          = object_id;
        doc["state_topic"]        = state_topic;
        doc["availability_topic"] = avail_topic;
        doc["value_template"]     = value_tpl;
        if (s.unit)         doc["unit_of_measurement"] = s.unit;
        if (s.device_class) doc["device_class"]        = s.device_class;
        if (s.state_class)  doc["state_class"]         = s.state_class;
        if (s.icon)         doc["icon"]                = s.icon;

        JsonObject device = doc["device"].to<JsonObject>();
        JsonArray  ids    = device["identifiers"].to<JsonArray>();
        ids.add(m_node_id);
        device["name"]         = m_node_id;
        device["manufacturer"] = "shufps / BitMaker-hub";
        device["model"]        = "NerdQAxe++";
        device["sw_version"]   = "esp-miner-nerdqaxeplus + mqtt";

        char cfg_topic[160];
        snprintf(cfg_topic, sizeof(cfg_topic),
                 "homeassistant/sensor/%s/%s/config", m_node_id, s.key);

        char buf[1024];
        size_t n = serializeJson(doc, buf, sizeof(buf));
        if (n > 0) {
            publish_json(cfg_topic, buf, 1, true);
        }
    }

    ESP_LOGI(TAG, "published %u HA discovery entries", (unsigned) (sizeof(kSensors)/sizeof(kSensors[0])));
}
