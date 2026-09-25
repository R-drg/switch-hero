#include "web.hpp"
#include <atomic>
#include <emscripten.h>

namespace {
std::atomic<int> added{0};
} // namespace

// Called by the page (web/shell.html) once it has written a song folder into
// the library and saved it to IndexedDB.
extern "C" EMSCRIPTEN_KEEPALIVE void fret_web_songs_added() { ++added; }

EM_ASYNC_JS(void, waitForAnimationFrame, (), { await new Promise(resolve => requestAnimationFrame(resolve)); });

namespace fret::web {
void endFrame() { waitForAnimationFrame(); }
int songsAdded() { return added; }
void closed() {
    MAIN_THREAD_EM_ASM({
        if (Module.onQuit)
            Module.onQuit();
    });
}
} // namespace fret::web
