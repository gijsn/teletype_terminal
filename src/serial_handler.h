#ifndef __SERIAL_HANDLER_H__
#define __SERIAL_HANDLER_H__

// system includes
#include <cstddef>
#include <cstdint>

class SerialHandler {
   public:
    SerialHandler(void);

    static void uart_rx_task(void* pvParameters);
    static void uart_tx(char buf);
    static void sendToTTY(char buf);
    static int uart_read(uint8_t* data);

    static void local_loop_enable();
    static void local_loop_disable();
    static bool get_local_loopback_enabled();

   private:
    static bool flush_buffer;
};

#endif