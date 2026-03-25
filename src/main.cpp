// system includes
#include <Arduino.h>
#include <ESP8266WiFi.h>
// esp-idf includes

// local includes
#include "serial_handler.h"
#include "stream_manager.h"
#include "teletype.h"

#define RX_PIN 2
#define TX_PIN 5
namespace {
constexpr const char TAG[] = "MAIN";
}  // namespace

WiFiServer server(23);  // Telnet port
WiFiClient client;
Teletype* tty;
SerialHandler* serial_handler;
StreamManager streamManager;

void setup() {
    Serial.begin(115200);

    // Set up WiFi AP
    WiFi.mode(WIFI_AP);
    WiFi.softAP("TeletypeTerminal", "password");
    IPAddress IP = WiFi.softAPIP();
    Serial.print("AP IP address: ");
    Serial.println(IP);

    server.begin();
    Serial.println("Telnet server started");

    Serial.printf("ESP8266 Teletype Multi-Source\n");

    tty = new Teletype(50, RX_PIN, TX_PIN, 68);
    serial_handler = new SerialHandler(tty);

    // Subscribe outputs to the stream
    streamManager.subscribe([](char c) { Serial.write(c); });
    streamManager.subscribe([](char c) { serial_handler->sendToTTY(c); });
    streamManager.subscribe([](char c) {
        if (client && client.connected()) {
            client.write(c);
        }
    });
}

void loop() {
    // Check for new WiFi client
    if (!client || !client.connected()) {
        client = server.available();
        if (client) {
            Serial.println("New client connected");
        }
    }

    // Handle Serial input
    if (Serial.available()) {
        char c = Serial.read();
        streamManager.publish(c);
    }

    // Handle WiFi input
    if (client && client.available()) {
        char c = client.read();
        streamManager.publish(c);
    }

    // Handle Teletype input
    char tty_char = tty->rx_bits_from_tty();
    if (tty_char != '\0') {
        streamManager.publish(tty_char);
    }

    delay(1);
}