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
 * 这个函数输入两个_mapbuffer中的键值对，考察二者能否合并
 * 如果能合并，就生成一个新的pair，并返回true；如果不能合并，就返回false
 * 这里与传统合并不同的地方在于，[4, 5], [5, 7]这种区间也是可以合并的
*/
bool merge_pair(const pair<size_t, string>& pair1, const pair<size_t, string>& pair2, 
               pair<size_t, string>& new_pair) {
    //假设都是闭区间，即pair1对应的区间是[left1, right1],pair2对应的区间是[left2, right2]
    int64_t left1 = pair1.first, right1 = pair1.first + pair1.second.length() - 1; 
    int64_t left2 = pair2.first, right2 = pair2.first + pair2.second.length() - 1; 

    //首先排除合并不了的情况
    if (right1 < left2 - 1 || right2 < left1 - 1) {
        return false;
    }

    //对各种可以合并的情况进行分类讨论
    //1:恰好相邻的情况，并且pair1左相邻于pair2
    if (right1 == left2 - 1) {
        new_pair.first = left1;
        new_pair.second = pair1.second + pair2.second;
        //new_pair.second = move(pair1.second);
        //new_pair.second += pair2.second;
        return true;
    }
    //2:恰好相邻的情况，并且pair2左相邻于pair1
    if (right2 == left1 - 1) {
        new_pair.first = left2;
        //new_pair.second = pair2.second + pair1.second;
        new_pair.second = move(pair2.second);
        new_pair.second += pair1.second;
        return true;
    }

    //3:pair1内含于pair2
    if (left1 >= left2 && right1 <= right2) {
        //new_pair = pair2;
        new_pair.first = pair2.first;
        new_pair.second = move(pair2.second);
        return true;
    }
    //4:pair2内含于pair1
    if (left2 >= left1 && right2 <= right1) {
        //new_pair = pair1;
        new_pair.first = pair1.first;
        new_pair.second = move(pair1.second);
        return true;
    }
    //5:pair1与pair2部分相交，并且pair1位于左侧
    if (right1 >= left2 && right1 < right2 && left1 < left2) {
        new_pair.first = left1;

        //new_pair.second = move(pair1.second); 
        //new_pair.second += pair2.second.substr(right1 - left2 + 1);
        new_pair.second = pair1.second + pair2.second.substr(right1 - left2 + 1);
        return true;
    }
    //6:pair1与pair2部分相交，并且pair1位于右侧
    if (right2 >= left1 && right2 < right1 && left2 < left1) {
        new_pair.first = left2;
        new_pair.second = move(pair2.second);
        new_pair.second += pair1.second.substr(right2 - left1 + 1);
        //new_pair.second = pair2.second + pair1.second.substr(right2 - left1 + 1);
        return true;
    }
    return false;
}

/**
 * 此函数用于对_mapbuffer进行合并
 * 完成的功能是：当新的键值对插入_mapbuffer时，将_mapbuffer中能
 * 合并的键值对都进行合并，使得新的_mapbuffer中的substring没有重叠
*/
void StreamReassembler::merge_mapbuffer(pair<size_t, string>&& insert_pair) {
    pair<size_t, string> new_pair;
    bool has_merge_pair = true;
    while (has_merge_pair) {
        has_merge_pair = false;
        for (auto&  primer_pair : _mapbuffer) {
            if (merge_pair(primer_pair, insert_pair, new_pair)) {
                _mapbuffer.erase(primer_pair.first);
                insert_pair = new_pair;
                has_merge_pair = true;
                break;
            }
        }
    }
    //! \bug 忘记把这个最终元素插入进去了，导致buffer中的数值凭空消失
    //! \note 将此处改为移动语义，可有效减少拷贝时间，提高reordering benchmark大约0.2Gbit/s  
    _mapbuffer.insert(move(insert_pair));
}

/**
 * 此函数的目的在于根据capacity和EOF的信息对数据进行截断
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
        merge_mapbuffer(pair<size_t, string>(index, move(new_data)));
    }

    //考察缓存_mapbuffer中的元素能否读出？
    bool can_readout = true;
    while (can_readout) {
        can_readout = false;
        for (auto& primer_pair : _mapbuffer) {
            if (push_onestr(primer_pair.second, primer_pair.first)) {
                _mapbuffer.erase(primer_pair.first);
                can_readout = true;
                break;
            }
        }        
    }
}

size_t StreamReassembler::unassembled_bytes() const { 
    size_t result = 0;
    for (const auto& elem : _mapbuffer) {
        result += elem.second.length();
    }
    return result;
}

/**
 * 如果_mapbuffer中没有元素，说明没有待集成的子字符串了
*/
bool StreamReassembler::empty() const { return _mapbuffer.empty(); }
