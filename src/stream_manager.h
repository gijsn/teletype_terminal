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
            sub(c);
        }
    }

   private:
    std::vector<Subscriber> subscribers;
};

#endif