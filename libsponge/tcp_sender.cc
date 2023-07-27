#include "tcp_sender.hh"

#include "tcp_config.hh"

#include <random>
#include <iostream>

// Dummy implementation of a TCP sender

// For Lab 3, please replace with a real implementation that passes the
// automated checks run by `make check_lab3`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

//! \param[in] capacity the capacity of the outgoing byte stream
//! \param[in] retx_timeout the initial amount of time to wait before retransmitting the oldest outstanding segment
//! \param[in] fixed_isn the Initial Sequence Number to use, if set (otherwise uses a random ISN)
TCPSender::TCPSender(const size_t capacity, const uint16_t retx_timeout, const std::optional<WrappingInt32> fixed_isn)
    : _isn(fixed_isn.value_or(WrappingInt32{random_device()()}))
    , _initial_retransmission_timeout{retx_timeout}
    , _retransmission_timeout{retx_timeout}
    , _stream(capacity) {}

uint64_t TCPSender::bytes_in_flight() const { 
    //思路明晰：可用还未发送的字节 - 发送尚未确认的字节
    return _next_seqno - _abs_ackno;
}

TCPSegment TCPSender::make_segment(uint64_t abs_seqno, bool syn, bool fin, string& payload) {
    TCPSegment SYN_segment{};
    SYN_segment.header().seqno = wrap(abs_seqno, _isn);
    SYN_segment.header().syn = syn;   
    SYN_segment.header().fin = fin; 
    if (payload.length() != 0) SYN_segment.payload() = Buffer(move(payload));
    return SYN_segment;
}

void TCPSender::fill_window() {
    //初始处理：假设窗口大小是1，则它只能发送SYN报文段
    //还需要把该报文段推入一个备用以待重发的数据结构
    //在发送方，我们需要关注：Seqno, SYN, FIN, Payload这四个元素
    if (!_SYN_sent) {
        string payload = "";
        TCPSegment SYN_segment = make_segment(_next_seqno, true, false, payload); 
        _segments_out.push(SYN_segment); 
        _segments_backup.push(SYN_segment);
        if (timer.timer_closed()) timer.start_timer(_retransmission_timeout);
        _next_seqno += SYN_segment.length_in_sequence_space(); 
        _SYN_sent = true; //BUG0:忘记设置SYN_sent比特位为真
        return;
    }

    //考虑其它的情况，即它需要充满WINDOW_SIZE
    //注意：此处需要填充窗口的大小(fill_size) = 收到的TCPReceiver窗口大小 - 发送还未确认的字节大小
    //另一个需要注意的细节：如果_window_size == 0，那么要把它当成1
    //其它细节：这里需要写一个循环！为什么？每次我们的最大报文段长度是1452，如果窗口很大，且有多个
    //报文段需要读取的情况下，必然需要使用循环语句
    uint16_t fill_size = max(static_cast<uint64_t>(_window_size), 1UL) > bytes_in_flight() ? 
                         max(static_cast<uint64_t>(_window_size), 1UL) - bytes_in_flight() : 0;
    
    //循环结束的两个条件：fill_size == 0 或者 没有信息需要获取
    //注意BUG：一旦发出FIN信号，就必须结束任何发送！
    while (fill_size > 0 && !_FIN_sent && _SYN_sent) {
        //BUG：报文段负载的最大长度不能超过1452，尝试充满整个窗口
        string payload = _stream.read(fill_size > TCPConfig::MAX_PAYLOAD_SIZE ? 
                                    TCPConfig::MAX_PAYLOAD_SIZE : fill_size);
        //_stream.eof()：我们将之当成一个普通的字节即可
        //fill_size > payload.size()表示窗口空间是否留有FIN比特的一席之地？
        TCPSegment segment = make_segment(_next_seqno, false, 
                                          fill_size > payload.size() ? _stream.eof() : false, payload);
        //如果发送了空的报文段，说明没有信息需要获取，直接结束循环即可
        if (segment.length_in_sequence_space() == 0) break;
        //如果报文段不是空报文段，则具有利用价值
        if (segment.header().fin) _FIN_sent = true; //FIN标记被派上用场，标志这发送的结束
        _segments_out.push(segment); 
        _segments_backup.push(segment);
        if (timer.timer_closed()) timer.start_timer(_retransmission_timeout); 
        _next_seqno += segment.length_in_sequence_space();
        fill_size -= segment.length_in_sequence_space();         
    }
}

bool TCPSender::check_seg_acked(const TCPSegment& seg) {
    //获取报文段的绝对序列号
    uint64_t abs_seq = unwrap(seg.header().seqno, _isn, _abs_ackno);
    return abs_seq + seg.length_in_sequence_space() <= _abs_ackno;
}

//! \param ackno The remote receiver's ackno (acknowledgment number)
//! \param window_size The remote receiver's advertised window size
void TCPSender::ack_received(const WrappingInt32 ackno, const uint16_t window_size) { 
    //第一部分：接收来自TCPReceiver的数据 
    //if判断是为了实现《自顶向下》中的逻辑：(y > SendBase)
    //注意测试用例：Impossible ackno (beyond next seqno) is ignored
    if (unwrap(ackno, _isn, _abs_ackno) > _abs_ackno && unwrap(ackno, _isn, _abs_ackno) <= _next_seqno) {
        _abs_ackno = unwrap(ackno, _isn, _abs_ackno);  //注意：此处可能会有坑_abs_ackno是否合理？  
    }
    _window_size = window_size;

    //第二部分：检查备份队列中是否有可以出队的报文段
    //即备份队列不空，并且队首元素已经被接收方确认收到，则队首元素可以出队
    //Timer doesn't restart without ACK of new data
    //因此需要设置flag，标志是否有新的报文段被确认，如果没有，就不重启计时器
    bool new_seg_acked{false};
    while (!_segments_backup.empty() && check_seg_acked(_segments_backup.front())) {
        _segments_backup.pop();
        new_seg_acked = true;
    }
    //如果所有发送但未确认的报文段都已经被确认了，那么就关闭计时器
    if (_segments_backup.empty()) timer.stop_timer();

    //第三部分：修改重传时限、或许需要重启计时器、累积重传次数归零
    _retransmission_timeout = _initial_retransmission_timeout;
    //只有在尚存未发送报文段，并且此次接收ack有新的报文段被确认，才能重启计时器
    if (!_segments_backup.empty() && new_seg_acked) timer.start_timer(_retransmission_timeout);
    _consecutive_retransmissions = 0;
 }

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void TCPSender::tick(const size_t ms_since_last_tick) { 
    timer.tick(ms_since_last_tick); //告知计时器时间流逝
    if (!timer.timer_expired()) return;
    //处理计时器计时结束的情况
    //首先需要重传具有最小序号的发送但未确认的报文段
    _segments_out.push(_segments_backup.front());
    //设置连续重传次数+1，超时间隔翻倍
    if (_window_size != 0) {
        _consecutive_retransmissions += 1;
        _retransmission_timeout *= 2;
    }

    //重新启动计时器
    timer.start_timer(_retransmission_timeout);
 }

unsigned int TCPSender::consecutive_retransmissions() const { return _consecutive_retransmissions; }

void TCPSender::send_empty_segment(bool rst_set) {
    //创造一个空报文段，注意：这种不占用绝对序列号的报文段不需要备份重发
    string payload = "";
    TCPSegment empty_segment = make_segment(_next_seqno, false, false, payload);
    empty_segment.header().rst = rst_set;
    _segments_out.push(empty_segment);
}
