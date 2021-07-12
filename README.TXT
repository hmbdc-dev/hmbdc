# README #

hmbdc is an open source C++ framework and library built on top of 
various real-world tested lockfree algorithms that facilitate 
ultra low latency and high throughput messaging 
(inter-thread, inter-process and thru network) programming. 
It aims to take most out of the allocated hardware resources 
(memory, cores, NICs and switches) to achieve latency 
and throughput goals. It works as a framework but it can 
also be used as libraries to collaborate with other 
frameworks.

Please refer to: 
http://www.hummingbirdcode.net
for reliability, scalability, performance and design 
information.

Keywords and features:
- multithreaded without the headache
- lockfree / disruptor pattern
- low latency / high throughput / kernel bypassing
- support multiple types of transport mechanisms with
  the consistent look and feel C++14 API:
    interthread(~200ns delay, free),
    interprocess(ipc, ~200ns delay, free),
    UDP multicast(mcast, ~30us delay, free),
    TCP(tcpcast, ~30us delay, free),
    netmap multicast(netmap, ~15us delay, free),
    reliable multicast (rmcast, ~30us delay, hmbdc-licensed)
    reliable netmap (rnetmap, ~15us delay, hmbdc-licensed)
- rmcast provides the same latency, throughput and 
  scalability as UDP multicast and is as reliabel as TCP. 
  rnetmap, even better, as reliable as TCP, it 
  further reduces latencies using the netmap driver.
- support symetric (no broker process) publish / subscribe 
  topic based network communication including subscribing 
  using topic prefixes
- other high performance computing features: packet or message
  level sniffing, network link traffic arbitration, 
  high speed async logging, timer, rate control ...
- straightforward integration with any other programming 
  languages that supports Linux piping - see the Python 
  example in package.
- performance measurement binaries using hmbdc are
  packaged in releases as well as their source code.
  both rmcast and rnetmap performance measure 
  tools are runnable without any license requirement.
- 100% open source for licensed users

The C++(14) flavor API are designed to be powerful and 
yet intuitive to use. The extensive template usage 
gives the compiler all the opportunities to optimize. 
The best way to get a taste of the lib is thru its 
examples and the API doc can be found at
/opt/hmbdc/doc/html/index.html and the source headers.

Some full examples to use hmbdc to send and receive 
messages (inter-thread, ipc and thru network) can be found 
in https://bitbucket.org/hmbd/hmbdc-rel/src
More examples are packaged in the release.

### What is this repository for? ###

* it is used in areas that are cpu / network 
  intensive with high performance (latency / throughput) 
  requirements. Where the aplication latencies is measured
  in the unit of microsecond, or the system throughput is 
  measured in millions per second - such as high frequency 
  trading, high performance server-backend, robotics, 
  multi-core real-time systems, etc. 
  hmbdc helps you get it done quick and fast.

### How do I get set up using pre-built packages? ###

* hmbdc supports 
  gcc 7.3.1 (or later) on Linux CentOs 7 (or later,
  and their compatible Red Hat RHEL platforms) and 
  Ubuntu 18.04 (or later)
  clang 5.0.1 (or later) on the above CentOs and RHEL
  platforms
  QNX SDP7.0 for x86_64 or aarch64
* go to the download page, and install the rpms (
  deb on Ubuntu, prebuilt tar for QNX and aarch64 platforms) 
  and the dependency will be checked automatically. 
  installed libraries and doc are located at /opt/hmbdc{arch} dir.
* boost (later than 1.62), netmap and rnetmap need 
  netmap driver
* /opt/hmbdc{arch}/bin has compiled binaries to test the 
  library functionality and performance on your platform
* package functionalities:
    hmbdc-free (inter-thread, inter-process messaging, 
      mcast, tcpcast, netmap) 
    hmbdc-licensed (rmcast and rnetmap)

### How do I build from src tar ball? - for licensed users only ###
0. make sure you have the required lib in place: boost(1-62 or later)
  and $(BOOST_ROOT)/include $(BOOST_ROOT)/lib present;
  make sure you have the QNX or aarch64 cross compiling tools ready 
  if you are building for them
  for QNX, source the qnx set env script now
1. mkdir <build dir>; cd <build dir>; tar axvf <hmbdc src tar.gz file>
3. make BOOST_ROOT=<boost root dir> #add aarch64=1 flag if that is the target (Linux or QNX)
4. sudo make install #add aarch64=1 flag if that is the target (Linux or QNX) 
5. done - check /opt/hmbdc{arch} dir for installed files

### Contribution guidelines ###
* all suggestions and issue reports are welcome
* please report the issue using the issues function on 
  the downloading website, or contact 
  dev@hummingbirdcode.net or hummingbirdcode@gmail.com

### Who do I talk to? ###
* public is granted all the rights to use and modify 
  hmbdc-free without redistributing the modifications 
  in source code format.
* hmbdc-free is public downloadable freeware owned by 
  Hummingbird Code Software LLC. 
  Please contact dev@hummingbirdcode.net or
  hummingbirdcode@gmail.com for hmbdc-licensed package.
* most part of hmbdc-free and hmbdc-licensed are header 
  only C++ libs.
* please direct questions to dev@hummingbirdcode.net or 
  hummingbirdcode@gmail.com 
  and thank you for your interests in hmbdc!
  
### Notes ###
* build qnx / aarch64 version of boost
./b2 toolset=qcc target-os=qnxnto threadapi=pthread link=static -l240 --layout=system --without-python cxxflags="-V gcc_ntox86_64_gpp -Wc,-std=gnu++0x -D_LITTLE_ENDIAN" linkflags="-V gcc_ntox86_64_gpp -lang-c++"


