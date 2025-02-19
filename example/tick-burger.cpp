// This file is to show how to use hmbdc/tips TickNode
// Tick is a frequently used concept in robotics, in which a Node groups a set of input messages, and when
// all messages in the group are received (with some condition checked - for example all messages have the close enough timestamp), 
// the Node starts to work to process the inputs (start to 'tick')
// In this example in an imaginary world, we have a 3 factory/Nodes that make Bread, Beef and Burger respectively
// The Bread and Beef productions are not synchronzed, and each produce(publish) Bread and Beef at their own rate,
// The Burger factory consumes both products(messgaes) by pairing them to make(publish) a Burger(Message). 
// To approximate the freshness, both the Beef and the Bread used in a particular Burger need to be ariving at the 
// BurgerFactory within 5ms, otherwise, just do not make the Burger.
// After making X Burgers, the Beef factory sends out a Stop, the other Nodes exit upon this message
// We make all 3 Nodes running as 3 threads in the same process in this demo example since it i trivial to mke them
// running as processes on diff hosts
// 
// to build:
// g++ burger-tick.cpp -std=c++1z -D BOOST_BIND_GLOBAL_PLACEHOLDERS -pthread  -Ipath-to-boost -lrt -o /tmp/burger-tick
// to run:
// /tmp/burger-tick
// 20241228153300.364989909 NOTICE  :  Starting making Burger
// 20241228153300.365075006 NOTICE  :  Starting making Beef
// 20241228153300.365129086 NOTICE  :  Starting making Bread
// 20241228153300.365181446 NOTICE  :  Made a burger freshness=-0.000009541
// 20241228153300.365204534 NOTICE  :  burger count = 1
// ...
// 20241228153405.117278379 NOTICE  :  Made a burger freshness=0.004889446
// 20241228153405.117453668 NOTICE  :  burger count = 100
// 20241228153405.117589948 NOTICE  :  Stop

#include "./tick-burger.hpp"

int main() {
    using MyLogger = hmbdc::app::SyncLogger;
    hmbdc::pattern::SingletonGuardian<MyLogger> logGuard(std::cout);
    IntraHostDomain domain(hmbdc::app::Config{});
    BreadFactory breadFactory;
    BeefFactory beefFactory;
    BurgerFactory burgerFactory;
    domain.add(burgerFactory).add(beefFactory).add(breadFactory);
    domain.startPumping();
    domain.join(); // wait for all Node threads to exit
    std::cout << "premiumBurgerCount = " << burgerFactory.premiumBurgerCount;
}
