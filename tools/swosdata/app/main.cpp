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
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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

extern "C" {
#include "swosdata.h"
}

#ifdef SD_WITH_DIALOGS
#include <nfd.h>
#endif

// Where World 2026-27's package is downloaded from (hosted apart from OpenSWOS: its squads are
// their authors'): "" until it is up, and the World 2026-27 row has no link.
static const char kWorldUrl[] = "";

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
      std::initializer_list<std::pair<const char*, const char*>> l) {
    auto m = std::make_shared<slint::VectorModel<LinkItem>>();
    for (const auto& [text, url] : l) m->push_back({Str(text), Str(url)});
    return m;
  }

  SourceRow MakeRow(int r) {
    SourceRow row{};   // value-initialised: the generated struct leaves its bools as they fall
    std::string path;
    switch (r) {
      case ROW_PC:
        row.label = "PC game";
        row.placeholder = "the game's folder (the one with SPRITE.DAT in it)";
        row.need_pc = "needed";
        row.need_amiga = "needed";
        row.about_pc = "Sensible World of Soccer 96/97 for PC. Buy it on GOG and install it: its folder is "
                       "found by itself. Or name any copy of the game's folder.";
        row.about_amiga = "The AGA version takes its pitches, backdrops and pictures from the PC game. Buy it "
                          "on GOG and install it: its folder is found by itself.";
        row.links = Links({{"GOG", "https://www.gog.com/en/game/sensible_world_of_soccer_9697"},
                           {"PC CD (archive.org)",
                            "https://archive.org/details/msdos_Sensible_World_of_Soccer_96-97_1996"}});
        path = src.dosDir;
        if (!src.dosDir[0] && gogInstaller) {
          row.detail = "GOG's installer is in your Downloads: run it, and the game it installs is found here by itself.";
          row.detail_warn = true;
        }
        break;
      case ROW_AMIGA: {
        row.label = "Amiga version";
        row.placeholder = "the floppies (.adf), a hard disk image (.hdf), a WHDLoad .lha, or a .zip of them";
        row.need_pc = "optional";
        row.need_amiga = "needed";
        row.about_pc = "Adds the Amiga's music, sound effects and loading screens -- or plays without the PC "
                       "game. Download one of the files on the right; ASWOS's swos.hdf works too.";
        row.about_amiga = "The game's own files. Download one of the files on the right (either will do), or "
                          "use both floppies, ASWOS's swos.hdf or a WHDLoad install.";
        row.links = Links({{"swos2425.hdf", "https://www.mediafire.com/file/4fu4wbhjiaco2ts/swos2425.hdf/file"},
                           {"WHDLoad .lha",
                            "https://www.mediafire.com/file/8l9nr2rjg0zteb5/SensibleWorldOfSoccer2425_v2.3.lha/file"},
                           {"about it", "https://www.sensiblesoccer.de/index.php?&view=newsdetails&id=206"}});
        path = AmigaPath();
        UpdateSeason();
        if (src.adf1[0] && !src.adf2[0]) {
          row.detail = "Disk 1 only: add disk 2 too.";
          row.detail_warn = true;
        } else if (src.adf2[0] && !src.adf1[0]) {
          row.detail = "Disk 2 only: add disk 1 too.";
          row.detail_warn = true;
        } else if (!amigaProblem.empty()) {
          row.detail = Str("This copy cannot be used: " + amigaProblem + ". Try another download.");
          row.detail_warn = true;
        } else if (amigaSeason == "96/97") {
          row.detail = "The 96/97 release.";
        } else if (amigaSeason == "an unknown season") {
          row.detail = "A season this setup does not know: it may install, or it may not.";
          row.detail_warn = true;
        } else if (!amigaSeason.empty()) {
          row.detail = Str("The " + amigaSeason + " season" + (src.adf1[0] ? " (both disks)" : "") +
                           ". OpenSWOS plays 96/97's teams: they come from the PC game when it is here.");
        } else if (src.adf1[0] && src.adf2[0]) {
          row.detail = "Both disks.";
        }
        break;
      }
      case ROW_JAG:
        row.label = "Atari Jaguar cartridge";
        row.placeholder = "Sensible Soccer International Edition (.jag, .j64, .rom or .bin)";
        row.need_pc = "optional";
        row.about_pc = "The Jaguar's redrawn players, an option in the game's graphics settings.";
        row.links = Links({{"Atarimania",
                            "https://www.atarimania.com/games/atari-jaguar-games-sensible-soccer-international-edition-30123"}});
        path = src.jagRom;
        break;
      case ROW_WORLD: {
        row.label = "World 2026-27";
        row.placeholder = "OpenSWOS-World-2627.zip";
        row.need_pc = "optional";
        row.need_amiga = "optional";
        row.about_pc = "The SWOS 96/97 Mod's World 2026-27: today's clubs, players and competitions. Its "
                       "package is a download of its own. The game plays it when SWOS.ini says world = 2627.";
        row.about_amiga = "The SWOS 96/97 Mod's World 2026-27: today's clubs, players and competitions. Its "
                          "package is a download of its own; tick it below to play it on the Amiga.";
        if (kWorldUrl[0]) row.links = Links({{"OpenSWOS-World-2627.zip", kWorldUrl}});
        path = src.world;
        SdWorldInfo wi;
        if (src.world[0] && SdSources_OpenWorld(src.world, &wi, nullptr, 0) == 0 && wi.fingerprint[0])
          row.detail = Str("Data " + std::string(wi.fingerprint) + ".");
        break;
      }
      default:
        row.label = "SWOS 2020";
        row.placeholder = "the SWOS 2020 install folder";
        row.need_pc = "optional";
        row.need_amiga = "optional";
        row.about_pc = "SWOS 2020's pitches, the set its launcher picked.";
        row.about_amiga = "SWOS 2020's pitches, the set its launcher picked.";
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
      auto pc = std::make_shared<slint::VectorModel<int>>(std::vector<int>{ROW_PC, ROW_AMIGA, ROW_JAG, ROW_2020, ROW_WORLD});
      auto am = std::make_shared<slint::VectorModel<int>>(std::vector<int>{ROW_AMIGA, ROW_PC, ROW_2020, ROW_WORLD});
      w().set_rows_pc(pc);
      w().set_rows_amiga(am);
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
      if (!src.adf1[0]) return "That is disk 2: add disk 1 too.";
      if (!src.adf2[0]) return "That is disk 1: add disk 2 too.";
      return "";
    }
    ClearAmiga();
    take(src.adf1, found.adf1);
    take(src.adf2, found.adf2);
    take(src.hdf, found.hdf);
    take(src.lha, found.lha);
    src.hdfVolume = found.hdfVolume;
    std::snprintf(src.hdfDir, sizeof src.hdfDir, "%s", found.hdfDir);
    std::snprintf(src.lhaDir, sizeof src.lhaDir, "%s", found.lhaDir);
    if (one) return found.adf1[0] ? "That is disk 1: add disk 2 too." : "That is disk 2: add disk 1 too.";
    return "";
  }

  // Put `text` in row r. Returns what to tell the player ("" for nothing).
  std::string SetRow(int r, const std::string& text) {
    const std::string path = CleanPath(text);
    unusable[r].clear();
    if (path.empty()) {
      if (r == ROW_AMIGA) ClearAmiga();
      else RowPath(r)[0] = '\0';
      return "";
    }
    SdSources found{};
    SdSources_AddFile(&found, path.c_str());
    auto take = [](char* to, const char* from) { std::snprintf(to, 1024, "%s", from); };
    switch (r) {
      case ROW_PC:
        if (found.dosDir[0]) { take(src.dosDir, found.dosDir); return ""; }
        src.dosDir[0] = '\0';
        unusable[r] = path;
        return found.s2020Dir[0] ? "That is SWOS 2020, not the PC game: it goes in the SWOS 2020 row."
                                 : "Not the PC game's folder: it is the one with SPRITE.DAT and DATA in it.";
      case ROW_AMIGA:
        if (found.adf1[0] || found.adf2[0] || found.hdf[0] || found.lha[0]) return TakeAmiga(found);
        unusable[r] = path;
        return "Not the Amiga version of Sensible World of Soccer: its floppies (.adf), a hard disk image "
               "(.hdf) or a WHDLoad .lha with the game on it.";
      case ROW_JAG:
        if (found.jagRom[0]) { take(src.jagRom, found.jagRom); return ""; }
        src.jagRom[0] = '\0';
        unusable[r] = path;
        return "Not the Sensible Soccer Jaguar cartridge (a 2 MB .jag, .j64, .rom or .bin).";
      case ROW_WORLD:
        if (found.world[0]) { take(src.world, found.world); return ""; }
        src.world[0] = '\0';
        unusable[r] = path;
        return "Not a world's package: OpenSWOS-World-2627.zip, or the folder it was unpacked to.";
      default:
        if (found.s2020Dir[0]) { take(src.s2020Dir, found.s2020Dir); return ""; }
        src.s2020Dir[0] = '\0';
        unusable[r] = path;
        return "Not a SWOS 2020 install: it is the folder with gameLauncher.exe and the swos folder in it.";
    }
  }

  // While typing: only the mark and the plan follow, nothing is said yet. (The Amiga
  // row waits for Enter: a half-typed path is not worth opening an image for.)
  void RowEdited(int r, const std::string& text) {
    if (r == ROW_AMIGA) return;
    SetRow(r, text);
    SourceRow row = MakeRow(r);
    row.value = Str(text);
    rows->set_row_data(r, row);
    RefreshPlan();
  }

  // Enter, or the box loses focus: the cleaned path, and what is wrong with it.
  void RowCommitted(int r, const std::string& text) {
    if (r == ROW_AMIGA && CleanPath(text) == Shown(AmigaPath())) return;   // unchanged
    const std::string msg = SetRow(r, text);
    Notice(msg, !msg.empty() && msg.rfind("That is disk", 0) != 0);
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
      w().set_status(Str("Some of the game's data in " + Shown(outDir) +
                         " was made by an older OpenSWOS Setup. " +
                         (sources ? "Click Update to make it again from your copy."
                                  : "Choose the folder your copy is in, then click Update.")));
    }
    else if (playable)
      w().set_status(Str("The game's data is already installed in " + Shown(outDir) +
                         (game.empty() ? ". Install again from your copy to add to it."
                                       : ". Play, or install again from your copy to add to it.")));
    else if (any)
      w().set_status(Str("Some of the game's data is in " + Shown(outDir) +
                         ", but not enough to play: install from your copy."));
    else
      w().set_status(Str("Ready."));
  }

  // Everything below the sources: where it goes and what will be made.
  void RefreshPlan() {
    w().set_out_dir(Str(Shown(outDir)));
    w().set_out_note(Shown(outDir) == Shown(defaultOutDir)
                         ? Str("This is the folder the game reads its data from.")
                         : Str("The game reads " + Shown(defaultOutDir) +
                               " — to use this folder instead, start the game with SWOS_ASSETS set to it."));

    SdJob job{};
    job.src = Usable();
    SdStepInfo info[SD_STEP_COUNT];
    SwosData_Plan(&job, info);
    auto left = std::make_shared<slint::VectorModel<PlanStep>>();
    auto right = std::make_shared<slint::VectorModel<PlanStep>>();
    bool any = false;
    for (int i = 0; i < SD_STEP_COUNT; i++) {
      (i < (SD_STEP_COUNT + 1) / 2 ? left : right)
          ->push_back({Str(info[i].name), bool(info[i].ready), Str(info[i].needs), InstalledState(i)});
      any = any || info[i].ready;
    }
    w().set_plan_left(left);
    w().set_plan_right(right);
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
    const bool world = info[SD_STEP_WORLD].ready || SwosData_HaveAmigaWorld(outDir.c_str());
    w().set_amiga_world_available(world);
    if (!world) w().set_amiga_world(false);
    if (!busy && !w().get_amiga_done() && !w().get_amiga_error()) {
      std::string text;
      if (amigaProgram.empty())
        text = "Choose the Amiga program first: SWOS-Amiga.zip, from the same place as this setup.";
      else if (!sources && !installedAlready)
        text = !SdSources_HaveAmiga(&src) && !SdSources_HaveDos(&src)
                   ? "Get the Amiga version and the PC game above."
               : !SdSources_HaveAmiga(&src) ? "Get the Amiga version above too."
                                            : "Get the PC game above too.";
      else
        text = kindDrawer ? "Ready to make the drawer." : "Ready to make the package.";
      w().set_amiga_status(Str(text));
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
      const std::string text = "Searching... " + std::to_string(folders) + " folders: " + Shown(dir);
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
    w().set_scan_status(Str("Searching " + Shown(dir) + " ..."));
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
      text = "Found " + std::to_string(added) + " in " + Shown(dir) + ".";
    else {
      text = "Nothing new in " + Shown(dir) + " or the folders inside it.";
      error = true;
    }
    if (stopped) text += " (Stopped before the end.)";
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
    w().set_status(Str("Installing to " + Shown(outDir) + " ..."));
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
      w().set_progress(1.0f);
      w().set_done(true);
      game = FindGame(exeDir);
      w().set_can_play(!game.empty());
      w().set_status(Str("Done. The game's data is in " + Shown(outDir) + "."));
      FileLog("install done: " + Shown(outDir));
      exitCode = 0;
    } else {
      w().set_status_error(true);
      w().set_status(Str("Failed: " + err));
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
      w().set_status(Str("Could not start " + game + "."));
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
    w().set_amiga_status(Str("Installing the game's data to " + Shown(outDir) + " ..."));
    log->clear();

    auto job = std::make_shared<SdJob>();
    std::memset(job.get(), 0, sizeof *job);
    job->src = Usable();
    job->src.disk1 = job->src.disk2 = job->src.hdfDisk = nullptr;
    job->src.lhaArc = nullptr;
    std::snprintf(job->outDir, sizeof job->outDir, "%s", outDir.c_str());
    std::snprintf(job->resDir, sizeof job->resDir, "%s", resDir.c_str());
    job->log = PipeLog;
    job->progress = PipeProgress;
    job->user = &pipe;
    SdStepInfo info[SD_STEP_COUNT];
    SwosData_Plan(job.get(), info);
    bool any = false;
    for (int i = 0; i < SD_STEP_COUNT; i++) any = any || info[i].ready;

    const std::string program = amigaProgram, dest = kindDrawer ? drawerTo : whdloadTo,
                      assets = outDir, res = resDir;
    const int32_t withWorld = w().get_amiga_world() ? 1 : 0;
    worker = std::thread([this, job, any, kindDrawer, program, dest, assets, res, withWorld] {
      char err[256] = {0};
      int32_t rc = any ? SwosData_Run(job.get(), err) : 0;
      SdSources_Close(&job->src);
      if (rc == 0) {
        PipeLog(&pipe, kindDrawer ? "Making the drawer..." : "Making the WHDLoad package...");
        rc = kindDrawer
                 ? SwosData_MakeAmigaDrawer(assets.c_str(), program.c_str(), dest.c_str(), withWorld, PipeLog,
                                            &pipe, err)
                 : SwosData_MakeWhdload(assets.c_str(), Join(program, "SWOS").c_str(), res.c_str(), dest.c_str(),
                                        withWorld, PipeLog, &pipe, err);
      }
      const std::string msg(err);
      slint::invoke_from_event_loop([this, rc, msg, kindDrawer, dest] {
        Flush();
        amigaRun = false;
        SetBusy(false);
        SwosData_Installed(outDir.c_str(), installed);
        w().set_amiga_done(rc == 0);
        w().set_amiga_error(rc != 0);
        w().set_amiga_progress(rc == 0 ? 1.0f : 0.0f);
        if (rc == 0)
          w().set_amiga_status(Str(kindDrawer
              ? "Done: the SWOS drawer is in " + Shown(dest) + ". Copy it, with SWOS.info, to your Amiga and double-click SWOS."
              : "Done: " + Shown(dest) + ". Unpack it on your Amiga (lha x OpenSWOSAGA-WHDL.lha), open the OpenSWOSAGA-NE drawer and double-click OpenSWOS."));
        else {
          w().set_amiga_status(Str("Failed: " + msg));
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
    if (found.dosDir[0] && !src.dosDir[0]) { take(src.dosDir, found.dosDir); unusable[ROW_PC].clear(); what = "the PC game"; }
    if (found.jagRom[0] && !src.jagRom[0]) { take(src.jagRom, found.jagRom); unusable[ROW_JAG].clear(); what = "the Jaguar cartridge"; }
    if (found.s2020Dir[0] && !src.s2020Dir[0]) { take(src.s2020Dir, found.s2020Dir); unusable[ROW_2020].clear(); what = "SWOS 2020"; }
    if (found.world[0] && !src.world[0]) { take(src.world, found.world); unusable[ROW_WORLD].clear(); what = "World 2026-27"; }
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
          return "a copy of the Amiga version that cannot be used (" + why + ")";
        }
        unusable[ROW_AMIGA].clear();
        what = "the Amiga version";
      }
    }
    return what;
  }

  void Watch(bool first) {
    if (busy) return;
    std::string found;
    auto note = [&found](const std::string& what, const std::string& where) {
      if (!what.empty()) found = "Found " + what + " in " + Shown(where) + ".";
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
          if (TakeProgram(e.path)) found = "Found the Amiga program in " + Shown(e.path) + ".";
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

  void StartWatching() {
    downloads = !watchDir.empty() ? watchDir : sd::DownloadsDir();
    w().set_watching(Str(downloads.empty()
        ? "What you download is not looked for (no Downloads folder): search the folder you saved it in."
        : "Watching " + Shown(downloads) + ": what you download from the links above is found by itself."));
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
      "OpenSWOS Setup %s\n"
      "usage: openswos-setup [--res DIR] [--scan DIR]... [--out DIR] [--amiga]\n"
      "                      [--program DIR] [--drawer-to DIR | --whdload-to FILE]\n"
      "  --res DIR         the game's own files (with setup/glyphs.txt); default: beside\n"
      "                    this program, else the nearest bin/ above it\n"
      "  --scan DIR        look for the game's sources in DIR and every folder inside\n"
      "                    it (repeatable)\n"
      "  --out DIR         the assets folder to write (default: the one the game reads)\n"
      "  --amiga           open on the Amiga tab\n"
      "  --program DIR     the Amiga program: an unpacked SWOS-Amiga.zip (default: the\n"
      "                    setup's own amiga/ folder)\n"
      "  --drawer-to DIR   make the SWOS drawer in DIR (the Amiga tab)\n"
      "  --whdload-to FILE make the WHDLoad package FILE (the Amiga tab)\n"
      "testing: --auto (press the tab's button)  --snapshot PREFIX  --quit  --play\n"
      "         --snapshot-at MS (when the -start snapshot is taken; 1200)\n"
      "         --no-watch (leave the Downloads folder alone)  --details  --about\n"
      "         --downloads DIR (watch DIR instead)  --no-gog (no GOG install lookup)\n",
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
    app.FileLog(std::string("OpenSWOS Setup ") + SD_VERSION + ", " + when);
    app.FileLog("renderer: " + std::string(backend && *backend ? backend : "Slint's default"));
    app.FileLog("program: " + Shown(app.exeDir));
    app.FileLog("game's files: " + (app.resDir.empty() ? std::string("(not found)") : Shown(app.resDir)));
    app.FileLog("install to: " + Shown(app.outDir));
  }

  // Wayland matches this against a .desktop file for the icon; X11 and
  // Windows/macOS use the Window's own icon.
  slint::set_xdg_app_id("openswos-setup");

  auto ui = MainWindow::create();
  app.ui = &ui;
  app.pipe.flush = [&app] { app.Flush(); };
  ui->set_log(app.log);
  ui->set_tab(amigaTab ? 1 : 0);
  ui->set_version(SD_VERSION);
  ui->set_show_about(about);
  ui->set_show_details(details);
  if (!whdloadArg.empty()) ui->set_amiga_kind(1);
#ifndef SD_WITH_DIALOGS
  ui->set_can_pick(false);
#endif
  app.FindProgram(programArg);

  for (const auto& s : scans) SdSources_Scan(&app.src, s.c_str());
  // What the last install was made from (ASSETS.ini), where --scan found nothing.
  SwosData_LoadSources(app.outDir.c_str(), &app.src);
  app.Refresh();
#if defined(__APPLE__)
  // Gatekeeper's App Translocation: an unsigned app opened where it was unzipped
  // runs from a random read-only copy, where SWOS.app beside it cannot be seen.
  // Moving the folder with Finder ends it.
  if (app.exeDir.find("/AppTranslocation/") != std::string::npos) {
    app.FileLog("running translocated: " + Shown(app.exeDir));
    app.standing = "macOS is running this app from a hidden copy, so Play cannot find SWOS.app "
                   "beside it. To fix it, move the OpenSWOS folder somewhere else with Finder "
                   "(your Desktop or Applications) and open the setup from there.";
    app.Notice("", true);
  } else
#endif
  if (app.resDir.empty())
    app.Notice("The game's own files (setup/glyphs.txt) were not found beside this program. "
               "Start it with --res DIR, or install the game first.",
               true);
#ifndef SD_WITH_DIALOGS
  else
    app.Notice("This build has no folder picker (built without dbus); start it with --scan DIR.", true);
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
  ui->on_source_edited([&app](int r, const slint::SharedString& t) { app.RowEdited(r, std::string(t)); });
  ui->on_source_committed([&app](int r, const slint::SharedString& t) { app.RowCommitted(r, std::string(t)); });
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

  // Windows only: the title-bar icon from the resource, once the window exists.
  slint::Timer icon_timer;
  int icon_tries = 0;
  icon_timer.start(slint::TimerMode::Repeated, std::chrono::milliseconds(100),
                   [&icon_timer, &icon_tries] {
                     if (sd::ApplyNativeWindowIcon() || ++icon_tries >= 30) icon_timer.stop();
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
  return app.exitCode;
}
