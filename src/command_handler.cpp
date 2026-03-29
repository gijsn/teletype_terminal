#include "command_handler.h"

#include <esp_log.h>
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

void CommandHandler::cmd_help() {
    strcpy(response_buf, "Available commands:\r\n");
    response_buf_index = response_buf + strlen(response_buf);
    for (const auto& cmd : cmdList) {
        sprintf(response_buf, "%s %s\r\n", response_buf, cmd.funcTag);
    }
}

void CommandHandler::execute_command(char* cmd) {
    char* cmd_str;
    cmd_str = strtok(cmd, " ");
    uint16_t list_index = 0;
    while (cmdList[list_index].funcAddr != 0) {
        if (strcmp(cmd_str, cmdList[list_index].funcTag) == 0) {
            printf(">%02X,", cmdList[list_index].CmdID);
            (this->*cmdList[list_index].funcAddr)();
            break;     // ready
        }
        list_index++;  // Next function
    }
}

char CommandHandler::read_response() {
    // Placeholder implementation - replace with actual response reading logic

    return '\0';
}
void CommandHandler::input(char c) {
    ESP_LOGI(TAG, "Received command input: '%c'", c);
    if (c == '\n' || c == '\r') {
        ESP_LOGI(TAG, "Command executed: '%s'", buf);
        // TODO: execute command in buf
        execute_command(buf);
        buf[0] = '\0';                 // Clear the buffer
    } else if (strlen(buf) < INPUT_BUF_SIZE - 1) {
        ESP_LOGI(TAG, "Appending '%c' to command buffer %s, strlen %d", c, buf, strlen(buf));
        const char c2[2] = {c, '\0'};  // Create a string with the character and null terminator
        strcat(buf, c2);               // Append character to buffer
    } else {
        buf[0] = '\0';                 // Clear the buffer to prevent overflow
        ESP_LOGW(TAG, "Command buffer overflow, input ignored");
    }
}
