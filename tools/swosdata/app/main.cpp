// OpenSWOS Setup: the window over the swosdata library.
//
// Two tabs, one per version of the game. The player gets their own copy of SWOS
// -- the PC game, the Amiga version (floppies, a hard disk image or a WHDLoad
// .lha), the Jaguar cartridge, a SWOS 2020 install for its pitches -- from the
// links on each, and it is found: the Downloads folder is watched, GOG's install
// folder looked up, or a folder searched. The PC tab's Install runs SwosData_Run
// into the assets folder the game reads; the Amiga tab's button does that too,
// then makes a ready SWOS drawer or the WHDLoad package from the Amiga program
// the setup carries (amiga/, an unpacked SWOS-Amiga.zip). Runs happen on a
// worker thread; their log and progress reach the window through
// slint::invoke_from_event_loop.
//
//   openswos-setup [--res DIR] [--scan DIR]... [--out DIR] [--amiga]
//                  [--program DIR] [--drawer-to DIR | --whdload-to FILE]
//   testing:       [--auto] [--snapshot PREFIX] [--snapshot-at MS] [--quit] [--play]
//                  [--no-watch] [--details] [--about] [--downloads DIR] [--no-gog]
//
// --auto presses the open tab's button once the window is up; --snapshot writes
// PREFIX-start.ppm and, with --auto, PREFIX-done.ppm; --quit closes the window
// when it has finished (exit status 0 on success), or after the -start snapshot
// without --auto; --play presses Play OpenSWOS
// when the install is done. --no-watch leaves the Downloads folder alone.
#include <atomic>
#include "../../../include/project_identity.h"
#include "../../../include/game_options.h"   // the wizard's GAME OPTIONS (the game's SWOS.ini keys)
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <cctype>
#include <ctime>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "app.h"
#include "platform.hpp"
#include "phone_server.hpp"
#include "tr.hpp"
using sd::T;
using sd::Tf;

extern "C" {
#include "swosdata.h"
}

#ifdef SD_WITH_DIALOGS
#include <nfd.h>
#endif

// Where World 2026-27's package is downloaded from: hosted apart from OpenSWOS, by its authors
// (the SWOS 96/97 Mod's Davide Lorigliola, 2026-11-02), its squads being theirs; the same URL for
// each new package. "" would show no World 2026-27 row at all (`swosdata --world FILE` still
// installs one, for testing). Its README beside it: the data's sources and credits.
static const char kWorldUrl[] = "https://worldofswos.com/static/openswos/OpenSWOS-World-2627.zip";
static const char kWorldCredits[] = "https://worldofswos.com/static/openswos/README.txt";

namespace {

slint::SharedString Str(const std::string& s) { return slint::SharedString(s); }

std::string Join(const std::string& a, const std::string& b) {
  if (a.empty()) return b;
  const char last = a.back();
  return (last == '/' || last == '\\') ? a + b : a + "/" + b;
}

// A path as the platform writes it. The library joins with '/', which Windows
// accepts but which reads oddly next to a picked "D:\Games\...".
std::string Shown(std::string p) {
#ifdef _WIN32
  for (char& c : p)
    if (c == '/') c = '\\';
#endif
  return p;
}

// --------------------------------------------------------------- pickers ---

#ifdef SD_WITH_DIALOGS
struct NfdSession {
  bool ok;
  NfdSession() : ok(NFD_Init() == NFD_OKAY) {}
  ~NfdSession() {
    if (ok) NFD_Quit();
  }
};
#endif

std::string PickFolder(const std::string& start) {
#ifdef SD_WITH_DIALOGS
  NfdSession s;
  if (!s.ok) return {};
  nfdu8char_t* out = nullptr;
  std::string picked;
  if (NFD_PickFolderU8(&out, start.empty() ? nullptr : start.c_str()) == NFD_OKAY && out) {
    picked = out;
    NFD_FreePathU8(out);
  }
  return picked;
#else
  (void)start;
  return {};
#endif
}

// One file, of the kinds `filterSpec` lists ("adf", "jag,j64"): "" when the
// player cancelled.
std::string PickFile(const char* filterName, const char* filterSpec) {
  std::string picked;
#ifdef SD_WITH_DIALOGS
  NfdSession s;
  if (!s.ok) return picked;
  const nfdu8filteritem_t filter[1] = {{filterName, filterSpec}};
  nfdu8char_t* p = nullptr;
  if (NFD_OpenDialogU8(&p, filter, 1, nullptr) == NFD_OKAY && p) {
    picked = p;
    NFD_FreePathU8(p);
  }
#else
  (void)filterName;
  (void)filterSpec;
#endif
  return picked;
}

std::string PickSaveFile(const char* filterName, const char* filterSpec, const std::string& dir,
                         const std::string& name) {
  std::string picked;
#ifdef SD_WITH_DIALOGS
  NfdSession s;
  if (!s.ok) return picked;
  const nfdu8filteritem_t filter[1] = {{filterName, filterSpec}};
  nfdu8char_t* p = nullptr;
  if (NFD_SaveDialogU8(&p, filter, 1, dir.empty() ? nullptr : dir.c_str(), name.c_str()) == NFD_OKAY &&
      p) {
    picked = p;
    NFD_FreePathU8(p);
  }
#else
  (void)filterName;
  (void)filterSpec;
  (void)dir;
  (void)name;
#endif
  return picked;
}

// ------------------------------------------------------- the game's files ---

// The game's own files (setup/glyphs.txt, data/credits_icons.bmp, ...): beside
// the executable in an install; above it in OpenSWOS-Setup.zip; in a dev build,
// the repo's bin/ above it.
#if defined(__APPLE__)
// On macOS the setup is its own .app beside SWOS.app (SWOS-macOS-app.zip), and
// the game's files are inside that: SWOS.app/Contents/MacOS. exeDir is
// "OpenSWOS Setup.app/Contents/MacOS", three levels below the folder they share.
static std::string GameAppMacOSDir(const std::string& exeDir) {
  const std::string shared = sd::ParentDir(sd::ParentDir(sd::ParentDir(exeDir)));
  const std::string dir = Join(shared, "SWOS.app/Contents/MacOS");
  return sd::IsDir(dir) ? dir : std::string();
}
#endif

std::string FindResDir(const std::string& exeDir) {
  if (exeDir.empty()) return {};
  if (sd::IsFile(Join(exeDir, "setup/glyphs.txt"))) return exeDir;
#if defined(__APPLE__)
  {
    // Its own copy, inside the bundle: found wherever macOS runs it from.
    const std::string res = Join(sd::ParentDir(exeDir), "Resources");
    if (sd::IsFile(Join(res, "setup/glyphs.txt"))) return res;
  }
  {
    const std::string mac = GameAppMacOSDir(exeDir);
    if (!mac.empty() && sd::IsFile(Join(mac, "setup/glyphs.txt"))) return mac;
  }
#endif
  // Above it: OpenSWOS-Setup.zip keeps them above its Windows/Linux/macOS
  // folders; the repo keeps them in bin/.
  for (std::string d = exeDir; !d.empty();) {
    if (sd::IsFile(Join(d, "setup/glyphs.txt"))) return d;
    const std::string bin = Join(d, "bin");
    if (sd::IsFile(Join(bin, "setup/glyphs.txt"))) return bin;
    const std::string up = sd::ParentDir(d);
    if (up == d) break;
    d = up;
  }
  return {};
}

// The game itself, beside this program.
std::string FindGame(const std::string& exeDir) {
  if (exeDir.empty()) return {};
#if defined(_WIN32)
  const char* names[] = {"SWOS.exe"};
#elif defined(__APPLE__)
  const char* names[] = {"SWOS"};
#elif defined(__x86_64__)
  const char* names[] = {"SWOS.x86_64", "SWOS"};
#elif defined(__aarch64__)
  const char* names[] = {"SWOS.aarch64", "SWOS"};
#elif defined(__arm__)
  const char* names[] = {"SWOS.armhf", "SWOS"};
#else
  const char* names[] = {"SWOS"};
#endif
  for (const char* n : names) {
    const std::string p = Join(exeDir, n);
    if (sd::IsFile(p)) return p;
  }
#if defined(__APPLE__)
  {
    const std::string mac = GameAppMacOSDir(exeDir);
    if (!mac.empty() && sd::IsFile(Join(mac, "SWOS"))) return Join(mac, "SWOS");
  }
#endif
  return {};
}

// ------------------------------------------------------------------- app ---

// What the worker hands back. Lines are batched: the worker appends under the
// mutex and posts one flush at a time, so a run that logs thousands of files
// wakes the event loop a few times a frame rather than once a line.
struct Pipe {
  std::mutex mu;
  std::vector<std::string> lines;
  int32_t done = 0, total = 0;
  bool flushQueued = false;
  std::function<void()> flush;  // set by the App; runs on the UI thread
};

void PipeLog(void* user, const char* line) {
  Pipe* p = static_cast<Pipe*>(user);
  bool post = false;
  {
    std::lock_guard<std::mutex> lock(p->mu);
    p->lines.emplace_back(line ? line : "");
    if (!p->flushQueued) post = p->flushQueued = true;
  }
  if (post) slint::invoke_from_event_loop(p->flush);
}

void PipeProgress(void* user, int32_t done, int32_t total) {
  Pipe* p = static_cast<Pipe*>(user);
  bool post = false;
  {
    std::lock_guard<std::mutex> lock(p->mu);
    p->done = done;
    p->total = total;
    if (!p->flushQueued) post = p->flushQueued = true;
  }
  if (post) slint::invoke_from_event_loop(p->flush);
}

struct App {
  slint::ComponentHandle<MainWindow>* ui = nullptr;
  SdSources src{};
  std::string outDir, defaultOutDir, resDir, exeDir, game;
  std::string standing;   // a notice that stays (macOS App Translocation)
  // The Amiga tab: the program (a folder with SWOS.info and the SWOS drawer), and
  // where the drawer or the WHDLoad package goes.
  std::string amigaProgram, drawerTo, whdloadTo;
  std::string programFrom;   // what it was found as, shown: the zip, or the folder
  bool ownProgram = false;
  bool amigaRun = false;   // the worker's progress is the Amiga tab's
  std::shared_ptr<slint::VectorModel<slint::SharedString>> log =
      std::make_shared<slint::VectorModel<slint::SharedString>>();
  Pipe pipe;
  std::thread worker;
  bool busy = false;

  // Testing hooks.
  bool autoRun = false, quitAfter = false, autoPlay = false;
  std::string snapshot;
  int exitCode = 0;

  MainWindow& w() { return **ui; }

  // setup.log (beside the assets folder): what happened, for a report.
  FILE* logFile = nullptr;
  void FileLog(const std::string& line) {
    if (!logFile) return;
    std::fprintf(logFile, "%s\n", line.c_str());
    std::fflush(logFile);
  }

  void Notice(const std::string& text, bool error) {
    if (!text.empty()) FileLog((error ? "notice (error): " : "notice: ") + text);
    // A standing notice stays under whatever else is said, until the program quits.
    if (standing.empty()) w().set_notice(Str(text));
    else w().set_notice(Str(text.empty() ? standing : text + "\n\n" + standing));
    w().set_notice_error(error || (text.empty() && !standing.empty()));
  }

  // ------------------------------------------------------------ the rows ---

  // The four sources, each a path the player can type, paste, browse for or have
  // found. The Amiga row is one whatever form the Amiga version came in: both
  // floppies, a hard disk image, a WHDLoad .lha, or a .zip of any of them. A path
  // that is not the thing its row asks for stays in the box (so it can be
  // corrected), marked, and is not used.
  enum Row { ROW_PC, ROW_AMIGA, ROW_JAG, ROW_2020, ROW_WORLD, ROW_COUNT };
  std::string unusable[ROW_COUNT];
  std::shared_ptr<slint::VectorModel<SourceRow>> rows;
  std::string amigaKey, amigaSeason, amigaProblem;   // the season, for this Amiga source

  char* RowPath(int r) {
    switch (r) {
      case ROW_PC: return src.dosDir;
      case ROW_JAG: return src.jagRom;
      case ROW_WORLD: return src.world;
      default: return src.s2020Dir;
    }
  }

  // The Amiga source as one path: the image or archive, or disk 1 of the pair.
  std::string AmigaPath() const {
    if (src.hdf[0]) return src.hdf;
    if (src.lha[0]) return src.lha;
    if (src.adf1[0]) return src.adf1;
    return src.adf2;
  }
  void ClearAmiga() { src.adf1[0] = src.adf2[0] = src.hdf[0] = src.lha[0] = '\0'; }

  // The sources a plan or a run uses: an Amiga copy that cannot be used (damaged, or
  // another release) stays in its row, marked, and is left out of both.
  SdSources Usable() {
    UpdateSeason();
    SdSources u = src;
    if (SdSources_HaveAmiga(&u) && amigaSeason.empty()) u.adf1[0] = u.adf2[0] = u.hdf[0] = u.lha[0] = '\0';
    return u;
  }

  // Which season the Amiga source is: read when it changes (it reads the team files).
  void UpdateSeason() {
    const std::string key = std::string(src.adf1) + "|" + src.adf2 + "|" + src.hdf + "|" + src.lha;
    if (key == amigaKey) return;
    amigaKey = key;
    amigaSeason.clear();
    amigaProblem.clear();
    if (!SdSources_HaveAmiga(&src)) return;
    const char* season = SdSources_AmigaSeason(&src);
    if (season) amigaSeason = season;
    else amigaProblem = SdSources_AmigaProblem();
  }

  static std::shared_ptr<slint::VectorModel<LinkItem>> Links(
      std::initializer_list<std::pair<std::string, std::string>> l) {
    auto m = std::make_shared<slint::VectorModel<LinkItem>>();
    // A download (its button's arrow): a file host's link, a .zip, archive.org's copy. The rest
    // (GOG's shop, a news page, the credits) are pages to read.
    for (const auto& [text, url] : l) {
      const std::string u(url);
      const bool file = u.find("mediafire.com/file") != std::string::npos ||
                        u.find("archive.org/details") != std::string::npos ||
                        (u.size() > 4 && u.compare(u.size() - 4, 4, ".zip") == 0);
      m->push_back({Str(text), Str(url), file});
    }
    return m;
  }

  SourceRow MakeRow(int r) {
    SourceRow row{};   // value-initialised: the generated struct leaves its bools as they fall
    std::string path;
    switch (r) {
      case ROW_PC:
        row.label = Str(T("PC game"));
        row.placeholder = Str(T("the game's folder (the one with SPRITE.DAT in it)"));
        row.need_pc = Str(T("needed"));
        row.need_amiga = Str(T("needed"));
        row.about_pc = Str(T("Sensible World of Soccer 96/97 for PC. Buy it on GOG and install it: its folder is "
                       "found by itself. Or name any copy of the game's folder."));
        row.about_amiga = Str(T("The AGA version takes its pitches, backdrops and pictures from the PC game. Buy it "
                          "on GOG and install it: its folder is found by itself."));
        row.links = Links({{"GOG", "https://www.gog.com/en/game/sensible_world_of_soccer_9697"},
                           {"PC CD (archive.org)",
                            "https://archive.org/details/msdos_Sensible_World_of_Soccer_96-97_1996"}});
        path = src.dosDir;
        if (!src.dosDir[0] && gogInstaller) {
          row.detail = Str(T("GOG's installer is in your Downloads: run it, and the game it installs is found here by itself."));
          row.detail_warn = true;
        }
        break;
      case ROW_AMIGA: {
        row.label = Str(T("Amiga version"));
        row.placeholder = Str(T("the floppies (.adf), a hard disk image (.hdf), a WHDLoad .lha, or a .zip of them"));
        row.need_pc = Str(T("optional"));
        row.need_amiga = Str(T("needed"));
        row.about_pc = Str(T("Adds the Amiga's music, sound effects and loading screens -- or plays without the PC "
                       "game. Download one of the files on the right; ASWOS's swos.hdf works too."));
        row.about_amiga = Str(T("The game's own files. Download one of the files on the right (either will do), or "
                          "use both floppies, ASWOS's swos.hdf or a WHDLoad install."));
        row.links = Links({{"swos2425.hdf", "https://www.mediafire.com/file/4fu4wbhjiaco2ts/swos2425.hdf/file"},
                           {"WHDLoad .lha",
                            "https://www.mediafire.com/file/8l9nr2rjg0zteb5/SensibleWorldOfSoccer2425_v2.3.lha/file"},
                           {T("about it"), "https://www.sensiblesoccer.de/index.php?&view=newsdetails&id=206"}});
        path = AmigaPath();
        UpdateSeason();
        if (src.adf1[0] && !src.adf2[0]) {
          row.detail = Str(T("Disk 1 only: add disk 2 too."));
          row.detail_warn = true;
        } else if (src.adf2[0] && !src.adf1[0]) {
          row.detail = Str(T("Disk 2 only: add disk 1 too."));
          row.detail_warn = true;
        } else if (!amigaProblem.empty()) {
          row.detail = Str(Tf("This copy cannot be used: {0}. Try another download.", amigaProblem));
          row.detail_warn = true;
        } else if (amigaSeason == "96/97") {
          row.detail = Str(T("The 96/97 release."));
        } else if (amigaSeason == "an unknown season") {
          row.detail = Str(T("A season this setup does not know: it may install, or it may not."));
          row.detail_warn = true;
        } else if (!amigaSeason.empty()) {
          row.detail = Str((src.adf1[0] ? Tf("The {0} season (both disks).", amigaSeason) : Tf("The {0} season.", amigaSeason)) + " " +
                           Tf("{0} plays 96/97's teams: they come from the PC game when it is here.", PROJECT_NAME));
        } else if (src.adf1[0] && src.adf2[0]) {
          row.detail = Str(T("Both disks."));
        }
        break;
      }
      case ROW_JAG:
        row.label = Str(T("Atari Jaguar cartridge"));
        row.placeholder = Str(T("Sensible Soccer International Edition (.jag, .j64, .rom or .bin)"));
        row.need_pc = Str(T("optional"));
        row.about_pc = Str(T("The Jaguar's redrawn players, an option in the game's graphics settings."));
        row.links = Links({{"Atarimania",
                            "https://www.atarimania.com/games/atari-jaguar-games-sensible-soccer-international-edition-30123"}});
        path = src.jagRom;
        break;
      case ROW_WORLD: {
        row.label = Str(T("World 2026-27"));
        row.placeholder = "OpenSWOS-World-2627.zip";
        row.need_pc = Str(T("optional"));
        row.need_amiga = Str(T("optional"));
        row.about_pc = Str(T("The SWOS 96/97 Mod's World 2026-27: today's clubs, players and competitions. Its "
                       "package is a download of its own. The game plays it when SWOS.ini says world = 2627."));
        row.about_amiga = Str(T("The SWOS 96/97 Mod's World 2026-27: today's clubs, players and competitions. Its "
                          "package is a download of its own; tick it below to play it on the Amiga."));
        if (kWorldUrl[0]) row.links = Links({{"OpenSWOS-World-2627.zip", kWorldUrl}, {T("credits"), kWorldCredits}});
        path = src.world;
        SdWorldInfo wi;
        if (src.world[0] && SdSources_OpenWorld(src.world, &wi, nullptr, 0) == 0 && wi.fingerprint[0])
          row.detail = Str(Tf("Data {0}.", wi.fingerprint));
        break;
      }
      default:
        row.label = Str(T("SWOS 2020"));
        row.placeholder = Str(T("the SWOS 2020 install folder"));
        row.need_pc = Str(T("optional"));
        row.need_amiga = Str(T("optional"));
        row.about_pc = Str(T("SWOS 2020's pitches, the set its launcher picked."));
        row.about_amiga = Str(T("SWOS 2020's pitches, the set its launcher picked."));
        row.links = Links({{"sensiblesoccer.de", "https://www.sensiblesoccer.de/"}});
        path = src.s2020Dir;
        break;
    }
    row.value = Str(!unusable[r].empty() ? unusable[r] : !path.empty() ? Shown(path) : std::string());
    const bool bad = r == ROW_AMIGA && !path.empty() &&
                     (!amigaProblem.empty() || !SdSources_HaveAmiga(&src));
    row.state = !unusable[r].empty() ? 2 : path.empty() ? 0 : bad ? 2 : 1;
    return row;
  }

  // In place: rebuilding the model would take the focus from the box the
  // player is typing in.
  void RefreshRows() {
    if (!rows) {
      rows = std::make_shared<slint::VectorModel<SourceRow>>();
      for (int r = 0; r < ROW_COUNT; r++) rows->push_back(MakeRow(r));
      w().set_sources(rows);
      return;
    }
    for (int r = 0; r < ROW_COUNT; r++) rows->set_row_data(r, MakeRow(r));
  }

  // A pasted path: spaces around it and the quotes Windows' "Copy as path"
  // puts round it go; ~/ is the home folder.
  static std::string CleanPath(std::string p) {
    while (!p.empty() && (p.back() == ' ' || p.back() == '\t' || p.back() == '\r' || p.back() == '\n')) p.pop_back();
    size_t b = 0;
    while (b < p.size() && (p[b] == ' ' || p[b] == '\t')) b++;
    p = p.substr(b);
    if (p.size() >= 2 && ((p.front() == '"' && p.back() == '"') || (p.front() == '\'' && p.back() == '\'')))
      p = p.substr(1, p.size() - 2);
#ifndef _WIN32
    if (p.rfind("~/", 0) == 0) {
      const char* home = std::getenv("HOME");
      if (home) p = std::string(home) + p.substr(1);
    }
#endif
    return p;
  }

  // The Amiga version the player named: it replaces what was there, except that one
  // floppy joins the other to make the pair. Returns what to tell the player.
  std::string TakeAmiga(const SdSources& found) {
    auto take = [](char* to, const char* from) { std::snprintf(to, 1024, "%s", from); };
    const bool one = (found.adf1[0] != 0) != (found.adf2[0] != 0) && !found.hdf[0] && !found.lha[0];
    if (one && !src.hdf[0] && !src.lha[0]) {
      // A disk: kept beside the other one of a pair in the making.
      if (found.adf1[0]) take(src.adf1, found.adf1);
      else take(src.adf2, found.adf2);
      if (!src.adf1[0]) return N_("That is disk 2: add disk 1 too.");
      if (!src.adf2[0]) return N_("That is disk 1: add disk 2 too.");
      return N_("");
    }
    ClearAmiga();
    take(src.adf1, found.adf1);
    take(src.adf2, found.adf2);
    take(src.hdf, found.hdf);
    take(src.lha, found.lha);
    src.hdfVolume = found.hdfVolume;
    std::snprintf(src.hdfDir, sizeof src.hdfDir, "%s", found.hdfDir);
    std::snprintf(src.lhaDir, sizeof src.lhaDir, "%s", found.lhaDir);
    if (one) return found.adf1[0] ? N_("That is disk 1: add disk 2 too.") : N_("That is disk 2: add disk 1 too.");
    return N_("");
  }

  // Put `text` in row r. Returns what to tell the player ("" for nothing).
  std::string SetRow(int r, const std::string& text) {
    const std::string path = CleanPath(text);
    unusable[r].clear();
    if (path.empty()) {
      if (r == ROW_AMIGA) ClearAmiga();
      else RowPath(r)[0] = '\0';
      return N_("");
    }
    SdSources found{};
    SdSources_AddFile(&found, path.c_str());
    auto take = [](char* to, const char* from) { std::snprintf(to, 1024, "%s", from); };
    switch (r) {
      case ROW_PC:
        if (found.dosDir[0]) { take(src.dosDir, found.dosDir); return N_(""); }
        src.dosDir[0] = '\0';
        unusable[r] = path;
        return found.s2020Dir[0] ? N_("That is SWOS 2020, not the PC game: it goes in the SWOS 2020 row.")
                                 : N_("Not the PC game's folder: it is the one with SPRITE.DAT and DATA in it.");
      case ROW_AMIGA:
        if (found.adf1[0] || found.adf2[0] || found.hdf[0] || found.lha[0]) return TakeAmiga(found);
        unusable[r] = path;
        return N_("Not the Amiga version of Sensible World of Soccer: its floppies (.adf), a hard disk image "
               "(.hdf) or a WHDLoad .lha with the game on it.");
      case ROW_JAG:
        if (found.jagRom[0]) { take(src.jagRom, found.jagRom); return N_(""); }
        src.jagRom[0] = '\0';
        unusable[r] = path;
        return N_("Not the Sensible Soccer Jaguar cartridge (a 2 MB .jag, .j64, .rom or .bin).");
      case ROW_WORLD:
        if (found.world[0]) { take(src.world, found.world); return N_(""); }
        src.world[0] = '\0';
        unusable[r] = path;
        return N_("Not a world's package: OpenSWOS-World-2627.zip, or the folder it was unpacked to.");
      default:
        if (found.s2020Dir[0]) { take(src.s2020Dir, found.s2020Dir); return N_(""); }
        src.s2020Dir[0] = '\0';
        unusable[r] = path;
        return N_("Not a SWOS 2020 install: it is the folder with gameLauncher.exe and the swos folder in it.");
    }
  }

  // A file or folder chosen (or dropped) for a row: the cleaned path, and what is wrong with it.
  void RowCommitted(int r, const std::string& text) {
    if (r == ROW_AMIGA && CleanPath(text) == Shown(AmigaPath())) return;   // unchanged
    const std::string msg = SetRow(r, text);
    Notice(T(msg), !msg.empty() && msg.rfind("That is disk", 0) != 0);
    RefreshRows();
    RefreshPlan();
  }

  void BrowseRow(int r) {
    std::string picked;
    const char* current = r == ROW_AMIGA ? "" : RowPath(r);
    switch (r) {
      case ROW_PC:
      case ROW_2020: picked = PickFolder(current[0] ? current : std::string()); break;
      case ROW_AMIGA: picked = PickFile("Amiga version", "adf,hdf,lha,zip"); break;
      case ROW_WORLD: picked = PickFile("World 2026-27 package", "zip"); break;
      default: picked = PickFile("Jaguar cartridge", "jag,j64,rom,bin"); break;
    }
    if (picked.empty()) return;
    RowCommitted(r, picked);
  }

  void Refresh() {
    RefreshRows();
    RefreshPlan();
  }

  // ------------------------------------------------------- installed ---

  // What the assets folder already has: read at launch, after an install, and
  // when the folder is changed.
  SdInstalled installed[SD_STEP_COUNT] = {};

  // 0 nothing, 1 installed, 2 incomplete, 3 installed but by an older setup: its
  // data version in ASSETS.ini is below this setup's (include/assets_ini.h).
  int InstalledState(int s) const {
    const SdInstalled& n = installed[s];
    if (n.total > 0 && n.present == n.total)
      return n.version < SwosData_PartVersion(SdStep(s)) ? 3 : 1;
    return n.present > 0 ? 2 : 0;
  }
  bool Has(int s) const { const int st = InstalledState(s); return st == 1 || st == 3; }
  bool Outdated() const {
    for (int s = 0; s < SD_STEP_COUNT; s++)
      if (InstalledState(s) == 3) return true;
    return false;
  }

  // Enough to play: the Amiga files, or the PC version's (its sound plus the
  // AGA graphics made from it); and the teams either way. Out of date still plays.
  bool Playable() const {
    return (Has(SD_STEP_AMIGA_FILES) || (Has(SD_STEP_DOS_FILES) && Has(SD_STEP_AGA_PC))) &&
           Has(SD_STEP_TEAMS);
  }

  void CheckInstalled() {
    SwosData_Installed(outDir.c_str(), installed);
    if (busy) return;
    const bool playable = Playable();
    w().set_done(playable);
    w().set_status_error(false);
    w().set_progress(playable ? 1.0f : 0.0f);
    game = FindGame(exeDir);
    w().set_can_play(playable && !game.empty());
    bool any = false;
    for (int s = 0; s < SD_STEP_COUNT; s++) any = any || installed[s].present > 0;
    const bool outdated = Outdated();
    w().set_outdated(outdated);
    if (outdated) {
      SdJob job{};
      job.src = Usable();
      SdStepInfo info[SD_STEP_COUNT];
      SwosData_Plan(&job, info);
      bool sources = false;
      for (int s = 0; s < SD_STEP_COUNT; s++) sources = sources || info[s].ready;
      w().set_status(Str(Tf("Some of the game's data in {0} was made by an older {1}.", Shown(outDir), PROJECT_SETUP_NAME) + " " +
                         (sources ? T("Click Update to make it again from your copy.")
                                  : T("Choose the folder your copy is in, then click Update."))));
    }
    else if (playable)
      w().set_status(Str(game.empty() ? Tf("The game's data is already installed in {0}. Install again from your copy to add to it.", Shown(outDir))
                                      : Tf("The game's data is already installed in {0}. Play, or install again from your copy to add to it.", Shown(outDir))));
    else if (any)
      w().set_status(Str(Tf("Some of the game's data is in {0}, but not enough to play: install from your copy.", Shown(outDir))));
    else
      w().set_status(Str(T("Ready.")));
  }

  // Everything below the sources: where it goes and what will be made.
  void RefreshPlan() {
    w().set_out_dir(Str(Shown(outDir)));
    w().set_phone_folder(Str(Shown(sd::ParentDir(outDir))));
    w().set_out_note(Shown(outDir) == Shown(defaultOutDir)
                         ? Str(T("This is the folder the game reads its data from."))
                         : Str(Tf("The game reads {0} — to use this folder instead, start the game with SWOS_ASSETS set to it.", Shown(defaultOutDir))));

    SdJob job{};
    job.src = Usable();
    SdStepInfo info[SD_STEP_COUNT];
    SwosData_Plan(&job, info);
    auto left = std::make_shared<slint::VectorModel<PlanStep>>();
    auto right = std::make_shared<slint::VectorModel<PlanStep>>();
    bool any = false;
    // (World 2026-27 listed once its package can be downloaded -- or was given, or is installed)
    const bool world = kWorldUrl[0] || info[SD_STEP_WORLD].ready || InstalledState(SD_STEP_WORLD) != 0;
    const int steps = world ? SD_STEP_COUNT : SD_STEP_COUNT - 1;
    for (int i = 0; i < SD_STEP_COUNT; i++) {
      if (i == SD_STEP_WORLD && !world) continue;
      (i < (steps + 1) / 2 ? left : right)
          ->push_back({Str(T(info[i].name)), bool(info[i].ready), Str(T(info[i].needs)), InstalledState(i)});
      any = any || info[i].ready;
    }
    w().set_plan_left(left);
    w().set_plan_right(right);
    // What GAME OPTIONS may offer: the data that is installed, or about to be.
    havePcData = info[SD_STEP_DOS_FILES].ready || Has(SD_STEP_DOS_FILES);
    have2020Data = info[SD_STEP_SWOS2020].ready || Has(SD_STEP_SWOS2020);
    haveJagData = info[SD_STEP_JAGUAR].ready || Has(SD_STEP_JAGUAR);
    haveWorldData = info[SD_STEP_WORLD].ready || Has(SD_STEP_WORLD);
    w().set_can_install(any && !outDir.empty());
    RefreshAmiga(info);
  }

  // The Amiga tab's button: there is a program to make it from, somewhere to put it,
  // and the Amiga version and the PC game -- as sources now, or installed already.
  void RefreshAmiga(const SdStepInfo info[SD_STEP_COUNT]) {
    const bool kindDrawer = w().get_amiga_kind() == 0;
    const std::string& dest = kindDrawer ? drawerTo : whdloadTo;
    w().set_amiga_dest(Str(Shown(dest)));
    w().set_amiga_program(Str(Shown(!programFrom.empty() ? programFrom : amigaProgram)));
    w().set_amiga_own_program(ownProgram);
    const bool sources = info[SD_STEP_AMIGA_FILES].ready && info[SD_STEP_AGA_PC].ready;
    const bool installedAlready = Has(SD_STEP_AMIGA_FILES) && Has(SD_STEP_AGA_PC);
    w().set_can_make_amiga(!amigaProgram.empty() && !dest.empty() && !outDir.empty() &&
                           (sources || installedAlready));
    // World 2026-27 on the Amiga: its package installed already, or about to be.
    const bool world = info[SD_STEP_WORLD].ready || SwosData_HaveAmigaWorld(outDir.c_str()) || HaveWorldMod();
    w().set_amiga_world_available(world);
    if (!world) w().set_amiga_world(false);
    if (!busy && !w().get_amiga_done() && !w().get_amiga_error()) {
      std::string text;
      if (amigaProgram.empty())
        text = N_("Choose the Amiga program first: SWOS-Amiga.zip, from the same place as this setup.");
      else if (!sources && !installedAlready)
        text = !SdSources_HaveAmiga(&src) && !SdSources_HaveDos(&src)
                   ? N_("It needs the Amiga version and the PC game first.")
               : !SdSources_HaveAmiga(&src) ? N_("It needs the Amiga version too.")
                                            : N_("It needs the PC game too.");
      else
        text = kindDrawer ? N_("Ready to make the drawer.") : N_("Ready to make the package.");
      w().set_amiga_status(Str(T(text)));
    }
  }

  void Log(const std::string& line) {
    FileLog(line);
    log->push_back(Str(line));
  }

  // Runs on the UI thread: move what the worker produced into the window.
  void Flush() {
    std::vector<std::string> lines;
    int32_t done, total;
    {
      std::lock_guard<std::mutex> lock(pipe.mu);
      lines.swap(pipe.lines);
      done = pipe.done;
      total = pipe.total;
      pipe.flushQueued = false;
    }
    for (const std::string& l : lines) Log(l);
    if (total > 0) {
      // The Amiga tab's run is the install and then the drawer or package: the
      // install fills most of its bar.
      if (amigaRun) w().set_amiga_progress(0.9f * float(done) / float(total));
      else w().set_progress(float(done) / float(total));
    }
  }

  // ----------------------------------------------------------- sources ---

  // The player chooses folders; each is searched, with every folder inside it,
  // on the worker (a big folder takes a while), and what it finds is added to
  // what earlier folders found. Stop keeps what was found so far.
  std::atomic<bool> stopScan{false};

  static int32_t CountFound(const SdSources& s) {
    return (s.adf1[0] != 0) + (s.adf2[0] != 0) + (s.hdf[0] != 0) + (s.lha[0] != 0) + (s.dosDir[0] != 0) +
           (s.jagRom[0] != 0) + (s.s2020Dir[0] != 0) + (s.world[0] != 0);
  }

  struct ScanProgress {
    App* app;
    std::chrono::steady_clock::time_point last;
  };

  static int32_t OnScanFolder(void* user, int32_t folders, const char* dir) {
    ScanProgress* p = static_cast<ScanProgress*>(user);
    const auto now = std::chrono::steady_clock::now();
    if (now - p->last >= std::chrono::milliseconds(100)) {
      p->last = now;
      App* app = p->app;
      const std::string text = Tf("Searching... {0} folders: {1}", folders, Shown(dir));
      slint::invoke_from_event_loop([app, text] { app->w().set_scan_status(Str(text)); });
    }
    return p->app->stopScan.load() ? 1 : 0;
  }

  void StartScan(const std::string& dir) {
    if (busy) return;
    if (worker.joinable()) worker.join();
    SetBusy(true);
    stopScan = false;
    w().set_scanning(true);
    w().set_scan_status(Str(Tf("Searching {0} ...", Shown(dir))));
    Notice("", false);
    const SdSources start = src;
    worker = std::thread([this, dir, start] {
      SdSources found = start;
      ScanProgress progress{this, std::chrono::steady_clock::now()};
      const int32_t rc = SdSources_ScanEx(&found, dir.c_str(), OnScanFolder, &progress);
      slint::invoke_from_event_loop([this, dir, found, rc, start] { FinishScan(dir, found, rc < 0, start); });
    });
  }

  void FinishScan(const std::string& dir, const SdSources& found, bool stopped, const SdSources& before) {
    SetBusy(false);
    w().set_scanning(false);
    w().set_scan_status("");
    src = found;
    for (int r = 0; r < ROW_COUNT; r++)
      if (r == ROW_AMIGA ? SdSources_HaveAmiga(&src) != 0 : RowPath(r)[0] != 0) unusable[r].clear();
    const int32_t added = CountFound(found) - CountFound(before);
    std::string text;
    bool error = false;
    if (added > 0)
      text = Tf("Found {0} in {1}.", added, Shown(dir));
    else {
      text = Tf("Nothing new in {0} or the folders inside it.", Shown(dir));
      error = true;
    }
    if (stopped) text += " " + T("(Stopped before the end.)");
    if (!SdSources_HaveDos(&src) && !SdSources_HaveAmiga(&src))
      text += " The game needs the PC game or the Amiga version.";
    else if (src.adf1[0] != 0 && src.adf2[0] == 0)
      text += " Disk 2 of the Amiga version is still missing.";
    else if (src.adf2[0] != 0 && src.adf1[0] == 0)
      text += " Disk 1 of the Amiga version is still missing.";
    Notice(text, error);
    Refresh();
  }

  void ClearSources() {
    if (busy) return;
    src = SdSources{};
    for (auto& u : unusable) u.clear();
    Notice("", false);
    Refresh();
  }

  // ------------------------------------------------------------ install ---

  void SetBusy(bool b) {
    busy = b;
    w().set_busy(b);
  }

  void Install() {
    if (busy) return;
    if (worker.joinable()) worker.join();
    SetBusy(true);
    w().set_done(false);
    w().set_status_error(false);
    w().set_progress(0.0f);
    w().set_status(Str(Tf("Installing to {0} ...", Shown(outDir))));
    log->clear();
    if (resDir.empty())
      Log("warning: the game's own files (setup/glyphs.txt) were not found beside this program; "
          "start it with --res DIR");

    auto job = std::make_shared<SdJob>();
    std::memset(job.get(), 0, sizeof *job);
    job->src = Usable();
    job->src.disk1 = job->src.disk2 = job->src.hdfDisk = nullptr;
    job->src.lhaArc = nullptr;
    std::snprintf(job->outDir, sizeof job->outDir, "%s", outDir.c_str());
    std::snprintf(job->resDir, sizeof job->resDir, "%s", resDir.c_str());
    std::snprintf(job->modsDir, sizeof job->modsDir, "%s", ModsDir().c_str());   // (a world: a mod)
    job->log = PipeLog;
    job->progress = PipeProgress;
    job->user = &pipe;

    worker = std::thread([this, job] {
      char err[256] = {0};
      const int32_t rc = SwosData_Run(job.get(), err);
      SdSources_Close(&job->src);
      const std::string msg(err);
      slint::invoke_from_event_loop([this, rc, msg] { FinishInstall(rc, msg); });
    });
  }

  void FinishInstall(int32_t rc, const std::string& err) {
    Flush();
    SetBusy(false);
    SwosData_Installed(outDir.c_str(), installed);
    RefreshPlan();
    if (rc == 0) {
      WorldModOn();
      w().set_progress(1.0f);
      w().set_done(true);
      game = FindGame(exeDir);
      w().set_can_play(!game.empty());
      w().set_status(Str(Tf("Done. The game's data is in {0}.", Shown(outDir))));
      FileLog("install done: " + Shown(outDir));
      exitCode = 0;
    } else {
      w().set_status_error(true);
      w().set_status(Str(Tf("Failed: {0}", err)));
      FileLog("install failed: " + err);
      Log("failed: " + err);
      exitCode = 1;
    }
    AutoFinished();
  }

  // Testing: what --snapshot / --play / --quit do once the work is done.
  void AutoFinished() {
    if (!autoRun) return;
    // Give the window a moment to draw the final state first.
    slint::Timer::single_shot(std::chrono::milliseconds(400), [this] {
      if (!snapshot.empty()) Snapshot(snapshot + "-done.ppm");
      if (autoPlay && w().get_can_play()) Play();
      else if (quitAfter) slint::quit_event_loop();
    });
  }

  void Play() {
    if (game.empty()) return;
    if (sd::Launch(game, exeDir)) {
      slint::quit_event_loop();
    } else {
      w().set_status_error(true);
      w().set_status(Str(Tf("Could not start {0}.", game)));
      FileLog("could not start " + game);
    }
  }

  // ------------------------------------------------------------- amiga ---

  // Where the program is: the setup's own copy (amiga/ beside setup/, from
  // SWOS-Amiga.zip), else SWOS-Amiga.zip itself -- named, or found beside this
  // program, in a dist/ above it (a developer's tree) or in Downloads. A folder may be
  // the one that holds SWOS.info and the SWOS drawer, or the drawer itself; a file in
  // the drawer stands for the drawer.
  static std::string Lower(std::string t) {
    for (char& c : t) c = char(std::tolower(static_cast<unsigned char>(c)));
    return t;
  }
  static bool EndsWith(const std::string& t, const char* end) {
    const size_t n = std::strlen(end);
    return t.size() >= n && Lower(t.substr(t.size() - n)) == end;
  }

  static std::string ProgramRoot(const std::string& path) {
    if (path.empty()) return {};
    if (sd::IsFile(path)) {
      if (EndsWith(path, ".zip")) {
        char dir[1100];
        return SdSources_UnpackAmigaProgram(path.c_str(), dir, sizeof dir) == 0 ? std::string(dir) : std::string();
      }
      return ProgramRoot(sd::ParentDir(path));
    }
    if (sd::IsFile(Join(path, "SWOS/SWOS2"))) return path;
    if (sd::IsFile(Join(path, "SWOS2"))) return sd::ParentDir(path);
    return {};
  }

  static bool ProgramZip(const std::string& path) {
    const std::string name = Lower(path.substr(path.find_last_of("/\\") + 1));
    return name.rfind("swos-amiga", 0) == 0 && EndsWith(name, ".zip");
  }

  // SWOS-Amiga.zip where a player or a developer has it: the newest in Downloads.
  std::string FindProgramZip() const {
    for (std::string d = exeDir; !d.empty();) {
      for (const char* rel : {"SWOS-Amiga.zip", "dist/SWOS-Amiga.zip"})
        if (sd::IsFile(Join(d, rel))) return Join(d, rel);
      const std::string up = sd::ParentDir(d);
      if (up == d) break;
      d = up;
    }
    std::string best;
    int64_t newest = 0;
    const std::string dl = !watchDir.empty() ? watchDir : sd::DownloadsDir();
    if (!dl.empty())
      for (const sd::DirEntry& e : sd::ListDir(dl))
        if (!e.dir && ProgramZip(e.path) && (best.empty() || e.mtime > newest)) {
          best = e.path;
          newest = e.mtime;
        }
    return best;
  }

  bool TakeProgram(const std::string& from) {
    const std::string root = ProgramRoot(from);
    if (root.empty()) return false;
    amigaProgram = root;
    programFrom = from;
    return true;
  }

  void FindProgram(const std::string& given) {
    ownProgram = !given.empty();
    if (ownProgram) {
      TakeProgram(given);
      return;
    }
    if (!resDir.empty() && TakeProgram(Join(resDir, "amiga"))) {
      programFrom.clear();   // the setup's own: nothing to say
      return;
    }
    ownProgram = true;
    const std::string zip = FindProgramZip();
    if (!zip.empty()) TakeProgram(zip);
  }

  void ChooseProgram() {
    const std::string picked = PickFile("SWOS-Amiga.zip, or SWOS.info in its drawer", "zip,info");
    if (picked.empty()) return;
    if (!TakeProgram(picked)) {
      w().set_amiga_error(true);
      w().set_amiga_status(Str(Shown(picked) + " is not the Amiga program: choose SWOS-Amiga.zip."));
      return;
    }
    ownProgram = true;
    w().set_amiga_error(false);
    RefreshPlan();
  }

  void ChangeAmigaDest() {
    if (w().get_amiga_kind() == 0) {
      const std::string dir = PickFolder(drawerTo);
      if (!dir.empty()) drawerTo = dir;
    } else {
      const std::string file =
          PickSaveFile("LhA archive", "lha", sd::ParentDir(whdloadTo), "OpenSWOSAGA-WHDL.lha");
      if (!file.empty()) whdloadTo = file;
    }
    AmigaKindChanged();
  }

  void AmigaKindChanged() {
    w().set_amiga_done(false);
    w().set_amiga_error(false);
    w().set_amiga_progress(0.0f);
    RefreshPlan();
  }

  // The install (what the sources allow), then the drawer or the package, on the worker.
  void MakeAmiga() {
    if (busy) return;
    if (!w().get_can_make_amiga()) {
      if (autoRun) {   // testing: nothing to make, say so and finish
        exitCode = 1;
        AutoFinished();
      }
      return;
    }
    if (worker.joinable()) worker.join();
    const bool kindDrawer = w().get_amiga_kind() == 0;
    SetBusy(true);
    amigaRun = true;
    w().set_amiga_done(false);
    w().set_amiga_error(false);
    w().set_amiga_progress(0.0f);
    w().set_amiga_status(Str(Tf("Installing the game's data to {0} ...", Shown(outDir))));
    log->clear();

    auto job = std::make_shared<SdJob>();
    std::memset(job.get(), 0, sizeof *job);
    job->src = Usable();
    job->src.disk1 = job->src.disk2 = job->src.hdfDisk = nullptr;
    job->src.lhaArc = nullptr;
    std::snprintf(job->outDir, sizeof job->outDir, "%s", outDir.c_str());
    std::snprintf(job->resDir, sizeof job->resDir, "%s", resDir.c_str());
    std::snprintf(job->modsDir, sizeof job->modsDir, "%s", ModsDir().c_str());   // (a world: a mod)
    job->log = PipeLog;
    job->progress = PipeProgress;
    job->user = &pipe;
    SdStepInfo info[SD_STEP_COUNT];
    SwosData_Plan(job.get(), info);
    bool any = false;
    for (int i = 0; i < SD_STEP_COUNT; i++) any = any || info[i].ready;

    const std::string program = amigaProgram, dest = kindDrawer ? drawerTo : whdloadTo,
                      assets = outDir, res = resDir;
    // The WHDLoad package's mods (docs/MODS.md): staged here, on this thread, as a drawer gets
    // them; the package takes the folder's mods/ and data/MODS.
    std::string modsStage;
    if (!kindDrawer) {
      modsStage = Join(UserDir(), ".whdload-mods");
      sd::RemoveTree(modsStage);
      sd::MakeDirs(Join(modsStage, "data"));
      ModsToDrawer(modsStage, w().get_amiga_world());
    }
    // A world as a mod goes in with the mods (ModsToDrawer); an earlier setup's copy in the
    // assets folder, as before.
    const bool worldMod = HaveWorldMod();
    const bool worldTick = w().get_amiga_world();
    const int32_t withWorld = worldTick && !worldMod ? 1 : 0;
    worker = std::thread([this, job, any, kindDrawer, program, dest, assets, res, withWorld, worldTick, modsStage] {
      char err[256] = {0};
      int32_t rc = any ? SwosData_Run(job.get(), err) : 0;
      SdSources_Close(&job->src);
      if (rc == 0) {
        PipeLog(&pipe, kindDrawer ? "Making the drawer..." : "Making the WHDLoad package...");
        rc = kindDrawer
                 ? SwosData_MakeAmigaDrawer(assets.c_str(), program.c_str(), dest.c_str(), withWorld, PipeLog,
                                            &pipe, err)
                 : SwosData_MakeWhdload(assets.c_str(), Join(program, "SWOS").c_str(), res.c_str(), dest.c_str(),
                                        withWorld, modsStage.c_str(), PipeLog, &pipe, err);
      }
      const std::string msg(err);
      slint::invoke_from_event_loop([this, rc, msg, kindDrawer, dest, worldTick] {
        Flush();
        if (!kindDrawer) sd::RemoveTree(Join(UserDir(), ".whdload-mods"));   // (the package has them)
        amigaRun = false;
        SetBusy(false);
        SwosData_Installed(outDir.c_str(), installed);
        w().set_amiga_done(rc == 0);
        w().set_amiga_error(rc != 0);
        w().set_amiga_progress(rc == 0 ? 1.0f : 0.0f);
        if (rc == 0 && kindDrawer) ModsToDrawer(Join(dest, "SWOS"), worldTick);
        if (rc == 0)
          w().set_amiga_status(Str(kindDrawer
              ? Tf("Done: the SWOS drawer is in {0}. Copy it, with SWOS.info, to your Amiga and double-click SWOS.", Shown(dest))
              : Tf("Done: {0}. Unpack it on your Amiga (lha x OpenSWOSAGA-WHDL.lha), open the OpenSWOSAGA-NE drawer and double-click OpenSWOS.", Shown(dest))));
        else {
          w().set_amiga_status(Str(Tf("Failed: {0}", msg)));
          FileLog("amiga failed: " + msg);
          Log("failed: " + msg);
        }
        exitCode = rc == 0 ? 0 : 1;
        CheckInstalled();
        RefreshPlan();
        AutoFinished();
      });
    });
  }

  // ------------------------------------------------------------ game options ---

  // The wizard's GAME OPTIONS: the few SWOS.ini settings worth choosing before the first start
  // (include/game_options.h; the game is the authority on the file -- --game-options-selftest).
  // A choice is written at once, and only its own line: the rest of the file is left as it is.
  // The computer's settings go to its SWOS.ini, beside the assets folder; the phone's to
  // phone/SWOS.ini there, which goes to the phone with the assets folder.
  std::vector<int> optChoice;                       // per kGameOptions: the value's index
  std::vector<std::pair<int, std::vector<int>>> optRows;   // a shown row: its option, its values
  bool haveWorldData = false, havePcData = false, have2020Data = false, haveJagData = false;

  std::string UserDir() const { return sd::ParentDir(defaultOutDir); }
  // The SWOS.ini each target's options go in: 0 this computer's (the game's own), 1 the phone's
  // (sent by the Wi-Fi copy), 2 the handheld's (merged into the card's by the handheld copy).
  enum { kComputer = 0, kPhone = 1, kHandheld = 2 };
  static int TargetBit(int t) { return t == kComputer ? GAME_OPTION_COMPUTER : t == kPhone ? GAME_OPTION_PHONE : GAME_OPTION_HANDHELD; }
  bool Ticked(int t) {
    return t == kComputer ? w().get_t_computer() : t == kPhone ? w().get_t_phone() : w().get_t_handheld();
  }
  std::string IniPath(int t) const {
    return Join(t == kComputer ? UserDir() : Join(UserDir(), t == kPhone ? "phone" : "handheld"), "SWOS.ini");
  }

  static std::string ReadFile(const std::string& path) {
    std::string text;
    if (FILE* f = sd::OpenFile(path, "rb")) {
      char buf[4096];
      size_t n;
      while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) text.append(buf, n);
      std::fclose(f);
    }
    return text;
  }

  // Whether an ini line sets this key ("key=", "key =", leading spaces too).
  static bool IniKeyIs(const std::string& line, const char* key) {
    const size_t eq = line.find('=');
    if (eq == std::string::npos) return false;
    size_t a = line.find_first_not_of(" \t"), b = eq;
    while (b > a && (line[b - 1] == ' ' || line[b - 1] == '\t')) b--;
    return a < b && line.compare(a, b - a, key) == 0 && b - a == std::strlen(key);
  }

  // [section] key=value's value in an ini text, or empty.
  static std::string IniValue(const std::string& text, const char* section, const char* key) {
    std::string sec;
    size_t pos = 0;
    while (pos < text.size()) {
      size_t end = text.find('\n', pos);
      if (end == std::string::npos) end = text.size();
      std::string line = text.substr(pos, end - pos);
      pos = end + 1;
      while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
      if (line.size() > 1 && line[0] == '[') {
        sec = line.substr(1, line.find(']') - 1);
        continue;
      }
      if (sec != section || !IniKeyIs(line, key)) continue;
      std::string v = line.substr(line.find('=') + 1);
      v.erase(0, v.find_first_not_of(" \t"));
      const size_t cut = v.find_first_of(" \t;");
      return cut == std::string::npos ? v : v.substr(0, cut);
    }
    return {};
  }

  // The text with [section] key=value set: its line replaced, or added to its section, or the
  // section added. Nothing else moves.
  static std::string IniSet(const std::string& text, const char* section, const char* key, const char* value) {
    std::string out, sec;
    bool done = false, inSec = false;
    size_t pos = 0, lastInSec = std::string::npos;
    std::vector<std::string> lines;
    while (pos < text.size()) {
      size_t end = text.find('\n', pos);
      if (end == std::string::npos) end = text.size();
      lines.push_back(text.substr(pos, end - pos));
      pos = end + 1;
    }
    for (size_t i = 0; i < lines.size(); i++) {
      std::string line = lines[i];
      if (!line.empty() && line.back() == '\r') line.pop_back();
      if (line.size() > 1 && line[0] == '[') {
        inSec = line.substr(1, line.find(']') - 1) == section;
        if (inSec) lastInSec = i;
        continue;
      }
      if (!inSec) continue;
      if (!line.empty()) lastInSec = i;
      if (!done && IniKeyIs(line, key)) {
        lines[i] = std::string(key) + "=" + value;
        done = true;
      }
    }
    if (!done) {
      if (lastInSec != std::string::npos)
        lines.insert(lines.begin() + long(lastInSec) + 1, std::string(key) + "=" + value);
      else {
        if (!lines.empty() && !lines.back().empty()) lines.push_back("");
        lines.push_back(std::string("[") + section + "]");
        lines.push_back(std::string(key) + "=" + value);
      }
    }
    for (size_t i = 0; i < lines.size(); i++) {
      out += lines[i];
      if (i + 1 < lines.size() || !lines[i].empty()) out += "\n";
    }
    return out;
  }

  bool ValueAvailable(const GameOptionValue& v) const {
    return v.needs == GAME_OPTION_VALUE_PC ? havePcData : v.needs == GAME_OPTION_VALUE_2020 ? have2020Data
         : v.needs == GAME_OPTION_VALUE_JAGUAR ? haveJagData : true;
  }

  // The choices from the files, once: the phone's file when there is one and no computer is
  // ticked, else the computer's; a value it does not have is the game's default.
  void LoadOptions() {
    std::string text[3];
    for (int t = 0; t < 3; t++) text[t] = ReadFile(IniPath(t));
    optChoice.assign(size_t(GAME_OPTION_COUNT(kGameOptions)), 0);
    for (int i = 0; i < GAME_OPTION_COUNT(kGameOptions); i++) {
      const GameOption& o = kGameOptions[i];
      std::string v;          // the first ticked target's file that says, then any that does
      for (int pass = 0; pass < 2 && v.empty(); pass++)
        for (int t = 0; t < 3 && v.empty(); t++)
          if ((o.where & TargetBit(t)) && (pass || Ticked(t))) v = IniValue(text[t], o.section, o.key);
      if (v.empty()) v = !std::strcmp(o.key, "language") ? sd::CurrentLanguage().c_str() : o.def;
      for (int k = 0; k < o.count; k++)
        if (v == o.values[k].value) optChoice[size_t(i)] = k;
    }
  }

  // The rows for what is ticked, with the values its data allows.
  void RefreshOptions() {
    if (optChoice.empty()) LoadOptions();
    int where = 0, ticked = 0;
    for (int t = 0; t < 3; t++)
      if (Ticked(t)) {
        where |= TargetBit(t);
        ticked++;
      }
    auto rows = std::make_shared<slint::VectorModel<OptionRow>>();
    optRows.clear();
    for (int i = 0; i < GAME_OPTION_COUNT(kGameOptions); i++) {
      const GameOption& o = kGameOptions[i];
      if (!(o.where & where) || (o.needs == GAME_OPTION_NEEDS_WORLD && !haveWorldData)) continue;
      std::vector<int> values;
      auto labels = std::make_shared<slint::VectorModel<slint::SharedString>>();
      int selected = 0;
      for (int k = 0; k < o.count; k++) {
        if (!ValueAvailable(o.values[k])) continue;
        if (k == optChoice[size_t(i)]) selected = int(values.size());
        values.push_back(k);
        labels->push_back(Str(T(o.values[k].label)));
      }
      if (values.size() < 2) continue;                 // nothing to choose between
      // Ticked targets it is not for: the tag names the ones it is.
      std::string tag;
      int applies = 0;
      for (int t = 0; t < 3; t++)
        if (Ticked(t) && (o.where & TargetBit(t))) {
          applies++;
          tag += std::string(tag.empty() ? "" : ", ") + T(t == kComputer ? "this computer" : t == kPhone ? "phone" : "handheld");
        }
      if (applies == ticked) tag.clear();
      rows->push_back({Str(T(o.label)), Str(T(o.about)), Str(tag), labels, selected});
      optRows.push_back({i, values});
    }
    w().set_options(rows);
  }

  // A choice: kept, and written to the SWOS.ini(s) it belongs in.
  void OptionChosen(int row, int choice) {
    if (row < 0 || row >= int(optRows.size()) || choice < 0 || choice >= int(optRows[size_t(row)].second.size())) return;
    const int i = optRows[size_t(row)].first;
    if (!std::strcmp(kGameOptions[i].key, "language")) gameLangChosen = true;
    WriteOption(i, optRows[size_t(row)].second[size_t(choice)]);
    RefreshOptions();
  }

  // Option i set to its value k, in the SWOS.ini of every ticked target it is for.
  void WriteOption(int i, int k) {
    if (optChoice.empty()) LoadOptions();
    optChoice[size_t(i)] = k;
    const GameOption& o = kGameOptions[i];
    const char* value = o.values[optChoice[size_t(i)]].value;
    for (int t = 0; t < 3; t++) {
      if (!(o.where & TargetBit(t)) || !Ticked(t)) continue;
      const std::string path = IniPath(t);
      if (t != kComputer) sd::MakeDirs(sd::ParentDir(path));
      const std::string text = IniSet(ReadFile(path), o.section, o.key, value);
      if (FILE* f = sd::OpenFile(path, "wb")) {
        std::fwrite(text.data(), 1, text.size(), f);
        std::fclose(f);
        FileLog("game options: " + Shown(path) + ": [" + o.section + "] " + o.key + "=" + value);
      } else
        Notice(Tf("Could not write {0}.", Shown(path)), true);
    }
  }

  // ------------------------------------------------------- the setup's language ---

  bool gameLangChosen = false;   // the player chose the game's language on GAME OPTIONS

  std::string SetupIni() const { return Join(UserDir(), "setup.ini"); }

  // The setup in `code`: its bundled translation, its own texts, and -- unless the player chose
  // the game's language themselves -- the game's language with it.
  void LanguageChosen(const std::string& code, bool remember) {
    sd::SetLanguage(code);
    slint::select_bundled_translation(sd::CurrentLanguage() == "en" ? "" : sd::CurrentLanguage());
    w().set_language(Str(sd::CurrentLanguage()));
    if (remember) {
      const std::string text = IniSet(ReadFile(SetupIni()), "setup", "language", sd::CurrentLanguage().c_str());
      sd::MakeDirs(UserDir());
      if (FILE* f = sd::OpenFile(SetupIni(), "wb")) {
        std::fwrite(text.data(), 1, text.size(), f);
        std::fclose(f);
      }
      FileLog("language: " + sd::CurrentLanguage());
      SyncGameLanguage();
    }
    // What C++ made in the old language, made again.
    Refresh();
    RefreshWizard();
    if (!optChoice.empty()) RefreshOptions();
    if (phoneTimer.running()) PhonePoll();
    HandheldShow();
    if (watchTimer.running()) w().set_watching(Str(WatchingText()));
  }

  // The game's language as the setup's, unless the player chose it on GAME OPTIONS themselves.
  void SyncGameLanguage() {
    if (gameLangChosen) return;
    for (int i = 0; i < GAME_OPTION_COUNT(kGameOptions); i++)
      if (!std::strcmp(kGameOptions[i].key, "language"))
        for (int k = 0; k < kGameOptions[i].count; k++)
          if (sd::CurrentLanguage() == kGameOptions[i].values[k].value) WriteOption(i, k);
  }

  // At the start: the language chosen last time, else the system's.
  std::string cliLanguage;      // --language=xx: this run in that language, not remembered

  void StartLanguage() {
    std::string code = !cliLanguage.empty() ? cliLanguage : IniValue(ReadFile(SetupIni()), "setup", "language");
    if (code.empty()) code = sd::SystemLanguage();
    LanguageChosen(code, false);
  }

  // --------------------------------------------- the handheld (PortMaster) ---

  // The card: a disk with PortMaster's ports folder where one of the firmwares keeps it
  // (ArkOS, ROCKNIX, AmberELEC: ports/ on the games partition; Knulli/Batocera: roms/ports;
  // stock TrimUI: Data/ports), or the folder the player picked. The port is ports/swos.
  std::string hhPorts;          // the card's ports folder, "" when none is found
  std::string hhZip;            // the port, OpenSWOS for PortMaster: swos.zip
  std::atomic<uint64_t> hhDone{0};
  std::string hhAuto;           // testing: --handheld-copy=DIR, that ports folder copied to at once
  uint64_t hhTotal = 0;
  slint::Timer hhTimer;

  static std::string PortsIn(const std::string& root) {
    for (const char* rel : {"ports", "roms/ports", "Roms/ports", "Data/ports", "ROMS/PORTS", "roms/PORTS"})
      if (sd::IsDir(Join(root, rel))) return Join(root, rel);
    return {};
  }

  static bool PortZip(const std::string& path) {
    const std::string name = Lower(path.substr(path.find_last_of("/\\") + 1));
    return EndsWith(name, ".zip") && (name == "swos.zip" || name.rfind("openswos-portmaster", 0) == 0) &&
           SdSources_ZipHas(path.c_str(), "Sensible World of Soccer.sh");
  }

  std::string FindPortZip() const {
    for (std::string d = exeDir; !d.empty();) {
      for (const char* rel : {"swos.zip", "portmaster/swos.zip", "dist/portmaster/swos.zip"})
        if (sd::IsFile(Join(d, rel)) && PortZip(Join(d, rel))) return Join(d, rel);
      const std::string up = sd::ParentDir(d);
      if (up == d) break;
      d = up;
    }
    std::string best;
    int64_t newest = 0;
    const std::string dl = !watchDir.empty() ? watchDir : sd::DownloadsDir();
    if (!dl.empty())
      for (const sd::DirEntry& e : sd::ListDir(dl))
        if (!e.dir && (best.empty() || e.mtime > newest) && PortZip(e.path)) {
          best = e.path;
          newest = e.mtime;
        }
    return best;
  }

  void HandheldFind() {
    if (hhZip.empty()) hhZip = FindPortZip();
    if (!hhAuto.empty()) {
      hhPorts = hhAuto;
      HandheldShow();
      hhAuto.clear();
      autoRun = true;
      HandheldCopy();
      return;
    }
    if (hhPorts.empty() || !sd::IsDir(hhPorts))
      for (const std::string& root : sd::VolumeRoots()) {
        const std::string ports = PortsIn(root);
        const bool ownDisk = root.size() > 1 && root[1] == ':' && exeDir.size() > 1 && exeDir[1] == ':' &&
                             std::toupper((unsigned char)root[0]) == std::toupper((unsigned char)exeDir[0]);
        if (!ports.empty() && !ownDisk) {   // (not this program's own disk)
          hhPorts = ports;
          break;
        }
      }
    HandheldShow();
  }

  void HandheldShow() {
    const bool port = !hhPorts.empty() && sd::IsFile(Join(hhPorts, "swos/SWOS.aarch64"));
    w().set_hh_card(Str(hhPorts.empty() ? "" : Shown(hhPorts)));
    w().set_hh_port(Str(hhZip.empty() ? "" : Shown(hhZip)));
    w().set_hh_installed(port);
    if (w().get_hh_state() == 2 || w().get_hh_state() == 3) return;   // copying, copied: as it was
    if (hhPorts.empty()) {
      w().set_hh_state(0);
      w().set_hh_status(Str(T("No handheld card found. Put its SD card in this computer, or choose its ports folder.")));
    } else if (!port && hhZip.empty()) {
      w().set_hh_state(4);
      w().set_hh_status(Str(Tf("{0} for PortMaster is not on this card, and this setup has no copy of it: "
                               "download swos.zip ({0} for PortMaster) and drop it on this window.", PROJECT_NAME)));
    } else {
      w().set_hh_state(1);
      w().set_hh_status(Str(port ? (hhZip.empty() ? T("Ready to copy the game's data to the card.")
                                                  : Tf("Ready: the game on the card is updated from {0}, and its data copied.", Shown(hhZip)))
                                 : Tf("Ready: {0} is installed on the card from {1}, with the game's data.", PROJECT_NAME, Shown(hhZip))));
    }
  }

  void HandheldChange() {
    const std::string dir = PickFolder(hhPorts.empty() ? std::string() : hhPorts);
    if (dir.empty()) return;
    std::string ports = PortsIn(dir);
    const std::string name = Lower(dir.substr(dir.find_last_of("/\\") + 1));
    if (ports.empty() && (name == "ports" || sd::IsDir(Join(dir, "swos")))) ports = dir;
    if (ports.empty() && name == "swos") ports = sd::ParentDir(dir);
    if (ports.empty()) {
      w().set_hh_state(4);
      w().set_hh_status(Str(Tf("No ports folder in {0}: choose the card itself, or its ports folder.", Shown(dir))));
      return;
    }
    hhPorts = ports;
    w().set_hh_state(0);
    HandheldShow();
  }

  void HandheldCopy() {
    if (busy || hhPorts.empty()) return;
    if (worker.joinable()) worker.join();
    SetBusy(true);
    w().set_hh_state(2);
    w().set_hh_progress(0.0f);
    const std::string ports = hhPorts, zip = hhZip, assets = outDir, sent = IniPath(kHandheld);
    const ModsForCopy mods = ModsForSdlCopy();     // (docs/MODS.md: the mods that are on)
    hhTotal = sd::TreeSize(assets, "amiga");
    hhDone = 0;
    w().set_hh_status(Str(T("Copying to the card...")));
    FileLog("handheld: copying " + Shown(assets) + " to " + Shown(ports) + (zip.empty() ? "" : ", the port from " + Shown(zip)));
    hhTimer.start(slint::TimerMode::Repeated, std::chrono::milliseconds(200), [this] {
      w().set_hh_progress(hhTotal ? float(double(hhDone) / double(hhTotal)) : 0.0f);
    });
    worker = std::thread([this, ports, zip, assets, sent, mods] {
      std::string err;
      const std::string game = Join(ports, "swos");
      // 1. The port: unpacked beside this setup, then its launcher and its folder onto the card
      //    (over what is there: its own files only, conf/ and assets/ stay); port.json and the
      //    README into its folder, not loose in ports/ where another port's could be.
      if (!zip.empty()) {
        const std::string tmp = Join(sd::ParentDir(outDir), ".portmaster-unpack");
        sd::RemoveTree(tmp);
        if (SdSources_Unzip(zip.c_str(), tmp.c_str()) != 0 || !sd::IsDir(Join(tmp, "swos")))
          err = Tf("could not unpack {0}", zip);
        if (err.empty() && !sd::CopyTree(Join(tmp, "swos"), game, "", nullptr, &err)) err = Tf("could not copy the port: {0}", err);
        for (const char* f : {"Sensible World of Soccer.sh", "port.json", "README.md"}) {
          if (!err.empty() || !sd::IsFile(Join(tmp, f))) continue;
          const std::string text = ReadFile(Join(tmp, f));
          const std::string to = std::string(f).back() == 'h' ? Join(ports, f) : Join(game, f);
          FILE* o = sd::OpenFile(to, "wb");
          if (!o || std::fwrite(text.data(), 1, text.size(), o) != text.size()) err = Tf("could not write {0} on the card", f);
          if (o) std::fclose(o);
        }
        sd::RemoveTree(tmp);
      }
      if (err.empty() && !sd::IsFile(Join(game, "SWOS.aarch64"))) err = Tf("{0} for PortMaster is not on the card", PROJECT_NAME);
      // 2. The game's data, into assets.part, then in the old one's place.
      const std::string part = Join(game, "assets.part"), dest = Join(game, "assets");
      if (err.empty()) {
        sd::RemoveTree(part);
        if (!sd::CopyTree(assets, part, "amiga", [this](uint64_t n) { hhDone += n; }, &err)) sd::RemoveTree(part);
      }
      if (err.empty()) {
        // A world on the card the setup does not have (copied by hand) stays.
        for (const sd::DirEntry& e : sd::ListDir(Join(dest, "data_json/worlds"))) {
          const std::string name = e.path.substr(e.path.find_last_of("/\\") + 1);
          const std::string to = Join(part, "data_json/worlds/" + name);
          if (e.dir && !sd::IsDir(to)) {
            sd::MakeDirs(sd::ParentDir(to));
            sd::RenamePath(e.path, to);
          }
        }
        sd::RemoveTree(dest);
        if (!sd::RenamePath(part, dest)) err = N_("could not put the game's data in place on the card");
      }
      // 3. The options chosen for the handheld, into its SWOS.ini (the port's defaults first).
      if (err.empty()) {
        const std::string ini = Join(game, "conf/.swos/SWOS.ini");
        std::string text = ReadFile(ini);
        if (text.empty()) text = ReadFile(Join(game, "SWOS.ini.default"));
        const std::string chosen = ReadFile(sent);
        int set = 0;
        for (int i = 0; i < GAME_OPTION_COUNT(kGameOptions); i++) {
          const GameOption& o = kGameOptions[i];
          const std::string v = IniValue(chosen, o.section, o.key);
          if (!(o.where & GAME_OPTION_HANDHELD) || v.empty()) continue;
          text = IniSet(text, o.section, o.key, v.c_str());
          set++;
        }
        // 4. The mods that are on, into the port's player folder (its HOME is conf/), and their
        //    [mods] lines into the same SWOS.ini.
        std::string modErr;
        if (!CopyModsTo(mods, Join(game, "conf/.swos/mods"), text, &modErr))
          err = Tf("could not copy the mods: {0}", modErr);
        sd::MakeDirs(Join(game, "conf/.swos/save"));
        if (FILE* f = sd::OpenFile(ini, "wb")) {
          std::fwrite(text.data(), 1, text.size(), f);
          std::fclose(f);
        } else
          err = N_("could not write the game's settings on the card");
      }
      slint::invoke_from_event_loop([this, err] {
        hhTimer.stop();
        SetBusy(false);
        if (err.empty()) {
          w().set_hh_progress(1.0f);
          w().set_hh_state(3);
          w().set_hh_status(Str(T("Copied. Eject the card safely, put it back in the handheld, and start "
                                  "Sensible World of Soccer from Ports (refresh the games list if it is not there).")));
          FileLog("handheld: copied");
        } else {
          w().set_hh_state(4);
          w().set_hh_status(Str(Tf("The copy did not work: {0}.", T(err))));
          FileLog("handheld: failed: " + err);
          exitCode = 1;
        }
        HandheldShow();
        AutoFinished();
      });
    });
  }

  // -------------------------------------------------------------- the mods ---
  //
  // docs/MODS.md §14: a mod is a folder with a mod.json; DLC mods are the player's, in the mods
  // folder beside SWOS.ini (UserDir()/mods), the game's own (canonical) ones beside the game
  // (its setup/ folder's parent's mods/). Installing one: its zip unpacked, its manifest checked
  // as the game checks it (a uuid; "sdl" or "amiga" in its targets; not a canonical uuid), the
  // folder put in place and the mod turned on ([mods] enabled in this computer's SWOS.ini). The
  // game reads the same list (src/mods.c); OPTIONS -> MODS toggles it there too.

  struct ModFile {
    std::string slug, dir, name, version, uuid, changes, problem;
    bool canonical = false, byDefault = false, sdl = false, amiga = false;
  };
  std::vector<ModFile> modList;
  static constexpr const char* kModChanges[] = { N_("cosmetic"), N_("gameplay"), N_("career"), N_("data"),
                                                 N_("modes") };   // (shown translated)

  // A world's mod (docs/MODS.md §17): its folder world<tag>, its data a world's.
  static bool IsWorldMod(const ModFile& m) {
    return m.slug.rfind("world", 0) == 0 && sd::IsDir(Join(m.dir, "data_json/worlds"));
  }
  // An install that put a world in (SwosData_InstallWorldMod) leaves its mod on: the player
  // asked for it.
  void WorldModOn() {
    ModsScan();
    for (const ModFile& m : modList)
      if (!m.canonical && IsWorldMod(m) && m.problem.empty() && !ModOn(m)) {
        SetModOn(m, true);
        FileLog("mods: " + m.slug + " on (the world just installed)");
      }
  }
  bool HaveWorldMod() {
    ModsScan();
    for (const ModFile& m : modList)
      if (IsWorldMod(m) && m.problem.empty() && m.amiga) return true;
    return false;
  }
  std::vector<std::string> modZipsFound;   // mod zips in Downloads, not installed

  std::string ModsDir() const { return Join(UserDir(), "mods"); }
  std::string CanonicalModsDir() const { return resDir.empty() ? std::string() : Join(sd::ParentDir(resDir), "mods"); }

  // A top-level "key": value of a manifest -- a string, true/false, or for "targets" a list of
  // strings. A small reader for a small, flat file (mod.json has nothing nested but that list).
  static std::string ManifestValue(const std::string& j, const char* key) {
    const std::string k = std::string("\"") + key + "\"";
    size_t at = j.find(k);
    if (at == std::string::npos) return {};
    at = j.find(':', at + k.size());
    if (at == std::string::npos) return {};
    at = j.find_first_not_of(" \t\r\n", at + 1);
    if (at == std::string::npos) return {};
    if (j[at] == '"') {
      std::string out;
      for (size_t i = at + 1; i < j.size() && j[i] != '"'; i++) {
        if (j[i] == '\\' && i + 1 < j.size()) i++;
        out += j[i];
      }
      return out;
    }
    if (j[at] == '[') {
      const size_t end = j.find(']', at);
      return end == std::string::npos ? std::string() : j.substr(at, end - at + 1);
    }
    size_t end = j.find_first_of(",}\r\n", at);
    std::string v = j.substr(at, end == std::string::npos ? std::string::npos : end - at);
    while (!v.empty() && (v.back() == ' ' || v.back() == '\t')) v.pop_back();
    return v;
  }
  static bool UuidOk(const std::string& u) {
    if (u.size() != 36) return false;
    for (size_t i = 0; i < 36; i++) {
      const char c = u[i];
      if (i == 8 || i == 13 || i == 18 || i == 23) { if (c != '-') return false; }
      else if (!std::isxdigit((unsigned char)c)) return false;
    }
    return true;
  }
  static bool SlugOk(const std::string& s) {
    if (s.empty() || s.size() >= 32) return false;
    for (char c : s)
      if (!std::isalnum((unsigned char)c) && c != '-' && c != '_') return false;
    return true;
  }

  // What a mod's folder says; problem "" when the game can take it.
  ModFile ReadMod(const std::string& dir, bool canonical) const {
    ModFile m;
    m.dir = dir;
    m.slug = dir.substr(dir.find_last_of("/\\") + 1);
    m.canonical = canonical;
    const std::string j = ReadFile(Join(dir, "mod.json"));
    if (j.empty()) { m.problem = N_("no mod.json in its folder"); return m; }
    m.name = ManifestValue(j, "name");
    if (m.name.empty()) m.name = m.slug;
    m.version = ManifestValue(j, "version");
    m.uuid = ManifestValue(j, "uuid");
    m.changes = ManifestValue(j, "changes");
    if (m.changes.empty()) m.changes = "gameplay";
    m.byDefault = ManifestValue(j, "default") == "true";
    const std::string targets = ManifestValue(j, "targets");
    m.sdl = targets.find("\"sdl\"") != std::string::npos;
    m.amiga = targets.find("\"amiga\"") != std::string::npos;
    if (!UuidOk(m.uuid)) m.problem = N_("its mod.json has no uuid");
    else if (!m.sdl && !m.amiga) m.problem = N_("not made for this game (no \"sdl\" or \"amiga\" in its targets)");
    return m;
  }

  // [mods] enabled / disabled of this computer's SWOS.ini, as lists of slugs.
  static std::vector<std::string> SplitList(const std::string& v) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : v + ",") {
      if (c == ',' || c == ' ' || c == '\t') { if (!cur.empty()) out.push_back(cur); cur.clear(); }
      else cur += c;
    }
    return out;
  }
  static std::string JoinList(const std::vector<std::string>& l) {
    std::string out;
    for (const std::string& s : l) out += (out.empty() ? "" : ", ") + s;
    return out;
  }
  bool ModOn(const ModFile& m) const {
    const std::string ini = ReadFile(IniPath(kComputer));
    const auto has = [](const std::vector<std::string>& l, const std::string& s) {
      return std::find(l.begin(), l.end(), s) != l.end();
    };
    if (m.canonical && m.byDefault) return !has(SplitList(IniValue(ini, "mods", "disabled")), m.slug);
    return has(SplitList(IniValue(ini, "mods", "enabled")), m.slug);
  }
  void SetModOn(const ModFile& m, bool on) {
    const std::string path = IniPath(kComputer);
    std::string ini = ReadFile(path);
    std::vector<std::string> en = SplitList(IniValue(ini, "mods", "enabled")),
                             dis = SplitList(IniValue(ini, "mods", "disabled"));
    en.erase(std::remove(en.begin(), en.end(), m.slug), en.end());
    dis.erase(std::remove(dis.begin(), dis.end(), m.slug), dis.end());
    if (m.canonical && m.byDefault) { if (!on) dis.push_back(m.slug); }
    else if (on) en.push_back(m.slug);
    ini = IniSet(ini, "mods", "enabled", JoinList(en).c_str());
    ini = IniSet(ini, "mods", "disabled", JoinList(dis).c_str());
    sd::MakeDirs(sd::ParentDir(path));
    if (FILE* f = sd::OpenFile(path, "wb")) {
      std::fwrite(ini.data(), 1, ini.size(), f);
      std::fclose(f);
    }
    FileLog("mods: " + m.slug + (on ? " on" : " off") + " ([mods] enabled = " + JoinList(en) + "; disabled = " + JoinList(dis) + ")");
  }

  // Every mod on disk: the canonical ones, then the DLC; and the mod zips in Downloads. (Disk
  // only: ModsRefresh shows it.)
  void ModsScan() {
    modList.clear();
    for (int pass = 0; pass < 2; pass++) {
      const std::string root = pass == 0 ? CanonicalModsDir() : ModsDir();
      if (root.empty()) continue;
      for (const sd::DirEntry& e : sd::ListDir(root)) {
        if (!e.dir) continue;
        ModFile m = ReadMod(e.path, pass == 0);
        if (!SlugOk(m.slug)) continue;
        modList.push_back(m);
      }
    }
    for (ModFile& m : modList)       // (a DLC with a canonical uuid: the game refuses it too)
      if (!m.canonical && m.problem.empty())
        for (const ModFile& c : modList)
          if (c.canonical && c.uuid == m.uuid) m.problem = N_("its uuid is the game's own mod's");
    modZipsFound.clear();
    const std::string dl = !watchDir.empty() ? watchDir : sd::DownloadsDir();
    if (!dl.empty())
      for (const sd::DirEntry& e : sd::ListDir(dl))
        if (!e.dir && EndsWith(Lower(e.path), ".zip") && ModZip(e.path).size()) {
          const std::string stem = Lower(e.path.substr(e.path.find_last_of("/\\") + 1));
          bool have = false;
          for (const ModFile& m : modList)
            if (stem == Lower(m.slug) + ".zip") have = true;
          if (!have) modZipsFound.push_back(e.path);
        }
  }

  void ModsRefresh() {
    ModsScan();
    auto rows = std::make_shared<slint::VectorModel<ModRow>>();
    for (const ModFile& m : modList) {
      ModRow r;
      r.name = Str(m.name + (m.version.empty() ? "" : " " + m.version));
      r.info = Str(Tf("{0}, {1}{2}", m.canonical ? T("part of the game") : T("downloaded"), T(m.changes),
                      m.amiga && m.sdl ? T(", also on the Amiga") : m.amiga ? T(", Amiga only") : ""));
      r.on = m.problem.empty() && ModOn(m);
      r.problem = Str(m.problem.empty() ? "" : T(m.problem.c_str()));
      r.canonical = m.canonical;
      rows->push_back(r);
    }
    w().set_mods(rows);
    auto found = std::make_shared<slint::VectorModel<slint::SharedString>>();
    for (const std::string& z : modZipsFound) found->push_back(Str(z.substr(z.find_last_of("/\\") + 1)));
    w().set_mods_found(found);
  }

  // The zip's mod.json entry ("mod.json", or "<folder>/mod.json" for a zip of one folder), or "".
  static std::string ModZip(const std::string& zip) {
    if (SdSources_ZipHas(zip.c_str(), "mod.json")) return "mod.json";
    std::string stem = zip.substr(zip.find_last_of("/\\") + 1);
    if (stem.size() > 4) stem.resize(stem.size() - 4);
    const std::string nested = stem + "/mod.json";
    return SdSources_ZipHas(zip.c_str(), nested.c_str()) ? nested : std::string();
  }

  // Install a mod's zip into the mods folder, turned on. "" when it is in, else why not.
  std::string ModInstall(const std::string& zip) {
    const std::string entry = ModZip(zip);
    if (entry.empty()) return Tf("{0} is not a mod (no mod.json in it)", Shown(zip));
    const std::string tmp = Join(UserDir(), ".mod-unpack");
    sd::RemoveTree(tmp);
    if (SdSources_Unzip(zip.c_str(), tmp.c_str()) != 0) { sd::RemoveTree(tmp); return Tf("could not unpack {0}", Shown(zip)); }
    const std::string from = entry == "mod.json" ? tmp : Join(tmp, entry.substr(0, entry.size() - 9));
    ModFile m = ReadMod(from, false);
    std::string slug = ManifestValue(ReadFile(Join(from, "mod.json")), "id");
    if (!SlugOk(slug)) {
      slug = zip.substr(zip.find_last_of("/\\") + 1);
      slug.resize(slug.size() - 4);
      for (char& c : slug) if (!std::isalnum((unsigned char)c) && c != '-' && c != '_') c = '_';
      if (slug.size() > 31) slug.resize(31);
    }
    for (const ModFile& c : modList)
      if (c.canonical && c.uuid == m.uuid) { sd::RemoveTree(tmp); return T("its uuid is the game's own mod's"); }
    if (!m.problem.empty()) { sd::RemoveTree(tmp); return T(m.problem.c_str()); }
    // The same mod (its uuid) under another folder is this one's older copy: replaced.
    for (const ModFile& c : modList)
      if (!c.canonical && c.uuid == m.uuid && c.slug != slug) { SetModOn(c, false); sd::RemoveTree(c.dir); }
    const std::string to = Join(ModsDir(), slug);
    std::string err;
    sd::RemoveTree(to);
    sd::MakeDirs(ModsDir());
    if (!sd::CopyTree(from, to, "", nullptr, &err)) { sd::RemoveTree(tmp); return Tf("could not install it: {0}", err); }
    sd::RemoveTree(tmp);
    m = ReadMod(to, false);
    SetModOn(m, true);
    FileLog("mods: installed " + slug + " (" + m.name + " " + m.version + ", " + m.uuid + ") from " + Shown(zip));
    return "";
  }

  void ModsInstallFrom(const std::string& zip) {
    const std::string why = ModInstall(zip);
    ModsRefresh();
    w().set_mods_status(Str(why.empty() ? Tf("Installed {0}: it is on from the next time the game starts.", Shown(zip))
                                        : Tf("Not installed: {0}.", why)));
    w().set_mods_status_error(!why.empty());
  }
  void ModsBrowse() {
    const std::string picked = PickFile("A mod", "zip");
    if (!picked.empty()) ModsInstallFrom(picked);
  }
  void ModsInstallFound(int i) {
    if (i >= 0 && i < int(modZipsFound.size())) ModsInstallFrom(modZipsFound[i]);
  }
  void ModsToggle(int i) {
    if (i < 0 || i >= int(modList.size()) || !modList[i].problem.empty()) return;
    const bool on = !ModOn(modList[i]);
    SetModOn(modList[i], on);
    ModsRefresh();
    w().set_mods_status(Str(Tf(on ? T("{0} is on from the next time the game starts.").c_str()
                                  : T("{0} is off from the next time the game starts.").c_str(), modList[i].name)));
    w().set_mods_status_error(false);
  }
  void ModsRemove(int i) {
    if (i < 0 || i >= int(modList.size()) || modList[i].canonical) return;
    const ModFile m = modList[i];
    SetModOn(m, false);
    sd::RemoveTree(m.dir);
    FileLog("mods: removed " + m.slug);
    ModsRefresh();
    w().set_mods_status(Str(Tf("Removed {0}.", m.name)));
    w().set_mods_status_error(false);
  }

  // The downloaded mods that are on and made for an SDL target (a phone, a handheld): theirs to
  // copy, with the [mods] lines that game needs -- this computer's lists (a canonical mod is in
  // the game it plays already; a slug the other game does not have is skipped there, and said).
  struct ModsForCopy { std::vector<ModFile> copy; std::string enabled, disabled; };
  ModsForCopy ModsForSdlCopy() {
    ModsScan();
    ModsForCopy out;
    for (const ModFile& m : modList)
      if (!m.canonical && m.sdl && m.problem.empty() && ModOn(m)) out.copy.push_back(m);
    const std::string ini = ReadFile(IniPath(kComputer));
    out.enabled = IniValue(ini, "mods", "enabled");
    out.disabled = IniValue(ini, "mods", "disabled");
    return out;
  }
  // Copy them into `modsRoot` (replacing what an earlier copy left there) and set the lines in
  // `iniText`. Worker-safe: disk and strings only. False and why on a failure.
  static bool CopyModsTo(const ModsForCopy& mods, const std::string& modsRoot, std::string& iniText,
                         std::string* err) {
    sd::RemoveTree(modsRoot);
    for (const ModFile& m : mods.copy)
      if (!sd::CopyTree(m.dir, Join(modsRoot, m.slug), "", nullptr, err)) return false;
    iniText = IniSet(iniText, "mods", "enabled", mods.enabled.c_str());
    iniText = IniSet(iniText, "mods", "disabled", mods.disabled.c_str());
    return true;
  }

  // An Amiga drawer made in `drawerGame` (its SWOS folder): the mods that are on and made for the
  // Amiga into its mods/, and data/MODS listing them (an id with '*': not cosmetic) -- or no
  // data/MODS when none is, so an earlier one does not stay.
  // A mod with an amiga/ folder gives the Amiga that folder's contents only (World 2026-27: its
  // Amiga team files, not the PC's). A world's mod goes in only with the World 2026-27 tick.
  void ModsToDrawer(const std::string& drawerGame, bool withWorld) {
    ModsScan();
    std::string list;
    const std::string data = Join(drawerGame, "data/MODS");
    sd::RemoveTree(Join(drawerGame, "mods"));
    for (const ModFile& m : modList) {
      if (!m.amiga || !m.problem.empty() || !ModOn(m)) continue;
      if (IsWorldMod(m) && !withWorld) continue;
      std::string err;
      const std::string from = sd::IsDir(Join(m.dir, "amiga")) ? Join(m.dir, "amiga") : m.dir;
      if (!sd::CopyTree(from, Join(Join(drawerGame, "mods"), m.slug), "", nullptr, &err)) {
        FileLog("mods: could not copy " + m.slug + " to the drawer: " + err);
        continue;
      }
      list += m.slug + (m.changes == "cosmetic" || m.canonical ? "" : "*") + "\n";
    }
    if (list.empty()) {
      std::remove(data.c_str());
      return;
    }
    if (FILE* f = sd::OpenFile(data, "wb")) {
      std::fwrite(list.data(), 1, list.size(), f);
      std::fclose(f);
    }
    FileLog("mods: the drawer's data/MODS = " + list);
  }

  // ------------------------------------------------------------ the wizard ---

  // The screens, from what was ticked (app.slint's Step): where you will play first; a computer
  // or a phone alone asks which copy you have (either will do), an Amiga needs both; then each
  // source, each extra, install, the phone's copy, the Amiga's outputs, done.
  void RefreshWizard() {
    // (a computer, a phone and a handheld all play the SDL game: the same data, options, install)
    const bool compPhone = w().get_t_computer() || w().get_t_phone() || w().get_t_handheld();
    const bool amigaT = w().get_t_amiga() || w().get_t_whdload();
    const int have = w().get_have();
    std::vector<int> steps{0};
    if (compPhone || amigaT) {
      const bool needPc = amigaT || have != 1, needAmiga = amigaT || have != 0;
      if (compPhone && !amigaT) steps.push_back(1);
      if (needPc) steps.push_back(2);
      if (needAmiga) steps.push_back(3);
      if (kWorldUrl[0] && w().get_x_world()) steps.push_back(4);
      if (w().get_x_2020()) steps.push_back(5);
      if (compPhone && w().get_x_jaguar()) steps.push_back(6);
      if (compPhone) steps.push_back(12);   // game options
      if (w().get_x_mods()) steps.push_back(14);   // mods (docs/MODS.md)
      if (compPhone) steps.push_back(7);
      if (w().get_t_phone()) steps.push_back(8);
      if (w().get_t_handheld()) steps.push_back(13);
      if (w().get_t_amiga()) steps.push_back(9);
      if (w().get_t_whdload()) steps.push_back(10);
      steps.push_back(11);
    }
    w().set_steps(std::make_shared<slint::VectorModel<int>>(steps));
    if (w().get_step() >= int(steps.size())) w().set_step(int(steps.size()) - 1);
  }

  // A screen comes up: the Amiga's two make the drawer or the package (one setting between
  // them), with World 2026-27 when it was ticked.
  void StepShown(int kind) {
    if (kind == 9 || kind == 10) {
      w().set_tab(1);
      w().set_amiga_kind(kind == 9 ? 0 : 1);
      AmigaKindChanged();
      if (w().get_x_world() && w().get_amiga_world_available()) w().set_amiga_world(true);
    } else if (kind == 7) {
      w().set_tab(0);
    } else if (kind == 12) {
      SyncGameLanguage();
      RefreshOptions();
    } else if (kind == 8) {
      PhoneStart();
    } else if (kind == 13) {
      HandheldFind();
    } else if (kind == 14) {
      ModsRefresh();
    }
  }

  // ------------------------------------------------- the Wi-Fi copy to a phone ---

  slint::Timer phoneTimer;
  std::string phoneSaid;

  // The phone screen: the assets folder served on the network, its QR code shown. Kept up
  // until the setup closes (a phone half way through a copy is not cut off by Back).
  void PhoneStart() {
    // The mods that are on (docs/MODS.md): served beside the assets, their [mods] lines in the
    // phone's SWOS.ini.
    const ModsForCopy mods = ModsForSdlCopy();
    std::vector<std::pair<std::string, std::string>> modDirs;
    for (const ModFile& m : mods.copy) modDirs.push_back({m.slug, m.dir});
    {
      const std::string path = IniPath(kPhone);
      std::string ini = ReadFile(path);
      ini = IniSet(ini, "mods", "enabled", mods.enabled.c_str());
      ini = IniSet(ini, "mods", "disabled", mods.disabled.c_str());
      sd::MakeDirs(sd::ParentDir(path));
      if (FILE* f = sd::OpenFile(path, "wb")) {
        std::fwrite(ini.data(), 1, ini.size(), f);
        std::fclose(f);
      }
    }
    if (!sd::PhoneServerStart(outDir, IniPath(kPhone), modDirs)) {
      const std::string err = sd::PhoneServerStatus().error;
      w().set_phone_status(Str(err));
      FileLog("phone copy: " + err);
      return;
    }
    const sd::PhoneServerState st = sd::PhoneServerStatus();
    w().set_phone_url(Str(st.url));
    int n = 0;
    const std::vector<bool> qr = sd::QrCode(sd::PhoneServerLink(), &n);
    if (n > 0) {
      const int scale = 6, border = 4, side = (n + 2 * border) * scale;
      slint::SharedPixelBuffer<slint::Rgb8Pixel> px{static_cast<uint32_t>(side), static_cast<uint32_t>(side)};
      slint::Rgb8Pixel* p = px.begin();
      for (int y = 0; y < side; y++)
        for (int x = 0; x < side; x++) {
          const int mx = x / scale - border, my = y / scale - border;
          const bool dark = mx >= 0 && my >= 0 && mx < n && my < n && qr[size_t(my * n + mx)];
          const uint8_t v = dark ? 0 : 255;
          p[y * side + x] = slint::Rgb8Pixel{v, v, v};
        }
      w().set_phone_qr(slint::Image(px));
    }
    if (phoneSaid.empty()) FileLog("phone copy: serving " + Shown(outDir) + " at " + st.url);
    PhonePoll();
    if (!phoneTimer.running())
      phoneTimer.start(slint::TimerMode::Repeated, std::chrono::milliseconds(400), [this] { PhonePoll(); });
  }

  void PhonePoll() {
    const sd::PhoneServerState st = sd::PhoneServerStatus();
    std::string said;
    int state = 0;     // app.slint's phone-state: 0 waiting, 1 copying, 2 done, 3 cannot
    if (!st.running) {
      said = T(st.error);
      state = 3;
    } else if (st.finished) {
      said = Tf("Copied to the phone ({0} files). {1} on it is ready to play.", st.files, PROJECT_NAME);
      state = 2;
    } else if (st.files > 0) {
      const int pct = st.bytes ? int(st.bytesSent * 100 / st.bytes) : 0;
      said = Tf("Copying to the phone: {0}% ({1} of {2} files)", pct > 100 ? 100 : pct, st.filesSent, st.files);
      state = 1;
    } else if (!st.phone.empty()) {
      said = Tf("The phone found this computer ({0}). Tap Open in {1} on it.", st.phone, PROJECT_NAME);
    } else
      said = T("Waiting for the phone...");
    w().set_phone_status(Str(said));
    w().set_phone_state(state);
    if (state != 1 && said != phoneSaid) FileLog("phone copy: " + said);
    phoneSaid = said;
  }

  // Files dropped on the window: each found as a download would be, a folder that is not one
  // searched.
  void Dropped(const std::vector<std::string>& paths) {
    if (busy) return;
    std::string said;
    bool notOne = false;
    for (const std::string& path : paths) {
      FileLog("dropped: " + Shown(path));
      if (EndsWith(Lower(path), ".zip") && !ModZip(path).empty()) {     // a mod (docs/MODS.md)
        ModsInstallFrom(path);
        return;
      }
      if (PortZip(path)) {
        hhZip = path;
        said = Tf("Found {0} for PortMaster in {1}.", PROJECT_NAME, Shown(path));
        HandheldShow();
        continue;
      }
      if (ProgramZip(path) && TakeProgram(path)) {
        ownProgram = true;
        said = Tf("Found the Amiga program in {0}.", Shown(path));
        continue;
      }
      const std::string what = Offer(path);
      if (!what.empty()) {
        said = Tf("Found {0} in {1}.", T(what), Shown(path));
        continue;
      }
      if (sd::IsDir(path)) {
        StartScan(path);
        return;
      }
      said = Tf("{0} is not one of the game's files.", Shown(path));
      notOne = true;
    }
    Notice(said, notOne);
    Refresh();
  }

  void OpenAmigaDest() {
    const std::string dest = w().get_amiga_kind() == 0 ? drawerTo : sd::ParentDir(whdloadTo);
    if (!dest.empty()) sd::OpenUrl(dest);
  }

  // ---------------------------------------------------------- downloads ---

  // The Downloads folder, looked at every two seconds: a file that has stopped
  // growing and was not looked at before is tried as a source, so what the player
  // downloads from the links shows up as found. Browsers name a download in
  // progress something else (.crdownload, .part), which is never a source. GOG's
  // install folder is looked up the same way until the PC game is found.
  std::string downloads;
  std::map<std::string, std::pair<uint64_t, int64_t>> seen, growing;
  bool gogInstaller = false;   // GOG's installer for the PC game is in Downloads
  std::string watchDir;         // testing: watch this folder instead of Downloads
  bool lookForGog = true;       // testing: --no-gog
  slint::Timer watchTimer;

  static bool Candidate(const sd::DirEntry& e) {
    const std::string name = Lower(e.path.substr(e.path.find_last_of("/\\") + 1));
    if (e.dir) return name.find("sensible") != std::string::npos || name.find("swos") != std::string::npos;
    const size_t dot = name.rfind('.');
    if (dot == std::string::npos) return false;
    const std::string ext = name.substr(dot + 1);
    // An archive or a hard disk image only when it is named for the game: a Downloads
    // folder is full of other Amiga downloads, and a .zip is unpacked to be looked in.
    const bool named = name.find("sensible") != std::string::npos || name.find("swos") != std::string::npos;
    if (ext == "hdf" || ext == "lha" || ext == "zip") return named;
    if (ext == "adf") return true;   // its volume name says what it is
    // The cartridge is 2 MB: a CD image named .bin is not read through.
    return (ext == "jag" || ext == "j64" || ext == "rom" || ext == "bin") && e.size <= 8u * 1024 * 1024;
  }

  // GOG's offline installer: it has to be run before there is a folder to find.
  static bool GogInstaller(const std::string& path) {
    const std::string name = Lower(path.substr(path.find_last_of("/\\") + 1));
    return name.find("sensible_world_of_soccer") != std::string::npos &&
           (name.size() > 4 && (name.compare(name.size() - 4, 4, ".exe") == 0 ||
                                name.compare(name.size() - 4, 4, ".pkg") == 0 ||
                                name.compare(name.size() - 3, 3, ".sh") == 0));
  }

  // One more source found by the watcher: the PC game, the Jaguar and SWOS 2020 only
  // where none is set, the Amiga version when it is better than the one there.
  std::string Offer(const std::string& path) {
    const SdSources before = src;
    SdSources found{};
    if (SdSources_AddFile(&found, path.c_str()) != 0) return {};
    auto take = [](char* to, const char* from) { std::snprintf(to, 1024, "%s", from); };
    std::string what;
    if (found.dosDir[0] && !src.dosDir[0]) { take(src.dosDir, found.dosDir); unusable[ROW_PC].clear(); what = N_("the PC game"); }
    if (found.jagRom[0] && !src.jagRom[0]) { take(src.jagRom, found.jagRom); unusable[ROW_JAG].clear(); what = N_("the Jaguar cartridge"); }
    if (found.s2020Dir[0] && !src.s2020Dir[0]) { take(src.s2020Dir, found.s2020Dir); unusable[ROW_2020].clear(); what = N_("SWOS 2020"); }
    if (kWorldUrl[0] && found.world[0] && !src.world[0]) { take(src.world, found.world); unusable[ROW_WORLD].clear(); what = N_("World 2026-27"); }
    if (found.adf1[0] || found.adf2[0] || found.hdf[0] || found.lha[0]) {
      // The library's own rule picks the better of the two Amiga versions.
      SdSources_AddFile(&src, path.c_str());
      if (std::strcmp(before.adf1, src.adf1) || std::strcmp(before.adf2, src.adf2) ||
          std::strcmp(before.hdf, src.hdf) || std::strcmp(before.lha, src.lha)) {
        UpdateSeason();
        if (amigaSeason.empty() && SdSources_HaveAmiga(&src)) {
          // Damaged, or not the release: said, and the one there before kept.
          const std::string why = amigaProblem;
          std::memcpy(src.adf1, before.adf1, sizeof src.adf1);
          std::memcpy(src.adf2, before.adf2, sizeof src.adf2);
          std::memcpy(src.hdf, before.hdf, sizeof src.hdf);
          std::memcpy(src.lha, before.lha, sizeof src.lha);
          std::memcpy(src.hdfDir, before.hdfDir, sizeof src.hdfDir);
          std::memcpy(src.lhaDir, before.lhaDir, sizeof src.lhaDir);
          src.hdfVolume = before.hdfVolume;
          // A lone disk that this one made a pair with is of the same unusable release.
          if (!SdSources_HaveAmiga(&src) && (found.adf1[0] || found.adf2[0])) ClearAmiga();
          UpdateSeason();
          return Tf("a copy of the Amiga version that cannot be used ({0})", why);
        }
        unusable[ROW_AMIGA].clear();
        what = N_("the Amiga version");
      }
    }
    return what;
  }

  void Watch(bool first) {
    if (busy) return;
    std::string found;
    auto note = [&found](const std::string& what, const std::string& where) {
      if (!what.empty()) found = Tf("Found {0} in {1}.", T(what), Shown(where));
    };
    if (!src.dosDir[0] && lookForGog)
      for (const std::string& dir : sd::GogInstallDirs()) {
        const std::string key = "gog:" + dir;
        if (seen.count(key)) continue;
        seen[key] = {0, 0};
        note(Offer(dir), dir);
      }
    if (!downloads.empty()) {
      for (const sd::DirEntry& e : sd::ListDir(downloads)) {
        if (GogInstaller(e.path)) {
          if (!gogInstaller) {
            gogInstaller = true;
            RefreshRows();
          }
          continue;
        }
        if (ProgramZip(e.path) && amigaProgram.empty() && !e.dir) {
          const std::pair<uint64_t, int64_t> stamp{e.size, e.mtime};
          if (seen.count(e.path) && seen[e.path] == stamp) continue;
          seen[e.path] = stamp;
          if (TakeProgram(e.path)) found = Tf("Found the Amiga program in {0}.", Shown(e.path));
          continue;
        }
        if (!Candidate(e)) continue;
        const std::pair<uint64_t, int64_t> stamp{e.size, e.mtime};
        auto s = seen.find(e.path);
        if (s != seen.end() && s->second == stamp) continue;
        // Still being written: wait for it to be the same twice running (not at start-up,
        // where everything there has long stopped).
        auto g = growing.find(e.path);
        if (!first && (g == growing.end() || g->second != stamp)) {
          growing[e.path] = stamp;
          continue;
        }
        growing.erase(e.path);
        seen[e.path] = stamp;
        note(Offer(e.path), e.path);
      }
    }
    if (!found.empty()) {
      Notice(found, false);
      Refresh();
    }
  }

  std::string WatchingText() const {
    return downloads.empty()
               ? T("What you download is not looked for (no Downloads folder): search the folder you saved it in.")
               : Tf("Watching {0}: what you download from the links above is found by itself.", Shown(downloads));
  }

  void StartWatching() {
    downloads = !watchDir.empty() ? watchDir : sd::DownloadsDir();
    w().set_watching(Str(WatchingText()));
    Watch(true);
    watchTimer.start(slint::TimerMode::Repeated, std::chrono::seconds(2), [this] { Watch(false); });
  }

  // ------------------------------------------------------------ testing ---

  void Snapshot(const std::string& path) {
    auto shot = w().window().take_snapshot();
    if (!shot) {
      std::fprintf(stderr, "snapshot: the window could not be captured\n");
      return;
    }
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return;
    const uint32_t width = shot->width(), height = shot->height();
    std::fprintf(f, "P6\n%u %u\n255\n", width, height);
    const slint::Rgba8Pixel* px = shot->begin();
    std::vector<uint8_t> row(size_t(width) * 3);
    for (uint32_t y = 0; y < height; y++) {
      for (uint32_t x = 0; x < width; x++) {
        const slint::Rgba8Pixel& p = px[size_t(y) * width + x];
        row[x * 3 + 0] = p.r;
        row[x * 3 + 1] = p.g;
        row[x * 3 + 2] = p.b;
      }
      std::fwrite(row.data(), 1, row.size(), f);
    }
    std::fclose(f);
    std::printf("snapshot: %s (%ux%u)\n", path.c_str(), width, height);
    std::fflush(stdout);
  }
};

int Usage() {
  sd::AttachParentConsole();
  std::printf(
      PROJECT_SETUP_NAME " %s\n"
      "usage: " PROJECT_SETUP_PROGRAM " [--res DIR] [--scan DIR]... [--out DIR] [--amiga]\n"
      "                      [--program DIR] [--drawer-to DIR | --whdload-to FILE]\n"
      "  --res DIR         the game's own files (with setup/glyphs.txt); default: beside\n"
      "                    this program, else the nearest bin/ above it\n"
      "  --scan DIR        look for the game's sources in DIR and every folder inside\n"
      "                    it (repeatable)\n"
      "  --out DIR         the assets folder to write (default: the one the game reads)\n"
      "  --amiga           start with the Amiga ticked (with --auto: make it for the Amiga)\n"
      "  --program DIR     the Amiga program: an unpacked SWOS-Amiga.zip (default: the\n"
      "                    setup's own amiga/ folder)\n"
      "  --drawer-to DIR   make the SWOS drawer in DIR (for the Amiga)\n"
      "  --whdload-to FILE make the WHDLoad package FILE (for the Amiga)\n"
      "testing: --auto (install, or with --amiga make it)  --snapshot PREFIX  --quit  --play\n"
      "         --snapshot-at MS (when the -start snapshot is taken; 1200)\n"
      "         --no-watch (leave the Downloads folder alone)  --details  --about\n"
      "         --downloads DIR (watch DIR instead)  --no-gog (no GOG install lookup)\n"
      "  --mod-install=ZIP install that mod into the mods folder, turned on, and quit (docs/MODS.md)\n",
      SD_VERSION);
  std::fflush(stdout);
  return 2;
}

}  // namespace

int main(int argc, char** argv) {
#if !defined(__APPLE__)
  // Slint's default renderer needs OpenGL, and without it (a virtual machine, Remote
  // Desktop, a bare graphics driver) it stops the program before the window opens -- it
  // does not fall back to another. The software renderer draws this window the same and
  // needs nothing. SLINT_BACKEND set by the user still wins. (Every Mac has the GPU.)
  sd::SetEnvDefault("SLINT_BACKEND", "winit-software");
#endif
  App app;
  app.exeDir = sd::ExeDir();
  std::vector<std::string> scans;
  std::string resArg, outArg, programArg, drawerArg, whdloadArg;
  bool amigaTab = false, watch = true, details = false, about = false;
  std::string modInstall;      // --mod-install=ZIP: install that mod (docs/MODS.md) and quit
  std::string wizardArg;
  int wizardStep = 0;
  int snapshotAt = 1200;

  for (int i = 1; i < argc; i++) {
    const std::string a = argv[i];
    const char* v = i + 1 < argc ? argv[i + 1] : nullptr;
    if (a == "--auto") {
      app.autoRun = true;
    } else if (a == "--quit") {
      app.quitAfter = true;
    } else if (a == "--play") {
      app.autoPlay = true;
    } else if (a == "--amiga") {
      amigaTab = true;
    } else if (a == "--no-watch") {
      watch = false;
    } else if (a == "--details") {
      details = true;
    } else if (a == "--about") {
      about = true;
    } else if (a.rfind("--wizard=", 0) == 0) {
      wizardArg = a.substr(9);
    } else if (a.rfind("--wizard-step=", 0) == 0) {
      wizardStep = std::atoi(a.c_str() + 14);
    } else if (a.rfind("--language=", 0) == 0) {
      app.cliLanguage = a.substr(11);
    } else if (a.rfind("--handheld-copy=", 0) == 0) {
      app.hhAuto = a.substr(16);
    } else if (a.rfind("--mod-install=", 0) == 0) {
      modInstall = a.substr(14);
    } else if (a == "--no-gog") {
      app.lookForGog = false;
    } else if (a == "--help" || a == "-h") {
      Usage();
      return 0;
    } else if ((a == "--res" || a == "--scan" || a == "--out" || a == "--snapshot" || a == "--program" ||
                a == "--drawer-to" || a == "--whdload-to" || a == "--snapshot-at" ||
                a == "--downloads") && v) {
      if (a == "--res") resArg = v;
      else if (a == "--downloads") app.watchDir = v;
      else if (a == "--snapshot-at") snapshotAt = std::atoi(v);
      else if (a == "--program") programArg = v;
      else if (a == "--drawer-to") drawerArg = v;
      else if (a == "--whdload-to") whdloadArg = v;
      else if (a == "--scan") scans.emplace_back(v);
      else if (a == "--out") outArg = v;
      else app.snapshot = v;
      i++;
    } else {
      return Usage();
    }
  }

  app.resDir = !resArg.empty() ? resArg : FindResDir(app.exeDir);
  char def[1024];
  SwosData_DefaultAssetsDir(def, sizeof def);
  app.defaultOutDir = def;
  app.outDir = !outArg.empty() ? outArg : app.defaultOutDir;

  // The Amiga outputs go to the Downloads folder unless told otherwise: the player
  // copies them to the Amiga from there.
  const std::string dl = !app.watchDir.empty() ? app.watchDir : sd::DownloadsDir();
  const std::string base = !dl.empty() ? dl : app.exeDir;
  app.drawerTo = !drawerArg.empty() ? drawerArg : Join(base, "OpenSWOS-Amiga");
  app.whdloadTo = !whdloadArg.empty() ? whdloadArg : Join(base, "OpenSWOSAGA-WHDL.lha");

  // setup.log, beside the assets folder the game reads, before anything touches Slint:
  // if the window cannot open, what Slint says about it lands here (sd::OpenLog).
  app.logFile = sd::OpenLog(Join(sd::ParentDir(app.defaultOutDir), "setup.log"));
  {
    const std::time_t now = std::time(nullptr);
    char when[32];
    std::strftime(when, sizeof when, "%Y-%m-%d %H:%M:%S", std::localtime(&now));
    const char* backend = std::getenv("SLINT_BACKEND");
    app.FileLog(std::string(PROJECT_SETUP_NAME " ") + SD_VERSION + ", " + when);
    app.FileLog("renderer: " + std::string(backend && *backend ? backend : "Slint's default"));
    app.FileLog("program: " + Shown(app.exeDir));
    app.FileLog("game's files: " + (app.resDir.empty() ? std::string("(not found)") : Shown(app.resDir)));
    app.FileLog("install to: " + Shown(app.outDir));
  }

  // Wayland matches this against a .desktop file for the icon; X11 and
  // Windows/macOS use the Window's own icon.
  slint::set_xdg_app_id(PROJECT_SETUP_PROGRAM);

  auto ui = MainWindow::create();
  app.ui = &ui;
  app.pipe.flush = [&app] { app.Flush(); };
  ui->set_log(app.log);
  ui->set_tab(amigaTab ? 1 : 0);
  ui->set_version(SD_VERSION);
  ui->set_project_name(PROJECT_NAME);
  ui->set_app_id(PROJECT_APP_ID);
  ui->set_project_url(PROJECT_URL);
  ui->set_show_about(about);
  ui->set_show_details(details);
  if (amigaTab) {             // (where to play: the Amiga, not this computer)
    ui->set_t_computer(false);
    ui->set_t_amiga(true);
  }
  ui->set_world_available(kWorldUrl[0] != '\0');
  if (!wizardArg.empty()) {   // testing: --wizard=computer,phone,amiga,whdload,world,2020,jaguar,amigacopy,both
    auto has = [&wizardArg](const char* t) { return ("," + wizardArg + ",").find(std::string(",") + t + ",") != std::string::npos; };
    ui->set_t_computer(has("computer"));
    ui->set_t_phone(has("phone"));
    ui->set_t_amiga(has("amiga"));
    ui->set_t_whdload(has("whdload"));
    ui->set_t_handheld(has("handheld"));
    ui->set_x_world(has("world"));
    ui->set_x_2020(has("2020"));
    ui->set_x_jaguar(has("jaguar"));
    ui->set_x_mods(has("mods"));
    ui->set_have(has("both") ? 2 : has("amigacopy") ? 1 : 0);
  }
  if (!whdloadArg.empty()) ui->set_amiga_kind(1);
#ifndef SD_WITH_DIALOGS
  ui->set_can_pick(false);
#endif
  app.FindProgram(programArg);

  for (const auto& s : scans) SdSources_Scan(&app.src, s.c_str());
  // What the last install was made from (ASSETS.ini), where --scan found nothing.
  SwosData_LoadSources(app.outDir.c_str(), &app.src);
  app.StartLanguage();
  app.Refresh();
  if (!modInstall.empty()) {   // (no window needed: the mods folder and SWOS.ini only)
    app.ModsRefresh();
    const std::string why = app.ModInstall(modInstall);
    std::printf("%s\n", why.empty() ? ("installed " + modInstall).c_str() : ("not installed: " + why).c_str());
    return why.empty() ? 0 : 1;
  }
#if defined(__APPLE__)
  // Gatekeeper's App Translocation: an unsigned app opened where it was unzipped
  // runs from a random read-only copy, where SWOS.app beside it cannot be seen.
  // Moving the folder with Finder ends it.
  if (app.exeDir.find("/AppTranslocation/") != std::string::npos) {
    app.FileLog("running translocated: " + Shown(app.exeDir));
    app.standing = "macOS is running this app from a hidden copy, so Play cannot find SWOS.app "
                   "beside it. To fix it, move the " PROJECT_NAME " folder somewhere else with Finder "
                   "(your Desktop or Applications) and open the setup from there.";
    app.Notice("", true);
  } else
#endif
  if (app.resDir.empty())
    app.Notice(T("The game's own files (setup/glyphs.txt) were not found beside this program. "
                 "Start it with --res DIR, or install the game first."),
               true);
#ifndef SD_WITH_DIALOGS
  else
    app.Notice(T("This build has no folder picker (built without dbus); start it with --scan DIR."), true);
#endif
  // What an earlier run left: Play is there at once if the game can be played.
  app.CheckInstalled();
  app.RefreshPlan();
  if (watch) app.StartWatching();
  else ui->set_watching("");

  ui->on_choose_folder([&app] {
    const std::string dir = PickFolder({});
    if (!dir.empty()) app.StartScan(dir);
  });
  ui->on_stop_scan([&app] { app.stopScan = true; });
  ui->on_browse_source([&app](int r) { app.BrowseRow(r); });
  ui->on_clear_sources([&app] { app.ClearSources(); });
  ui->on_change_out([&app] {
    const std::string dir = PickFolder(sd::IsDir(app.outDir) ? app.outDir : std::string());
    if (dir.empty()) return;
    app.outDir = dir;
    SwosData_LoadSources(app.outDir.c_str(), &app.src);   // that folder's last install
    app.CheckInstalled();
    app.Refresh();
  });
  ui->on_install([&app] { app.Install(); });
  ui->on_play([&app] { app.Play(); });
  ui->on_open_url([](const slint::SharedString& url) { sd::OpenUrl(std::string(url)); });
  ui->on_change_amiga_dest([&app] { app.ChangeAmigaDest(); });
  ui->on_choose_amiga_program([&app] { app.ChooseProgram(); });
  ui->on_make_amiga([&app] { app.MakeAmiga(); });
  ui->on_amiga_kind_changed([&app] { app.AmigaKindChanged(); });
  ui->on_open_amiga_dest([&app] { app.OpenAmigaDest(); });
  ui->on_wizard_changed([&app] { app.RefreshWizard(); });
  ui->on_option_chosen([&app](int row, int choice) { app.OptionChosen(row, choice); });
  ui->on_language_chosen([&app](slint::SharedString code) { app.LanguageChosen(std::string(code), true); });
  ui->on_step_shown([&app](int kind) { app.StepShown(kind); });
  ui->on_open_out([&app] { sd::OpenUrl(sd::ParentDir(app.outDir)); });
  ui->on_hh_change([&app] { app.HandheldChange(); });
  ui->on_hh_copy([&app] { app.HandheldCopy(); });
  ui->on_hh_find([&app] { app.HandheldFind(); });
  ui->on_mods_toggle([&app](int i) { app.ModsToggle(i); });
  ui->on_mods_remove([&app](int i) { app.ModsRemove(i); });
  ui->on_mods_browse([&app] { app.ModsBrowse(); });
  ui->on_mods_install_found([&app](int i) { app.ModsInstallFound(i); });
  ui->on_close_setup([] { slint::quit_event_loop(); });
  app.RefreshWizard();
  if (wizardStep > 0) {
    ui->set_step(wizardStep);
    app.RefreshWizard();   // (clamped)
    app.StepShown(ui->get_steps()->row_data(ui->get_step()).value_or(0));
  }
  // Files dropped on the window (Windows): found as a download is.
  ui->set_can_drop(sd::EnableFileDrop(
      [&app](std::vector<std::string> paths) {
        slint::invoke_from_event_loop([&app, paths] { app.Dropped(paths); });
      },
      [&app](bool hover) {
        slint::invoke_from_event_loop([&app, hover] { app.w().set_drop_hover(hover); });
      }));

  // Windows only: the title-bar icon from the resource, once the window exists.
  // (And the window takes dropped files, Windows too: the same window, once it is there.)
  slint::Timer icon_timer;
  int icon_tries = 0;
  bool icon_done = false, drop_done = !ui->get_can_drop();
  icon_timer.start(slint::TimerMode::Repeated, std::chrono::milliseconds(100),
                   [&app, &icon_timer, &icon_tries, &icon_done, &drop_done] {
                     icon_done = icon_done || sd::ApplyNativeWindowIcon();
                     if (!drop_done && sd::AttachFileDrop()) {
                       drop_done = true;
                       app.FileLog("dropped files: taken on the window");
                     }
                     if ((icon_done && drop_done) || ++icon_tries >= 30) icon_timer.stop();
                   });

  if (!app.snapshot.empty())
    slint::Timer::single_shot(std::chrono::milliseconds(snapshotAt), [&app] {
      app.Snapshot(app.snapshot + "-start.ppm");
      if (app.quitAfter && !app.autoRun) slint::quit_event_loop();   // nothing else to wait for
    });
  if (app.autoRun)
    slint::Timer::single_shot(std::chrono::milliseconds(1500), [&app] {
      if (app.w().get_tab() == 1) app.MakeAmiga();
      else app.Install();
    });

  ui->run();

  // Closed mid-install: let the worker finish writing rather than cut a file
  // in half. Its posts to the stopped event loop are dropped.
  if (app.worker.joinable()) app.worker.join();
  sd::PhoneServerStop();
  return app.exitCode;
}
