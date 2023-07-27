#ifndef SPONGE_LIBSPONGE_TCP_RECEIVER_HH
#define SPONGE_LIBSPONGE_TCP_RECEIVER_HH

#include "byte_stream.hh"
#include "stream_reassembler.hh"
#include "tcp_helpers/tcp_segment.hh"
#include "wrapping_integers.hh"

#include <optional>

//! \brief The "receiver" part of a TCP implementation.

//! Receives and reassembles segments into a ByteStream, and computes
//! the acknowledgment number and window size to advertise back to the
//! remote TCPSender.
class TCPReceiver {
    //! Our data structure for re-assembling bytes.
    StreamReassembler _reassembler;

    //! The maximum number of bytes we'll store.
    size_t _capacity;

    //接收来自TCPSender的初始序号
    std::optional<WrappingInt32> _isn{};
    std::optional<WrappingInt32> _ackno{};


    /**
     * _abs_first_unassembled与_first_unassembled的区别在于二者相差1
     * 加_abs的计入FIN和SYN；不加_abs的不计FIN和SYN，与StreamReassembler中的指标一致
     * 如果尚未收到isn(SYN)，则此时认为期待第一个收到的字节序列是0
     * 在一定范围内：_abs_first_unassembled = _first_unassembled + 1;
    */
    uint64_t get_abs_first_unassembled() {
      if (!_isn.has_value() || !_ackno.has_value()) { return 0; }
      return unwrap(_ackno.value(), _isn.value(), _reassembler.get_first_unassembled()); 
    }

    //_abs_first_unacceptable和_first_unacceptable的区别在于二者相差1
    uint64_t get_abs_first_unacceptable() {
      return get_abs_first_unassembled() + window_size();
    }

  public:
    //! \brief Construct a TCP receiver
    //!
    //! \param capacity the maximum number of bytes that the receiver will
    //!                 store in its buffers at any give time.
    TCPReceiver(const size_t capacity) : _reassembler(capacity), _capacity(capacity) {}

    //! \name Accessors to provide feedback to the remote TCPSender
    //!@{

    //! \brief The ackno that should be sent to the peer
    //! \returns empty if no SYN has been received
    //!
    //! This is the beginning of the receiver's window, or in other words, the sequence number
    //! of the first byte in the stream that the receiver hasn't received.
    std::optional<WrappingInt32> ackno() const;

    //! \brief The window size that should be sent to the peer
    //!
    //! Operationally: the capacity minus the number of bytes that the
    //! TCPReceiver is holding in its byte stream (those that have been
    //! reassembled, but not consumed).
    //!
    //! Formally: the difference between (a) the sequence number of
    //! the first byte that falls after the window (and will not be
    //! accepted by the receiver) and (b) the sequence number of the
    //! beginning of the window (the ackno).
    //！ 注意：_reassembler中的失序字节流被储存在_reassembler自己的缓存中
    //！ 完全不需要考虑其容量大小，只需要考虑inbound bytestream中缓存的字节
    size_t window_size() const;
    //!@}

    //! \brief number of bytes stored but not yet reassembled
    size_t unassembled_bytes() const { return _reassembler.unassembled_bytes(); }

    //! \brief handle an inbound segment
    void segment_received(const TCPSegment &seg);

    //! \name "Output" interface for the reader
    //!@{
    ByteStream &stream_out() { return _reassembler.stream_out(); }
    const ByteStream &stream_out() const { return _reassembler.stream_out(); }
    //!@}
};

   /**
     * std::optional简易教程：
     * 对于optional<int> test;
     * test.has_value()
     * test.value()
     * test.emplace() 该方法用来赋值，也可以使用等号赋值
    */

#endif  // SPONGE_LIBSPONGE_TCP_RECEIVER_HH
