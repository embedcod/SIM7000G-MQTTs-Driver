/*
 * SIM7000G mqtt.h
 *
 *  Created on: Aug 03, 2025
 *      Author: ERIC MULWA
 */

#ifndef QUECTEL_MQTT_H
#define QUECTEL_MQTT_H

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "cJSON.h"
#include <math.h>

// Definitions
#define MODEM_UART_NUM UART_NUM_1
#define TX_PIN 27
#define RX_PIN 26
#define PWR_PIN 4 
#define DTR_PIN 25
#define ESP_BAUDRATE 115200
#define SIM7000G_BAUDRATE 115200
#define TAG_GSM "MODEM"

// MQTT Configuration (Local Mosquitto Broker)
#define MQTT_BROKER "test.mosquitto.org"    // Your Broker IP/Domain
//#define MQTT_PORT 8883             			// TLS port
#define MQTT_PORT 1883             			// No TLS port
#define MQTT_CLIENT_ID "alphax"    			// Unique per device
#define MQTT_USERNAME ""           			// Leave empty if no auth
#define MQTT_PASSWORD ""           			// Leave empty if no auth
#define BUF_SIZE 512

static const int RETRIES = 5;
static const int CMD_DELAY_MS = 5000;

// External definitions
extern bool bufferReady;
extern char APN[32];
extern int dataCount;
extern int writeIndex; 
extern void resetWatchdog(int core_id);
extern void error(int index);
extern char* create_json_payloadi(int index);
extern void getChipIdString(char *deviceSerial, size_t size);

void uart_init();
void gsm_reset();
void gsm_poweron();
void hardware_poweroff();
void at_poweroff();
bool send_at_command(const char *command, const char *expected_response, int retries, uint32_t timeout_ms);
bool poweron_modem();
bool activate_pdp();
bool open_mqtts();
bool mqtts_disconnect();
bool deactivate_pdp();
void powerdown_modem();
bool mqtts_init();
void mqtts_deinit();
bool configure_mqtts_settings();
void handle_mqtt_error(int err_code);
void check_mqtt_link_status();
bool check_mqtt_urc();
bool mqtt_pubclient();
bool mqtt_client();
void mqtts_error_reconnect();

#endif // QUECTEL_MQTT_H
