#pragma once
#include "enchor.hpp"
#include <atomic>
#include <condition_variable>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace fret {
// Searches Chorus Encore and downloads charts into the song library on a
// worker thread, so the screen keeps drawing while the network is slow. The
// UI posts jobs and reads a snapshot each frame.
class Downloader {
  public:
    enum class State { Idle, Searching, Downloading };
    struct View {
        State state = State::Idle;
        std::vector<enchor::Chart> results;
        std::string query, error, status;
        int found = 0, page = 0;
        float progress = 0;     // of the current download, 0..1
        double megabytes = 0;   // downloaded so far
        int downloads = 0;      // finished this session
        std::string lastFolder; // the newest finished download
        bool more() const { return int(results.size()) < found; }
    };

    explicit Downloader(std::filesystem::path library);
    ~Downloader();
    Downloader(const Downloader &) = delete;
    Downloader &operator=(const Downloader &) = delete;

    // A new query replaces the results; nextPage() appends to them.
    void search(const std::string &query);
    void nextPage();
    void download(const enchor::Chart &chart);
    // Abandons a download in progress.
    void cancel();
    View view() const;
    bool inLibrary(const enchor::Chart &chart) const;

  private:
    struct Job {
        enum class Kind { None, Search, Download } kind = Kind::None;
        std::string query;
        int page = 1;
        enchor::Chart chart;
    };
    void run();
    void runSearch(const Job &job);
    void runDownload(const Job &job);

    std::filesystem::path library;
    mutable std::mutex mutex;
    std::condition_variable wake;
    Job pending;
    View state;
    std::atomic<bool> stopping{false}, cancelled{false};
    std::thread worker;
};
} // namespace fret
