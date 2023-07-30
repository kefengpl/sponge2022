#include "router.hh"

#include <iostream>

using namespace std;

// Dummy implementation of an IP router

// Given an incoming Internet datagram, the router decides
// (1) which interface to send it out on, and
// (2) what next hop address to send it to.

// For Lab 6, please replace with a real implementation that passes the
// automated checks run by `make check_lab6`.

// You will need to add private members to the class declaration in `router.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

//! \param[in] route_prefix The "up-to-32-bit" IPv4 address prefix to match the datagram's destination address against
//! \param[in] prefix_length For this route to be applicable, how many high-order (most-significant) bits of the route_prefix will need to match the corresponding bits of the datagram's destination address?
//! \param[in] next_hop The IP address of the next hop. Will be empty if the network is directly attached to the router (in which case, the next hop address should be the datagram's final destination).
//! \param[in] interface_num The index of the interface to send the datagram out on.
void Router::add_route(const uint32_t route_prefix,
                       const uint8_t prefix_length,
                       const optional<Address> next_hop,
                       const size_t interface_num) {
    cerr << "DEBUG: adding route " << Address::from_ipv4_numeric(route_prefix).ip() << "/" << int(prefix_length)
         << " => " << (next_hop.has_value() ? next_hop->ip() : "(direct)") << " on interface " << interface_num << "\n";

    // Your code here.
    _routing_table.push_back(RouteTableEntry(route_prefix, prefix_length, next_hop, interface_num));
}

//! \param[in] dgram The datagram to be routed
void Router::route_one_datagram(InternetDatagram &dgram) {
    //ttl自减1判断是否是0，如果是，则丢弃数据报
    if (dgram.header().ttl == 0) return;
    if (--dgram.header().ttl == 0) return;
    //获得数据报的目标IP地址，以32位数字表示
    uint32_t dst_ip = dgram.header().dst;
    uint8_t max_prelen = 0; //记录哪个表项与dst_ip相匹配的的prefix_length最长？
    RouteTableEntry matched_entry{}; //记录最匹配的表项
    bool find_match{false};
    //遍历转发表，查找哪一项的前缀和dst_ip的前缀匹配
    for (const auto& entry : _routing_table) {
        //! \bug 特殊情况：_prefix_length前缀为0的情况属于通配情况
        //! \bug 不要让uint32算术右移32位，这会造成无法预知的错误
        if (entry._prefix_length == 0 && (max_prelen <= entry._prefix_length)) {
            find_match = true;
            max_prelen = entry._prefix_length;
            matched_entry = entry;
            continue;            
        }
        //检查两个数的前缀是否匹配
        if ((entry._route_prefix >> (32 - entry._prefix_length)) == 
            (dst_ip >> (32 - entry._prefix_length)) && (max_prelen <= entry._prefix_length)) { 
            find_match = true;
            max_prelen = entry._prefix_length;
            matched_entry = entry;
        }
    }
    //如果匹配失败，没有找到对应的表项，就丢弃该数据报
    if (!find_match) return;
    //匹配成功，就转发该数据报
    _interfaces[matched_entry._interface_num].send_datagram(dgram, 
        matched_entry._next_hop.has_value() ? matched_entry._next_hop.value() : Address::from_ipv4_numeric(dst_ip));
}

void Router::route() {
    // Go through all the interfaces, and route every incoming datagram to its proper outgoing interface.
    for (auto &interface : _interfaces) {
        auto &queue = interface.datagrams_out();
        while (not queue.empty()) {
            route_one_datagram(queue.front());
            queue.pop();
        }
    }
}
