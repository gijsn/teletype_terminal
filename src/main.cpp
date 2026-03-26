// system includes
#include <esp_event.h>
#include <esp_log.h>
#include <esp_netif.h>
#include <esp_system.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <lwip/netdb.h>
#include <lwip/sockets.h>

#include <cstring>

// local includes
#include "serial_handler.h"
#include "stream_manager.h"
#include "teletype.h"

#define RX_PIN 2
#define TX_PIN 5
#define TELNET_PORT 23
#define MAX_CLIENTS 5

namespace {
constexpr const char TAG[] = "MAIN";
}  // namespace

Teletype* tty = nullptr;
SerialHandler* serial_handler = nullptr;
StreamManager streamManager;
int telnet_socket = -1;
int client_sockets[MAX_CLIENTS] = {-1, -1, -1, -1, -1};

void wifiInit() {
    esp_netif_init();
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();

    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    wifi_config_t wifi_config = {};
    strcpy((char*)wifi_config.ap.ssid, "TeletypeTerminal");
    wifi_config.ap.ssid_len = strlen("TeletypeTerminal");
    strcpy((char*)wifi_config.ap.password, "password");
    wifi_config.ap.max_connection = 4;
    wifi_config.ap.authmode = WIFI_AUTH_WPA_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi STA started");
}

void telnetTask(void* pvParameters) {
    telnet_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (telnet_socket < 0) {
        ESP_LOGE(TAG, "Unable to create socket");
        vTaskDelete(nullptr);
        return;
    }

    struct sockaddr_in sock_addr = {};
    sock_addr.sin_family = AF_INET;
    sock_addr.sin_port = htons(TELNET_PORT);
    sock_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(telnet_socket, (struct sockaddr*)&sock_addr, sizeof(sock_addr)) < 0) {
        ESP_LOGE(TAG, "Socket bind failed");
        close(telnet_socket);
        vTaskDelete(nullptr);
        return;
    }

    listen(telnet_socket, 1);
    ESP_LOGI(TAG, "Telnet server listening on port %d", TELNET_PORT);

    int client_count = 0;
    while (1) {
        struct sockaddr_in client_addr = {};
        socklen_t client_addr_len = sizeof(client_addr);

        int client_socket = accept(telnet_socket, (struct sockaddr*)&client_addr, &client_addr_len);
        if (client_socket < 0) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        if (client_count < MAX_CLIENTS) {
            client_sockets[client_count] = client_socket;
            client_count++;
            ESP_LOGI(TAG, "New telnet client connected");
        } else {
            close(client_socket);
        }

        // Clean up disconnected clients
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (client_sockets[i] >= 0) {
                char dummy;
                int result = recv(client_sockets[i], &dummy, 1, MSG_PEEK | MSG_DONTWAIT);
                if (result == 0) {
                    close(client_sockets[i]);
                    client_sockets[i] = -1;
                    client_count--;
                }
            }
        }
    }
}

void streamTask(void* pvParameters) {
    uint8_t uart_buf[256];
    while (1) {
        // Handle UART (Serial) input
        if (serial_handler != nullptr) {
            int len = serial_handler->uart_read(uart_buf);
            for (int i = 0; i < len; i++) {
                streamManager.publish((char)uart_buf[i]);
            }
        }

        // Handle Teletype input
        if (tty != nullptr) {
            char tty_char = tty->read_rx_bits_tty();
            if (tty_char != '\0') {
                streamManager.publish(tty_char);
            }
        }

        // Broadcast to all connected clients
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (client_sockets[i] >= 0) {
                char buf[1];
                int ret = recv(client_sockets[i], buf, 1, MSG_DONTWAIT);
                if (ret > 0) {
                    streamManager.publish(buf[0]);
                } else if (ret == 0) {
                    close(client_sockets[i]);
                    client_sockets[i] = -1;
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

void app_main() {
    ESP_LOGI(TAG, "Starting ESP8266 Teletype Multi-Source");

    // Initialize teletype
    tty = new Teletype(50, RX_PIN, TX_PIN, 68);
    serial_handler = new SerialHandler();

    // Subscribe outputs to the stream
    streamManager.subscribe([](char c) {
        // Write to UART
        if (serial_handler != nullptr) {
            serial_handler->uart_tx(c);
        }
    });

    streamManager.subscribe([](char c) {
        // Write to all telnet clients
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (client_sockets[i] >= 0) {
                send(client_sockets[i], &c, 1, 0);
            }
        }
    });

    streamManager.subscribe([](char c) {
        if (tty != nullptr) {
            tty->print_ascii_character_to_tty(c);
        }
    });

    // Initialize WiFi
    wifiInit();

    // Create tasks
    xTaskCreate(telnetTask, "Telnet", 4096, nullptr, 5, nullptr);
    xTaskCreate(streamTask, "Stream", 4096, nullptr, 5, nullptr);

    // Task completed, suspend
    vTaskSuspend(nullptr);
}

extern "C" int main() {
    app_main();
    return 0;
}