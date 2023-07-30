#ifndef SPONGE_LIBSPONGE_NETWORK_INTERFACE_HH
#define SPONGE_LIBSPONGE_NETWORK_INTERFACE_HH

#include "ethernet_frame.hh"
#include "tcp_over_ip.hh"
#include "tun.hh"

#include <optional>
#include <queue>
#include <map>

//! \brief A "network interface" that connects IP (the internet layer, or network layer)
//! with Ethernet (the network access layer, or link layer).

//! This module is the lowest layer of a TCP/IP stack
//! (connecting IP with the lower-layer network protocol,
//! e.g. Ethernet). But the same module is also used repeatedly
//! as part of a router: a router generally has many network
//! interfaces, and the router's job is to route Internet datagrams
//! between the different interfaces.

//! The network interface translates datagrams (coming from the
//! "customer," e.g. a TCP/IP stack or router) into Ethernet
//! frames. To fill in the Ethernet destination address, it looks up
//! the Ethernet address of the next IP hop of each datagram, making
//! requests with the [Address Resolution Protocol](\ref rfc::rfc826).
//! In the opposite direction, the network interface accepts Ethernet
//! frames, checks if they are intended for it, and if so, processes
//! the the payload depending on its type. If it's an IPv4 datagram,
//! the network interface passes it up the stack. If it's an ARP
//! request or reply, the network interface processes the frame
//! and learns or replies as necessary.
class NetworkInterface {
  private:
    //! Ethernet (known as hardware, network-access-layer, or link-layer) address of the interface
    EthernetAddress _ethernet_address;

    //! IP (known as internet-layer or network-layer) address of the interface
    Address _ip_address;

    //! outbound queue of Ethernet frames that the NetworkInterface wants sent
    std::queue<EthernetFrame> _frames_out{};

    //! \brief 记录了没有MAC地址待发的IP数据报，需要记录next_hop
    std::queue<std::pair<InternetDatagram, Address>> _frames_waited{};

    //! \brief ip to MAC地址的映射，并记录存续时间[如果>=30s，就删除记录]
    //! \bug 需要注意的细节：IPV4地址需要用uint32_t表示，否则无法查找Address
    std::map<uint32_t, std::pair<EthernetAddress, size_t>> _ip_map{};

    //! \brief 记录某个ip对应的ARP发布已经过去的时间
    //! \bug 需要注意的细节：IPV4地址需要用uint32_t表示，否则无法查找Address
    std::map<size_t, size_t> _ARP_timer{};

  public:
    //! \brief Construct a network interface with given Ethernet (network-access-layer) and IP (internet-layer) addresses
    NetworkInterface(const EthernetAddress &ethernet_address, const Address &ip_address);

    //! \brief Access queue of Ethernet frames awaiting transmission
    std::queue<EthernetFrame> &frames_out() { return _frames_out; }

    //! \brief Sends an IPv4 datagram, encapsulated in an Ethernet frame (if it knows the Ethernet destination address).

    //! Will need to use [ARP](\ref rfc::rfc826) to look up the Ethernet destination address for the next hop
    //! ("Sending" is accomplished by pushing the frame onto the frames_out queue.)
    void send_datagram(const InternetDatagram &dgram, const Address &next_hop);

    //! \brief Receives an Ethernet frame and responds appropriately.

    //! If type is IPv4, returns the datagram.
    //! If type is ARP request, learn a mapping from the "sender" fields, and send an ARP reply.
    //! If type is ARP reply, learn a mapping from the "sender" fields.
    std::optional<InternetDatagram> recv_frame(const EthernetFrame &frame);

    //! \brief Called periodically when time elapses
    void tick(const size_t ms_since_last_tick);

    //! \brief 发送ARP FRAME
    void send_ARP(uint16_t opcode,
                  EthernetAddress target_ethernet_address, 
                  uint32_t target_ip_address);
    
    //! \brief 发送链路层帧
    void send_frame(const InternetDatagram &dgram, EthernetAddress dst);

    //映射表中每个记录存在是时间上限是30s，即300000ms
    size_t MAPPING_TIME_LIMIT = 30 * 1000;
    //一个IP ARP发布5s之内不允许
    size_t ARP_TIME = 5 * 1000;
};

#endif  // SPONGE_LIBSPONGE_NETWORK_INTERFACE_HH
