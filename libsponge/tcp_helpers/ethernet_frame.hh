#ifndef SPONGE_LIBSPONGE_ETHERNET_FRAME_HH
#define SPONGE_LIBSPONGE_ETHERNET_FRAME_HH

#include "buffer.hh"
#include "ethernet_header.hh"

//! \brief Ethernet frame
class EthernetFrame {
  private:
    EthernetHeader _header{};
    BufferList _payload{};

  public:
    //! \brief Parse the frame from a string
    //! \note 将string解析为EthernetFrame类
    ParseResult parse(const Buffer buffer);

    //! \brief Serialize the frame to a string
    //! \note 将该链路层帧序列化为string
    BufferList serialize() const;

    //! \name Accessors
    //!@{
    const EthernetHeader &header() const { return _header; }
    EthernetHeader &header() { return _header; }

    const BufferList &payload() const { return _payload; }
    BufferList &payload() { return _payload; }
    //!@}
};

#endif  // SPONGE_LIBSPONGE_ETHERNET_FRAME_HH
