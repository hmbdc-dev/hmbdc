#include "hmbdc/Copyright.hpp"
#pragma once

#include "hmbdc/tips/Node.hpp"
#include "hmbdc/tips/Domain.hpp"
#include "hmbdc/tips/SingleNodeDomain.hpp"
#include "hmbdc/tips/Messages.hpp"

namespace hmbdc { namespace tips {
/**
 * @brief TIPS stands for Type Inferred Pub / Sub
 * It is a C++ Typed based (vs string topic based) messgage delivery
 * system covers inter thread, inter process and network communication.
 *  
 * Most - but not all - Message filterring decision is based 
 * on the compile time (through C++ template) Message type information.
 * 
 * Runtime message filterring is provided through a special Message
 * category runtime-tagged Message - where a message type could be associated 
 * with a runtime decided tag value - this enables the Message type 
 * filterring decision to be made on a runtime configured criteria.
 * For example, an ImageFrame Message if defnined as runtime-tagged, it 
 * could be used by both the left camera and the right camera, and this Message
 * could be filterred by TIPS depending on its origin (left vs right) by associate
 * different tags to the Message runtime instances
 * 
 **/

/* LOGIC (concept) VIEW
                                                                                  +--------+
                                                                                  |Node    |
                                                          XXXXXXXXXXX             |        |
    +--------------------+                          XXXXXX           XXXXX        +--+-----+
    | Node               |     Nodes join         XX                      XXXXXX     |
    |an OS thread        +---------------------+XX                              +----+
    +--------------------+  same domain receive X           domain               X
    | (static)           |      pub / sub       X                                +------+
    | callback(Msg1)     |       messges.        X                              X       |
    | ...                |   pub can happen       XXXXX+XX      XXXXXXXXXXXXXXXX    +---+----+
    | callback(MsgN)     |  anywhere through the       |  XXXXXX                    |Node    |
    |                    |   domain handle             |                            |        |
    +--------------------+                             |                            +--------+
    |                    |                       +-----+--+
    |  timers callbacks  |                       |Nodes   +-+
    |  (dynamic)         |                       |pooled  | +-+
    |                    |                       +--------+ | |
    |                    |                            +-----+ |
    |                    |                                +---+
    +--------------------+

   PHYSICAL (deployment) VIEW
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
