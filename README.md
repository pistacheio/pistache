# Pistache
[![N|Solid](http://pistache.io/assets/images/logo.png)](https://www.github.com/oktal/pistache)
[![Travis Build Status](https://travis-ci.org/oktal/pistache.svg?branch=master)](https://travis-ci.org/oktal/pistache)

Pistache is a modern and elegant HTTP and REST framework for C++. It is entirely written in pure-C++11 and provides a clear and pleasant API. 

# Documentation

We are still looking for a volunteer to document fully the API. In the mean time, partial documentation is available at [http://pistache.io](http://pistache.io). If you are interested in helping with this, please open an issue ticket.

# Contributing

Pistache is released under the [Apache License 2.0](https://www.apache.org/licenses/LICENSE-2.0). Contributors are welcome!

Pistache was originally created by Mathieu Stefani, but he is no longer actively maintaining Pistache. A team of volunteers has taken over. To reach the original maintainer, drop a private message to `@octal` in [cpplang Slack channel](https://cpplang.now.sh/).

For those that prefer IRC over Slack, the rag-tag crew of maintainers idle in `#pistache` on Freenode. Please come and join us!

# Precompiled Packages

## Debian and Ubuntu
We have submitted both a [Request for Packaging](https://bugs.debian.org/cgi-bin/bugreport.cgi?bug=929593) and a [Request for Sponsorship](https://bugs.debian.org/cgi-bin/bugreport.cgi?bug=927839) downstream to Debian. Once Pistache has an official Debian package maintainer intimately familiar with the [Debian Policy Manual](https://www.debian.org/doc/debian-policy/), we can expect to eventually see Pistache available in Debian and all Debian based distributions (e.g. Ubuntu and many others).

But until then currently Pistache has partially compliant upstream Debianization. Our long term goal is to have our source package properly Debianized downstream by a Debian Policy Manual SME. In the mean time consider using our PPAs to avoid having to build from source.

### Supported Architectures
Currently Pistache is built and tested on a number of [architectures](https://wiki.debian.org/SupportedArchitectures). Some of these are suitable for desktop or server use and others for embedded environments. As of this writing we do not currently have any MIPS related packages that have been either built or tested.

- amd64
- arm64
- armhf (build fails at this moment)
- i386
- ppc64el
- s390x

### Ubuntu PPA (Stable)
If you would like to use [stable](https://launchpad.net/%7Ekip/+archive/ubuntu/pistache) packages, run the following:

```console
$ sudo add-apt-repository ppa:kip/pistache
$ sudo apt update
$ sudo apt install libpistache-dev
```

### Ubuntu PPA (Unstable)
To use [unstable](https://launchpad.net/%7Ekip/+archive/ubuntu/pistache-unstable) packages, run the following:

```console
$ sudo add-apt-repository ppa:kip/pistache-unstable
$ sudo apt update
$ sudo apt install libpistache-dev
```

## Use via pkg-config

If you would like to automatically have your project's build environment use the appropriate compiler and linker build flags necessary to use Pistache, [pkg-config](https://www.freedesktop.org/wiki/Software/pkg-config/) can greatly simplify things. The `libpistache-dev` package includes a pkg-config manifest.

To use with the GNU Autotools, as an example, include the following snippet in your project's `configure.ac`:

```

    # Pistache...
    PKG_CHECK_MODULES(
        [libpistache], [libpistache >= 0.0], [],
        [AC_MSG_ERROR([libpistache >= 0.0 missing...])])
    YOURPROJECT_CXXFLAGS="$YOURPROJECT_CXXFLAGS $libpistache_CFLAGS"
    YOURPROJECT_LIBS="$YOURPROJECT_LIBS $libpistache_LIBS"
    
```

# Building from source

To download the latest available release, clone the repository over github.

```console
    git clone https://github.com/oktal/pistache.git
```

Then, init the submodules:

```console
    git submodule update --init
```

Now, compile the sources:

```console
    cd pistache
    mkdir -p {build,prefix}
    cd build
    cmake -G "Unix Makefiles" \
        -DCMAKE_BUILD_TYPE=Release \
        -DPISTACHE_BUILD_EXAMPLES=true \
        -DPISTACHE_BUILD_TESTS=true \
        -DPISTACHE_BUILD_DOCS=false \
        -DPISTACHE_USE_SSL=true \
        -DCMAKE_INSTALL_PREFIX=$PWD/../prefix \
        ../
    make -j
    make install
```

If you chose to build the examples, then perform the following to build the examples.
```console
    cd examples
    make -j
```

Optionally, you can also build and run the tests (tests require the examples):

```console
    cmake -G "Unix Makefiles" -DPISTACHE_BUILD_EXAMPLES=true -DPISTACHE_BUILD_TESTS=true ..
    make test test_memcheck
```

Be patient, async_test can take some time before completing. And that's it, now you can start playing with your newly installed Pistache framework.

Some other CMAKE defines:

| Option                        | Default     | Description                                    |
|-------------------------------|-------------|------------------------------------------------|
| PISTACHE_BUILD_EXAMPLES       | False       | Build all of the example apps                  |
| PISTACHE_BUILD_TESTS          | False       | Build all of the unit tests                    |
| PISTACHE_ENABLE_NETWORK_TESTS | True        | Run unit tests requiring remote network access |
| PISTACHE_USE_SSL              | False       | Build server with SSL support                  |

# Example

## Hello World (server)

```cpp
#include <pistache/endpoint.h>

using namespace Pistache;

struct HelloHandler : public Http::Handler {
  HTTP_PROTOTYPE(HelloHandler)
  void onRequest(const Http::Request&, Http::ResponseWriter writer) override{
    writer.send(Http::Code::Ok, "Hello, World!");
  }
};

int main() {
  Http::listenAndServe<HelloHandler>("*:9080");
}
```
