#include "wrapping_integers.hh"

// Dummy implementation of a 32-bit wrapping integer

// For Lab 2, please replace with a real implementation that passes the
// automated checks run by `make check_lab2`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

//! Transform an "absolute" 64-bit sequence number (zero-indexed) into a WrappingInt32
//! \param n The input absolute 64-bit sequence number
//! \param isn The initial sequence number
WrappingInt32 wrap(uint64_t n, WrappingInt32 isn) {
    uint32_t val = static_cast<uint64_t>(isn.raw_value() + n) % (1UL << 32);
    return WrappingInt32{ val };
}

//! Transform a WrappingInt32 into an "absolute" 64-bit sequence number (zero-indexed)
//! \param n The relative sequence number
//! \param isn The initial sequence number
//! \param checkpoint A recent absolute 64-bit sequence number
//! \returns the 64-bit sequence number that wraps to `n` and is closest to `checkpoint`
//!
//! \note Each of the two streams of the TCP connection has its own ISN. One stream
//! runs from the local TCPSender to the remote TCPReceiver and has one ISN,
//! and the other stream runs from the remote TCPSender to the local TCPReceiver and
//! has a different ISN.
uint64_t unwrap(WrappingInt32 n, WrappingInt32 isn, uint64_t checkpoint) {
    //获取初始绝对序列号，它的范围必然位于 0 ~ (2^31 - 1)
    uint64_t init_absseqno = n.raw_value() >= isn.raw_value() ?
                             n.raw_value() - isn.raw_value() : n.raw_value() + (1UL << 32) - isn.raw_value();
    //checkpoint含有多少个完整的2^32 ?
    uint64_t n_power = checkpoint / (1UL << 32);
    //将其左偏移、右偏移、不偏移，考察哪个距离最短即可
    uint64_t absseqno0 = init_absseqno + (n_power <= 1 ? n_power : n_power - 1) * (1UL << 32);
    uint64_t absseqno1 = init_absseqno + n_power * (1UL << 32);
    uint64_t absseqno2 = init_absseqno + (n_power + 1) * (1UL << 32);
    uint64_t distance0 = absseqno0 >= checkpoint ? absseqno0 - checkpoint : checkpoint - absseqno0;
    uint64_t distance1 = absseqno1 >= checkpoint ? absseqno1 - checkpoint : checkpoint - absseqno1;
    uint64_t distance2 = absseqno2 >= checkpoint ? absseqno2 - checkpoint : checkpoint - absseqno2;
    return distance0 <= distance1 ? 
           (distance0 <= distance2 ? absseqno0 : absseqno2) :
           (distance1 <= distance2 ? absseqno1 : absseqno2);

}
