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
        void (CommandHandler::*funcAddr)(char* arg);
    };
    static const commandItem_t cmdList[];
    void cmd_help(char* arg);
    void cmd_wifi(char* arg);
    void execute_command(char* buf);

   public:
};
#endif  // __COMMAND_HANDLER_H__
