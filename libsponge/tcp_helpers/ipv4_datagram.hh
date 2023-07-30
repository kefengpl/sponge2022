#ifndef SPONGE_LIBSPONGE_IPV4_DATAGRAM_HH
#define SPONGE_LIBSPONGE_IPV4_DATAGRAM_HH

#include "buffer.hh"
#include "ipv4_header.hh"

//! \brief [IPv4](\ref rfc::rfc791) Internet datagram
class IPv4Datagram {
  private:
    IPv4Header _header{};
    BufferList _payload{};

  public:
    //! \brief Parse the segment from a string
    //! \note 解析是指将字节流解析成struct IPv4Datagram
    ParseResult parse(const Buffer buffer);

    //! \brief Serialize the segment to a string
    //! \note 序列化指将 该数据报(datagram)struct形式解析成字节流(string)
    BufferList serialize() const;

    //! \name Accessors
    //!@{
    const IPv4Header &header() const { return _header; }
    IPv4Header &header() { return _header; }

    const BufferList &payload() const { return _payload; }
    BufferList &payload() { return _payload; }
    //!@}
};

using InternetDatagram = IPv4Datagram;

#endif  // SPONGE_LIBSPONGE_IPV4_DATAGRAM_HH
