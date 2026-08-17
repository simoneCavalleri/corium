#pragma once

#include <cstdio>
#include <cstdlib>
#include <cstdint>

namespace corium {

/// @ingroup embedded
/// @brief Signature of the custom bare-metal panic handler callback.
using PanicHandlerFn = void (*)(const char* file, int line, const char* message) noexcept;

namespace internal {

inline PanicHandlerFn& getPanicHandler() noexcept {
    static PanicHandlerFn handler = nullptr;
    return handler;
}

inline void panic(const char* file, int line, const char* message) noexcept {
    if (auto fn = getPanicHandler()) {
        fn(file, line, message);
        return;
    }
#if defined(CORIUM_CUSTOM_PANIC)
    CORIUM_CUSTOM_PANIC(file, line, message);
#elif defined(__arm__) || defined(__thumb__)
    __asm volatile("bkpt #0");
    for (;;) {}
#else
    std::fprintf(stderr, "Corium Panic: %s (%s:%d)\n", message, file, line);
    std::abort();
#endif
}

} // namespace internal

/// @ingroup embedded
/// @brief Configure the global bare-metal panic handler hook (e.g. for Hardware Fault logging).
inline void setPanicHandler(PanicHandlerFn fn) noexcept {
    internal::getPanicHandler() = fn;
}

} // namespace corium

#if defined(CORIUM_ENABLE_ASSERTIONS) || !defined(NDEBUG)
#define CORIUM_ASSERT(cond, msg) \
    do { \
        if (!(cond)) [[unlikely]] { \
            ::corium::internal::panic(__FILE__, __LINE__, msg); \
        } \
    } while (0)
#else
#define CORIUM_ASSERT(cond, msg) do { (void)sizeof(cond); } while (0)
#endif

#define CORIUM_PANIC(msg) ::corium::internal::panic(__FILE__, __LINE__, msg)
