#ifndef __COMMAND_HANDLER_H__
#define __COMMAND_HANDLER_H__
// system includes
#include <gpio.h>

class CommandHandler {
   public:
    CommandHandler();
    void input(char c);
};
#endif  // __COMMAND_HANDLER_H__
