#include "../tstreamer.h"

#include <iostream>

int main(int argc, char const* argv[]) {

    auto t = TStreamer::createFromFileName("Avengers.torrent");

//    // signal handler
//    signal(SIGINT, [](int signum) {
//
//    });

    t->dumpFilesInfo();
    t->setDownloadFileIndex(0);
    t->start();

    return 0;

}