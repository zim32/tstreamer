#ifndef T_STREAMER_LIB_LIBRARY_H
#define T_STREAMER_LIB_LIBRARY_H

#include <libtorrent/torrent_info.hpp>
#include <libtorrent/session.hpp>

#include <string>
#include <memory>
#include <functional>

class TStreamer {

public:
    TStreamer(const TStreamer&) = delete;
    TStreamer(const TStreamer&&) = delete;
    TStreamer& operator=(TStreamer) = delete;
    TStreamer& operator=(const TStreamer&) = delete;

    static std::shared_ptr<TStreamer> createFromFileName(const std::string &fileName);

    // Prints info about files in torrent to stdout
    void dumpFilesInfo() const;

    void setDownloadFileIndex(uint index);

    void start();
    void pause();
    void resume();
    void shutdown();

    std::function<void(const char* , size_t)> mDataCallback;
    std::function<void(std::string)> mLogCallback;

private:
    TStreamer() = default;

    std::shared_ptr<lt::torrent_info> mTorrentInfo;
    std::unique_ptr<lt::session> mSession;
    lt::torrent_handle mTorrentHandle;

    uint mDownloadFileIndex = 0;
    bool mInited  = false;

    void startMainLoop();

    void log(const std::string&) const;
};

#endif //T_STREAMER_LIB_LIBRARY_H
