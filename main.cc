#include "listener.h"
#include "net.h"
#include <iostream>
#include <cstring>

using namespace std;

int main() {
    Net::Address addr(Net::Ipv4::any(), 9080);

    Net::Tcp::Listener listener(addr);
    if (listener.bind()) {
        cout << "Now listening on " << addr.host() << " " << addr.port() << endl;
        listener.run();
    } 
    else {
        cout << "Failed to listen lol" << endl;
        cout << "errno = " << strerror(errno) << endl;
    }

}
