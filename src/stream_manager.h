#ifndef __STREAM_MANAGER_H__
#define __STREAM_MANAGER_H__

#include <functional>
#include <string>
#include <vector>

enum StreamSource {
    SOURCE_TELETYPE = 0,
    SOURCE_COMMAND = 1,
    SOURCE_SERIAL = 2,
    SOURCE_EXTERNAL = 4,
    SOURCE_UNKNOWN = 8,
};

class StreamManager {
   public:
    using Subscriber = std::function<void(char, int)>;
    void subscribe(Subscriber sub) {
        subscribers.push_back(sub);
    }

    void publish(char c, int source) {
        for (auto& sub : subscribers) {
            sub((char)tolower(c), source);
        }
    }

    void publish(char* c, int source) {
        for (int i = 0; i < strlen(c); i++) {
            publish(c[i], source);
        }
    }

   private:
    std::vector<Subscriber> subscribers;
};

#endif