# Pistache

[![Travis Build Status](https://travis-ci.org/oktal/pistache.svg?branch=master)](https://travis-ci.org/oktal/pistache)

Pistache is a modern and elegant HTTP and REST framework for C++.

It is entirely written in pure-C++11 and provides a clear and pleasant API

Full documentation is located at [http://pistache.io](http://pistache.io).

# Contributing

Pistache is an open-source project and will always stay open-source.  Contributors are welcome!

Pistache was created by Mathieu Stefani, who can be reached via [cpplang Slack channel](https://cpplang.now.sh/). Drop a private message to `@octal` and he will invite you to the channel dedicated to Pistache.

For those that prefer IRC over Slack, the rag-tag crew of maintainers are on #pistache on freenode.

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

    cmake -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release -DPISTACHE_BUILD_EXAMPLES=true ..

After running the above, you can then cd into the build/examples directory and run make.

Optionally, you can also build and run the tests (tests require the examples):

    cmake -G "Unix Makefiles" -DPISTACHE_BUILD_EXAMPLES=true -DPISTACHE_BUILD_TESTS=true ..
    make test

Be patient, async_test can take some time before completing.

And that's it, now you can start playing with your newly installed Pistache framework.

Some other CMAKE defines:

| Option                    | Default     | Description                                                 |
|---------------------------|-------------|-------------------------------------------------------------|
| PISTACHE_BUILD_EXAMPLES   | False       | Build all of the example apps                               |
| PISTACHE_BUILD_TESTS      | False       | Build all of the unit tests                                 |

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
