
// system includes
#include <driver/uart.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include <cstring>

// local includes
#include "serial_handler.h"

namespace {
constexpr const char TAG[] = "SERIAL";
constexpr int UART_NUM = 0;  // UART0
constexpr int UART_BAUD_RATE = 115200;
constexpr int BUF_SIZE = 256;
uint8_t data[BUF_SIZE];
uint16_t data_len = 0;
}  // namespace

// static members
bool SerialHandler::flush_buffer{};
static xQueueHandle uart_queue = nullptr;

SerialHandler::SerialHandler() {
    // Configure UART0
    uart_config_t uart_config = {
        .baud_rate = UART_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 122,
    };

    // Set UART configuration
    uart_param_config(UART_NUM_0, &uart_config);

    // Install UART driver
    uart_driver_install(UART_NUM_0, BUF_SIZE * 2, BUF_SIZE * 2, 20, &uart_queue, 0);

    ESP_LOGI(TAG, "SerialHandler initialized at %d baud", UART_BAUD_RATE);
}

void SerialHandler::uart_rx_task() {
    // This task can be used to monitor UART events if needed
    // Currently handled in main stream task via uart_read_bytes
    while (1) {
        // Check for UART events
        if (data_len >= BUF_SIZE) {
            return;
        }
        uart_event_t event;
        if (xQueueReceive(uart_queue, (void*)&event, pdMS_TO_TICKS(10))) {
            switch (event.type) {
                case UART_DATA: {
                    int len = uart_read_bytes(UART_NUM_0, data, event.size > (BUF_SIZE - data_len) ? (BUF_SIZE - data_len) : event.size, pdMS_TO_TICKS(10));
                    data_len += len;

                    // Process data if needed
                    ESP_LOGD(TAG, "UART data received: %d bytes", len);
                    break;
                }
                case UART_FIFO_OVF:
                    ESP_LOGW(TAG, "UART FIFO overflow");
                    uart_flush_input(UART_NUM_0);
                    break;
                case UART_BUFFER_FULL:
                    ESP_LOGW(TAG, "UART buffer full");
                    uart_flush_input(UART_NUM_0);
                    break;
                default:
                    break;
            }
        }

        if (flush_buffer) {
            uart_flush_input(UART_NUM_0);
            flush_buffer = false;
            ESP_LOGI(TAG, "UART buffer flushed");
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

int SerialHandler::uart_read(uint8_t* tmp_data) {
    if (data == nullptr || data_len == 0) {
        return 0;
    }

    strcpy((char*)tmp_data, (char*)data);
    uint16_t tmp_data_len = data_len;
    data_len = 0;  // Clear buffer after reading
    return tmp_data_len;
}
void SerialHandler::uart_tx(char buf) {
    if (buf == '\0') {
        return;
    }
    uart_write_bytes(UART_NUM_0, (const char*)&buf, 1);
}

bool SerialHandler::get_local_loopback_enabled() {
    return false;
}