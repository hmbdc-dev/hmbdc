# README #

hmbdc is an open source headers only C++ framework and library built on top of 
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
- low latency / high throughput / kernel bypassing
- support multiple types of transport mechanisms with
  the consistent look and feel C++7 API:
    interthread
    interprocess
    UDP multicast
    TCP
    netmap multicast
    reliable multicast
    reliable netmap
- support easy to use symetric (no broker process) type based message 
  publish / subscribe with recording and playback tool
- other high performance computing features: packet or message
  level sniffing, network link traffic arbitration, 
  high speed async logging, timer, rate control ...
- straightforward integration with any other programming 
  languages that supports Linux piping - see the Python 
  example in package.
- provide performance measurement tools

### What is this repository for? ###
* it is used in areas that are cpu / network 
  intensive with high performance (latency / throughput) 
  requirements such as high frequency 
  trading, high performance server-backend, robotics, 
  multi-core real-time systems, etc. 
  hmbdc helps you get it done quick and fast.

### How do I build code in example and tools dir ###
0. make sure you have the required lib in place: boost(1-62 or later)
  make sure you have the QNX or aarch64 cross compiling tools ready 
  if you are building for them
  for QNX, source the qnx set env script now
1. use the following recommneded g++ options to build the executables:
g++ <cpp-file> -std=c++1z -fconcepts -D BOOST_BIND_GLOBAL_PLACEHOLDERS -O3 
-D NDEBUG -Wall -Werror -pthread -isystem <boost-include> 
-I. -s 
<boost-lib>/libboost_program_options.a <boost-lib>/libboost_system.a 
<boost-lib>/libboost_iostreams.a  
-lrt -o <output-executable>
for example:
g++ tools/perf-base-lat.cpp  -std=c++1z -fconcepts \
-D BOOST_BIND_GLOBAL_PLACEHOLDERS -O3 -D NDEBUG -Wall -Werror -pthread \
-isystem /usr/local/boost/include -I. -s \
/usr/local/boost/lib/libboost_program_options.a \
/usr/local/boost/lib/libboost_system.a \
/usr/local/boost/lib/libboost_iostreams.a  -lrt -o /tmp/perf-base-lat

### Contribution guidelines ###
* all suggestions and issue reports are welcome
* please report the issue using the issues function on 
  the downloading website, or contact 
  dev@hummingbirdcode.net or hummingbirdcode@gmail.com

### Who do I talk to? ###
* public is granted all the rights to use and modify 
  hmbdc without redistributing the modifications 
  in source code format.
* hmbdc is public downloadable freeware owned by 
  Hummingbird Code Software LLC. 
  Please contact dev@hummingbirdcode.net or
  hummingbirdcode@gmail.com for hmbdc-licensed package.
* please direct questions to dev@hummingbirdcode.net or 
  hummingbirdcode@gmail.com 
  and thank you for your interests in hmbdc!
  
### Notes ###
* build qnx / aarch64 version of boost
./b2 toolset=qcc target-os=qnxnto threadapi=pthread link=static -l240 --layout=system --without-python cxxflags="-V gcc_ntox86_64_gpp -Wc,-std=gnu++0x -D_LITTLE_ENDIAN" linkflags="-V gcc_ntox86_64_gpp -lang-c++"


