#include "hmbdc/Copyright.hpp"
#pragma once

#include "hmbdc/MetaUtils.hpp"

namespace hmbdc { namespace tips {
namespace tips_detail {
template <typename Node, typename MessageTuple, typename IgnoredMessagesTuple> 
struct printForNode {
    void operator()(bool isSub, std::ostream & os){}
};

template <typename Node, typename Message, typename ...Messages, typename IgnoredMessagesTuple> 
struct printForNode<Node, std::tuple<Message, Messages...>, IgnoredMessagesTuple> {
    void operator()(bool isSub, std::ostream & os) {
        if constexpr (!is_in_tuple_v<Message, IgnoredMessagesTuple>) {
            os << '"' << type_name<Node>() << "\" [shape=ellipse];\n" 
                << '"' << type_name<Message>() << "\" [shape=note];\n";
            if (!isSub) {
                os << '"' << type_name<Node>() << "\" -> " 
                    << '"' << type_name<Message>() << "\";"  << std::endl;
            } else {
                os << '"' << type_name<Message>() << "\" -> " 
                    << '"' << type_name<Node>() << "\";"  << std::endl;
            }
        }
        printForNode<Node, std::tuple<Messages...>, IgnoredMessagesTuple>{}(isSub, os);
    }
};

template <typename NodeTuple, typename IgnoredMessagesTuple> 
struct printDot {
    void operator() (std::ostream & os, std::set<std::string> const& ignoreNodeNames) {}
};

template <typename Node, typename ...Nodes, typename IgnoredMessagesTuple>
struct printDot<std::tuple<Node, Nodes...>, IgnoredMessagesTuple> {
    void operator ()(std::ostream & os, std::set<std::string> const& ignoreNodeNames) {
        if (ignoreNodeNames.find(std::string(type_name<Node>())) == ignoreNodeNames.end()) {
            using Subs = typename Node::RecvMessageTuple;
            using Pubs = typename Node::SendMessageTuple;
            printForNode<Node, Subs, IgnoredMessagesTuple>{}(true, os);
            printForNode<Node, Pubs, IgnoredMessagesTuple>{}(false, os);
            printDot<std::tuple<Nodes...>, IgnoredMessagesTuple>{}(os, ignoreNodeNames);
        }
    }
};
}    // tips_detail

/**
 * @brief output a group of Node's pub sub topology in dot file
 * format - you can use dot or online sites to diplay it graphically
 * 
 * @tparam NodesTuple std tuple of Nodes 
 * @tparam IgnoredMessagesTuple std tuple of ignored message types
 * @param name dot graph name - cannot contain special char like /
 * @param os send output to this stream
 * @param IgnoredMessagesTuple set of Message to exclude from the diagram
 */
template <typename NodeTuple, typename IgnoredMessagesTuple = std::tuple<>>
void printNodePubSubDot(std::string const& name, std::ostream & os
    , std::set<std::string> const& ignoreNodeNames = std::set<std::string>{}) {
    os << "digraph " << name << " {\n";
    tips_detail::printDot<NodeTuple, IgnoredMessagesTuple>{}(os, ignoreNodeNames);
    os << "}\n";
}
}}
