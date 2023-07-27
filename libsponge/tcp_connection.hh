#ifndef SPONGE_LIBSPONGE_TCP_FACTORED_HH
#define SPONGE_LIBSPONGE_TCP_FACTORED_HH

#include "tcp_config.hh"
#include "tcp_receiver.hh"
#include "tcp_sender.hh"
#include "tcp_state.hh"

//! \brief A complete endpoint of a TCP connection
class TCPConnection {
  private:
    TCPConfig _cfg;
    TCPReceiver _receiver{_cfg.recv_capacity};
    TCPSender _sender{_cfg.send_capacity, _cfg.rt_timeout, _cfg.fixed_isn};
    /**
     * @attention _sender的ByteStream对应outBoundStream
     * @attention _receiver对应的是inBoundStream
    */

    //! outbound queue of segments that the TCPConnection wants sent
    std::queue<TCPSegment> _segments_out{};

    //! Should the TCPConnection stay active (and keep ACKing)
    //! for 10 * _cfg.rt_timeout milliseconds after both streams have ended,
    //! in case the remote TCPConnection doesn't know we've received its whole stream?
    bool _linger_after_streams_finish{true};

    //当前是否处于徘徊状态？
    bool _lingering{false};

    //接收到上个报文段时过去了多久？
    size_t _time_since_last_segrecv{0};

    //使用状态变量来维护是否保持_active的状态
    //由于初始读入和读出字节流都在运行，因此初始是true
    bool _active{true};

  public:
    //! \name "Input" interface for the writer
    //!@{

    //! \brief Initiate a connection by sending a SYN segment
    void connect();

    //! \brief Write data to the outbound byte stream, and send it over TCP if possible
    //! \returns the number of bytes from `data` that were actually written.
    size_t write(const std::string &data);

    //! \returns the number of `bytes` that can be written right now.
    //! ALERT!::此函数的实现可能是错误的！
    size_t remaining_outbound_capacity() const;

    //! \brief Shut down the outbound byte stream (still allows reading incoming data)
    void end_input_stream();
    //!@}

    //! \name "Output" interface for the reader
    //!@{

    //! \brief The inbound byte stream received from the peer
    ByteStream &inbound_stream() { return _receiver.stream_out(); }
    //!@}

    //! \name Accessors used for testing

    //!@{
    //! \brief number of bytes sent and not yet acknowledged, counting SYN/FIN each as one byte
    size_t bytes_in_flight() const;
    //! \brief number of bytes not yet reassembled
    size_t unassembled_bytes() const;
    //! \brief Number of milliseconds since the last segment was received
    size_t time_since_last_segment_received() const;
    //!< \brief summarize the state of the sender, receiver, and the connection
    TCPState state() const { return {_sender, _receiver, active(), _linger_after_streams_finish}; };
    //!@}

    //! \name Methods for the owner or operating system to call
    //!@{

    //! Called when a new segment has been received from the network
    void segment_received(const TCPSegment &seg);

    //! Called periodically when time elapses
    void tick(const size_t ms_since_last_tick);

    //! \brief TCPSegments that the TCPConnection has enqueued for transmission.
    //! \note The owner or operating system will dequeue these and
    //! put each one into the payload of a lower-layer datagram (usually Internet datagrams (IP),
    //! but could also be user datagrams (UDP) or any other kind).
    std::queue<TCPSegment> &segments_out() { return _segments_out; }

    //! \note 经验：采用_active变量直接托管active状态更直观，有3个unclean shudown，2个clean shutdown会结束active
    //! \brief Is the connection still alive in any way?
    //! \returns `true` if either stream is still running or if the TCPConnection is lingering
    //! after both streams have finished (e.g. to ACK retransmissions from the peer)
    bool active() const;
    //!@}

    //! Construct a new connection from a configuration
    explicit TCPConnection(const TCPConfig &cfg) : _cfg{cfg} {}

    /**
     * 此函数用于对TCPSender中的报文段加入ackno和window_size的信息
     * 本函数会直接对sender_segment进行修改，因此传入引用
     * @param sender_segment 表示从TCPSender中得到的报文段
    */
    void handle_sender_segment(TCPSegment& sender_segment);

    /**
     * 此函数之作用在于将TCPSender待发送队列中的[所有]报文段都
     * 抽出来并进行封装，然后再通过TCPConnection发送出去
    */
    void send_all();

    /**
     * 此函数用于处理收到或者发送RST报文段的情况
     * @param rst_sent 是否发送ret报文段
    */
    void unclean_shutdown(bool rst_sent = true);

    /**
     * 此函数的作用在于：检测clean shutdown的三个先决条件是否得到满足？
     * 并判断是否需要linger
     * @return 如果prereq1 && prereq2 && prereq3，即三个条件皆满足则返回
     * true，否则返回false。此外，该函数还会在满足条件的情况下将
     * _linger_after_streams_finish的值置为false
    */
    bool follow_prereq();

    //! \name construction and destruction
    //! moving is allowed; copying is disallowed; default construction not possible

    //!@{
    ~TCPConnection();  //!< destructor sends a RST if the connection is still open
    TCPConnection() = delete;
    TCPConnection(TCPConnection &&other) = default;
    TCPConnection &operator=(TCPConnection &&other) = default;
    TCPConnection(const TCPConnection &other) = delete;
    TCPConnection &operator=(const TCPConnection &other) = delete;
    //!@}
};

#endif  // SPONGE_LIBSPONGE_TCP_FACTORED_HH
