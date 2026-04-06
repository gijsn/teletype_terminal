// system includes
#include <esp_event.h>
#include <esp_log.h>
#include <esp_netif.h>
#include <esp_system.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/task.h>
#include <lwip/netdb.h>
#include <lwip/sockets.h>
#include <mdns.h>
#include <nvs.h>
#include <nvs_flash.h>

#include <cstring>

// local includes
#include "command_handler.h"
#include "serial_handler.h"
#include "stream_manager.h"
#include "teletype.h"

#define RX_PIN 5
#define TX_PIN 2
#define WIFI_LED
#define TELNET_PORT 23
#define MAX_CLIENTS 5

#define WIFI_CONNECTING_BIT BIT0
#define WIFI_CONNECTED_BIT BIT1
#define WIFI_FAIL_BIT BIT2

/*


ESP WiFi - telnet controller for teletypes Siemens t100, 45.45 baud

The stream manager takes care of inputs and outputs to other modules
they can subscribe and publish to the stream manager, which then pushes data using the task to specific functions

                            +--------+
                            |  WiFi  |
                            +--------+
                                 ^
                                 |
                                 v
        +----------+         +-------+         +-------+
        |  serial  | <-----> |  ESP  | <-----> |  TTY  |
        +----------+         +-------+         +-------+
                                 ^
                                 |
                                 v
                             +-------+
                             |  CMD  |
                             +-------+


*/

namespace {
constexpr const char TAG[] = "MAIN";
}  // namespace

Teletype* tty = nullptr;
SerialHandler* serial_handler = nullptr;
StreamManager stream_manager;
CommandHandler* command_handler = nullptr;

SemaphoreHandle_t cmd_mutex_stream;
int telnet_socket = -1;
int client_sockets[MAX_CLIENTS] = {-1, -1, -1, -1, -1};
static EventGroupHandle_t s_wifi_event_group;
static int s_retry_num = 0;

static void event_handler(void* arg, esp_event_base_t event_base,
                          int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
        xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        xEventGroupClearBits(s_wifi_event_group, WIFI_FAIL_BIT);
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTING_BIT);
        stream_manager.publish("Connecting to WiFi...\r\n");
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < 10) {
            esp_wifi_connect();
            s_retry_num++;
            wifi_config_t conf;
            esp_wifi_get_config(ESP_IF_WIFI_STA, &conf);
            ESP_LOGD(TAG, "retry to connect to the AP: %s\r\n", conf.sta.ssid);
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
            ESP_LOGI(TAG, "connect to the AP fail");
            xSemaphoreTake(cmd_mutex_stream, portMAX_DELAY);
            stream_manager.publish("Disconnected from WiFi\r\n");
            xSemaphoreGive(cmd_mutex_stream);
            mdns_free();
        }

    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*)event_data;
        ESP_LOGI(TAG, "got ip:%s",
                 ip4addr_ntoa(&event->ip_info.ip));
        // TODO sprintf line
        wifi_config_t conf;
        esp_wifi_get_config(ESP_IF_WIFI_STA, &conf);
        char* resp;
        asprintf(&resp, "Connected to WiFi network with SSID: %s, and IP %s\r\n", conf.sta.ssid, ip4addr_ntoa(&event->ip_info.ip));
        xSemaphoreTake(cmd_mutex_stream, portMAX_DELAY);
        stream_manager.publish(resp);
        free(resp);
        xSemaphoreGive(cmd_mutex_stream);
        s_retry_num = 0;
        xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTING_BIT);
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);

        // Initialize mDNS
        ESP_ERROR_CHECK(mdns_init());
        ESP_ERROR_CHECK(mdns_hostname_set("teletype"));
        ESP_ERROR_CHECK(mdns_service_add(NULL, "_telnet", "_tcp", TELNET_PORT, NULL, 0));
        ESP_LOGI(TAG, "mDNS initialized: teletype.local");
    }
}

void wifiInit() {
    s_wifi_event_group = xEventGroupCreate();
    esp_netif_init();
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();

    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL));
    wifi_config_t conf;
    esp_wifi_get_config(ESP_IF_WIFI_STA, &conf);
    ESP_LOGI(TAG, "Connecting to the AP: %s, with Password %s\r\n", conf.sta.ssid, conf.sta.password);
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
    // ESP_ERROR_CHECK(esp_wifi_set_auto_connect(true));

    ESP_LOGI(TAG, "wifi_init_sta finished.");
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
        vTaskDelay(pdMS_TO_TICKS(10));
        if (xEventGroupGetBits(s_wifi_event_group) | WIFI_CONNECTING_BIT) {
            ESP_LOGI(TAG, ".");
            return;
        }
        if (xEventGroupGetBits(s_wifi_event_group) | WIFI_CONNECTED_BIT) {
            wifi_config_t conf;
            esp_wifi_get_config(WIFI_IF_STA, &conf);

            ESP_LOGI(TAG, "WiFi connected to %s", conf.sta.ssid);
            xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        } else {
            return;
        }
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
    while (1) {
        // Handle UART (Serial) input
        // if (serial_handler != nullptr) {
        //     int len = serial_handler->uart_read(uart_buf);
        //     if (len > 0) {
        //         ESP_LOGI("STREAM", "Read %d bytes from UART", len);
        //         for (int i = 0; i < len; i++) {
        //             streamManager.publish((char)uart_buf[i]);
        //         }
        //     }
        // }

        // Handle Teletype input
        // if (tty != nullptr) {
        //     char tty_char = tty->read_rx_bits_tty();
        //     // ESP_LOGI("STREAM", "Read char '%c' from Teletype", tty_char);
        //     if (tty_char != '\0') {
        //         streamManager.publish(tty_char);
        //     }
        // }
        // if (command_handler != nullptr) {
        //     char response = command_handler->read_response();
        //     if (response != '\0') {
        //         streamManager.publish(response);
        //     }
        // }

        // Broadcast to all connected clients
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (client_sockets[i] >= 0) {
                // ESP_LOGI("STREAM", "Checking client socket %d for incoming data", i);
                char buf[1];
                int ret = recv(client_sockets[i], buf, 1, MSG_DONTWAIT);
                if (ret > 0) {
                    xSemaphoreTake(cmd_mutex_stream, portMAX_DELAY);
                    stream_manager.publish(buf[0]);
                    xSemaphoreGive(cmd_mutex_stream);
                } else if (ret == 0) {
                    close(client_sockets[i]);
                    client_sockets[i] = -1;
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void write_header() {
    char header[] = "   __  ____            ______    __       \r\n  /  |/  (_)__________/_  __/__ / /____ __\r\n / /|_/ / / __/ __/ _ \\/ / / -_) / -_) \\ /\r\n/_/  /_/_/\\__/_/  \\___/_/  \\__/_/\\__/_\\_\r\n   __  ____            ______    __       \r\n  /  |/  (_)__________/_  __/__ / /____ __\r\n / /|_/ / / __/ __/ _ \\/ / / -_) / -_) \\ /\r\n/_/  /_/_/\\__/_/  \\___/_/  \\__/_/\\__/_\\_\r\n";
    // ESP_LOGI(TAG, " header);
    xSemaphoreTake(cmd_mutex_stream, portMAX_DELAY);
    for (int i = 0; i < strlen(header); i++) {
        stream_manager.publish(header[i]);
    }
    xSemaphoreGive(cmd_mutex_stream);
}

extern "C" void app_main() {
    ESP_LOGI(TAG, "Starting ESP8266 Teletype Multi-Source");
    ESP_ERROR_CHECK(nvs_flash_init());
    cmd_mutex_stream = xSemaphoreCreateMutex();

    // Initialize teletype
    tty = new Teletype(50, RX_PIN, TX_PIN, 68);
    serial_handler = new SerialHandler();
    command_handler = new CommandHandler();

    // Subscribe outputs to the stream
    stream_manager.subscribe([](char c) {
        // Write to UART
        if (serial_handler != nullptr) {
            serial_handler->uart_tx(c);
        }
    });

    stream_manager.subscribe([](char c) {
        // Write to all telnet clients
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (client_sockets[i] >= 0) {
                send(client_sockets[i], &c, 1, 0);
            }
        }
    });

    stream_manager.subscribe([](char c) {
        if (tty != nullptr) {
            tty->print_ascii_character_to_tty(c);
        }
    });
    stream_manager.subscribe([](char c) {
        command_handler->input(c);
    });
    wifiInit();
    //  Create tasks
    //  xTaskCreate(telnetTask, "Telnet", 4096, nullptr, 5, nullptr);
    //  xTaskCreate(streamTask, "Stream", 4096, nullptr, 5, nullptr);
    xTaskCreate(SerialHandler::uart_rx_task, "UART_RX", 4096, nullptr, 5, nullptr);

    // write telex header
    vTaskDelay(pdMS_TO_TICKS(5000));
    // write_header();

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    // Task completed, suspend
    vTaskSuspend(nullptr);
}