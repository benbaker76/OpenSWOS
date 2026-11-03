// OpenSWOS Setup's languages: see tr.hpp.
#include "tr.hpp"

#include <cstdlib>
#include <cstring>
#include <unordered_map>

#ifdef _WIN32
#include <windows.h>
#elif defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#endif

namespace sd {

const Language kLanguages[] = {
    {"en", "English"}, {"de", "Deutsch"}, {"fr", "Français"}, {"es", "Español"}, {"it", "Italiano"}, {"nl", "Nederlands"},
};
const int kLanguageCount = int(sizeof kLanguages / sizeof kLanguages[0]);

namespace {

struct Entry {
  const char* lang;
  const char* en;
  const char* tr;
};
const Entry kTable[] = {
#include "tr_table.inc"
    {nullptr, nullptr, nullptr},
};

std::string g_lang = "en";
std::unordered_map<std::string, const char*> g_map;   // the current language's

bool Known(const std::string& code) {
  for (int i = 0; i < kLanguageCount; i++)
    if (code == kLanguages[i].code) return true;
  return false;
}

}  // namespace

void SetLanguage(const std::string& code) {
  g_lang = Known(code) ? code : "en";
  g_map.clear();
  for (const Entry* e = kTable; e->lang; e++)
    if (g_lang == e->lang && e->tr[0]) g_map[e->en] = e->tr;
}

const std::string& CurrentLanguage() { return g_lang; }

std::string SystemLanguage() {
  std::string code;
#ifdef _WIN32
  switch (PRIMARYLANGID(GetUserDefaultUILanguage())) {
    case LANG_GERMAN: code = "de"; break;
    case LANG_FRENCH: code = "fr"; break;
    case LANG_SPANISH: code = "es"; break;
    case LANG_ITALIAN: code = "it"; break;
    case LANG_DUTCH: code = "nl"; break;
    default: code = "en";
  }
#elif defined(__APPLE__)
  if (CFArrayRef langs = CFLocaleCopyPreferredLanguages()) {
    if (CFArrayGetCount(langs) > 0) {
      char buf[16] = "";
      if (CFStringGetCString((CFStringRef)CFArrayGetValueAtIndex(langs, 0), buf, sizeof buf, kCFStringEncodingUTF8))
        code.assign(buf, 2);
    }
    CFRelease(langs);
  }
#else
  for (const char* var : {"LC_ALL", "LC_MESSAGES", "LANG"}) {
    const char* v = std::getenv(var);
    if (v && std::strlen(v) >= 2) {
      code.assign(v, 2);
      break;
    }
  }
#endif
  return Known(code) ? code : "en";
}

std::string T(const char* en) {
  if (!en || !*en || g_map.empty()) return en ? en : "";
  auto it = g_map.find(en);
  return it == g_map.end() ? std::string(en) : std::string(it->second);
}

namespace detail {
std::string Fill(std::string text, const std::string* args, int count) {
  for (int i = 0; i < count; i++) {
    const std::string mark = "{" + std::to_string(i) + "}";
    for (size_t at = text.find(mark); at != std::string::npos; at = text.find(mark, at + args[i].size()))
      text.replace(at, mark.size(), args[i]);
  }
  return text;
}
}  // namespace detail

}  // namespace sd
