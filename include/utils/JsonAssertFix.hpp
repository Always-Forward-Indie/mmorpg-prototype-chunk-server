#pragma once
// Override nlohmann's default assert()-based JSON_ASSERT which is removed
// in Release/NDEBUG builds, making malformed JSON silently pass through.
#ifndef JSON_ASSERT
#define JSON_ASSERT(x) if (!(x)) { std::abort(); }
#endif
