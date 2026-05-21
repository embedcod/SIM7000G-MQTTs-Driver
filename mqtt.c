/*
 * SIM7000G mqtt.c
 *
 *  Created: 03/08/2025
 *	Modified: 04/08/2025
 *    
 *		Author: ERIC MULWA
 */

#include "mqtt.h"
char urcbuffer[BUF_SIZE];
/**
*  @brief UART Configuration
*/
void uart_init() {
    uart_config_t uart_config = {
        .baud_rate = SIM7000G_BAUDRATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE
    };

    uart_driver_install(MODEM_UART_NUM, BUF_SIZE * 2, 0, 0, NULL, 0);
    uart_param_config(MODEM_UART_NUM, &uart_config);
    uart_set_pin(MODEM_UART_NUM, TX_PIN, RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
}


/**
*  @brief Hardware Reset
*/
void gsm_reset() {
	hardware_poweroff();
	vTaskDelay(pdMS_TO_TICKS(1000)); 
	gsm_poweron();
}

/**
*  @brief Hardware Power On
*/
void gsm_poweron() {
	// power on with PWR 
	esp_rom_gpio_pad_select_gpio(PWR_PIN);
    gpio_set_direction(PWR_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(PWR_PIN, 0);
    vTaskDelay(pdMS_TO_TICKS(1000)); 
    gpio_set_level(PWR_PIN, 1); 
    vTaskDelay(pdMS_TO_TICKS(12000)); // Wait for the module to initialize (minimum of 10s)
}

/**
*  @brief Hardware Power Down
*/
void hardware_poweroff() {
	esp_rom_gpio_pad_select_gpio(PWR_PIN);
    gpio_set_direction(PWR_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(PWR_PIN, 0);
    vTaskDelay(pdMS_TO_TICKS(1500)); 
    gpio_set_level(PWR_PIN, 1); 
}

/**
*  @brief AT Command Power Down
*/
void at_poweroff() {
	bool power_down = false;
    for (int attempt = 0; attempt < 3; attempt++) {
        if (!send_at_command("AT+CPOWD=1", "OK", RETRIES, CMD_DELAY_MS)) {
            ESP_LOGW(TAG_GSM, "AT command power down failed on attempt %d, retrying...\n", attempt + 1);
        } else {
            power_down = true;
            ESP_LOGI(TAG_GSM, "Modem powered down successfully!\n");         
			break;     
        }
        
    } if (!power_down) {
		error(3);
        ESP_LOGE(TAG_GSM, "AT command power down failed, exiting task...\n");
    }
  return;
}

/**
*  @brief Boolean to send AT Commands with response check
*/
bool send_at_command(const char *command, const char *expected_response, int retries, uint32_t timeout_ms) {
    char response[BUF_SIZE] = {0};
    int attempt;
    
    for (attempt = 0; attempt < retries; attempt++) {
        // Clear receive buffer before sending
        uint8_t dump[BUF_SIZE];
        while (uart_read_bytes(MODEM_UART_NUM, dump, BUF_SIZE, 10 / portTICK_PERIOD_MS) > 0);
        
        // Send command with proper line endings
        uart_write_bytes(MODEM_UART_NUM, command, strlen(command));
        uart_write_bytes(MODEM_UART_NUM, "\r\n", 2);
        
        uint64_t start_time = esp_timer_get_time();
        int index = 0;
        bool got_response = false;
        
        while ((esp_timer_get_time() - start_time) < (timeout_ms * 1000)) {
            int len = uart_read_bytes(MODEM_UART_NUM, (uint8_t*)&response[index], 
                                     BUF_SIZE - index - 1, 20 / portTICK_PERIOD_MS);
            if (len > 0) {
                index += len;
                response[index] = '\0';
                // Check for expected response
                if (strstr(response, expected_response)) {
                    got_response = true;
                    break;
                }
                // Check for ERROR response
                if (strstr(response, "ERROR")) {
                    break;
                }
            }
        }       
        ESP_LOGI(TAG_GSM, "Attempt %d: Cmd: '%s', Resp: '%s'", attempt+1, command, response);
        if (got_response) {
            return true;
        }      
        vTaskDelay(pdMS_TO_TICKS(200 * (attempt + 1))); // Progressive backoff
    }
    ESP_LOGE(TAG_GSM, "Command '%s' failed after %d retries", command, retries);
    return false;
}

/**
*  @brief Publisher Client
*/
bool mqtt_pubclient() {	
    for (int i = 0; i < dataCount; i++) { 
	char *payload = create_json_payloadi(i);
    bool qpub = false;
	char pub_cmd[100];
	ESP_LOGI(TAG_GSM, "Generated JSON Payload: %s\n", payload);
	vTaskDelay(pdMS_TO_TICKS(100));
	sprintf(pub_cmd, "AT+SMPUB=\"alpha/up\",\"%d\",0,1", strlen(payload)); 
	    for (int attempt = 0; attempt < 2; attempt++) {
			if (!send_at_command(pub_cmd, ">", RETRIES, CMD_DELAY_MS)) {
				error(3);
			    ESP_LOGE(TAG_GSM, "MQTT publish command failed!");
			} else {
		        uart_write_bytes(MODEM_UART_NUM, payload, strlen(payload));
		        vTaskDelay(pdMS_TO_TICKS(500));
		        uart_write_bytes(MODEM_UART_NUM, "\x1A", 1); // terminate
		        vTaskDelay(pdMS_TO_TICKS(100));
		        qpub = true;	        
		        printf("Payload: %d published successfully!\n", i);   
		        break; 
			}
	    }
    if (!qpub) {
		error(3);
	    ESP_LOGE(TAG_GSM, "MQTT publish command failed after 3 attempts and exited.");
	    free(payload);
	    return false;
    }
   resetWatchdog(0);
   vTaskDelay(pdMS_TO_TICKS(100));
   check_mqtt_link_status();
   vTaskDelay(pdMS_TO_TICKS(100));
  }
	bufferReady = false;
	writeIndex = 0;
    dataCount = 0; 
	vTaskDelay(pdMS_TO_TICKS(100));  
 return true;
}

/**
*  @brief MQTTs Main SClient 
*/
bool mqtt_client() {	
	// init mqtts
    bool mqttinit = false;
    for (int attempt = 0; attempt < 2; attempt++) {
		if (!mqtts_init()) {
		    ESP_LOGW(TAG_GSM, "MQTT initialization failed on attempt %d. Retrying...", attempt + 1);
        } else {
            mqttinit = true;
            printf("MQTT initialization successful!\n");
            break; 
        }
    }
    if (!mqttinit) {
		error(3);
        ESP_LOGE(TAG_GSM, "MQTT initialization failed after %d retries.", RETRIES);
        powerdown_modem();
        return false;
    } 
   
   check_mqtt_link_status();
   
	// Mqtt Termination (optional)
	// mqtts_deinit();
 return true;
}

/**
*  @brief Init uart and power on Modem 
*/
bool poweron_modem() {
    uart_init();
    esp_rom_gpio_pad_select_gpio(DTR_PIN);
    gpio_set_direction(DTR_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(DTR_PIN, 1);  // Modem remains awake
    // Clear any existing UART data
    uint8_t dump[BUF_SIZE];
    while (uart_read_bytes(MODEM_UART_NUM, dump, BUF_SIZE, 10 / portTICK_PERIOD_MS) > 0);
    // Power cycle the module
    gsm_reset();
    // Extended initialization wait with progressive checks
    bool modem_ready = false;
    for (int attempt = 0; attempt < 10; attempt++) {
        // Send AT and wait for response
        uart_write_bytes(MODEM_UART_NUM, "AT\r\n", 4);       
        uint64_t start_time = esp_timer_get_time();
        while ((esp_timer_get_time() - start_time) < 1000000) { // 1 second timeout
            uint8_t data;
            if (uart_read_bytes(MODEM_UART_NUM, &data, 1, 10 / portTICK_PERIOD_MS) > 0) {
                if (data == 'O' || data == 'K') { // Looking for "OK" response
                    // Read remaining characters if any
                    while (uart_read_bytes(MODEM_UART_NUM, &data, 1, 10 / portTICK_PERIOD_MS) > 0);
                    modem_ready = true;
                    break;
                }
            }
        }      
        if (modem_ready) {
            ESP_LOGI(TAG_GSM, "Modem responded to AT command");
            break;
        } else {
            ESP_LOGW(TAG_GSM, "Modem not responding, attempt %d", attempt+1);
            vTaskDelay(pdMS_TO_TICKS(1000));
            
            // Try power cycle every 3 attempts
            if ((attempt % 3) == 2) {
                ESP_LOGI(TAG_GSM, "Attempting power cycle");
                gsm_reset();
            }
        }
	   resetWatchdog(0);
	   vTaskDelay(pdMS_TO_TICKS(100));
    }
    if (!modem_ready) {
        error(3);
        ESP_LOGE(TAG_GSM, "Failed to initialize modem after multiple attempts");
        return false;
    }
    return true;
}

/**
*  @brief Latch to Network, Activate PDP & configure MQTTs
*/
bool activate_pdp() {
    // Enable network registration URCs
    if (!send_at_command("AT+CGREG=1", "OK", RETRIES, CMD_DELAY_MS)) {
        ESP_LOGW(TAG_GSM, "Failed to enable network registration URCs");
    }
    // GSM Network Latch Check with improved state handling
    bool latch = false;
    bool pdpactive = false;
    for (int attempt = 0; attempt < 8; attempt++) {
        // Check SIM status first
        if (!send_at_command("AT+CPIN?", "READY", RETRIES, CMD_DELAY_MS)) {
            ESP_LOGW(TAG_GSM, "SIM not ready on attempt %d", attempt + 1);
            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }
        // Check network registration with multiple acceptable states
        if (send_at_command("AT+CGREG?", "1,1", 1, CMD_DELAY_MS) ||  // Home network
        	send_at_command("AT+CGREG?", "0,1", 1, CMD_DELAY_MS) ||  // Home network
            send_at_command("AT+CGREG?", "1,5", 1, CMD_DELAY_MS) ||  // Roaming
            send_at_command("AT+CGREG?", "0,5", 1, CMD_DELAY_MS)) {  // Roaming
            ESP_LOGI(TAG_GSM, "Network registered successfully");
            // Check PDP activation status
            if (send_at_command("AT+CGACT?", "1,1", 1, 5000)) {
                latch = true;
                pdpactive = true;
                ESP_LOGI(TAG_GSM, "PDP already active");
                break;
            } else {
                ESP_LOGI(TAG_GSM, "PDP not active, will activate");
                latch = true;
                break;
            }
        }
        ESP_LOGW(TAG_GSM, "Network latch attempt %d failed, retrying...", attempt + 1);
        vTaskDelay(pdMS_TO_TICKS(3000 + (attempt * 1000)));  // Progressive backoff
	   resetWatchdog(0);
	   vTaskDelay(pdMS_TO_TICKS(100));
    }

    if (!latch) {
        error(3);
        ESP_LOGE(TAG_GSM, "GSM Network Latch failed after retries");
        return false;
    }

    // PDP Context configuration and activation

    bool success = false;
    char pdp_cmd[64];
    sprintf(pdp_cmd, "AT+CNACT=1,\"%s\"", APN);
    if (!pdpactive) {
    for (int attempt = 0; attempt < 5; attempt++) {
        if (send_at_command(pdp_cmd, "ACTIVE", 1, 15000)) {
            success = true;
            ESP_LOGI(TAG_GSM, "PDP Context activated successfully");
            break;
        }
        // Check if we got ERROR response
        if (send_at_command(pdp_cmd, "ERROR", 1, 5000)) {
            ESP_LOGE(TAG_GSM, "PDP Activation rejected by network");
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }
        ESP_LOGW(TAG_GSM, "PDP Activation attempt %d failed, retrying...", attempt + 1);
        vTaskDelay(pdMS_TO_TICKS(5000));
	   resetWatchdog(0);
	   vTaskDelay(pdMS_TO_TICKS(100));
    }

    if (!success) {
        error(3);
        ESP_LOGE(TAG_GSM, "Failed to activate PDP context");
        return false;
    }
   }
   pdpactive = false;
    // Verify PDP activation status
    if (!send_at_command("AT+CNACT?", "1", RETRIES, CMD_DELAY_MS)) {
        error(3);
        ESP_LOGE(TAG_GSM, "PDP activation verification failed");
        return false;
    }
    // Configure MQTT SSL 
    for (int attempt = 0; attempt < 3; attempt++) {
        if (configure_mqtts_settings()) {
            ESP_LOGI(TAG_GSM, "SSL configuration successful");
            return true;
        }
        ESP_LOGW(TAG_GSM, "SSL configuration failed, attempt %d", attempt + 1);
        vTaskDelay(pdMS_TO_TICKS(2000));
        // Reset PDP context if SSL fails multiple times
        if (attempt == 1) {
            send_at_command("AT+CNACT=0", "OK", 1, 5000);
            vTaskDelay(pdMS_TO_TICKS(2000));
            send_at_command(pdp_cmd, "ACTIVE", 1, 15000);
        }
	   resetWatchdog(0);
	   vTaskDelay(pdMS_TO_TICKS(100));
    }
    error(3);
    ESP_LOGE(TAG_GSM, "MQTT SSL configuration failed after retries");
    return false;
}


/**
*  @brief Open and connect to mqtts
*/
bool open_mqtts() {
 	// Connect to MQTT broker (with credentials if required)
    bool conn = false;
    if (strlen(MQTT_USERNAME) > 0 && strlen(MQTT_PASSWORD) > 0) {
        // Set client id, username, password
        char cmd1[80], cmd2[80], cmd3[80];
        sprintf(cmd1, "AT+SMCONF=\"CLIENTID\",\"%s\"", MQTT_CLIENT_ID);
        sprintf(cmd2, "AT+SMCONF=\"USERNAME\",\"%s\"", MQTT_USERNAME);
        sprintf(cmd3, "AT+SMCONF=\"PASSWORD\",\"%s\"", MQTT_PASSWORD);
        send_at_command(cmd1, "OK", RETRIES, CMD_DELAY_MS);
        send_at_command(cmd2, "OK", RETRIES, CMD_DELAY_MS);
        send_at_command(cmd3, "OK", RETRIES, CMD_DELAY_MS);
    } else {
        char cmd1[80];
        sprintf(cmd1, "AT+SMCONF=\"CLIENTID\",\"%s\"", MQTT_CLIENT_ID);
        send_at_command(cmd1, "OK", RETRIES, CMD_DELAY_MS);
    }

    for (int attempt = 0; attempt < 3; attempt++) {
    	if (!send_at_command("AT+SMCONN", "OK", RETRIES, 5000)) {
			error(3);
	        ESP_LOGE(TAG_GSM, "MQTT connect failed!");
	        return false;
        } else {
            conn = true;
            ESP_LOGI(TAG_GSM, "MQTT connect successful!");
            break; 
        }
	   resetWatchdog(0);
	   vTaskDelay(pdMS_TO_TICKS(100));
    }
    if (!conn) {
		error(3);
        ESP_LOGE(TAG_GSM, "MQTT connect failed after %d retries.", RETRIES);
        return false;
    } 	
	
  return true;		
}

/**
*  @brief Disconnect MQTTs
*/
bool mqtts_disconnect() {
	bool disconnect = false;
	for (int attempt = 0; attempt < 2; attempt++) {
	    if (!send_at_command("AT+SMDISC", "OK", RETRIES, CMD_DELAY_MS)) {
	        ESP_LOGW(TAG_GSM, "Failed to disconnect from mqtt on attempt %d, retrying...", attempt + 1);
	    } else {
	        ESP_LOGI(TAG_GSM, "Disconnected from mqtt successfully on attempt %d.", attempt + 1);
	        disconnect = true;
	        break;
	    }    
	} if (!disconnect) {
		error(3);
	    ESP_LOGE(TAG_GSM, "Failed to disconnect from mqtt after 2 attempts.");
	    return false;
	}
  return true;	
}

/**
*  @brief Deactivate pdp context
*/
bool deactivate_pdp() {
	const int retries_pdp_deactivate = 2;
	bool pdp_deactivated = false;
	for (int attempt = 0; attempt < retries_pdp_deactivate; attempt++) {
	    if (!send_at_command("AT+CNACT=0", "DEACTIVE", RETRIES, CMD_DELAY_MS)) {
	        ESP_LOGW(TAG_GSM, "Failed to deactivate PDP context on attempt %d, retrying...", attempt + 1);
	    } else {
	        ESP_LOGI(TAG_GSM, "PDP context deactivated successfully on attempt %d.", attempt + 1);
	        pdp_deactivated = true;
	        break;
	    }    
	} if (!pdp_deactivated) {
		error(3);
	    ESP_LOGE(TAG_GSM, "Failed to deactivate PDP context after %d attempts.", retries_pdp_deactivate);
	    return false;
	}
  return true;	
}

/**
*  @brief Power down modem
*/
void powerdown_modem() {
    const int retries_modem_power_down = 2;
    bool modem_powered_down = false;
    for (int attempt = 0; attempt < retries_modem_power_down; attempt++) {
        if (!send_at_command("AT+CPOWD=1", "NORMAL POWER DOWN", 2, CMD_DELAY_MS)) {
            ESP_LOGW(TAG_GSM, "Failed to AT power down modem on attempt %d, retrying...\n", attempt + 1);
        } else {
            modem_powered_down = true;
            printf("Modem powered down successfully!\n");
            break;
        }
        
    } if (!modem_powered_down) {
		error(3);
        ESP_LOGE(TAG_GSM, "AT command power down failed after %d retries. Initiating Hardware Power Down...\n", retries_modem_power_down);
        hardware_poweroff();
    }
    
	// Clean up
	uart_driver_delete(MODEM_UART_NUM);	
  return;	
}

/**
*  @brief Initialize MQTTs 
*/
bool mqtts_init() {
	if (!poweron_modem() || !activate_pdp() || !open_mqtts()) {
		error(3);
	    ESP_LOGE(TAG_GSM, "MQTTs initialization failed!");
	    powerdown_modem();
        return false;
    } else {
        printf("MQTTs initialized successfully!\n");
    }
   return true;
}

/**
*  @brief Deinitialize MQTTs
*/
void mqtts_deinit() {
	if (!mqtts_disconnect() || !deactivate_pdp()) {
		error(3);
	    ESP_LOGE(TAG_GSM, "MQTTs deinitialization failed!");
	    powerdown_modem();
    } else {
        printf("MQTTs deinitialized successfully!\n");
        powerdown_modem();
    }
   return;
}

/**
 * @brief Check MQTT link state 
 */
void check_mqtt_link_status() {
    if (!send_at_command("AT+SMSTATE?", "1", 1, 1000)) {
		error(3);
        ESP_LOGE(TAG_GSM, "MQTTs link disconnected! Reconnecting...");
        mqtts_error_reconnect();
    } else {
        printf("MQTTs link still active.\n");
    }
  return;
}

/**
*  @brief Configure MQTT with Optimal Settings TODO
*
bool configure_mqtts_settings() {
    char seturl[100];
    sprintf(seturl, "AT+SMCONF=\"URL\",\"%s\",\"%d\"", MQTT_BROKER, MQTT_PORT);
 
    if (!send_at_command("ATE0", "OK", RETRIES, CMD_DELAY_MS) ||
		!send_at_command(seturl, "OK", RETRIES, CMD_DELAY_MS) ||
        !send_at_command("AT+SMCONF=\"KEEPTIME\",60", "OK", RETRIES, CMD_DELAY_MS) ||
        !send_at_command("AT+CSSLCFG=\"CONVERT\",2,\"rootCA.pem\"", "OK", RETRIES, CMD_DELAY_MS) ||
        !send_at_command("AT+CSSLCFG=\"CONVERT\",1,\"cert.pem\",\"key.pem\"", "OK", RETRIES, CMD_DELAY_MS) ||
        !send_at_command("AT+SMSSL=1,rootCA.pem,cert.pem", "OK", RETRIES, CMD_DELAY_MS)) { 
		error(3);
        ESP_LOGE(TAG_GSM, "Failed to set Core configurations!\n");
        return false;
    } else {
       printf("MQTTs Core configurations set successfully!\n"); 
    }
    return true;
} */


/**
*  @brief Configure basic MQTT connection (non-SSL)
*/
bool configure_mqtts_settings() {
    // Set basic MQTT parameters
    char seturl[100];
    sprintf(seturl, "AT+SMCONF=\"URL\",\"%s\",\"%d\"", MQTT_BROKER, MQTT_PORT);
    if (!send_at_command("ATE0", "OK", 1, CMD_DELAY_MS)) {
        ESP_LOGE(TAG_GSM, "Failed to disable echo");
        return false;
    }
    if (!send_at_command(seturl, "OK", 1, CMD_DELAY_MS)) {
        ESP_LOGE(TAG_GSM, "Failed to set broker URL");
        return false;
    }
    if (!send_at_command("AT+SMCONF=\"KEEPTIME\",60", "OK", 1, CMD_DELAY_MS)) {
        ESP_LOGE(TAG_GSM, "Failed to set keepalive");
        return false;
    }
    ESP_LOGI(TAG_GSM, "MQTT configured without SSL encryption");
    return true;
}


/**
* @brief URC handler
*/
bool check_mqtt_urc() {
    static char urc_buffer[192];
    static int urc_index = 0;
    uint8_t data;
    
    while (uart_read_bytes(MODEM_UART_NUM, &data, 1, 20 / portTICK_PERIOD_MS) > 0) {
        if (urc_index < sizeof(urc_buffer)-1) {
            urc_buffer[urc_index++] = data;
            
            if (data == '\n') {
                urc_buffer[urc_index] = '\0';
                ESP_LOGI(TAG_GSM, "RAW URC: %s", urc_buffer);
                                        
                // Handle +SMSTATE:
                if (strstr(urc_buffer, "+SMSTATE:")) {
                    int err_code;
                    if (sscanf(urc_buffer, "+SMSTATE: %d", &err_code) == 1) {
						error(3);
                        ESP_LOGE(TAG_GSM, "MQTT Error: %d", err_code);
                        handle_mqtt_error(err_code);
                    }
                }
                
                urc_index = 0;
                return true;
            }
        } else {
            urc_index = 0; // Prevent overflow
        }
    }
    return false;
}

/**
* @brief Handles MQTT connection errors
* @param err_code From +SMTSTATE URC
*/
void handle_mqtt_error(int err_code) {
    switch(err_code) {
        case 0: // Link disconnected
            ESP_LOGI(TAG_GSM, "Reconnecting MQTTs...");
            mqtts_error_reconnect();
          break;
        default:
        	error(3);
            ESP_LOGE(TAG_GSM, "Unhandled error: %d - mcu restarting...", err_code);
		    powerdown_modem();
		    esp_restart();
    }
}


/**
* @brief +SMSTATE URC mqtts reconnection 
*/
void mqtts_error_reconnect() {
	if (!open_mqtts()) {
		error(3);
	    ESP_LOGE(TAG_GSM, "mqtts reopen failed, restarting mqtts...");
	    gsm_reset();		   
		if (!activate_pdp()) {
			error(3);
		    ESP_LOGE(TAG_GSM, "Failed to activate pdp after gsm reset, initiating mcu restart...!");
		    powerdown_modem();
		    esp_restart();
	    } else {	
		    printf("Pdp activated successfully after gsm reset!");
	    }
		if (!open_mqtts()) {
			error(3);
		    ESP_LOGE(TAG_GSM, "Failed to open mqqts after gsm reset, initiating mcu restart...!");
		    powerdown_modem();
		    esp_restart();
	    } else {	
		    printf("Mqtts reopened successfully after gsm reset!\n");
	    }
	    return;
    } else {	
	    printf("mqtts reopened successfully!\n");
    }
   return;
}