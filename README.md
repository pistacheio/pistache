# Pistache

[![Travis Build Status](https://travis-ci.org/oktal/pistache.svg?branch=master)](https://travis-ci.org/oktal/pistache)

Pistache is a modern and elegant HTTP and REST framework for C++.

It is entirely written in pure-C++11 and provides a clear and pleasant API

Full documentation is located at [http://pistache.io](http://pistache.io).

# Example

## Hello World (server)

```cpp
#include <pistache/endpoint.h>

using namespace Net;

struct HelloHandler : public Http::Handler {
    void onRequest(const Http::Request& request, Http::ResponseWriter writer) {
        writer.send(Http::Code::Ok, "Hello, World!");
    }
};

int main() {
    Http::listenAndServe<HelloHandler>("*:9080");
}
```
