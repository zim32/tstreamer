#include "tstreamer.h"
#include "mem_storage.hpp"

#include <libtorrent/torrent_info.hpp>
#include <libtorrent/settings_pack.hpp>
#include <libtorrent/alert_types.hpp>

#include <iostream>
#include <memory>
#include <stdexcept>
#include <sstream>

download_context_t download_ctx;

std::shared_ptr<TStreamer> TStreamer::createFromFileName(const std::string &fileName) {
    auto t = new TStreamer();

    t->mTorrentInfo = std::make_shared<libtorrent::torrent_info>(fileName);

    return std::shared_ptr<TStreamer>(t);
}

void TStreamer::dumpFilesInfo() const {
    auto &storage      = mTorrentInfo->files();
    const int filesQty = storage.num_files();

    for (int x=0; x < filesQty; x++) {
        std::cout << "[" << x << "] Name: " << storage.file_name(x) << std::endl;
        std::cout << "[" << x << "] Size: " << storage.file_size(x) << std::endl;
        std::cout << "[" << x << "] Offset (bytes): " << storage.file_offset(x) << std::endl;
        std::cout << "[" << x << "] Path: " << storage.file_path(x) << std::endl;
    }
}

void TStreamer::start() {
    if (mInited) {
        return;
    }

    // settings pack
    lt::settings_pack pack;
    pack.set_int(lt::settings_pack::alert_mask, lt::alert::error_notification | lt::alert::piece_progress_notification);

    mSession = std::make_unique<lt::session>(pack);

    // add torrent params
    lt::add_torrent_params atp(temp_storage_constructor);
    atp.save_path = ".";
    atp.ti = mTorrentInfo;
    atp.flags = lt::torrent_flags::sequential_download | lt::torrent_flags::paused;


    // set file priorities
    auto &storage      = mTorrentInfo->files();
    const int filesQty = storage.num_files();

    atp.file_priorities.resize(filesQty);

    for (int x=0; x < filesQty; x++) {
        if (x == mDownloadFileIndex) {
            atp.file_priorities[x] = lt::default_priority;
        } else {
            atp.file_priorities[x] = lt::dont_download;
        }
    }

    // setup global download context
    download_ctx.file_idx = mDownloadFileIndex;
    download_ctx.bytes_offset = storage.file_offset(mDownloadFileIndex);
    download_ctx.piece_size = mTorrentInfo->piece_length();

    // compute offsets
    download_ctx.start_piece_offset = download_ctx.bytes_offset / mTorrentInfo->piece_length();
    download_ctx.end_piece_offset = ceil(
            (double)(download_ctx.bytes_offset + storage.file_size(mDownloadFileIndex)) /
               mTorrentInfo->piece_length()
    );
    download_ctx.offset_in_first_piece = download_ctx.bytes_offset - download_ctx.start_piece_offset * mTorrentInfo->piece_length();


    // add torrent to session
    mTorrentHandle = mSession->add_torrent(atp);
    mTorrentHandle.pause();
    // force to download first piece first
    mTorrentHandle.set_piece_deadline(download_ctx.start_piece_offset, 5000);

    mInited = true;
}

void TStreamer::setDownloadFileIndex(uint index) {
    if (mInited) {
        throw std::logic_error("Can not set download file index after start");
    }

    mDownloadFileIndex = index;
}

void TStreamer::pause() {

}

void TStreamer::resume() {

}

void TStreamer::startMainLoop() {

    // flush_queue
    bool flush_started = false;
    size_t last_flushed_piece = 0;
    // piece miss map
    std::map<size_t, uint> piece_misses;

    while (true) {
        // process alerts
        std::vector<lt::alert*> alerts;
        mSession->pop_alerts(&alerts);

        for (lt::alert const* alert : alerts) {
            if (lt::alert_cast<lt::torrent_finished_alert>(alert)) {
                return this->shutdown();
            }

            // piece finished alert
            if (lt::alert_cast<lt::piece_finished_alert>(alert)) {
                size_t piece_index = (lt::alert_cast<lt::piece_finished_alert>(alert))->piece_index;

                // skip if not first piece and flushing not started yet
                if (
                        !flush_started &&
                        piece_index != download_ctx.start_piece_offset
                        ) {
                    std::cout << "Wait for first piece to begin flush" << std::endl;
                    continue;
                }

                flush_started = true;

                // get piece data from mem_storage
                auto tmp_storage = dynamic_cast<temp_storage*>(mTorrentHandle.get_storage_impl());
                const pieces_container::storage_t &pieces_storage = tmp_storage->get_pieces_storage();

                if (pieces_storage.empty()) {
                    continue;
                }

                std::cout << "Dump local pieces start" << std::endl;
                // print available pieces in storage
                for (const auto &item : pieces_storage) {
                    std::cout << "Local piece: " << item.first << ". Complete: " << (mTorrentHandle.have_piece(item.first) ? "yes" : "no") << std::endl;
                }
                std::cout << "Dump local pieces end" << std::endl;

                auto it = pieces_storage.cbegin();

                while (it != pieces_storage.end()) {
                    size_t current_piece = it->first;

                    if (current_piece < last_flushed_piece) {
                        std::cout << "Removing old piece from storage " << current_piece << std::endl;
                        it = tmp_storage->remove_piece_from_storage(it);
                        continue;
                    }

                    if (!(
                            current_piece == download_ctx.start_piece_offset ||
                            current_piece == last_flushed_piece + 1
                    )) {
                        break;
                    }

                    if (!mTorrentHandle.have_piece(current_piece)) {
                        std::cout << "Skipping incomplete piece " << current_piece << std::endl;
                        piece_misses[current_piece]++;

                        if (piece_misses[current_piece] > 2) {
                            std::cout << "Set piece deadline " << it->first << std::endl;
                            mTorrentHandle.set_piece_deadline(current_piece, 5000);
                            piece_misses[current_piece] = 0;
                        }

                        break;
                    }

                    piece_misses.erase(current_piece);

//                    std::ostringstream s;
//                    s << "Flushing piece " << current_piece << std::endl;
//                    this->log(s.str());

                    size_t flush_offset;
                    size_t flush_length;

                    if (
                            current_piece == download_ctx.start_piece_offset &&
                            download_ctx.offset_in_first_piece != 0
                            ) {
                        flush_offset = download_ctx.offset_in_first_piece;
                        flush_length = it->second.size() - download_ctx.offset_in_first_piece;
                    } else {
                        flush_offset = 0;
                        flush_length = it->second.size();
                    }

//                    outfile.write(it->second.data() + flush_offset, flush_length);
//                    outfile.flush();
//                    std::cerr.write(it->second.data() + flush_offset, flush_length);
//                    std::cerr.flush();

                    if (mDataCallback) {
                        mDataCallback(it->second.data() + flush_offset, flush_length);
                    }

                    last_flushed_piece = current_piece;

                    it = tmp_storage->remove_piece_from_storage(it);
                }
            }
        }
    }
}

void TStreamer::shutdown() {

}

void TStreamer::log(const std::string& message) const {
    if (mLogCallback) {
        mLogCallback(message);
    }
}
