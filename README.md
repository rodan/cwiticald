
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

the included entropy tests are written by Philipp Rumpf and are part of the [rng-tools-6.14](https://github.com/nhorman/rng-tools) package.

### Description

cwiticald reads blocks of 2500 bytes from /dev/truerng, verifies their compliance against FIPS 140-1 and FIPS 140-2 tests, fills up a large buffer and provides it to clients that connect to it via TCPv4 or TCPv6.

an ekey-egd-linux service can be used as client to cwiticald :

```
ekey-egd-linux -H cwiticald-server -p 41300 -b 2 -r 10
```

### Build requirements

dependencies include a gcc-based linux toolchain, together with the pthread and libevent-2.* libraries

### Build and install

if you're using gentoo, a portage overlay is provided. a simple

```
emerge cwiticald
```

will compile and install the application.

for any other distribution, you can use the following commands:

```
cd ./src
make
install -m 755 ./cwiticald /usr/sbin/
install -m 644 ../doc/cwiticald.1 /usr/share/man/man1/
```

the serial connection needs to be configured via stty:

for Linux

```
cat << EOF > /etc/udev/rules.d
# ubld.it TrueRNG
#
# This rule creates a symlin to newly attached CDC-ACM device
# Also includes fix for wrong termios settings on some linux kernels
SUBSYSTEM=="tty", ATTRS{product}=="TrueRNG", MODE="0640", GROUP="rngd", SYMLINK+="truerng", RUN+="/bin/stty raw -echo -ixoff -F /dev/%k speed 3000000"
ATTRS{idVendor}=="04d8", ATTRS{idProduct}=="f5fe", ENV{ID_MM_DEVICE_IGNORE}="1"
EOF
```

for FreeBSD:

```
stty -f /dev/cuaU0.init raw -echo -ixoff speed 3000000
```

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



