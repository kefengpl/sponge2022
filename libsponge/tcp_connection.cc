#include "tcp_connection.hh"

#include <iostream>
#include <limits>

// Dummy implementation of a TCP connection

// For Lab 4, please replace with a real implementation that passes the
// automated checks run by `make check`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

size_t TCPConnection::remaining_outbound_capacity() const { return _sender.stream_in().buffer_size(); }

size_t TCPConnection::bytes_in_flight() const { return _sender.bytes_in_flight(); }

size_t TCPConnection::unassembled_bytes() const { return _receiver.unassembled_bytes(); }

size_t TCPConnection::time_since_last_segment_received() const { return _time_since_last_segrecv; }

void TCPConnection::segment_received(const TCPSegment &seg) {
    //如果连接已经被杀掉了，就直接返回
    if (!active()) return;
    //接受到任何报文段，都可以把计时器刷新，即刚刚(0ms前)接受了新的报文段
    _time_since_last_segrecv = 0;

    //把报文段交给TCPReceiver
    _receiver.segment_received(seg);

    //! \bug 收到rst报文段就进行unclean shutdown
    //! \bug 只不过收到rst报文段时无需发送rst报文段
    if (seg.header().rst) {
        unclean_shutdown(false);
        return;
    }

    //如果ack被设置，将报文段中的ackno和window_size交给TCPSender
    if (seg.header().ack) {
        _sender.ack_received(seg.header().ackno, seg.header().win);    
    }

    //! \bug 判断此处是否需要将_linger_after_streams_finish设置为false
    follow_prereq();

    //在收到的报文段占用序号大于等于1的情况下
    //我们确保至少有一个ACK报文段将被发出去
    if (seg.length_in_sequence_space() == 0) {
        //如果收到对方的“keep alive” Segment，应作出如下处理
        if (_receiver.ackno().has_value() && 
            seg.header().seqno == _receiver.ackno().value() - 1) {
            _sender.send_empty_segment();
        }
        send_all();
        return;
    }

    /**
     * BUG:特殊情况：如果只收到了syn没有ack，
     * 此时需要回复SYNACK报文段，除此之外
     * 只需要回复普通报文段
    */
    if (seg.header().syn && !seg.header().ack) {
        _sender.fill_window(); 
    }

    //在报文段占用序号大于等于1的情况, 并且当前没有待发送的报文段，就创建一个空的ACK报文段
    if (_sender.segments_out().empty()) _sender.send_empty_segment(); 
    //此函数会将_sender中发送队列的所有报文段发出，可让对方得知当前的ackno和window_size
    send_all();
 }

bool TCPConnection::active() const { return _active; }

size_t TCPConnection::write(const string &data) {
    //调用LAB0的接口：将数据写入outbound stream
    size_t written_size = _sender.stream_in().write(data);
    //让TCPSender打包可用的报文段
    _sender.fill_window();

    //此函数的另一功能是通过TCP发送出这些数据
    //从TCPSender中抽取所有报文段并发送
    //此函数结束后，_sender的发送队列一定是空值
    send_all();
    return written_size;
}

//! \param[in] ms_since_last_tick number of milliseconds since the last call to this method
void TCPConnection::tick(const size_t ms_since_last_tick) { 
    //cout << "调用tick " << ms_since_last_tick << endl;
    //1:告知TCPSender时间的流逝，并记录接收到上个报文段后过去了多久
    _sender.tick(ms_since_last_tick);
    _time_since_last_segrecv += ms_since_last_tick;

    //2:如果连续重传次数超出上限，首先需要[终止连接]，然后发送带有RST的空报文段
    //cout << "进入分支2 如果连续重传次数超出上限 " << _sender.consecutive_retransmissions() << endl;
    if (_sender.consecutive_retransmissions() > TCPConfig::MAX_RETX_ATTEMPTS) {
        unclean_shutdown();
        return;
    }
    
    /**
     * BUG: 注意此处需要把能发送的报文段发送出去
     * BUG: 这里绝对不能发送SYN或者SYNACK报文段
    */
    if (_sender.syn_sent()) _sender.fill_window();
    send_all();

    //cout << "进入分支3 fill window 与 send all" << ms_since_last_tick << endl;

    //3:在必要的情况下结束此次连接，CLEANEY
    //三个条件都满足才能CLEANY地结束此次连接
    if (!follow_prereq()) { return; }
    if (!_linger_after_streams_finish) {
        _active = false;
    }
    //cout << "是否能进入徘徊区域？" << ms_since_last_tick << endl;

    //进入徘徊阶段后，如果距离上次收到报文段已经过去了至少10 * _cfg.rt_timeout，就结束徘徊
    if (_linger_after_streams_finish 
        && _time_since_last_segrecv >= 10 * _cfg.rt_timeout) {
        _lingering = false;
        _active = false;
    }
 }

/**
 * BUG:注意此函数在结束输入之后需要考察是否有空FIN比特发送
*/
void TCPConnection::end_input_stream() { 
    _sender.stream_in().end_input(); 
    //! \bug 处理FIN比特的发送
    _sender.fill_window();
    send_all();
}

void TCPConnection::connect() {
    //初始阶段，_sender只会发送一个SYN报文段以建立连接
    _sender.fill_window();
    //将该SYN报文段处理加入ackno和之后发送出去
    send_all();
}

void TCPConnection::handle_sender_segment(TCPSegment& sender_segment) {
    //只有在有确认号的时候才应该在报文段中增加ack信息
    if (_receiver.ackno().has_value()) {
        sender_segment.header().ack = true;
        sender_segment.header().ackno = _receiver.ackno().value(); 
    }
    //可能出错的逻辑：无论如何，都把窗口大小设置上，注意窗口大小是16位，因此存在上限
    sender_segment.header().win = _receiver.window_size() > numeric_limits<uint16_t>::max() ? 
                               numeric_limits<uint16_t>::max() : _receiver.window_size();
}

void TCPConnection::send_all() {
    while (!_sender.segments_out().empty()) {
        //从TCPSender中抽取SYN报文段并进行包装，即装载Receiver的ackno和window_size
        TCPSegment segment =  _sender.segments_out().front();
        _sender.segments_out().pop();   
        //添加首部的ackno和header的信息
        handle_sender_segment(segment); 
        //然后“发送”出该报文段
        _segments_out.push(segment);
    }    
}

bool TCPConnection::follow_prereq() {
    //条件1：The inbound stream has been fully assembled and has ended.
    bool prereq1 = (_receiver.stream_out().input_ended()); 
    /**
     * 条件2：outbound stream 达到 EOF，并被完全发送(包括FIN)
     * BUG: prereq2的条件应该是 
     * _sender.next_seqno_absolute() == _sender.stream_in().bytes_written() + 2
     * 而不是_sender.fin_sent()，注意按照lab3实验文档来写即可
    */
    bool prereq2 = (_sender.stream_in().eof() && 
                    _sender.next_seqno_absolute() == _sender.stream_in().bytes_written() + 2);
    //条件3：The outbound stream has been fully acknowledged by the remote peer
    bool prereq3 = (bytes_in_flight() == 0);

    //判断是否需要linger？
    // inbound stream ends before the TCPConnection has ever sent a fin segment
    //then the TCPConnection doesn’t need to linger after both streams finish.
    if (_receiver.stream_out().eof() && !_sender.fin_sent()) {
        _linger_after_streams_finish = false;
    }
    return prereq1 && prereq2 && prereq3; 
}

void TCPConnection::unclean_shutdown(bool rst_sent) {
    /**
     * 只能制作普通空报文段，需要在后面加入RST比特位
     * 可能还有其它信息需要制成报文段，因此仍需调用fill_window
     * BUG: 此处只需要_snder将第一个待发送报文段加上rst作
     * BUG: 如果_sender.segments_out()是空值，则创建一个空的rst报文段
    */
    if (rst_sent) {
        _sender.fill_window(); 
        if (_sender.segments_out().empty()) {
            _sender.send_empty_segment(true);
        } else {
            _sender.segments_out().front().header().rst = true;
        }
        send_all();         
    }       
    //把两个ByteStream都设置成错误状态，连接自动变为不活跃，active返回false
    _receiver.stream_out().set_error();
    _sender.stream_in().set_error();  
    //杀死TCPConnection
    _active = false;  
}

TCPConnection::~TCPConnection() {
    try {
        if (active()) {
            cerr << "Warning: Unclean shutdown of TCPConnection\n";

            // Your code here: need to send a RST segment to the peer
            unclean_shutdown();
        }
    } catch (const exception &e) {
        std::cerr << "Exception destructing TCP FSM: " << e.what() << std::endl;
    }
}
