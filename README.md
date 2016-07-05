# SolidFrame

Cross-platform C++ framework for scalable, asynchronous, distributed client-server applications.

## License

Boost Software License - Version 1.0 - August 17th, 2003

## Resources
 * [API Reference](solid/API.md)
 * [Wiki](https://github.com/vipalade/solidframe/wiki)
 * [Release Notes](RELEASES.md)
 * [IPC library](solid/frame/ipc/README.md)

## Prerequisites
* C++11 enabled compiler
* [CMake](https://cmake.org/) 
* [boost](http://www.boost.org/)
* [OpenSSL](https://www.openssl.org/)

## Supported platforms

* Linux - (tested on latest Fedora i686/x86_64 and Raspian on Raspberry Pi 2 armv7l) - gcc
* FreeBSD - (tested on FreeBSD/PcBSD 10.3) - llvm
* Darwin/OSX - (waiting for XCode 8 with support for thread_local) - llvm
* Windows - (partial) - latest VisualStudio

## Libraries

* __solid_system__:
	* Wrappers for socket/file devices, socket address, directory access
	* Debug log engine
* __solid_utility__:
	* _any_ - similar to boost::any
	* _event_ - Event class containing an ID an solid::Any object and a Category (similar to std::error_category)
	* _innerlist_ - bidirectional list mapped over a vector/deque
	* _stack_ - alternative to std::stack
	* _queue_ - alternative to std:queue
	* _workpool_ - generic thread pool
* __solid_serialization__: binary serialization/marshalling
* __solid_frame__:
	* _object_ - reactive object
	* _manager_ - store services and notifies objects within services
	* _service_ - store and notifies objects
	* _reactor_ - active store of objects - allows objects to asynchronously react on events
	* _scheduler_ - a thread pool of reactors
	* _timer_ - allows objects to schedule time based events
	* _shared store_ - generic store of shared objects that need either multiple read or single write access
* __solid_frame_aio__: asynchronous communication library using epoll on Linux and kqueue on FreeBSD/macOS
	* _object_ - reactive object with support for Asynchronous IO
	* _reactor_ - reactor with support for Asynchronous IO
	* _listener_ - asynchronous TCP listener/server socket
	* _stream_ - asynchronous TCP socket
	* _datagram_ - asynchronous UDP socket
	* _timer_ - allows objects to schedule time based events
* __solid_frame_aio_openssl__: SSL support via OpenSSL
* __solid_frame_file__
	* _file store_ - a shared store for files
* __solid_frame_ipc__: asynchronous Secure/Plain TCP inter-process communication engine ([IPC library](libraries/frame/ipc/README.md))
	* _ipcservice_ - asynchronously sends and receives messages to/from multiple peers.

## Getting Started

### Install

Following are the steps for:
* fetching the _SolidFrame_ code
* building the prerequisites folder
* building and installing _SolidFrame_
* use _SolidFrame_ in your projects

Note that [boost framework](http://www.boost.org) is only used for building _SolidFrame_.
Normally, _SolidFrame_ libraries would not depend on boost.

#### Linux/macOS/FreeBSD

System prerequisites:
 * C++11 enabled compiler: gcc-c++ on Linux and clang on FreeBSD and macOS (minimum: XCode 8/Clang 8).
 * [CMake](https://cmake.org/)

Bash commands for installing _SolidFrame_:

```bash
$ mkdir ~/work
$ cd ~/work
$ git clone git@github.com:vipalade/solidframe.git
$ mkdir extern
$ cd extern
$ ../solidframe/prerequisites/prepare_extern.sh
# ... wait until the prerequisites are built
$ cd ../solidframe
$ ./configure -e ~/work/extern --prefix ~/work/extern
$ cd build/release
$ make install
# ... when finished, the header files will be located in ~/work/extern/include/solid
# and the libraries at ~/work/extern/lib/libsolid_*.a
```
#### Windows
Windows is not yet supported.

### Use
Use _SolidFrame_ libraries in your applications via: -I ~/work/extern/include and -L ~/work/extern/lib.

For how can _SolidFrame_ help you in your projects, please follow:
 * [Wiki](https://github.com/vipalade/solidframe/wiki)
 * [API Reference](solid/API.md)
 
