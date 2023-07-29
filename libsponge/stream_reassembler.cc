#include "stream_reassembler.hh"

// Dummy implementation of a stream reassembler.

// For Lab 1, please replace with a real implementation that passes the
// automated checks run by `make check_lab1`.

// You will need to add private members to the class declaration in `stream_reassembler.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

StreamReassembler::StreamReassembler(const size_t capacity) : _output(capacity), _capacity(capacity) {}

/**
 * @return 此函数的目的在于根据capacity和EOF的信息对数据进行截断
 * 基本上用于截断这个数据的尾部
*/
string StreamReassembler::truncation_data(const string& data, const size_t index) {
    //size_t data_length = data.length();
    string new_data = data;
    //如果我们已经得到了EOF，即最后一个字节的序号
    //那么超出的部分将被直接丢弃
    if (_has_eof && index + data.length() > _final_idx + 1) {
        new_data = new_data.substr(0, _final_idx - index + 1);
    }
    //其次考虑总容量的截断作用
    //通过_capacity可以得到第一个不可接受之index
    //data超出该不可接受index的部分将被截断
    size_t _first_unacceptable = get_first_unacceptable();
    if (index + new_data.length() > _first_unacceptable) {
        new_data = new_data.substr(0, _first_unacceptable - index);
    }
    return new_data;
}

bool StreamReassembler::push_onestr(const std::string &new_data, const size_t index) {
    //简单情况：如果new_data直接契合_first_unassembled
    if (index == _first_unassembled) {
        _output.write(new_data);
        _first_unassembled = _first_unassembled + new_data.length();
        if (_has_eof && _first_unassembled == _final_idx + 1) { _output.end_input(); } 
        return true;
    }
    //稍显复杂的情况：如果new_data没有对新数据更新作出任何贡献
    if (index + new_data.length() <= _first_unassembled) { 
        if (_has_eof && _first_unassembled == _final_idx + 1) { _output.end_input(); } 
        return true; 
    }

    //稍显复杂的情况：如果new_data部分契合_first_unassembled
    //那么就部分发送这个数据
    if (index < _first_unassembled && index + new_data.length() > _first_unassembled) {
        _output.write(new_data.substr(_first_unassembled - index, 
                                        index + new_data.length() - _first_unassembled));
        _first_unassembled = _first_unassembled + index + new_data.length() - _first_unassembled;
        if (_has_eof && _first_unassembled == _final_idx + 1) { _output.end_input(); } 
        return true;
    }   
    return false;   
}

//! \details This function accepts a substring (aka a segment) of bytes,
//! possibly out-of-order, from the logical stream, and assembles any newly
//! contiguous substrings and writes them into the output stream in order.
void StreamReassembler::push_substring(const string &data, const size_t index, const bool eof) {
    //维护变量记录这个字节流的最后一个字节序列号
    if (eof) { _has_eof = true;  _final_idx = index + data.length() - 1; }
    //对数据进行截断，防止出现不可接受之字节位
    string new_data = truncation_data(data, index);
    
    //尝试直接读出new_data
    push_onestr(new_data, index);

    //更加复杂的情况：new_data不能直接读出，只能暂存，进入_mapbuffer
    if (index > _first_unassembled) {
        //! \bug 忘记把这个最终元素插入进去了，导致buffer中的数值凭空消失
        //! \note 将此处改为移动语义，可有效减少拷贝时间，提高reordering benchmark大约0.2Gbit/s  
        _mapbuffer.insert(pair<size_t, string>(index, move(new_data)));
    }

    //考察缓存_mapbuffer中的元素能否读出？
    bool can_readout = true;
    while (can_readout) {
        can_readout = false;
        for (auto& primer_pair : _mapbuffer) {
            if (push_onestr(primer_pair.second, primer_pair.first)) {
                _mapbuffer.erase(primer_pair);
                can_readout = true;
                break;
            }
        }        
    }
}

/**
 * @note 实现策略：遍历_mapbuffer，用集合统计每个比特位是否被占据
 * 由于集合的唯一性，所以同一个比特位只能出现一次
*/
size_t StreamReassembler::unassembled_bytes() const { 
    set<int> _unassembled_bytes{};
    for (const auto& elem : _mapbuffer) {
        for (size_t i = 0; i < elem.second.length(); ++i) {
            _unassembled_bytes.insert(elem.first + i);
        }
    }
    return _unassembled_bytes.size();
}

/**
 * @note 如果_mapbuffer中没有元素，说明没有待集成的子字符串了
*/
bool StreamReassembler::empty() const { return _mapbuffer.empty(); }
