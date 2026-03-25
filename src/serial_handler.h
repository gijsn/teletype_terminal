#ifndef __SERIAL_HANDLER_H__
#define __SERIAL_HANDLER_H__

// system includes
#include <memory>
#include <mutex>
#include <thread>
// local includes

class SerialHandler {
   public:
    SerialHandler(void);

    [[noreturn]] static void uart_task_rx();
    static void uart_task_tx(char buf);
    static void sendToTTY(char buf);

    static void local_loop_enable();
    static void local_loop_disable();
    static bool get_local_loopback_enabled();

   private:
    static bool flush_buffer;
};

#endif