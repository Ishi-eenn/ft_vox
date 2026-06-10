// Single compilation unit that expands the miniaudio and stb_vorbis
// implementation bodies.  No other .cpp must define MINIAUDIO_IMPLEMENTATION.

#if defined(__clang__)
#  pragma clang diagnostic push
#  pragma clang diagnostic ignored "-Wunused-function"
#  pragma clang diagnostic ignored "-Wsign-compare"
#  pragma clang diagnostic ignored "-Wimplicit-fallthrough"
#  pragma clang diagnostic ignored "-Wdeprecated-declarations"
#  pragma clang diagnostic ignored "-Wmissing-field-initializers"
#elif defined(__GNUC__)
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wunused-function"
#  pragma GCC diagnostic ignored "-Wsign-compare"
#  pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#  pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#  pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#  pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#endif

// stb_vorbis must be included before MINIAUDIO_IMPLEMENTATION
// so that miniaudio can detect and use it for OGG decoding.
#include "stb_vorbis.c"

#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>

#if defined(__clang__)
#  pragma clang diagnostic pop
#elif defined(__GNUC__)
#  pragma GCC diagnostic pop
#endif
