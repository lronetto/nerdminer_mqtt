#pragma once

#include "esp_http_server.h"

esp_err_t GET_mqtt_info(httpd_req_t *req);
esp_err_t PATCH_update_mqtt(httpd_req_t *req);
