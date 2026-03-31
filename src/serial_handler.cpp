
// system includes
// clang-format off
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <driver/uart.h>
#include <esp_log.h>

//clang-format on

#include <cstring>

// local includes
#include "serial_handler.h"
#include "stream_manager.h"
namespace {
constexpr const char TAG[] = "SERIAL";
constexpr int UART_NUM = 0;  // UART0
constexpr int UART_BAUD_RATE = 115200;
constexpr int BUF_SIZE = 256;
}  // namespace

extern StreamManager stream_manager;


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
    uart_driver_install(UART_NUM_0, BUF_SIZE * 2, BUF_SIZE * 2, 64, &uart_queue, 0);

    ESP_LOGI(TAG, "SerialHandler initialized at %d baud", UART_BAUD_RATE);
}

void SerialHandler::uart_rx_task(void* pvParameters)  {
    // This task can be used to monitor UART events if needed
    // Currently handled in main stream task via uart_read_bytes
    while (1) {
        // Check for UART events
        uart_event_t event;
        if (xQueueReceive(uart_queue, &event, pdMS_TO_TICKS(10))) {
            switch (event.type) {
                case UART_DATA: {
                    // TODO: read only one byte at a time, then clean for non-baudot characters
                    uint8_t buf[event.size+1];
                    int read = uart_read_bytes(UART_NUM_0, buf, event.size, 0);
                    buf[event.size] = '\0';
                    ESP_LOGI(TAG, "UART data received: %d bytes of %d", read,event.size);               
                    stream_manager.publish((char*)buf);  
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
void SerialHandler::uart_tx(char buf) {
    if (buf == '\0') {
        return;
    }
    uart_write_bytes(UART_NUM_0, (const char*)&buf, 1);
}

bool SerialHandler::get_local_loopback_enabled() {
    return false;
}