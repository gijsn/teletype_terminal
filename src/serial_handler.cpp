
// local includes
#include "serial_handler.h"

#include <Arduino.h>
namespace {
constexpr const char TAG[] = "SERIAL";
}  // namespace

// static members
bool SerialHandler::flush_buffer{};
Teletype* SerialHandler::tty{};

SerialHandler::SerialHandler(Teletype* tty) {
    if (tty != nullptr) {
        this->tty = tty;
    }
}

void SerialHandler::uart_task_rx() {
    // Removed Serial reading, now handled in main loop
    // Only handle flush if needed
    if (flush_buffer) {
        Serial.printf("FLUSHING RX BUFFER\r\n");
        Serial.flush();
        flush_buffer = false;
    }
}

void SerialHandler::sendToTTY(char buf) {
    tty->print_ascii_character(buf);
    if (buf == '\n') {
        tty->print_ascii_character('\r');
    }
}
}

void SerialHandler::uart_task_tx(char buf) {
    // Serial.printf("Hello from the UART TX Task\r\n");
    if (buf == '\0') {
        return;
    }  // TODO: decide if we can remove this, we don't want to meddle with uart tx
    Serial.write(buf);
}