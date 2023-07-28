#ifndef SPONGE_LIBSPONGE_STREAM_REASSEMBLER_HH
#define SPONGE_LIBSPONGE_STREAM_REASSEMBLER_HH

#include "byte_stream.hh"

#include <cstdint>
#include <string>
#include <map>

//! \brief A class that assembles a series of excerpts from a byte stream (possibly out of order,
//! possibly overlapping) into an in-order byte stream.
class StreamReassembler {
  private:
    // Your code here -- add private members as necessary.

    ByteStream _output;  //!< The reassembled in-order byte stream
    size_t _capacity;    //!< The maximum number of bytes
    size_t _bytes_in_buffer{0};

    bool _has_eof{false};
    size_t _final_idx{0}; //标记这个字节流的最后一个字符的编号
    //_mapbuffer 永远保证储存的substring是不会发生重叠现象的，它本身是没有容量限制的
    std::map<size_t, std::string> _mapbuffer{}; //储存未被集成的子字符串
    size_t _first_unassembled{0}; //第一个未被按序接受的字节序号
    size_t get_first_unacceptable() {
      //_first_unassembled + (_capacity - buffer.size())
      return _output.remaining_capacity() + _first_unassembled;
    }

    /**
     * 此函数用于对_mapbuffer进行合并
     * 完成的功能是：当新的键值对插入_mapbuffer时，将_mapbuffer中能
     * 合并的键值对都进行合并，使得新的_mapbuffer中的substring没有重叠
    */
    void merge_mapbuffer(std::pair<size_t, std::string>&& insert_pair);

    /**
     * 根据_final_idx和_first_unacceptable对数据进行截断
     * 直接忽略(也就是截断)超出这些范围的字节
    */
    std::string truncation_data(const std::string& data, const size_t index);

    /**
     * 此函数的作用在于，对于已经截断的数据，判断其是否能直接读出(_output.write())
     * 如果能，就读出并更新相应变量，并返回true；
     * 如果不能，返回false，说明该string需要加入缓存或仍需待在缓存里面；
    */
    bool push_onestr(const std::string &new_data, const size_t index);

  public:
    //! \brief Construct a `StreamReassembler` that will store up to `capacity` bytes.
    //! \note This capacity limits both the bytes that have been reassembled,
    //! and those that have not yet been reassembled.
    StreamReassembler(const size_t capacity);

    //! \brief Receive a substring and write any newly contiguous bytes into the stream.
    //!
    //! The StreamReassembler will stay within the memory limits of the `capacity`.
    //! Bytes that would exceed the capacity are silently discarded.
    //!
    //! \param data the substring
    //! \param index indicates the index (place in sequence) of the first byte in `data`
    //! \param eof the last byte of `data` will be the last byte in the entire stream
    void push_substring(const std::string &data, const uint64_t index, const bool eof);

    //! \name Access the reassembled byte stream
    //!@{
    const ByteStream &stream_out() const { return _output; }
    ByteStream &stream_out() { return _output; }
    //!@}

    //! The number of bytes in the substrings stored but not yet reassembled
    //!
    //! \note If the byte at a particular index has been pushed more than once, it
    //! should only be counted once for the purpose of this function.
    size_t unassembled_bytes() const;

    //! \brief Is the internal state empty (other than the output stream)?
    //! \returns `true` if no substrings are waiting to be assembled
    bool empty() const;

    size_t get_first_unassembled() { return _first_unassembled; }
    
    /**
     * 如何判断该字节流的接收已经达到最后了呢？
     * 需要已经收到了结束标记、_first_unassembled恰好为_final_idx的下一个值
     * 这个函数主要用于为TCPReceiver服务，用于处理FIN比特
    */
    bool reach_end() { return _has_eof && _final_idx + 1 == _first_unassembled; }
};

#endif  // SPONGE_LIBSPONGE_STREAM_REASSEMBLER_HH
