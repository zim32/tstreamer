#ifndef T_STREAMER_PIECES_CONTAINER_HPP
#define T_STREAMER_PIECES_CONTAINER_HPP

#include <map>
#include <vector>
#include <cstring>

class pieces_container {

public:
    typedef std::map<size_t, std::vector<char>> storage_t;

    // piece_idx => data
    storage_t storage;

    explicit pieces_container(size_t piece_size) {
        this->piece_size = piece_size;
    }

    void add_piece_data(size_t piece_idx, char *data, size_t data_length, size_t data_offset) {
        std::vector<char> &piece_data = this->storage[piece_idx];

        // start vector of piece_size size
        if (piece_data.empty()) {
            piece_data.resize(this->piece_size);
        }

        // get pointer to vector internal buffer
        char *vector_buff = piece_data.data();

        // copy data to internal buffer
        std::memcpy(vector_buff + data_offset, data, data_length);
    }

    storage_t::const_iterator remove_piece_data(storage_t::const_iterator pos) {
        return storage.erase(pos);
    }

    void lock() {
        mLock.lock();
    }

    void unlock() {
        mLock.unlock();
    }

private:
    size_t piece_size;
    std::mutex mLock;
};


#endif
