# Pistache

[![Travis Build Status](https://travis-ci.org/oktal/pistache.svg?branch=master)](https://travis-ci.org/oktal/pistache)

Pistache is a modern and elegant HTTP and REST framework for C++.

It is entirely written in pure-C++11 and provides a clear and pleasant API

Full documentation is located at [http://pistache.io](http://pistache.io).

# Contributing

Pistache is an open-source project and will always stay open-source. However, working on an open-source project while having a full-time job is sometimes a difficult task to accomplish.

That's why your help is needed. If you would like to contribute to the project in any way (submitting ideas, fixing bugs, writing documentation, ...), please join the 
[cpplang Slack channel](https://cpplang.now.sh/). Drop a private message to `@octal` and I will invite you to the channel dedicated to Pistache.

Hope to see you there !

# Example

## Hello World (server)

```cpp
#include <pistache/endpoint.h>

using namespace Pistache;

struct HelloHandler : public Http::Handler {
    HTTP_PROTOTYPE(HelloHandler)

    void onRequest(const Http::Request& request, Http::ResponseWriter writer) {
        writer.send(Http::Code::Ok, "Hello, World!");
    }
};

int main() {
    Http::listenAndServe<HelloHandler>("*:9080");
}
```
