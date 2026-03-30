#include "command_handler.h"

#include <esp_log.h>
#include <esp_wifi.h>
#include <stdlib.h>

#include <cstring>

#define INPUT_BUF_SIZE 128
#define RESPONSE_BUF_SIZE 1024
char* buf;
char* response_buf;
char* response_buf_index;

const CommandHandler::commandItem_t CommandHandler::cmdList[] =
    {
        {0x02, "help?", &CommandHandler::cmd_help},  // Help function
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

    response_buf = (char*)malloc(RESPONSE_BUF_SIZE * sizeof(char));
    memset(response_buf, '\0', RESPONSE_BUF_SIZE);
}

void CommandHandler::cmd_help(char* arg) {
    strcpy(response_buf, "Available commands:\r\n");
    response_buf_index = response_buf + strlen(response_buf);
    for (const auto& cmd : cmdList) {
        sprintf(response_buf, "%s %s\r\n", response_buf, cmd.funcTag);
    }
    printf("remainder %s\r\n", arg);
}

void CommandHandler::cmd_wifi(char* arg) {
    // get next argument
    arg = strtok(NULL, " ");
    wifi_config_t cfg;

    if (arg != NULL) {
        ESP_LOGI(TAG, "SSID: %s\n", arg);
        strcpy((char*)cfg.sta.ssid, arg);
    } else {
        ESP_LOGI(TAG, "Useage: \"wifi {ssid} [password]\"");
        return;
    }

    arg = strtok(NULL, " ");
    if (arg != NULL) {  // otherwise password is blank
        ESP_LOGI(TAG, "Password: %s\n", arg);
        strcpy((char*)cfg.sta.password, arg);
    } else {
        ESP_LOGI(TAG, "No password\n");
        strcpy((char*)cfg.sta.password, "");
    }

    if (strlen((char*)cfg.sta.password)) {
        cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    }
    // TODO: save to nvs if it works!
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &cfg));
    esp_wifi_connect();
    while (arg != NULL) {
        printf("Arg: %s\n", arg);
        arg = strtok(NULL, " ");
    }
    // TODO get SSID & password, then connect
}

void CommandHandler::execute_command(char* cmd) {
    char* cmd_str;
    cmd_str = strtok(cmd, " ");
    if (cmd_str == NULL) {
        return;
    }
    uint16_t list_index = 0;
    while (cmdList[list_index].funcAddr != 0) {
        ESP_LOGI(TAG, "checking command %s against %s", cmd_str, cmdList[list_index].funcTag);
        if (strcmp(cmd_str, cmdList[list_index].funcTag) == 0) {
            ESP_LOGI(TAG, ">%02X,", cmdList[list_index].CmdID);
            (this->*cmdList[list_index].funcAddr)(cmd);
            return;    // ready
        }
        ESP_LOGD(TAG, "next %d", list_index);
        list_index++;  // Next function
    }
}

char CommandHandler::read_response() {
    // Placeholder implementation - replace with actual response reading logic

    return '\0';
}
void CommandHandler::input(char c) {
    ESP_LOGD(TAG, "Received command input: '%c'", c);
    if (c == '\n' || c == '\r') {
        esp_log_buffer_hexdump_internal(TAG, "Command executed: '%s'", buf);
        // TODO: execute command in buf
        execute_command(buf);
        buf[0] = '\0';                 // Clear the buffer
    } else if (strlen(buf) < INPUT_BUF_SIZE - 1) {
        ESP_LOGD(TAG, "Appending '%c' to command buffer %s, strlen %d", c, buf, strlen(buf));
        const char c2[2] = {c, '\0'};  // Create a string with the character and null terminator
        strcat(buf, c2);               // Append character to buffer
    } else {
        buf[0] = '\0';                 // Clear the buffer to prevent overflow
        ESP_LOGW(TAG, "Command buffer overflow, input ignored");
    }
}
