#pragma once

#include "mqtt_client_wrapper.h"

void mqtt_task(void *pvParameters);

void mqtt_task_set_temperature(float temp, float temp2);
void mqtt_task_set_pwr(float vin, float iin, float pin, float vout, float iout, float pout);
void mqtt_task_set_fan(float pwm0, float rpm0, float pwm1, float rpm1);
