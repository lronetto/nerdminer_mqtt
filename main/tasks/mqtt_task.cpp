#include <pthread.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_mac.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"

#include "global_state.h"
#include "nvs_config.h"
#include "mqtt_task.h"
#include "stratum/stratum_manager.h"

static const char *TAG = "mqtt_task";

static MqttPublisher *publisher = nullptr;
static int last_block_found = 0;

static void uptime_timer_cb(TimerHandle_t)
{
    if (!publisher) return;
    pthread_mutex_lock(&publisher->m_lock);
    publisher->m_stats.uptime += 1;
    publisher->m_stats.total_uptime += 1;
    pthread_mutex_unlock(&publisher->m_lock);
}

void mqtt_task_set_temperature(float temp, float temp2)
{
    if (!publisher) return;
    pthread_mutex_lock(&publisher->m_lock);
    publisher->m_stats.temp = temp;
    publisher->m_stats.temp2 = temp2;
    pthread_mutex_unlock(&publisher->m_lock);
}

void mqtt_task_set_pwr(float vin, float iin, float pin, float vout, float iout, float pout)
{
    if (!publisher) return;
    pthread_mutex_lock(&publisher->m_lock);
    publisher->m_stats.pwr_vin = vin;
    publisher->m_stats.pwr_iin = iin;
    publisher->m_stats.pwr_pin = pin;
    publisher->m_stats.pwr_vout = vout;
    publisher->m_stats.pwr_iout = iout;
    publisher->m_stats.pwr_pout = pout;
    pthread_mutex_unlock(&publisher->m_lock);
}

void mqtt_task_set_fan(float pwm0, float rpm0, float pwm1, float rpm1)
{
    if (!publisher) return;
    pthread_mutex_lock(&publisher->m_lock);
    publisher->m_stats.fan_pwm_0 = pwm0;
    publisher->m_stats.fan_rpm_0 = rpm0;
    publisher->m_stats.fan_pwm_1 = pwm1;
    publisher->m_stats.fan_rpm_1 = rpm1;
    pthread_mutex_unlock(&publisher->m_lock);
}

static void fetch_from_stratum(StratumManager *m)
{
    float best = m->getBestSessionDiff();
    publisher->m_stats.best_difficulty = best;
    if (best > publisher->m_stats.total_best_difficulty) {
        publisher->m_stats.total_best_difficulty = best;
    }
    publisher->m_stats.shares_accepted = m->getSharesAccepted();
    publisher->m_stats.shares_rejected = m->getSharesRejected();
    publisher->m_stats.pool_errors     = m->getPoolErrors();
    publisher->m_stats.pool_difficulty = m->getPoolDifficulty();

    int found = m->getFoundBlocks();
    if (found && !last_block_found) {
        publisher->m_stats.blocks_found++;
        publisher->m_stats.total_blocks_found++;
    }
    last_block_found = found;
}

static void fetch_from_system(System *s)
{
    publisher->m_stats.hashrate    = s->getCurrentHashrate();
    publisher->m_stats.hashrate_1m = s->getCurrentHashrate1m();
}

static void halt_forever()
{
    ESP_LOGI(TAG, "halting mqtt_task");
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(15000));
    }
}

static void build_node_id(char *out, size_t out_len, const char *hostname)
{
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    if (hostname && *hostname) {
        snprintf(out, out_len, "%s-%02X%02X%02X", hostname, mac[3], mac[4], mac[5]);
    } else {
        snprintf(out, out_len, "nerdqaxe-%02X%02X%02X", mac[3], mac[4], mac[5]);
    }
    for (char *p = out; *p; ++p) {
        if (*p == ' ' || *p == '/' || *p == '#' || *p == '+') *p = '-';
    }
}

void mqtt_task(void *pvParameters)
{
    System *sys = &SYSTEM_MODULE;

    if (!Config::isMqttEnabled()) {
        ESP_LOGI(TAG, "MQTT disabled");
        halt_forever();
    }

    char *uri      = Config::getMqttURI();
    char *user     = Config::getMqttUser();
    char *pass     = Config::getMqttPass();
    char *prefix   = Config::getMqttTopicPrefix();
    char *hostname = Config::getHostname();
    bool  discovery = Config::isMqttDiscoveryEnabled();
    uint16_t interval_s = Config::getMqttPublishInterval();
    if (interval_s == 0) interval_s = 15;

    char node_id[64];
    build_node_id(node_id, sizeof(node_id), hostname);

    char client_id[80];
    snprintf(client_id, sizeof(client_id), "nerdqaxe-%s", node_id);

    publisher = new MqttPublisher();
    if (!publisher->init(uri, user, pass, client_id, prefix, node_id, discovery)) {
        ESP_LOGE(TAG, "MQTT init failed");
        free(uri); free(user); free(pass); free(prefix); free(hostname);
        delete publisher; publisher = nullptr;
        halt_forever();
    }
    free(uri); free(user); free(pass); free(prefix); free(hostname);

    if (!publisher->start()) {
        ESP_LOGE(TAG, "MQTT start failed");
        halt_forever();
    }

    TimerHandle_t t = xTimerCreate("MqttUptime", pdMS_TO_TICKS(1000), pdTRUE, nullptr, uptime_timer_cb);
    if (t) xTimerStart(t, 0);

    while (1) {
        if (POWER_MANAGEMENT_MODULE.isShutdown()) {
            ESP_LOGW(TAG, "suspended");
            vTaskSuspend(NULL);
        }
        if (publisher->is_connected()) {
            pthread_mutex_lock(&publisher->m_lock);
            fetch_from_system(sys);
            fetch_from_stratum(STRATUM_MANAGER);
            pthread_mutex_unlock(&publisher->m_lock);
            publisher->publish_state();
        }
        vTaskDelay(pdMS_TO_TICKS(interval_s * 1000));
    }
}
