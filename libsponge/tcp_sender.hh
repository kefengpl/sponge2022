#ifndef SPONGE_LIBSPONGE_TCP_SENDER_HH
#define SPONGE_LIBSPONGE_TCP_SENDER_HH

#include "byte_stream.hh"
#include "tcp_config.hh"
#include "tcp_segment.hh"
#include "wrapping_integers.hh"

#include <functional>
#include <queue>

/**
 * Timer类，即TCPSender中的“计时器”
 * 一般而言，TCP协议建议仅采用一个计时器
*/
class Timer {
  private:
    unsigned int _RTO; //标记本次启动计时器设定的_RTO
    bool _active; //Timer是否启动？
    unsigned int _time_passed; //从Timer启动到多次调用tick之后到底经过了多久？
    bool _expired; //判断计时器是否记时结束
  public:
    //构造函数
    Timer () : _RTO{0}, _active{false}, _time_passed{0}, _expired{false} {} 

    /**
     * 停止计时器，由于各种状态会在启动计时器后刷新
     * 因此这里只需要把_active的状态设置为false
    */
    void stop_timer() { _active = false; }

    /**
     * 此函数用于返回该函数是否处于关闭状态
    */
    bool timer_closed() { return _active == false; }

    /**
     * 此函数的作用是重启计时器并刷新全部状态，又名“重启”
     * @param RTO 表示此次启动计时器希望的倒计时时限是多少 
    */
    void start_timer(unsigned int RTO) {
      _RTO = RTO; _active = true; _time_passed = 0; _expired = false;
    }

    /**
     * 此函数用于提示该计时器时间的流逝
     * 该函数几毫秒就会被调用一次
     * 如果计时器没有运行或者计时结束则不再记录时间流逝
    */
    void tick(const size_t ms_since_last_tick) {
      if (!_active) return;
      if (_expired) return;
      _time_passed += ms_since_last_tick;  
      if (_time_passed >= _RTO) _expired = true;
    }

    /**
     * 此函数用于判断计时器是不是计时结束了
     * 一般在某次tick调用结束后紧随调用timer_expired
    */
   bool timer_expired() { return _expired; }
};


//! \brief The "sender" part of a TCP implementation.

//! Accepts a ByteStream, divides it up into segments and sends the
//! segments, keeps track of which segments are still in-flight,
//! maintains the Retransmission Timer, and retransmits in-flight
//! segments if the retransmission timer expires.
class TCPSender {
  private:
    //! our initial sequence number, the number for our SYN.
    WrappingInt32 _isn;

    //! outbound queue of segments that the TCPSender wants sent
    std::queue<TCPSegment> _segments_out{};

    //该数据结构用于暂存那些发出，还未确认的报文段，以用于今后超时重传
    std::queue<TCPSegment> _segments_backup{};

    //! retransmission timer for the connection
    unsigned int _initial_retransmission_timeout;

    //记录当前的重传时限
    unsigned int _retransmission_timeout;

    //记录连续重传的次数[一般认为报文段首次发送不算作重传]
    unsigned int _consecutive_retransmissions{0};

    //! outgoing stream of bytes that have not yet been sent
    ByteStream _stream;

    //! the (absolute) sequence number for the next byte to be sent
    uint64_t _next_seqno{0};

    //收到TCPReceiver的绝对序列号，一般初始是0，收到SYNACK(无负载)后变为1
    uint64_t _abs_ackno{0};

    //收到的窗口大小。我们规定：当尚未收到来自对方的SYNACK时，假设窗口大小是1
    //接收窗口的范围也是发送的范围，具体是：[_abs_ackno, _abs_ackno + _window_size)
    uint16_t _window_size{1};
    
    //作为发送方，SYN比特是否发送出去了？
    bool _SYN_sent{false};
    //作为发送方，FIN比特是否发送出去了？
    bool _FIN_sent{false};
    //单个计时器
    Timer timer{}; 

  public:
    //! Initialize a TCPSender
    TCPSender(const size_t capacity = TCPConfig::DEFAULT_CAPACITY,
              const uint16_t retx_timeout = TCPConfig::TIMEOUT_DFLT,
              const std::optional<WrappingInt32> fixed_isn = {});

    //! \name "Input" interface for the writer
    //!@{
    ByteStream &stream_in() { return _stream; }
    const ByteStream &stream_in() const { return _stream; }
    //!@}

    //! \name Methods that can cause the TCPSender to send a segment
    //!@{

    //! \brief A new acknowledgment was received
    void ack_received(const WrappingInt32 ackno, const uint16_t window_size);

    /**
     * \brief Generate an empty-payload segment (useful for creating empty ACK segments)
     * @param rst_set 是否设置RST比特位？[此功能为TCPConnection设计] 为了和旧版本兼容，默认参数设为false
    */
    void send_empty_segment(bool rst_set = false);

    //! \brief create and send segments to fill as much of the window as possible
    void fill_window();

    //! \brief Notifies the TCPSender of the passage of time
    void tick(const size_t ms_since_last_tick);
    //!@}

    //! \name Accessors
    //!@{

    //! \brief How many sequence numbers are occupied by segments sent but not yet acknowledged?
    //! \note count is in "sequence space," i.e. SYN and FIN each count for one byte
    //! (see TCPSegment::length_in_sequence_space())
    size_t bytes_in_flight() const;

    //! \brief Number of consecutive retransmissions that have occurred in a row
    unsigned int consecutive_retransmissions() const;

    //! \brief TCPSegments that the TCPSender has enqueued for transmission.
    //! \note These must be dequeued and sent by the TCPConnection,
    //! which will need to fill in the fields that are set by the TCPReceiver
    //! (ackno and window size) before sending.
    std::queue<TCPSegment> &segments_out() { return _segments_out; }
    //!@}

    //! \name What is the next sequence number? (used for testing)
    //!@{

    //! \brief absolute seqno for the next byte to be sent
    uint64_t next_seqno_absolute() const { return _next_seqno; }

    //! \brief relative seqno for the next byte to be sent
    WrappingInt32 next_seqno() const { return wrap(_next_seqno, _isn); }
    //!@}


    /**
     * 为了便于处理，我们设置一个从发送方制作报文段的函数
    */
    TCPSegment make_segment(uint64_t abs_seqno, bool syn, bool fin, std::string& payload);

    /**
     * 此函数的作用：检查更新后的绝对ackno下，某个报文段是否
     * 已经被完全确认收到，即_abs_ackno是否大于该报文段的所有
     * 字节的序列号
    */
    bool check_seg_acked(const TCPSegment& seg);

    /**
     *接口函数：FIN比特是否被发送了出去？
     *如果被发送出去了，说明所有内容都被发出去了
    */
    bool fin_sent() { return _FIN_sent; }

    /**
     * 接口函数：返回_SYN_sent
    */
    bool syn_sent() { return _SYN_sent; }
};

#endif  // SPONGE_LIBSPONGE_TCP_SENDER_HH
