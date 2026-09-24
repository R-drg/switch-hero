#pragma once
// Interface text in English or Portuguese. Strings are looked up by their
// English text, so the source reads as it always has; anything without a
// translation (song titles, error details) passes through unchanged.
#include <initializer_list>
#include <string>

namespace fret::lang {
enum class Language { English, Portuguese };
constexpr int Count = 2;
// Each language's own name, as the picker shows it.
const char *nativeName(Language l);

void set(Language l);
Language current();
inline bool portuguese() { return current() == Language::Portuguese; }

const char *tr(const char *english);
std::string tr(const std::string &english);

// Translates `english`, then fills each "{}" with the next argument in order.
std::string fill(const std::string &pattern, std::initializer_list<std::string> args);
inline std::string arg(const std::string &s) { return s; }
inline std::string arg(const char *s) { return s; }
template <class T> std::string arg(T n) { return std::to_string(n); }
template <class A, class... R> std::string tr(const char *english, const A &first, const R &...rest) {
    return fill(tr(english), {arg(first), arg(rest)...});
}
} // namespace fret::lang
