#ifndef T_STREAMER_LIB_LIBRARY_H
#define T_STREAMER_LIB_LIBRARY_H

#include <libtorrent/torrent_info.hpp>
#include <libtorrent/session.hpp>

#include <string>
#include <memory>
#include <functional>
#include <thread>

#include "mem_storage.hpp"

class TStreamer {

public:
    // prevent copying
    TStreamer(const TStreamer&) = delete;
    TStreamer(const TStreamer&&) = delete;
    TStreamer& operator=(TStreamer) = delete;
    TStreamer& operator=(const TStreamer&) = delete;

    static std::unique_ptr<TStreamer> createFromFileName(const std::string &fileName);

    // Prints info about files in torrent to stdout
    void dumpFilesInfo() const;
    void dumpPiecesInfo();

    // Return number of bytes currently stored in memory
    size_t getMemoryUsed() const;
    const download_context_t& getDownloadContext() const;
    lt::torrent_status getTorrentStatus() const;

    void setDownloadFileIndex(uint index);
    void setDownloadLimit(int limit);
    void setUploadLimit(int limit);
    void setMemoryLimit(int limit);
    void setPiecesToPreBuffer(uint pieces);

    void start();
    void pause();
    void resume();
    void shutdown();

    std::function<void(const char* , size_t)> mDataCallback;
    std::function<void(const std::string&)> mLogCallback;

private:
    TStreamer() = default;

    std::shared_ptr<lt::torrent_info> mTorrentInfo;
    std::unique_ptr<lt::session> mSession;
    lt::torrent_handle mTorrentHandle;

    uint mDownloadFileIndex = 0;
    bool mInited  = false;
    int mDownloadLimit = -1;
    int mUploadLimit = -1;
    int mMemoryLimit = -1;
    uint mPiecesToPreBuffer = 0;
    bool mPaused = false;
    bool mPausedByMemoryLimit = false;
    long mLastFlushedPiece = -1;
    void flushPieces();
    void requestPieces();

    void startMainLoop();
    void setAllPiecesPriority(lt::download_priority_t);
    bool checkPreBuffer() const;

    void log(const std::string&) const;
};

#endif //T_STREAMER_LIB_LIBRARY_H
