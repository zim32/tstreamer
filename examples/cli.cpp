#include "../tstreamer.h"

#include <libtorrent/torrent_status.hpp>

#include <iostream>

using namespace std::chrono_literals;

bool sigintReceived = false;

void check_signal(std::unique_ptr<TStreamer> &t) {
    if (sigintReceived) {
        std::cout << "SIGNAL received. Shutting down...\n";
        t.reset(); // release unique pointer to destroy TStreamer
        exit(0);
    }
}

int main(int argc, char const* argv[]) {
    if (argc < 2) {
        std::cout << "Usage: ./tstreamer file_name [--download]\n";
        exit(1);
    }

    auto fileName = std::string(argv[1]);

    auto t = TStreamer::createFromFileName(fileName);

    auto sigHandler = [](int signum) {
        sigintReceived = true;
    };

    // signal handlers
    signal(SIGINT, sigHandler);
    signal(SIGTERM, sigHandler);
    signal(SIGSTOP, sigHandler);
    signal(SIGPIPE, sigHandler);

    t->mLogCallback = [](const std::string &message) {
        std::cout << message << std::endl;
    };

    t->mDataCallback = [&t](const char *buff, size_t length) {
        check_signal(t);
        std::cerr.write(buff, length);
        std::cerr.flush();

        t->dumpPiecesInfo();
    };

    t->dumpFilesInfo();
    t->setDownloadFileIndex(0);
    t->setMemoryLimit(100 * 1024 * 1024);
    t->setPiecesToPreBuffer(1);

    if (argc == 3 && std::strcmp(argv[2], "--download") == 0) {
        t->start(); // block till the end of data
    }

    return 0;
}