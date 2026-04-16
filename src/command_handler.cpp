#include "command_handler.h"

#include <esp_log.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/task.h>
#include <stdlib.h>

#include <cstring>

#include "stream_manager.h"
#include "teletype.h"

#define INPUT_BUF_SIZE 128
#define RESPONSE_BUF_SIZE 1024

extern StreamManager stream_manager;
extern Teletype* tty;
extern SemaphoreHandle_t cmd_mutex_stream;
char* buf;
char* response_buf;

const CommandHandler::commandItem_t CommandHandler::cmdList[] =
    {
        {0x02, "help", &CommandHandler::cmd_help},  // Help function
        {0x03, "wifi", &CommandHandler::cmd_wifi},
        {0x04, "scan", &CommandHandler::cmd_scan},
        {0x05, "baud", &CommandHandler::cmd_baudrate},
        {0x06, "rxpol", &CommandHandler::cmd_rxpol},
        {0x07, "txpol", &CommandHandler::cmd_txpol},

};

namespace {
constexpr const char TAG[] = "CMD";
}  // namespace

CommandHandler::CommandHandler() {
    ESP_LOGI(TAG, "CommandHandler initialized");
    buf = (char*)malloc(INPUT_BUF_SIZE * sizeof(char));
    response_buf = nullptr;
    memset(buf, '\0', INPUT_BUF_SIZE);
}

void CommandHandler::cmd_help(char* arg) {
    asprintf(&response_buf, "Available commands:\r\n");
    for (const auto& cmd : cmdList) {
        ESP_LOGI(TAG, "command %s", cmd.funcTag);
        asprintf(&response_buf, "%s->%s\r\n", response_buf, cmd.funcTag);
    }
}

void CommandHandler::cmd_scan(char* arg) {
    // Check if this is a scan command
    asprintf(&response_buf, "Scanning for WiFi networks...\r\n");

    // Start WiFi scan
    wifi_scan_config_t scan_config = {0};
    scan_config.ssid = NULL;
    scan_config.bssid = NULL;
    scan_config.channel = 0;
    scan_config.show_hidden = true;
    scan_config.scan_type = WIFI_SCAN_TYPE_PASSIVE;

    // Only initialize ONE union member:
    scan_config.scan_time.passive = 120;  // 120ms per channel for passive scan

    esp_err_t err = esp_wifi_scan_start(&scan_config, true);
    if (err != ESP_OK) {
        asprintf(&response_buf, "WiFi scan failed: %s\r\n", esp_err_to_name(err));
        return;
    }

    uint16_t ap_count = 0;
    esp_wifi_scan_get_ap_num(&ap_count);
    ESP_LOGI(TAG, "Found %d WiFi networks", ap_count);

    if (ap_count == 0) {
        asprintf(&response_buf, "No WiFi networks found\r\n");
        return;
    }

    wifi_ap_record_t* ap_list = (wifi_ap_record_t*)malloc(ap_count * sizeof(wifi_ap_record_t));
    if (ap_list == NULL) {
        asprintf(&response_buf, "Memory allocation failed for AP list\r\n");
        return;
    }

    esp_wifi_scan_get_ap_records(&ap_count, ap_list);

    asprintf(&response_buf, "Found %d networks:\r\n", ap_count);
    asprintf(&response_buf, "%sNo. SSID                         RSSI Auth\r\n", response_buf);
    asprintf(&response_buf, "%s--- ------------------------------ ---- -----\r\n", response_buf);

    for (uint16_t i = 0; i < ap_count; i++) {
        const char* authmode_str = "OPEN";
        switch (ap_list[i].authmode) {
            case WIFI_AUTH_OPEN:
                authmode_str = "OPEN";
                break;
            case WIFI_AUTH_WEP:
                authmode_str = "WEP";
                break;
            case WIFI_AUTH_WPA_PSK:
                authmode_str = "WPA";
                break;
            case WIFI_AUTH_WPA2_PSK:
                authmode_str = "WPA2";
                break;
            case WIFI_AUTH_WPA_WPA2_PSK:
                authmode_str = "WPA/WPA2";
                break;
            case WIFI_AUTH_WPA3_PSK:
                authmode_str = "WPA3";
                break;
            case WIFI_AUTH_WPA2_WPA3_PSK:
                authmode_str = "WPA2/WPA3";
                break;
            default:
                authmode_str = "OTHER";
                break;
        }

        char ssid_str[33];
        strcpy((char*)ssid_str, (char*)ap_list[i].ssid);

        asprintf(&response_buf, "%s%2d  %-30s %4d %s\r\n",
                 response_buf, i + 1, ssid_str, ap_list[i].rssi, authmode_str);
    }
    free(ap_list);
    return;
}

void CommandHandler::cmd_wifi(char* arg) {
    if (arg == NULL) {
        ESP_LOGI(TAG, "Usage: \"wifi scan\" or \"wifi {ssid} [password]\"");
        wifi_config_t cfg = {0};
        esp_wifi_get_config(ESP_IF_WIFI_STA, &cfg);
        tcpip_adapter_ip_info_t ip_info;
        tcpip_adapter_get_ip_info(TCPIP_ADAPTER_IF_STA, &ip_info);
        asprintf(&response_buf, "Usage: 'wifi scan' or 'wifi {ssid} [password]'\r\nCurrent SSID: %s\r\nIs connected: %s\r\n", cfg.sta.ssid, ip_info.ip.addr ? "Yes" : "No");
        return;
    }

    // Normal WiFi connection
    wifi_config_t cfg = {0};
    esp_wifi_get_config(ESP_IF_WIFI_STA, &cfg);

    strncpy((char*)cfg.sta.ssid, arg, sizeof(cfg.sta.ssid) - 1);
    cfg.sta.ssid[sizeof(cfg.sta.ssid) - 1] = '\0';
    cfg.sta.scan_method = WIFI_FAST_SCAN;
    cfg.sta.channel = 0;

    ESP_LOGI(TAG, "SSID: \"%s\"\n", arg);

    arg = strtok(NULL, " ");
    if (arg != NULL) {
        strncpy((char*)cfg.sta.password, arg, sizeof(cfg.sta.password) - 1);
        cfg.sta.password[sizeof(cfg.sta.password) - 1] = '\0';
        ESP_LOGI(TAG, "Password: \"%s\"\n", arg);
        cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    } else {
        ESP_LOGI(TAG, "No password\n");
        cfg.sta.password[0] = '\0';
        cfg.sta.threshold.authmode = WIFI_AUTH_OPEN;
    }

    esp_wifi_disconnect();
    esp_wifi_stop();
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &cfg));
    esp_wifi_start();
    asprintf(&response_buf, "Connecting to WiFi SSID: %s\r\n", cfg.sta.ssid);
}

void CommandHandler::cmd_baudrate(char* arg) {
    if (arg != NULL) {
        int baudrate = atoi(arg);
        ESP_LOGI(TAG, "Setting baudrate to %d", baudrate);
        tty->set_baudrate(baudrate);
        asprintf(&response_buf, "Baudrate set to %d\r\n", baudrate);
    }
}

void CommandHandler::cmd_rxpol(char* arg) {
    if (arg != NULL) {
        bool normal = atoi(arg) == 1;
        ESP_LOGI(TAG, "Setting RX polarity to %s", normal ? "inverted" : "normal");
        tty->set_rx_polarity(normal);
        asprintf(&response_buf, "RX polarity set to %s\r\n", normal ? "inverted" : "normal");
    } else {
        ESP_LOGI(TAG, "Usage: \"rxpol {0|1}\"");
        asprintf(&response_buf, "Usage: \"rxpol {0|1}\"\r\nCurrent RX polarity: %s\r\n", tty->get_rx_polarity() ? "inverted" : "normal");
    }
}

void CommandHandler::cmd_txpol(char* arg) {
    if (arg != NULL) {
        bool normal = atoi(arg) == 1;
        ESP_LOGI(TAG, "Setting TX polarity to %s", normal ? "inverted" : "normal");
        tty->set_tx_polarity(normal);
        asprintf(&response_buf, "TX polarity set to %s\r\n", normal ? "inverted" : "normal");
    } else {
        ESP_LOGI(TAG, "Usage: \"txpol {0|1}\"");
        asprintf(&response_buf, "Usage: \"txpol {0|1}\"\r\nCurrent TX polarity: %s\r\n", tty->get_tx_polarity() ? "inverted" : "normal");
    }
}

void CommandHandler::execute_command_task(void* arg) {
    CommandTaskParams* params = static_cast<CommandTaskParams*>(arg);
    if (params == nullptr) {
        vTaskDelete(NULL);
        return;
    }
    // Check if command is empty
    char* cmd_str = strtok(params->command, " ");
    if (cmd_str == NULL) {
        if (response_buf) {
            free(response_buf);
            response_buf = nullptr;
        }
        vTaskDelete(NULL);
        return;
    }

    uint16_t list_index = 0;
    for (const auto& cmd : cmdList) {
        ESP_LOGD(TAG, "checking command %s against %s", cmd_str, cmd.funcTag);
        if (strcmp(cmd_str, cmd.funcTag) == 0) {
            // get next argument
            cmd_str = strtok(NULL, " ");
            (params->handler->*cmd.funcAddr)(cmd_str);
            if (response_buf && strlen(response_buf) > 0) {
                ESP_LOGD(TAG, "Command response: %s", response_buf);
                xSemaphoreTake(cmd_mutex_stream, portMAX_DELAY);
                stream_manager.publish(response_buf);  // local only
                xSemaphoreGive(cmd_mutex_stream);
            }
            if (response_buf) {
                free(response_buf);
                response_buf = nullptr;
            }
            vTaskDelete(NULL);
        }
        ESP_LOGD(TAG, "next %d", list_index);
        list_index++;                                  // Next function
    }
    ESP_LOGD(TAG, "Command not found: %s", cmd_str);
    if (response_buf) {
        free(response_buf);
        response_buf = nullptr;
    }
    vTaskDelete(NULL);
}
bool capital = false;
void CommandHandler::input(char c) {
    ESP_LOGD(TAG, "Received command input: '%c'", c);
    if (c == '\n' || c == '\r') {
        if (!command_in_progress || strlen(buf) == 0) {
            return;
        }
        ESP_LOGD(TAG, "Command executed: '%s'", buf);
        char* cmd_copy = (char*)malloc(strlen(buf) + 1);
        if (cmd_copy != nullptr) {
            strcpy(cmd_copy, buf);
            CommandTaskParams* params = (CommandTaskParams*)malloc(sizeof(CommandTaskParams));
            if (params != nullptr) {
                params->handler = this;
                params->command = cmd_copy;
                ESP_LOGD(TAG, "Starting command execution task for command: '%s'", cmd_copy);
                xTaskCreate(CommandHandler::execute_command_task, "execute_command", 4096, params, 5, NULL);
            } else {
                free(cmd_copy);
                ESP_LOGE(TAG, "Failed to allocate command task params");
            }
        } else {
            ESP_LOGE(TAG, "Failed to allocate command buffer copy");
        }
        ESP_LOGD(TAG, "Command execution task started, clearing buffer");

        buf[0] = '\0';  // Clear the buffer
        capital = false;
        command_in_progress = false;
    } else if (c == '+') {
        // next character is a captital
        if (capital && command_in_progress) {
            // if ++, then + is escaped, and is a character)
            capital = false;
            const char c2[2] = {c, '\0'};
            strcat(buf, c2);
        }
        capital = true;
    } else if (c == '/') {
        command_in_progress = true;
    } else if (strlen(buf) < INPUT_BUF_SIZE - 1) {
        if (!command_in_progress) {
            return;
        }
        if (capital) {
            c = (char)toupper(c);
            capital = false;
        }
        ESP_LOGD(TAG, "Appending '%c' to command buffer %s, strlen %d", c, buf, strlen(buf));
        const char c2[2] = {c, '\0'};  // Create a string with the character and null terminator
        strcat(buf, c2);               // Append character to buffer
    } else {
        buf[0] = '\0';                 // Clear the buffer to prevent overflow
        ESP_LOGW(TAG, "Command buffer overflow, input ignored");
    }
}
