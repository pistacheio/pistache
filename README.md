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

# To Build:

To download the latest available release, clone the repository over github.

git clone https://github.com/oktal/pistache.git

Then, init the submodules:

git submodule update --init

Now, compile the sources:

    cd pistache
    mkdir build
    cd build
    cmake -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release ..
    make
    sudo make install

If you want the examples built, then change change the cmake above to:

    cmake -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release -DCPISTACHE_BUILD_EXAMPLES=true    ..

After running the above, you can then cd into the build/examples directory and run make.

Optionally, you can also run the tests:

    make test

Be patient, async_test can take some time before completing.

And that’s it, now you can start playing with your newly installed Pistache framework.

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
