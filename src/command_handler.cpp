#include "command_handler.h"

#include <esp_log.h>
#include <esp_wifi.h>
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
        {0x04, "baud", &CommandHandler::cmd_baudrate},

};

namespace {
constexpr const char TAG[] = "CMD";
}  // namespace

CommandHandler::CommandHandler() {
    ESP_LOGI(TAG, "CommandHandler initialized");
    buf = (char*)malloc(INPUT_BUF_SIZE * sizeof(char));
    memset(buf, '\0', INPUT_BUF_SIZE);
}

void CommandHandler::cmd_help(char* arg) {
    asprintf(&response_buf, "Available commands:\r\n");
    for (const auto& cmd : cmdList) {
        ESP_LOGI(TAG, "command %s\r\n", cmd.funcTag);
        asprintf(&response_buf, "%s->%s\r\n", response_buf, cmd.funcTag);
    }
}

void CommandHandler::cmd_wifi(char* arg) {
    // get next argument
    arg = strtok(NULL, " ");
    wifi_config_t cfg = {};

    if (arg != NULL) {
        ESP_LOGI(TAG, "SSID: \"%s\"\n", arg);
        strcpy((char*)cfg.sta.ssid, arg);
    } else {
        ESP_LOGI(TAG, "Usage: \"wifi {ssid} [password]\"");
        esp_wifi_get_config(ESP_IF_WIFI_STA, &cfg);
        asprintf(&response_buf, "Usage: \"wifi {ssid} [password]\"\r\nCurrent settings, SSID: %s, Password: %s\r\n", cfg.sta.ssid, cfg.sta.password);
        return;
    }

    arg = strtok(NULL, " ");
    if (arg != NULL) {  // otherwise password is blank
        ESP_LOGI(TAG, "Password: \"%s\"\n", arg);
        strcpy((char*)cfg.sta.password, arg);
    } else {
        ESP_LOGI(TAG, "No password\n");
        strcpy((char*)cfg.sta.password, "");
    }

    if (strlen((char*)cfg.sta.password)) {
        cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    }
    // TODO: save to nvs if it works!
    esp_wifi_disconnect();
    esp_wifi_stop();
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &cfg));
    esp_wifi_start();
    asprintf(&response_buf, "Connecting to WiFi SSID: %s\r\n", cfg.sta.ssid);
}

void CommandHandler::cmd_baudrate(char* cmd) {
    char* arg = strtok(NULL, " ");
    if (arg != NULL) {
        int baudrate = atoi(arg);
        ESP_LOGI(TAG, "Setting baudrate to %d", baudrate);
        tty->set_baudrate(baudrate);
        asprintf(&response_buf, "Baudrate set to %d\r\n", baudrate);
    }
}

uint8_t CommandHandler::execute_command(char* arg) {
    char* cmd_str;
    cmd_str = strtok(arg, " ");
    if (cmd_str == NULL) {
        return 0;
    }
    uint16_t list_index = 0;
    for (const auto& cmd : cmdList) {
        ESP_LOGD(TAG, "checking command %s against %s", cmd_str, cmd.funcTag);
        if (strcmp(cmd_str, cmd.funcTag) == 0) {
            (this->*cmd.funcAddr)(arg);
            return 1;  // ready
        }
        ESP_LOGD(TAG, "next %d", list_index);
        list_index++;  // Next function
    }
    ESP_LOGD(TAG, "Command not found: %s", cmd_str);
    return 0;
}
bool capital = false;
bool command = false;
void CommandHandler::input(char c) {
    ESP_LOGD(TAG, "Received command input: '%c'", c);
    if (c == '\n' || c == '\r') {
        ESP_LOGD(TAG, "Command executed: '%s'", buf);
        // TODO: execute command in buf
        if (execute_command(buf) && strlen(response_buf) > 0) {
            xSemaphoreTake(cmd_mutex_stream, portMAX_DELAY);
            stream_manager.publish(response_buf);
            xSemaphoreGive(cmd_mutex_stream);
        }
        ESP_LOGD(TAG, "Command execution finished, clearing buffer");
        free(response_buf);
        buf[0] = '\0';  // Clear the buffer
        capital = false;
        command = false;
    } else if (c == '+') {
        // next character is a captital
        if (capital) {
            // if ++, then + is escaped, and is a character)
            capital = false;
            const char c2[2] = {c, '\0'};
            strcat(buf, c2);
        }
        capital = true;
    } else if (c == '/') {
        command = true;
    } else if (strlen(buf) < INPUT_BUF_SIZE - 1) {
        if (!command) {
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
