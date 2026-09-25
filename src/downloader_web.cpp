// The browser build has no curl, and Chorus Encore's servers are not set up to
// answer a web page on another site, so the download screen explains that
// instead of opening. Songs come in through the page's "Add songs" button.
#include "downloader.hpp"
#include <stdexcept>

namespace fret {
struct Downloader::Network {};

Downloader::Downloader(std::filesystem::path library) : library(std::move(library)) {
    throw std::runtime_error("Downloads need the Switch or desktop version - use Add songs below the game");
}
Downloader::~Downloader() = default;
void Downloader::search(const std::string &) {}
void Downloader::nextPage() {}
void Downloader::download(const enchor::Chart &) {}
void Downloader::unqueue(const std::string &) {}
void Downloader::cancel() {}
void Downloader::cancelAll() {}
Downloader::View Downloader::view() const { return {}; }
int Downloader::completed() const { return 0; }
bool Downloader::busy() const { return false; }
bool Downloader::inLibrary(const enchor::Chart &) const { return false; }
void Downloader::wantArt(const std::string &) {}
std::shared_ptr<const std::string> Downloader::art(const std::string &) const { return {}; }
} // namespace fret
