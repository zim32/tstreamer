#include "tstreamer.h"
#include "mem_storage.hpp"

#include <libtorrent/torrent_info.hpp>
#include <libtorrent/settings_pack.hpp>
#include <libtorrent/alert_types.hpp>

#include <fmt/format.h>

#include <iostream>
#include <memory>
#include <stdexcept>
#include <chrono>

using namespace std::chrono_literals;

download_context_t download_ctx;

std::unique_ptr<TStreamer> TStreamer::createFromFileName(const std::string &fileName) {
    auto t = new TStreamer();

    t->mTorrentInfo = std::make_shared<libtorrent::torrent_info>(fileName);

    return std::unique_ptr<TStreamer>(t);
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

    if (!mDataCallback) {
        throw std::logic_error("Can not start. Data callback not provided");
    }

    if (mMemoryLimit == -1) {
        this->log("Warning: memory limit is not set. It is advisable to set memory limit. Default is 100BM");
        mMemoryLimit = 100 * 1024 * 1024;
    }

    this->log("Starting TStreamer...");

    // settings pack
    lt::settings_pack pack;
    pack.set_int(lt::settings_pack::alert_mask, lt::alert::error_notification | lt::alert::piece_progress_notification);

    mSession = std::make_unique<lt::session>(pack);

    // add torrent params
    lt::add_torrent_params atp(temp_storage_constructor);
    atp.save_path = ".";
    atp.ti = mTorrentInfo;
    atp.flags = lt::torrent_flags::sequential_download;

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


    // init last flush piece
    mLastFlushedPiece = static_cast<long>(download_ctx.start_piece_offset) - 1;

    // add torrent to session
    mTorrentHandle = mSession->add_torrent(atp);

    // force to download first piece first
    this->setAllPiecesPriority(lt::dont_download);
    mTorrentHandle.set_piece_deadline(download_ctx.start_piece_offset, 1000);

    // set limits
    mTorrentHandle.set_download_limit(mDownloadLimit);
    mTorrentHandle.set_upload_limit(mUploadLimit);

    mInited = true;

//    this->startServiceLoop();
    this->startMainLoop();
}

void TStreamer::setDownloadFileIndex(uint index) {
    if (mInited) {
        throw std::logic_error("Can not set download file index after start");
    }

    mDownloadFileIndex = index;
}

void TStreamer::pause() {
    if (mPaused) {
        return;
    }
    mTorrentHandle.pause(lt::torrent_handle::graceful_pause);
    mPaused = true;
}

void TStreamer::resume() {
    if (!mPaused) {
        return;
    }
    mTorrentHandle.resume();
    mPaused = false;
}

void TStreamer::startMainLoop() {
    if (!mInited) {
        throw std::logic_error("Can not start main loop (not inited)");
    }

    this->log("Starting main loop...");

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
                this->log(fmt::format("Piece finished {}", piece_index));

                if (!this->checkPreBuffer()) {
                    continue;
                }

                this->flushPieces();
            }
        }

        this->flushPieces();

        // break main loop if last piece was flushed
        if (mLastFlushedPiece == download_ctx.end_piece_offset) {
            return;
        }

        // all alerts processed, sleep for some time
        std::this_thread::sleep_for(1s);
    }
}

void TStreamer::shutdown() {
    mSession->abort();
}

void TStreamer::log(const std::string& message) const {
    if (mLogCallback) {
        mLogCallback(message);
    }
}

size_t TStreamer::getMemoryUsed() const {
    auto tmp_storage = dynamic_cast<temp_storage*>(mTorrentHandle.get_storage_impl());
    const pieces_container::storage_t &pieces_storage = tmp_storage->get_pieces_storage();

    return mTorrentInfo->piece_length() * pieces_storage.size();
}

const download_context_t& TStreamer::getDownloadContext() const {
    return download_ctx;
}

void TStreamer::setDownloadLimit(int limit) {
    if (mInited) {
        mTorrentHandle.set_download_limit(limit);
    }

    mDownloadLimit = limit;
}

void TStreamer::setUploadLimit(int limit) {
    if (mInited) {
        mTorrentHandle.set_upload_limit(limit);
    }

    mUploadLimit = limit;
}

lt::torrent_status TStreamer::getTorrentStatus() const {
    return mTorrentHandle.status();
}

void TStreamer::setAllPiecesPriority(lt::download_priority_t priority) {
    for (lt::piece_index_t x = 0; x <= mTorrentInfo->last_piece(); x++) {
        mTorrentHandle.piece_priority(x, priority);
    }
}

void TStreamer::setMemoryLimit(int limit) {
    this->mMemoryLimit = limit;
}

void TStreamer::dumpPiecesInfo() {
    auto const tmpStorage = dynamic_cast<temp_storage*>(mTorrentHandle.get_storage_impl());
    const auto &piecesStorage = tmpStorage->get_pieces_storage();

    std::cout << "====== Dump local pieces ======" << std::endl;

    for (const auto &item : piecesStorage) {
        std::cout << "Local piece: " << item.first << ". Complete: " << (mTorrentHandle.have_piece(item.first) ? "yes" : "no") << std::endl;
    }

    std::cout << "================================" << std::endl;
}

bool TStreamer::checkPreBuffer() const {
    if (mLastFlushedPiece >= 0) {
        return true;
    }

    auto left = mPiecesToPreBuffer;

    for (auto x = download_ctx.start_piece_offset; x <= download_ctx.end_piece_offset; x++) {
        if (left <= 0) {
            return true;
        }

        if (mTorrentHandle.have_piece(x)) {
            left--;
        } else {
            return false;
        }
    }

    return false;
}

void TStreamer::setPiecesToPreBuffer(uint pieces) {
    if (mInited) {
        throw std::logic_error("Can not call setPiecesToPreBuffer after init");
    }

    mPiecesToPreBuffer = pieces;
}

void TStreamer::flushPieces() {
    // piece miss map
    static std::map<size_t, uint> piece_misses;

    // get piece data from mem_storage
    auto tmp_storage = dynamic_cast<temp_storage*>(mTorrentHandle.get_storage_impl());
    const pieces_container::storage_t &pieces_storage = tmp_storage->get_pieces_storage();

    if (pieces_storage.empty()) {
        return;
    }

    auto it = pieces_storage.cbegin();

    while (true) {
        if (it == pieces_storage.cend()) {
            break;;
        }

        size_t current_piece = it->first;
        auto neededPiece = mLastFlushedPiece + 1;

        // skip flushed pieces
        if (current_piece < neededPiece) {
            it++;
            continue;
        }

        // skip future piece
        if (current_piece != neededPiece) {
            break;
        }

        // check piece is complete
        if (!mTorrentHandle.have_piece(neededPiece)) {
            this->log(fmt::format("Skipping incomplete piece {}", neededPiece));
            break;
        }

        piece_misses.erase(current_piece);

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

        if (mDataCallback) {
            this->log(fmt::format("Flushing piece {} ...", current_piece));
            // this potentially could block for a long time
            mDataCallback(it->second.data() + flush_offset, flush_length);
            this->log(fmt::format("Flushing of piece {} done", current_piece));
        }

        mLastFlushedPiece = current_piece;

        it = tmp_storage->remove_piece_from_storage(it);
        this->log(fmt::format("Piece {} removed from storage", current_piece));

        this->requestPieces();
    }
}

void TStreamer::requestPieces() {
    // request pieces upfront
    auto neededPiece = mLastFlushedPiece + 1;
    auto piecesToRequest = mMemoryLimit / download_ctx.piece_size;

    if (neededPiece > 0) {
        for (auto x = neededPiece; x <= neededPiece + piecesToRequest; x++) {
            if (mTorrentHandle.piece_priority(x) != lt::dont_download) {
                continue;
            }
            this->log(fmt::format("Requesting piece {} ...", x));
            mTorrentHandle.set_piece_deadline(x, 1000 + x);
            mTorrentHandle.piece_priority(x, lt::default_priority);
        }
    }
}
