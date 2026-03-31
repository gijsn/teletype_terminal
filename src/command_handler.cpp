#include "command_handler.h"

#include <esp_log.h>
#include <esp_wifi.h>
#include <stdlib.h>

#include <cstring>

#include "stream_manager.h"

#define INPUT_BUF_SIZE 128
#define RESPONSE_BUF_SIZE 1024

extern StreamManager stream_manager;
char* buf;
char* response_buf;

const CommandHandler::commandItem_t CommandHandler::cmdList[] =
    {
        {0x02, "help", &CommandHandler::cmd_help},  // Help function
        {0x03, "wifi", &CommandHandler::cmd_wifi},
        {0x04, "eol", nullptr},

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

uint8_t CommandHandler::execute_command(char* cmd) {
    char* cmd_str;
    cmd_str = strtok(cmd, " ");
    if (cmd_str == NULL) {
        return 0;
    }
    uint16_t list_index = 0;
    while (cmdList[list_index].funcAddr != 0) {
        ESP_LOGD(TAG, "checking command %s against %s", cmd_str, cmdList[list_index].funcTag);
        if (strcmp(cmd_str, cmdList[list_index].funcTag) == 0) {
            ESP_LOGI(TAG, ">%02X,", cmdList[list_index].CmdID);
            (this->*cmdList[list_index].funcAddr)(cmd);
            return 1;  // ready
        }
        ESP_LOGD(TAG, "next %d", list_index);
        list_index++;  // Next function
    }
    return 0;
}
bool capital = false;
void CommandHandler::input(char c) {
    ESP_LOGD(TAG, "Received command input: '%c'", c);
    if (c == '\n' || c == '\r') {
        esp_log_buffer_hexdump_internal(TAG, "Command executed: '%s'", buf);
        // TODO: execute command in buf
        response_buf = (char*)'\0';
        if (execute_command(buf) && strlen(response_buf) > 0) {
            stream_manager.publish(response_buf);
        }
        buf[0] = '\0';  // Clear the buffer
        capital = false;
    } else if (c == '+') {
        // next character is a captital
        if (capital) {
            // if ++, then + is escaped, and is a character)
            capital = false;
            const char c2[2] = {c, '\0'};
            strcat(buf, c2);
        }
        capital = true;
    } else if (strlen(buf) < INPUT_BUF_SIZE - 1) {
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
