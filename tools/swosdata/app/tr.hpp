// OpenSWOS Setup's languages (app/lang/README.md): the game's own -- English, German, French,
// Spanish, Italian, Dutch.
//
// One set of gettext files, app/lang/<code>/LC_MESSAGES/openswos-setup.po, serves both halves:
// the .slint files' @tr() texts are bundled into the program by the Slint compiler, and the
// texts made here in C++ -- statuses, what was found, the C library's step names, the GAME
// OPTIONS -- are looked up by T() in a table made from the same files (tr_table.inc, by
// lang/make_tr.py). A text with no translation is shown in English.
#pragma once

#include <string>

// An English text that is translated later, where it is shown (T(msg)): marked for
// lang/extract.py, nothing else.
#define N_(text) text

namespace sd {

struct Language {
  const char* code;   // "de": the .po folder, and SWOS.ini's [general] language
  const char* name;   // in itself: "Deutsch"
};
extern const Language kLanguages[];
extern const int kLanguageCount;

// The language the setup speaks ("en" when the code is not one of them).
void SetLanguage(const std::string& code);
const std::string& CurrentLanguage();

// The system's language when it is one of ours, else "en".
std::string SystemLanguage();

// `en` in the current language.
std::string T(const char* en);
inline std::string T(const std::string& en) { return T(en.c_str()); }

// T(en) with {0}, {1}, ... replaced by the arguments (the same placeholders as Slint's @tr).
namespace detail {
std::string Fill(std::string text, const std::string* args, int count);
inline std::string Arg(const std::string& s) { return s; }
inline std::string Arg(const char* s) { return s; }
template <typename N> std::string Arg(N n) { return std::to_string(n); }
}  // namespace detail
template <typename... A>
std::string Tf(const char* en, const A&... args) {
  const std::string list[] = {detail::Arg(args)...};
  return detail::Fill(T(en), list, int(sizeof...(A)));
}

}  // namespace sd
