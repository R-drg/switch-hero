#include "downloader.hpp"
#include "background.hpp"
#include <algorithm>
#include <cstdio>
#include <curl/curl.h>
#include <stdexcept>
#ifdef __SWITCH__
#include <switch.h>
#endif

namespace fret {
namespace fs = std::filesystem;
namespace {
constexpr int perPage = 25;
#ifndef SWITCH_HERO_VERSION
#define SWITCH_HERO_VERSION "dev"
#endif
const char *const userAgent = "switch-hero/" SWITCH_HERO_VERSION " (homebrew rhythm game)";

constexpr size_t artCacheSize = 32;
constexpr size_t artMaxBytes = 4u << 20;
} // namespace

// Sockets are only brought up once the download screen is first opened, so
// players who never use it pay nothing for them.
struct Downloader::Network {
    Network() {
#ifdef __SWITCH__
        if (R_FAILED(socketInitializeDefault()))
            throw std::runtime_error("Could not start networking");
#endif
        if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
#ifdef __SWITCH__
            socketExit();
#endif
            throw std::runtime_error("Could not start networking");
        }
    }
    ~Network() {
        curl_global_cleanup();
#ifdef __SWITCH__
        socketExit();
#endif
    }
};

namespace {
struct Handle {
    CURL *curl = curl_easy_init();
    curl_slist *headers = nullptr;
    char error[CURL_ERROR_SIZE] = {};
    Handle() {
        if (!curl)
            throw std::runtime_error("Could not start networking");
        curl_easy_setopt(curl, CURLOPT_USERAGENT, userAgent);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
        curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, error);
        // Give up on a transfer that stalls for 30 s rather than hanging forever.
        curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1L);
        curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 30L);
    }
    ~Handle() {
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
    }
    // Runs the transfer and turns every failure into a readable message.
    void perform() {
        const CURLcode code = curl_easy_perform(curl);
        if (code == CURLE_ABORTED_BY_CALLBACK)
            throw std::runtime_error("Cancelled");
        if (code != CURLE_OK)
            throw std::runtime_error(code == CURLE_COULDNT_RESOLVE_HOST || code == CURLE_COULDNT_CONNECT
                                         ? std::string("No connection - is the console online?")
                                         : std::string("Network error: ") + (error[0] ? error : curl_easy_strerror(code)));
        long status = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
        // The search endpoint answers 201 Created on success.
        if (status < 200 || status >= 300)
            throw std::runtime_error("Server said " + std::to_string(status));
    }
};

size_t toString(char *data, size_t size, size_t count, void *out) {
    static_cast<std::string *>(out)->append(data, size * count);
    return size * count;
}
size_t toFile(char *data, size_t size, size_t count, void *out) {
    return std::fwrite(data, size, count, static_cast<FILE *>(out)) * size;
}
} // namespace

Downloader::Downloader(std::filesystem::path root) : library(std::move(root)) {
    worker = std::thread([this] { run(); });
    artWorker = std::thread([this] { runArt(); });
}
Downloader::~Downloader() {
    {
        std::lock_guard<std::mutex> lock(mutex);
        stopping = true;
        cancelled = true;
    }
    wake.notify_all();
    artWake.notify_all();
    worker.join();
    artWorker.join();
}
void Downloader::startNetwork() {
    std::lock_guard<std::mutex> lock(networkMutex);
    if (!network)
        network = std::make_unique<Network>();
}

void Downloader::wantArt(const std::string &url) {
    std::lock_guard<std::mutex> lock(mutex);
    if (url.empty())
        return;
    for (const auto &entry : artCache)
        if (entry.first == url)
            return;
    artWanted = url;
    artWake.notify_all();
}
std::shared_ptr<const std::string> Downloader::art(const std::string &url) const {
    std::lock_guard<std::mutex> lock(mutex);
    for (const auto &entry : artCache)
        if (entry.first == url)
            return entry.second;
    return nullptr;
}
void Downloader::runArt() {
    runInBackground();
    while (true) {
        std::string url;
        {
            std::unique_lock<std::mutex> lock(mutex);
            artWake.wait(lock, [&] { return stopping || !artWanted.empty(); });
            if (stopping)
                return;
            url = std::move(artWanted);
            artWanted.clear();
        }
        std::string bytes;
        try {
            startNetwork();
            Handle h;
            curl_easy_setopt(h.curl, CURLOPT_URL, url.c_str());
            curl_easy_setopt(h.curl, CURLOPT_TIMEOUT, 20L);
            curl_easy_setopt(h.curl, CURLOPT_WRITEFUNCTION, toString);
            curl_easy_setopt(h.curl, CURLOPT_WRITEDATA, &bytes);
            // Abort covers that are unreasonably large, and quit on shutdown.
            curl_easy_setopt(h.curl, CURLOPT_NOPROGRESS, 0L);
            curl_easy_setopt(h.curl, CURLOPT_XFERINFODATA, this);
            curl_easy_setopt(
                h.curl, CURLOPT_XFERINFOFUNCTION,
                +[](void *data, curl_off_t, curl_off_t now, curl_off_t, curl_off_t) -> int {
                    return static_cast<Downloader *>(data)->stopping || now > curl_off_t(artMaxBytes) ? 1 : 0;
                });
            h.perform();
        } catch (const std::exception &) {
            bytes.clear(); // no cover is shown; the chart details still are
        }
        std::lock_guard<std::mutex> lock(mutex);
        artCache.emplace_back(url, std::make_shared<const std::string>(std::move(bytes)));
        if (artCache.size() > artCacheSize)
            artCache.erase(artCache.begin());
    }
}

void Downloader::search(const std::string &query) {
    std::lock_guard<std::mutex> lock(mutex);
    pending = {Job::Kind::Search, query, 1, {}};
    state.query = query;
    state.results.clear();
    state.found = 0, state.page = 0;
    state.error.clear();
    wake.notify_all();
}
void Downloader::nextPage() {
    std::lock_guard<std::mutex> lock(mutex);
    // Browsing carries on while the queue downloads: the page is fetched
    // before the next queued chart starts.
    if (state.state == State::Searching || pending.kind != Job::Kind::None || !state.more())
        return;
    pending = {Job::Kind::Search, state.query, state.page + 1, {}};
    wake.notify_all();
}
void Downloader::download(const enchor::Chart &chart) {
    std::lock_guard<std::mutex> lock(mutex);
    if (chart.md5 == state.current ||
        std::find(state.queued.begin(), state.queued.end(), chart.md5) != state.queued.end())
        return;
    queue.push_back(chart);
    state.queued.push_back(chart.md5);
    state.error.clear();
    wake.notify_all();
}
void Downloader::unqueue(const std::string &md5) {
    std::lock_guard<std::mutex> lock(mutex);
    queue.erase(std::remove_if(queue.begin(), queue.end(), [&](const enchor::Chart &c) { return c.md5 == md5; }),
                queue.end());
    state.queued.erase(std::remove(state.queued.begin(), state.queued.end(), md5), state.queued.end());
}
void Downloader::cancel() { cancelled = true; }
void Downloader::cancelAll() {
    std::lock_guard<std::mutex> lock(mutex);
    queue.clear();
    state.queued.clear();
    cancelled = true;
}
Downloader::View Downloader::view() const {
    std::lock_guard<std::mutex> lock(mutex);
    return state;
}
int Downloader::completed() const {
    std::lock_guard<std::mutex> lock(mutex);
    return state.downloads;
}
bool Downloader::busy() const {
    std::lock_guard<std::mutex> lock(mutex);
    return !state.current.empty() || !queue.empty();
}
bool Downloader::inLibrary(const enchor::Chart &chart) const {
    std::error_code ec;
    return fs::is_directory(library / enchor::folderName(chart), ec);
}

void Downloader::run() {
    runInBackground(); // parsing results and unpacking charts is real work
    while (true) {
        Job job;
        {
            std::unique_lock<std::mutex> lock(mutex);
            wake.wait(lock, [&] { return stopping || pending.kind != Job::Kind::None || !queue.empty(); });
            if (stopping)
                return;
            if (pending.kind != Job::Kind::None) {
                job = std::move(pending);
                pending = {};
            } else {
                job = {Job::Kind::Download, {}, 0, std::move(queue.front())};
                queue.pop_front();
                state.queued.erase(state.queued.begin());
                state.current = job.chart.md5, state.currentName = job.chart.name;
                cancelled = false;
            }
            state.state = job.kind == Job::Kind::Search ? State::Searching : State::Downloading;
            state.status = job.kind == Job::Kind::Search ? "searching" : "connecting";
            state.progress = 0, state.megabytes = 0;
        }
        try {
            startNetwork();
            if (job.kind == Job::Kind::Search)
                runSearch(job);
            else
                runDownload(job);
        } catch (const std::exception &e) {
            std::lock_guard<std::mutex> lock(mutex);
            state.error = cancelled && job.kind == Job::Kind::Download ? "Download cancelled" : e.what();
        }
        std::lock_guard<std::mutex> lock(mutex);
        state.state = State::Idle;
        state.status.clear();
        if (job.kind == Job::Kind::Download)
            state.current.clear(), state.currentName.clear();
    }
}

void Downloader::runSearch(const Job &job) {
    Handle h;
    const std::string body = enchor::searchBody(job.query, job.page, perPage);
    std::string response;
    h.headers = curl_slist_append(h.headers, "Content-Type: application/json");
    curl_easy_setopt(h.curl, CURLOPT_URL, enchor::searchUrl);
    curl_easy_setopt(h.curl, CURLOPT_HTTPHEADER, h.headers);
    curl_easy_setopt(h.curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(h.curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(h.curl, CURLOPT_WRITEFUNCTION, toString);
    curl_easy_setopt(h.curl, CURLOPT_WRITEDATA, &response);
    try {
        h.perform();
    } catch (const std::runtime_error &) {
        // A rejected query still explains itself in the body.
        if (!response.empty())
            enchor::parseSearch(response);
        throw;
    }
    auto page = enchor::parseSearch(response);
    std::lock_guard<std::mutex> lock(mutex);
    // A newer search may have started while this one was in flight.
    if (state.query != job.query)
        return;
    if (job.page == 1)
        state.results.clear();
    state.results.insert(state.results.end(), page.charts.begin(), page.charts.end());
    state.found = page.found;
    state.page = job.page;
}

void Downloader::runDownload(const Job &job) {
    const fs::path temp = library / ".switch-hero-download.sng";
    std::error_code ec;
    fs::create_directories(library, ec);
    FILE *file = std::fopen(temp.string().c_str(), "wb");
    if (!file)
        throw std::runtime_error("Cannot write to the songs folder");
    struct Progress {
        Downloader *self;
    } progress{this};
    try {
        Handle h;
        const std::string url = enchor::downloadUrl(job.chart);
        curl_easy_setopt(h.curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(h.curl, CURLOPT_WRITEFUNCTION, toFile);
        curl_easy_setopt(h.curl, CURLOPT_WRITEDATA, file);
        curl_easy_setopt(h.curl, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(h.curl, CURLOPT_XFERINFODATA, &progress);
        curl_easy_setopt(
            h.curl, CURLOPT_XFERINFOFUNCTION,
            +[](void *data, curl_off_t total, curl_off_t now, curl_off_t, curl_off_t) -> int {
                auto *self = static_cast<Progress *>(data)->self;
                std::lock_guard<std::mutex> lock(self->mutex);
                self->state.status = "downloading";
                self->state.megabytes = double(now) / (1024 * 1024);
                self->state.progress = total > 0 ? float(double(now) / double(total)) : 0;
                return self->cancelled ? 1 : 0;
            });
        h.perform();
        if (std::fclose(file) != 0) {
            file = nullptr;
            throw std::runtime_error("Could not write to the SD card");
        }
        file = nullptr;
        {
            std::lock_guard<std::mutex> lock(mutex);
            state.status = "unpacking";
            state.progress = 1;
        }
        const auto name = enchor::folderName(job.chart);
        enchor::unpackSng(temp, library / name);
        fs::remove(temp, ec);
        std::lock_guard<std::mutex> lock(mutex);
        ++state.downloads;
        state.lastFolder = name;
    } catch (...) {
        if (file)
            std::fclose(file);
        fs::remove(temp, ec);
        throw;
    }
}
} // namespace fret
