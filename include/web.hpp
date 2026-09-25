#pragma once
// The browser build (Emscripten). Elsewhere these do nothing, so callers need
// no #ifdef of their own.

namespace fret::web {
#ifdef __EMSCRIPTEN__
// Hands the frame to the browser and returns at its next animation frame. The
// game keeps its own main loop (ASYNCIFY unwinds it here), so without this the
// page would never repaint or deliver input.
void endFrame();
// Song folders the page has copied into the library so far. The song list
// checks the library again whenever this changes, as it does after a download.
int songsAdded();
// Tells the page the player quit, so it can offer to start again rather than
// leave the last frame frozen on the canvas.
void closed();
#else
inline void endFrame() {}
inline int songsAdded() { return 0; }
inline void closed() {}
#endif
} // namespace fret::web
