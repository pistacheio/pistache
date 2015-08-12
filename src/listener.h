/* listener.h
   Mathieu Stefani, 12 August 2015
   
  A TCP Listener
*/

#pragma once

#include "net.h"
#include "mailbox.h"
#include <vector>
#include <memory>
#include <thread>

namespace Net {

namespace Tcp {

class IoWorker {
public:
    Mailbox<int> mailbox;

    IoWorker();
    ~IoWorker();

    void start();
private:
    int epoll_fd;
    std::unique_ptr<std::thread> thread;

    void readIncomingData(int fd);
    void run();
};

    class Listener {
    public:
        Listener(const Address& address);

        bool bind();
        void run();

    private: 

        Address addr_; 
        int listen_fd;
        std::vector<std::unique_ptr<IoWorker>> ioGroup;

        void dispatchConnection(int fd);
    };

} // namespace Tcp

} // namespace Net
