

# Pistache
[![N|Solid](http://pistache.io/assets/images/logo.png)](https://www.github.com/oktal/pistache)

[![Build Status](https://travis-ci.org/pistacheio/pistache.svg?branch=master)](https://travis-ci.org/pistacheio/pistache)

Pistache is a modern and elegant HTTP and REST framework for C++. It is entirely written in pure-C++14 and provides a clear and pleasant API.

# Documentation

We are still looking for a volunteer to document fully the API. In the mean time, partial documentation is available at [http://pistache.io](http://pistache.io). If you are interested in helping with this, please open an issue ticket.

# Dependencies

Pistache has the following third party dependencies

- [CMake](https://cmake.org)
- [Doxygen](https://www.doxygen.nl/)
- [Googletest](https://github.com/google/googletest)
- [OpenSSL](https://www.openssl.org/)
- [RapidJSON](https://rapidjson.org/)

# Contributing

Pistache is released under the [Apache License 2.0](https://www.apache.org/licenses/LICENSE-2.0). Contributors are welcome!

Pistache was originally created by Mathieu Stefani, but he is no longer actively maintaining Pistache. A team of volunteers has taken over. To reach the original maintainer, drop a private message to `@octal` in [cpplang Slack channel](https://cpplang.now.sh/).

For those that prefer IRC over Slack, the rag-tag crew of maintainers idle in `#pistache` on Freenode. Please come and join us!

The [Launchpad Team](https://launchpad.net/~pistache+team) administers the daily and stable Ubuntu pre-compiled packages.

## Release Versioning

Please update `version.txt` accordingly with each unstable or stable release.

## Interface Versioning

The version of the library's public interface (ABI) is not the same as the release version. The interface version is primarily associated with the _external_ interface of the library. Different platforms handle this differently, such as AIX, GNU/Linux, and Solaris.

GNU Libtool abstracts each platform's idiosyncrasies away because it is more portable than using `ar(1)` or `ranlib(1)` directly. However, it is a pain to integrate with CMake so we made do without it by setting the SONAME directly.

When Pistache is installed it will normally ship:
- `libpistache-<release>.so.X.Y`: This is the actual shared-library binary file. The _X_ and _Y_ values are the major and minor interface versions respectively.

- `libpistache-<release>.so.X`: This is the _soname_ soft link that points to the binary file. It is what other programs and other libraries reference internally. You should never need to directly reference this file in your build environment.

- `libpistache-<release>.so`: This is the _linker name_ entry. This is also a soft link that refers to the soname with the highest major interface version. This linker name is what is referred to on the linker command line. This file is created by the installation process.

- `libpistache-<release>.a`: This is the _static archive_ form of the library. Since when using a static library all of its symbols are normally resolved before runtime, an interface version in the filename is unnecessary.

If your contribution has modified the interface, you may need to update the major or minor interface versions. Otherwise user applications and build environments will eventually break. This is because they will attempt to link against an incorrect version of the library -- or worse, link correctly but with undefined runtime behaviour.

The major version should be incremented every time a non-backward compatible change occured in the ABI. The minor version should be incremented every time a backward compatible change occurs. This can be done by modifying `version.txt` accordingly.

# Precompiled Packages
If you have no need to modify the Pistache source, you are strongly recommended to use precompiled packages for your distribution. This will save you time.

## Debian and Ubuntu
We have submitted a [Request for Packaging](https://bugs.debian.org/cgi-bin/bugreport.cgi?bug=929593) downstream to Debian. Once we have an official Debian package maintainer intimately familiar with the [Debian Policy Manual](https://www.debian.org/doc/debian-policy/), we can expect to eventually see it become available in Debian and all derivatives (e.g. Ubuntu and many others).

But until then currently Pistache has partially compliant upstream Debianization. Our long term goal is to have our source package properly Debianized downstream by a Debian Policy Manual SME. In the mean time consider using our PPAs to avoid having to build from source.

### Supported Architectures
Currently Pistache is built and tested on a number of [architectures](https://wiki.debian.org/SupportedArchitectures). Some of these are suitable for desktop or server use and others for embedded environments. As of this writing we do not currently have any MIPS related packages that have been either built or tested. The `ppc64el` builds are occasionally tested on POWER9 hardware, courtesy of IBM.

- amd64
- arm64
- armhf
- i386
- ppc64el
- s390x

### Ubuntu PPA (Unstable)
The project builds [daily unstable snapshots](https://launchpad.net/~pistache+team/+archive/ubuntu/unstable) in a separate unstable PPA. To use it, run the following:

```console
$ sudo add-apt-repository ppa:pistache+team/unstable
$ sudo apt update
$ sudo apt install libpistache-dev
```

### Ubuntu PPA (Stable)
Currently there are no stable release of Pistache published into the [stable](https://launchpad.net/~pistache+team/+archive/ubuntu/stable) PPA. However, when that time comes, run the following to install a stable package:

```console
$ sudo add-apt-repository ppa:pistache+team/stable
$ sudo apt update
$ sudo apt install libpistache-dev
```

## Other Distributions
Package maintainers, please insert instructions for users to install pre-compiled packages from your respective repositories here.

# Use via pkg-config

If you would like to automatically have your project's build environment use the appropriate compiler and linker build flags, [pkg-config](https://www.freedesktop.org/wiki/Software/pkg-config/) can greatly simplify things. It is the portable international _de facto_ standard for determining build flags. The development packages include a pkg-config manifest.

## GNU Autotools
To [use](https://autotools.io/pkgconfig/pkg_check_modules.html) with the GNU Autotools, as an example, include the following snippet in your project's `configure.ac`:

```

    # Pistache...
    PKG_CHECK_MODULES(
        [libpistache], [libpistache >= 0.0.2], [],
        [AC_MSG_ERROR([libpistache >= 0.0.2 missing...])])
    YOURPROJECT_CXXFLAGS="$YOURPROJECT_CXXFLAGS $libpistache_CFLAGS"
    YOURPROJECT_LIBS="$YOURPROJECT_LIBS $libpistache_LIBS"
    
```

## CMake
To use with a CMake build environment, use the [FindPkgConfig](https://cmake.org/cmake/help/latest/module/FindPkgConfig.html) module. Here is an example:

```cmake
    cmake_minimum_required(3.4 FATAL_ERROR)
    project("MyPistacheProject")

    # Find the library.
    find_package(Pistache 0.0.2 REQUIRED)

    add_executable(${PROJECT_NAME} main.cpp)
    target_link_libraries(${PROJECT_NAME} pistache_shared)
```

## Makefile
To use within a vanilla makefile, you can call `pkg-config` directly to supply compiler and linker flags using shell substitution.

```
    CFLAGS=-g3 -Wall -Wextra -Werror ...
    LDFLAGS=-lfoo ...
    ...
    CFLAGS+= $(pkg-config --cflags libpistache)
    LDFLAGS+= $(pkg-config --libs libpistache)
```

# Building from source

To download the latest available release, clone the repository over github.

```console
    $ git clone https://github.com/oktal/pistache.git
```

Then, init the submodules:

```console
    $ git submodule update --init
```

Now, compile the sources:

```console
    $ cd pistache
    $ mkdir -p {build,prefix}
    $ cd build
    $ cmake -G "Unix Makefiles" \
        -DCMAKE_BUILD_TYPE=Release \
        -DPISTACHE_BUILD_EXAMPLES=true \
        -DPISTACHE_BUILD_TESTS=true \
        -DPISTACHE_BUILD_DOCS=false \
        -DPISTACHE_USE_SSL=true \
        -DCMAKE_INSTALL_PREFIX=$PWD/../prefix \
        ../
    $ make -j
    $ make install
```

If you chose to build the examples, then perform the following to build the examples.
```console
    $ cd examples
    $ make -j
```

Optionally, you can also build and run the tests (tests require the examples):

```console
    $ cmake -G "Unix Makefiles" -DPISTACHE_BUILD_EXAMPLES=true -DPISTACHE_BUILD_TESTS=true ..
    $ make test test_memcheck
```

Be patient, async_test can take some time before completing. And that's it, now you can start playing with your newly installed Pistache framework.

Some other CMAKE defines:

| Option                        | Default     | Description                                    |
|-------------------------------|-------------|------------------------------------------------|
| PISTACHE_BUILD_EXAMPLES       | False       | Build all of the example apps                  |
| PISTACHE_BUILD_TESTS          | False       | Build all of the unit tests                    |
| PISTACHE_ENABLE_NETWORK_TESTS | True        | Run unit tests requiring remote network access |
| PISTACHE_USE_SSL              | False       | Build server with SSL support                  |

# Continuous Integration Testing

It is important that all patches pass unit testing. Unfortunately developers make all kinds of changes to their local development environment that can have unintended consequences. This can means sometimes tests on the developer's computer pass when they should not, and other times failing when they should not have. 

To properly validate that things are working, continuous integration (CI) is required. This means compiling, performing local in-tree unit tests, installing through the system package manager, and finally testing the actually installed build artifacts to ensure they do what the user expects them to do.

The key thing to remember is that in order to do this properly, this all needs to be done within a realistic end user system that hasn't been unintentionally modified by a developer. This might mean a chroot container with the help of QEMU and KVM to verify that everything is working as expected. The hermetically sealed test environment validates that the developer's expected steps for compilation, linking, unit testing, and post installation testing are actually replicable.

There are [different ways](https://wiki.debian.org/qa.debian.org#Other_distributions) of performing CI on different distros. The most common one is via the international [DEP-8](https://dep-team.pages.debian.net/deps/dep8/) standard as used by hundreds of different operating systems.

## Autopkgtest
On Debian based distributions, `autopkgtest` implements the DEP-8 standard. To create and use a build image environment for Ubuntu, follow these steps. First install the autopkgtest(1) tools:
```
$ sudo apt install autopkgtest
```

Next create the test image, substituting `groovy` or `amd64` for other releases or architectures:
```
$ autopkgtest-buildvm-ubuntu-cloud -r groovy -a amd64
```

Generate a Pistache source package in the parent directory of `pistache_source`:
```
$ cd pistache_source
$ sudo apt build-dep pistache
$ ./debian/rules get-orig-source
$ debuild -S -sa
```

Test the source package on the host architecture in QEMU with KVM support and 8GB of RAM and four CPUs:
```
$ autopkgtest --shell-fail --apt-upgrade ../pistache_(...).dsc -- \
      qemu --ram-size=8192 --cpus=4 --show-boot path_to_build_image.img \
      --qemu-options='-enable-kvm'
```

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
  Http::listenAndServe<HelloHandler>(Pistache::Address("*:9080"));
}
```

