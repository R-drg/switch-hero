#pragma once
// Console state the game has to react to. On desktop these answer with the
// values that keep every feature reachable, so multiplayer can be developed and
// tested without a console in hand.
#ifdef __SWITCH__
#include <switch.h>
#endif

namespace fret::platform {
// Docked, as in sitting in the dock driving a TV. Multiplayer is docked-only:
// four highways on the handheld screen would be unreadable, and the second to
// fourth players have no way to hold a Joy-Con that is attached to the console.
inline bool docked() {
#ifdef __SWITCH__
    return appletGetOperationMode() == AppletOperationMode_Console;
#else
    return true;
#endif
}
} // namespace fret::platform
