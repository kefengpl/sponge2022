#include "network_interface.hh"

#include "arp_message.hh"
#include "ethernet_frame.hh"

#include <iostream>

// Dummy implementation of a network interface
// Translates from {IP datagram, next hop address} to link-layer frame, and from link-layer frame to IP datagram

// For Lab 5, please replace with a real implementation that passes the
// automated checks run by `make check_lab5`.

// You will need to add private members to the class declaration in `network_interface.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

//! \param[in] ethernet_address Ethernet (what ARP calls "hardware") address of the interface
//! \param[in] ip_address IP (what ARP calls "protocol") address of the interface
NetworkInterface::NetworkInterface(const EthernetAddress &ethernet_address, const Address &ip_address)
    : _ethernet_address(ethernet_address), _ip_address(ip_address) {
    cerr << "DEBUG: Network interface has Ethernet address " << to_string(_ethernet_address) << " and IP address "
         << ip_address.ip() << "\n";
}

//! \param[in] dgram the IPv4 datagram to be sent
//! \param[in] next_hop the IP address of the interface to send it to (typically a router or default gateway, but may also be another host if directly connected to the same network as the destination)
//! (Note: the Address type can be converted to a uint32_t (raw 32-bit IP address) with the Address::ipv4_numeric() method.)
void NetworkInterface::send_datagram(const InternetDatagram &dgram, const Address &next_hop) {
    // convert IP address of next hop to raw 32-bit representation (used in ARP header)
    const uint32_t next_hop_ip = next_hop.ipv4_numeric();
    
    //If the destination Ethernet address is already known, send it right away.
    if (_ip_map.find(next_hop_ip) != _ip_map.end()) {
        //将ip数据报报装成链路层帧后发送
        send_frame(dgram, _ip_map[next_hop_ip].first);
    } else {
        /*If the destination Ethernet address is unknown, broadcast an ARP request for the
        next hop’s Ethernet address, and queue the IP datagram so it can be sent after
        the ARP reply is received*/
        //将尚未找到dst MAC的IP数据报存入待发队列
        _frames_waited.push(pair<InternetDatagram, Address>(dgram, next_hop));
        //如果5s之内(5000ms)对该目标IP发送了ARP分组，则不再发送ARP分组
        if (_ARP_timer.find(next_hop_ip) != _ARP_timer.end()) return;
        //将ARP分组包装后广播出去
        send_ARP(ARPMessage::OPCODE_REQUEST,
                 ETHERNET_BROADCAST, next_hop_ip);
        //设置该ARP对应目标IP分组发送的计时
        _ARP_timer[next_hop_ip] = 0;
    }
}

//! \param[in] frame the incoming Ethernet frame
optional<InternetDatagram> NetworkInterface::recv_frame(const EthernetFrame &frame) {
    optional<InternetDatagram> parse_result{};
    //对于目标MAC地址不符的，直接丢弃该链路层帧
    if (frame.header().dst != _ethernet_address && frame.header().dst != ETHERNET_BROADCAST) return {};
    //如果里面包含的是IPv4数据报，则抽取该IP数据报
    if (frame.header().type == EthernetHeader::TYPE_IPv4) {
        InternetDatagram ip_datagram{};
        if (ip_datagram.parse(Buffer(frame.payload().concatenate())) == ParseResult::NoError) {
            parse_result = ip_datagram;
        } 
        return parse_result; 
    }
    //如果里面包含的是ARP分组，则抽取该分组，并将该映射关系记录30s
    if (frame.header().type == EthernetHeader::TYPE_ARP) {
        ARPMessage arp_message{};
        if (arp_message.parse(Buffer(frame.payload().concatenate())) == ParseResult::NoError) {
            //cout << arp_message.sender_ip_address << endl;
            //cout << _frames_waited.front().second.ipv4_numeric() << endl;
            //记录发送方IP地址和MAC地址的映射关系
           // _ip_map[Address::from_ipv4_numeric(arp_message.sender_ip_address)] = 
           //     pair<EthernetAddress, size_t>{arp_message.sender_ethernet_address, 0UL};
            _ip_map[arp_message.sender_ip_address] = 
                    pair<EthernetAddress, size_t>{arp_message.sender_ethernet_address, 0UL};
            // if it’s an ARP request asking for our IP address, send an appropriate ARP reply.
            //! \bug 注意，在ETHERNET_BROADCAST的情况下，还需要ARP中的target IP是本机IP
            if (arp_message.opcode == ARPMessage::OPCODE_REQUEST && 
                arp_message.target_ip_address == _ip_address.ipv4_numeric()) {
                //发送回应报文段，以告知发送方请求IP的MAC地址
                send_ARP(ARPMessage::OPCODE_REPLY,  
                         arp_message.sender_ethernet_address, arp_message.sender_ip_address);
            }
            //考察添加ip--mac映射后，队列中的待发送数据报是否能被发出
            while (!_frames_waited.empty()) {
                if (_ip_map.find(_frames_waited.front().second.ipv4_numeric()) != _ip_map.end()) {
                    send_frame(_frames_waited.front().first, 
                               _ip_map[_frames_waited.front().second.ipv4_numeric()].first);  
                    _frames_waited.pop();  
                } else {
                    break;
                }
            }
        }    
    }
    return parse_result;
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void NetworkInterface::tick(const size_t ms_since_last_tick) { 
    //! \bug 注意map如何遍历删除元素！如果使用普通方法会报错
    //! \bug 普通方法：for (auto it = _ip_map.begin(); it != _ip_map.end();++it)
    //首先更新ip--mac映射表中记录存留的时间
    for (auto it = _ip_map.begin(); it != _ip_map.end();) {
        it->second.second += ms_since_last_tick;
        //erase会返回删除元素的下一个元素的迭代器
        //如果某个记录存在时间超过30s，就把它删除
        if (it->second.second > MAPPING_TIME_LIMIT) it = _ip_map.erase(it);
        else ++it;
    }
    //其次更新某个ip对应的ARP发布已经过去的时间
    for (auto it = _ARP_timer.begin(); it != _ARP_timer.end();) {
        it->second += ms_since_last_tick;   
        //如果某个ARP发送时间已经超过5s，就将它删除
        if (it->second > ARP_TIME) it = _ARP_timer.erase(it);     
        else ++it;   
    }
 }

void NetworkInterface::send_ARP(uint16_t opcode, 
                                EthernetAddress target_ethernet_address, 
                                uint32_t target_ip_address) {
    //创建请求ARP，请求下一跳IP地址对应的MAC地址
    ARPMessage arp_message;
    arp_message.opcode = opcode;
    arp_message.sender_ethernet_address = _ethernet_address;
    arp_message.sender_ip_address = _ip_address.ipv4_numeric();
    if (opcode == ARPMessage::OPCODE_REPLY) {
        arp_message.target_ethernet_address = target_ethernet_address;    
    }
    arp_message.target_ip_address = target_ip_address; 

    //设置链路层帧首部字节
    EthernetHeader linkframe_header{};
    linkframe_header.dst = target_ethernet_address;
    linkframe_header.src = _ethernet_address;
    linkframe_header.type = EthernetHeader::TYPE_ARP;

    //Frame header + ARP message的序列化，创立buffer
    Buffer serialized_frame(linkframe_header.serialize() + arp_message.serialize());
    //解析该序列，得到structure 链路层帧
    EthernetFrame ethernet_frame{};
    ethernet_frame.parse(serialized_frame);
    //把ARP message广播/发送出去
    _frames_out.push(ethernet_frame);

}

void NetworkInterface::send_frame(const InternetDatagram &dgram, EthernetAddress dst) {
    //设置链路层帧
    EthernetHeader linkframe_header{};
    linkframe_header.dst = dst;
    linkframe_header.src = _ethernet_address;
    linkframe_header.type = EthernetHeader::TYPE_IPv4;

    //设置序列化的 链路层首部字段 + IP数据报
    Buffer serialized_frame(linkframe_header.serialize() + dgram.serialize().concatenate());
    //解析该序列，得到structure 链路层帧
    EthernetFrame ethernet_frame{};
    ethernet_frame.parse(serialized_frame);
    //将得到的链路层帧发送出去
    _frames_out.push(ethernet_frame);    
}

