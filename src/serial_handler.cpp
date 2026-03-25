
// local includes
#include "serial_handler.h"

#include <Arduino.h>
namespace {
constexpr const char TAG[] = "SERIAL";
}  // namespace

SerialHandler::SerialHandler() {
}
// static members
bool SerialHandler::flush_buffer{};

void SerialHandler::uart_task_rx() {
    // Removed Serial reading, now handled in main loop
    // Only handle flush if needed
    if (flush_buffer) {
        Serial.printf("FLUSHING RX BUFFER\r\n");
        Serial.flush();
        flush_buffer = false;
    }
}

void SerialHandler::uart_task_tx(char buf) {
    // Serial.printf("Hello from the UART TX Task\r\n");
    if (buf == '\0') {
        return;
    }  // TODO: decide if we can remove this, we don't want to meddle with uart tx
    Serial.write(buf);
}