#include "hmbdc/Copyright.hpp"
#pragma once

#include "hmbdc/pots/Domains.hpp"
#include "hmbdc/pots/Node.hpp"
#include "hmbdc/pots/Messages.hpp"
#include "hmbdc/tips/Tips.hpp"

namespace hmbdc { namespace pots {
/**
 * @brief POTS stands for Publish On Topic String
 * It is a C++ string topic based messgage delivery lib built on top of Type Inferred Pub/Sub (see tips dir)
 * The purpose of pots is to provide the string topic based systems a strightforward path to 
 * use hmbdc.
 * 
 * Just like TIPS, POTS covers inter thread, inter process and network communication. 
 * Unlike TIPS that supports type based filtering, POTS only uses runtime filtering on topic strings.
 * 
 **/

/* LOGIC (concept) VIEW - same as TIPS
                                                                                  +--------+
                                                                                  |Node    |
                                                          XXXXXXXXXXX             |        |
    +--------------------+                          XXXXXX           XXXXX        +--+-----+
    | Node               |     Nodes join         XX                      XXXXXX     |
    |an OS thread        +---------------------+XX                              +----+
    +--------------------+  same domain receive X           domain               X
    |                    |      pub / sub       X                                +------+
    | potsCb(topic,bytes)|       messges.        X                              X       |
    |                    |   pub can happen       XXXXX+XX      XXXXXXXXXXXXXXXX    +---+----+
    |                    |  anywhere through the       |  XXXXXX                    |Node    |
    |                    |   domain handle             |                            |        |
    +--------------------+                             |                            +--------+
    |                    |                       +-----+--+
    |  timers callbacks  |                       |Nodes   +-+
    |  (dynamic)         |                       |pooled  | +-+
    |                    |                       +--------+ | |
    |                    |                            +-----+ |
    |                    |                                +---+
    +--------------------+

   PHYSICAL (deployment) VIEW - same as TIPS
  +---------------------------+               +---------------------------------------------+
  |   +--------------------+  |               | +-------------+                             |
  |   | +--------+         |  |               | |process      |          +---------------+  |
  |   | |Node    |         |  |               | |             +----------+ ipc transport |  |
  |   | |        |         |  |       +---------+             |          +-------+-------+  |
  |   | +--------+         |  |       |       | |             |                  |          |
  |   |                    |  |       |       | +---------+   |                  |          |
  |   |                    |  |       |       | ||Node    |   |                  |          |
  |   |                    |  |       |       | ||        |   |  +---------------------+    |
  |   |                    |  |       |       | |---------+   |  |         +--------+  |    |
  |   |                    |  |       |       | +-------------+  |         |Node    |  |    |
  |   |         +--------+ |  |       |       |                  |         |        |  |    |
  |   |         |Node    | +-------+  |       |                  |         +--------+  |    |
  |   |         |        | |  |    |  |       |                  | +--------+          |    |
  |   |         +--------+ |  |    |  |       |                  | |Node    |          |    |
  |   |                    |  |   ++--++      |                  | |        |          |    |
  |   |                    |  |   |net |      |                  | +--------+          |    |
  |   |process             |  |   +-+--+      |                  |     +--------+      |    |
  |   +--------------------+  |     |         |                  |     |Nodes   +-+    |    |
  |                           |     |         |                  |     |pooled  | +-+  |    |
  |                           |     +----------------------------+     +--------+ | |  |    |
  |                           |               |                  |          +-----+ |  |    |
  |                           |               |                  |process       +---+  |    |
  |                           |               |                  +---------------------+    |
  | host                      |               |                                       host  |
  +---------------------------+               +---------------------------------------------+
*/
}}
