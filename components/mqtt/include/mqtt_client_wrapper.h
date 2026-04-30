#pragma once

#include <pthread.h>
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    float hashrate;
    float hashrate_1m;
    float temp;
    float temp2;
    float pwr_vin;
    float pwr_iin;
    float pwr_pin;
    float pwr_vout;
    float pwr_iout;
    float pwr_pout;
    float fan_pwm_0;
    float fan_pwm_1;
    float fan_rpm_0;
    float fan_rpm_1;
    float best_difficulty;
    float total_best_difficulty;
    int   shares_accepted;
    int   shares_rejected;
    int   pool_difficulty;
    int   pool_errors;
    int   uptime;
    int   total_uptime;
    int   blocks_found;
    int   total_blocks_found;
} MqttStats;

class MqttPublisher {
  protected:
    char *m_uri;
    char *m_user;
    char *m_pass;
    char *m_client_id;
    char *m_topic_prefix;
    char *m_node_id;       // e.g. nerdqaxe-AABBCC (mac suffix)

    void *m_handle;        // esp_mqtt_client_handle_t (forward, kept opaque)
    bool  m_connected;
    bool  m_discovery_enabled;
    bool  m_discovery_published;

    void publish_json(const char *topic, const char *payload, int qos, bool retain);
    void publish_discovery();

  public:
    MqttStats m_stats;
    pthread_mutex_t m_lock;

    MqttPublisher();
    ~MqttPublisher();

    bool init(const char *uri, const char *user, const char *pass,
              const char *client_id, const char *topic_prefix,
              const char *node_id, bool discovery_enabled);
    bool start();
    void publish_state();

    bool is_connected() const { return m_connected; }
    void on_connected();
    void on_disconnected();
};
