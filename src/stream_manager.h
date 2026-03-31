#ifndef __STREAM_MANAGER_H__
#define __STREAM_MANAGER_H__

#include <functional>
#include <vector>

class StreamManager {
   public:
    using Subscriber = std::function<void(char)>;

    void subscribe(Subscriber sub) {
        subscribers.push_back(sub);
    }

    void publish(char c) {
        for (auto& sub : subscribers) {
            sub((char)tolower(c));
        }
    }

    void publish(char* c) {
        for (int i = 0; i < strlen(c); i++) {
            publish(c[i]);
        }
    }

   private:
    std::vector<Subscriber> subscribers;
};

#endif