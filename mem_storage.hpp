#ifndef T_STREAMER_MEM_STORAGE_HPP
#define T_STREAMER_MEM_STORAGE_HPP

#include <libtorrent/storage.hpp>
#include <iostream>
#include <mutex>
#include "pieces_container.hpp"

struct download_context_t {
    int file_idx;
    size_t bytes_offset;
    size_t start_piece_offset;
    size_t end_piece_offset;
    size_t offset_in_first_piece;
    size_t piece_size;
};

extern download_context_t download_ctx;

struct temp_storage:  lt::storage_interface
{
    pieces_container m_pieces{download_ctx.piece_size};

    explicit temp_storage(lt::file_storage const& fs) : lt::storage_interface(fs) {}
    void initialize(lt::storage_error&) override {}
    bool has_any_file(lt::storage_error&) override { return false; }
    void set_file_priority(lt::aux::vector<lt::download_priority_t, lt::file_index_t>&, lt::storage_error&) override {}

    int readv(lt::span<lt::iovec_t const> bufs, lt::piece_index_t piece, int offset, lt::open_mode_t, lt::storage_error&) override {
        std::map<size_t, std::vector<char>> &storage = m_pieces.storage;
        auto const i = storage.find(piece);
        if (i == storage.end()) return 0;
        if (int(i->second.size()) <= offset) return 0;
        lt::iovec_t data{ i->second.data() + offset, int(i->second.size() - offset) };
        int ret = 0;
        for (lt::iovec_t const& b : bufs) {
            int const to_copy = std::min(int(b.size()), int(data.size()));
            memcpy(b.data(), data.data(), to_copy);
            data = data.subspan(to_copy);
            ret += to_copy;
            if (data.empty()) break;
        }
        return ret;
    }

    int writev(lt::span<lt::iovec_t const> bufs, lt::piece_index_t const piece, int offset, lt::open_mode_t, lt::storage_error&) override {
        m_pieces.lock();

        if (piece < download_ctx.start_piece_offset || piece > download_ctx.end_piece_offset) {
            std::cout << "Mem storage: Piece " << piece << "is out of range. Ignoring" << std::endl;
            return 0;
        }

//        std::cout << "Writing... Piece index: " << piece << ", Offset: " << offset << std::endl;
        int wrote_bytes = 0;

        int local_offset{offset};

        for (auto &buf: bufs) {
            wrote_bytes += buf.size();
            m_pieces.add_piece_data(piece, buf.data(), buf.size(), local_offset);
            local_offset += buf.size();
        }

//        std::cout << "Wrote " << wrote_bytes << " bytes" << std::endl;

        m_pieces.unlock();

        return wrote_bytes;
    }

    void rename_file(lt::file_index_t, std::string const&, lt::storage_error&) override {
        assert(false);
    }

    lt::status_t move_storage(std::string const&, lt::move_flags_t, lt::storage_error&) override {
        return lt::status_t::no_error;
    }

    bool verify_resume_data(
        lt::add_torrent_params const&,
        lt::aux::vector<std::string,
        lt::file_index_t> const&,
        lt::storage_error&) override
    {
        return false;
    }

    void release_files(lt::storage_error&) override {}
    void delete_files(lt::remove_flags_t, lt::storage_error&) override {}

public:
    const std::vector<char>& get_piece_data(size_t piece_idx) {
        return m_pieces.storage[piece_idx];
    }

    const pieces_container::storage_t & get_pieces_storage() const {
        return m_pieces.storage;
    }

    pieces_container::storage_t::const_iterator remove_piece_from_storage(pieces_container::storage_t::const_iterator pos) {
        m_pieces.lock();
        auto result = m_pieces.remove_piece_data(pos);
        m_pieces.unlock();
        return result;
    }

    bool have_piece(size_t piece_index) {
        return m_pieces.storage.find(piece_index) != m_pieces.storage.end();
    }
};

lt::storage_interface* temp_storage_constructor(lt::storage_params const& params, lt::file_pool&)
{
    return new temp_storage(params.files);
}

#endif