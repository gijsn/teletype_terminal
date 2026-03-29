#ifndef __COMMAND_HANDLER_H__
#define __COMMAND_HANDLER_H__
// system includes
#include <gpio.h>

class CommandHandler {
   public:
    CommandHandler();
    void input(char c);
    char read_response();

   private:
    struct commandItem_t {
        uint8_t CmdID;
        char* funcTag;
        void (CommandHandler::*funcAddr)();
    };
    static const commandItem_t cmdList[];
    void cmd_help();
    void execute_command(char* buf);

   public:
};
#endif  // __COMMAND_HANDLER_H__
