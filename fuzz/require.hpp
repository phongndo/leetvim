#pragma once

#include <cstdio>
#include <cstdlib>

// Reports a broken property and aborts, so libFuzzer saves the input that broke it.
inline void require(bool holds, const char* property) {
    if (holds) return;
    (void)std::fprintf(stderr, "property failed: %s\n", property);
    std::abort();
}
