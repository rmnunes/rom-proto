// WASM entry point — ensures the C API symbols are linked in.
// The actual API is defined in include/protocoll/protocoll.h and
// compiled into the static library. This file just forces linkage.

#include "protocoll/protocoll.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>

// Keep the module alive (prevent Emscripten from exiting)
extern "C" {
    EMSCRIPTEN_KEEPALIVE
    int pcol_wasm_version(void) {
        return 1;
    }
}
#endif
