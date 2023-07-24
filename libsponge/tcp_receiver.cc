#include "tcp_receiver.hh"

// Dummy implementation of a TCP receiver

// For Lab 2, please replace with a real implementation that passes the
// automated checks run by `make check_lab2`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

void TCPReceiver::segment_received(const TCPSegment &seg) {
    //如果还没有设置SYN但是已经有报文段到达了，则直接忽略
    if (!_isn.has_value() && !seg.header().syn) { return; }

    //首个到达的含有SYN的报文段所含的序号被标记为初始序列号_isn
    //我们对其进行单独处理。该部分结束后,_isn和_ackno都已经收到了
    //理论上TCPReceiver只会收到一次带有SYN的报文段
    if (seg.header().syn && !_isn.has_value()) {
        _isn = seg.header().seqno;
        string payload = seg.payload().copy();
        _reassembler.push_substring(payload, 0, seg.header().fin);
        _ackno = wrap(1 + _reassembler.get_first_unassembled() + seg.header().fin, _isn.value());
        return;
    }

    //能运行到这个位置，说明SYN报文段已经被收到了，此时_isn和_ackno都已经被接收到了
    //我们尝试将数据送入reassembler
    //1：尝试获得reassembler.push_substring所要求的字符串初始序列号
    uint64_t abs_seqno = unwrap(seg.header().seqno, _isn.value(), _reassembler.get_first_unassembled());
    uint64_t seg_idx = abs_seqno - 1;
    //2：把它包含的负载(string)放入reassembler中
    _reassembler.push_substring(seg.payload().copy(), seg_idx, seg.header().fin);
    //3：更新_ackno，期待收到的下一个字节
    //细节在于需要处理FIN比特位：如果reassembler到达了字节流末尾，则需要加上FIN占用的序列号
    _ackno = wrap(_reassembler.get_first_unassembled() + 1 + (_reassembler.reach_end() ? 1 : 0), _isn.value());
}

optional<WrappingInt32> TCPReceiver::ackno() const { return _ackno; }

size_t TCPReceiver::window_size() const { return _capacity - _reassembler.stream_out().buffer_size() ; }
