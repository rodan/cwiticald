
<a href="https://scan.coverity.com/projects/rodan-cwiticald">
  <img alt="Coverity Scan Build Status"
       src="https://scan.coverity.com/projects/23941/badge.svg"/>
</a>

## cwiticald

an Entropy Gathering Daemon that provides a random stream generated by a USB TrueRNG device via TCP

```
 source:       https://github.com/rodan/cwiticald
 author:       Petre Rodan <2b4eda@subdimension.ro>
 license:      GNU GPLv3
```

the included entropy tests are written by Philipp Rumpf and part of the rng-tools-6.14 package.

### Description

cwiticald reads blocks of 2500 bytes from /dev/truerng, verifies their compliance against FIPS 140-1 and FIPS 140-2 tests, fills up a large buffer and provides it to clients that connect to it via TCPv4 or TCPv6.

an ekey-egd-linux service can be used as client to cwiticald :

```
ekey-egd-linux -H cwiticald-server -p 41300 -b 2 -r 10
```

### Build requirements

dependencies include a gcc-based linux toolchain, together with the pthread and libevent-2.* libraries

### Usage

a manual is provided
```
man ./doc/cwiticald.1
```

***SYNOPSIS***

cwiticald [-hv] [-d, --device NAME] [-4, --ipv4 IP] [-6, --ipv6 IP]
[-p, --port NUM] [-b, --buffer-size NUM] [-t, --trigger NUM]


### Testing

the application can be stress-tested with the tools present in the [tests](./tests) directory. the following command will do it's best to continually deplete cwiticald's entropy buffer:

```
perl tests/sucker.pl host:port
```

the code itself is static-scanned by [llvm's scan-build](https://clang-analyzer.llvm.org/), [cppcheck](http://cppcheck.net/) and [coverity](https://scan.coverity.com/projects/rodan-cwiticald?tab=overview). Dynamic memory allocation in the PC applications is checked with [valgrind](https://valgrind.org/).



