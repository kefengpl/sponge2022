#include "byte_stream.hh"

// Dummy implementation of a flow-controlled in-memory byte stream.

// For Lab 0, please replace with a real implementation that passes the
// automated checks run by `make check_lab0`.

// You will need to add private members to the class declaration in `byte_stream.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

ByteStream::ByteStream(const size_t capacity) : _capacity(capacity) {}

size_t ByteStream::write(const string &data) {
    //新增处理逻辑：如果error，则禁止写入
    if (input_ended() || error()) {
        return 0;
    }
    if (remaining_capacity() == 0) {
        return 0;
    }
    size_t write_bytes = remaining_capacity() < data.length() ? 
                           remaining_capacity() : data.length();
    for (size_t i = 0; i < write_bytes; i++) {
        _buffer.push_back(data[i]);
    }
    _bytes_written += write_bytes;
    return write_bytes;
}

//! \param[in] len bytes will be copied from the output side of the buffer
string ByteStream::peek_output(const size_t len) const {
    size_t new_len = len > _buffer.size() ? _buffer.size() : len;
    return string(_buffer.begin(), _buffer.begin() + new_len);
}

//! \param[in] len bytes will be removed from the output side of the buffer
void ByteStream::pop_output(const size_t len) { 
    size_t new_len = len > _buffer.size() ? _buffer.size() : len;
    _bytes_read += new_len;
    for (size_t i = 0; i < new_len; i++) {
        _buffer.pop_front();
    }
 }

//! Read (i.e., copy and then pop) the next "len" bytes of the stream
//! \param[in] len bytes will be popped and returned
//! \returns a string
std::string ByteStream::read(const size_t len) {
    //新增处理逻辑：如果error，则禁止读出
    if (error()) { return ""; }
    
    string read_string = peek_output(len);
    pop_output(len);
    return read_string;
}

void ByteStream::end_input() { _input_end = true; }

bool ByteStream::input_ended() const { return _input_end; }

size_t ByteStream::buffer_size() const { return _buffer.size(); }

bool ByteStream::buffer_empty() const { return _buffer.empty(); }

bool ByteStream::eof() const { return _input_end && _buffer.empty(); }

size_t ByteStream::bytes_written() const { return _bytes_written; }

size_t ByteStream::bytes_read() const { return _bytes_read; }

size_t ByteStream::remaining_capacity() const { return _capacity - _buffer.size();}
