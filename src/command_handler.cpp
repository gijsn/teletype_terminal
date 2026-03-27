#include "command_handler.h"

#include <esp_log.h>
#include <stdlib.h>

#include <cstring>

#define BUF_SIZE 128
char* buf;

namespace {
constexpr const char TAG[] = "CMD";
}  // namespace

CommandHandler::CommandHandler() {
    ESP_LOGI(TAG, "CommandHandler initialized");
    buf = (char*)malloc(BUF_SIZE * sizeof(char));
    memset(buf, '\0', BUF_SIZE);
}

void CommandHandler::input(char c) {
    ESP_LOGI(TAG, "Received command input: '%c'", c);
    if (c == '\n' || c == '\r') {
        ESP_LOGI(TAG, "Command executed: '%s'", buf);
        // TODO: execute command in buf
        buf[0] = '\0';                 // Clear the buffer
    } else if (strlen(buf) < BUF_SIZE - 1) {
        ESP_LOGI(TAG, "Appending '%c' to command buffer %s, strlen %d", c, buf, strlen(buf));
        const char c2[2] = {c, '\0'};  // Create a string with the character and null terminator
        strcat(buf, c2);               // Append character to buffer
    } else {
        buf[0] = '\0';                 // Clear the buffer to prevent overflow
        ESP_LOGW(TAG, "Command buffer overflow, input ignored");
    }
}
