#include "platform.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <functional>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <ole2.h>
#include <io.h>
#else
#include <cerrno>
#include <climits>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
#endif

namespace sd {

#ifdef _WIN32
static std::wstring Widen(const std::string& s) {
  if (s.empty()) return {};
  const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
  std::wstring w(size_t(n), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), w.data(), n);
  return w;
}

static std::string Narrow(const std::wstring& w) {
  if (w.empty()) return {};
  const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), nullptr, 0, nullptr, nullptr);
  std::string s(size_t(n), '\0');
  WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), s.data(), n, nullptr, nullptr);
  return s;
}
#endif

std::string ParentDir(const std::string& path) {
  size_t end = path.size();
  while (end > 0 && (path[end - 1] == '/' || path[end - 1] == '\\')) --end;
  if (end == 0) return {};
  const size_t cut = path.find_last_of("/\\", end - 1);
  if (cut == std::string::npos) return {};
  if (cut == 0) return path.substr(0, 1);                    // "/usr" -> "/"
  if (cut == 2 && path[1] == ':') return path.substr(0, 3);  // "C:\x" -> "C:\"
  return path.substr(0, cut);
}

std::string ExeDir() {
#ifdef _WIN32
  std::wstring buf(32768, L'\0');
  const DWORD n = GetModuleFileNameW(nullptr, buf.data(), DWORD(buf.size()));
  if (n == 0 || n >= buf.size()) return {};
  buf.resize(n);
  return ParentDir(Narrow(buf));
#elif defined(__APPLE__)
  uint32_t size = 0;
  _NSGetExecutablePath(nullptr, &size);
  std::vector<char> raw(size + 1, '\0');
  if (_NSGetExecutablePath(raw.data(), &size) != 0) return {};
  char real[PATH_MAX];
  if (!realpath(raw.data(), real)) return ParentDir(raw.data());
  return ParentDir(real);
#else
  char buf[PATH_MAX];
  const ssize_t n = readlink("/proc/self/exe", buf, sizeof buf - 1);
  if (n <= 0) return {};
  buf[n] = '\0';
  return ParentDir(buf);
#endif
}

bool IsFile(const std::string& path) {
#ifdef _WIN32
  const DWORD a = GetFileAttributesW(Widen(path).c_str());
  return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
#else
  struct stat st;
  return stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
#endif
}

bool IsDir(const std::string& path) {
#ifdef _WIN32
  const DWORD a = GetFileAttributesW(Widen(path).c_str());
  return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
#else
  struct stat st;
  return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
#endif
}

#ifndef _WIN32
// fork twice so the child is not ours to reap, exec without a shell (paths
// have spaces and quotes in them).
static bool RunDetached(const std::vector<const char*>& argv, const char* cwd) {
  const pid_t pid = fork();
  if (pid < 0) return false;
  if (pid == 0) {
    if (fork() == 0) {
      const int devnull = ::open("/dev/null", O_RDWR);
      if (devnull >= 0) {
        dup2(devnull, 0);
        dup2(devnull, 1);
        dup2(devnull, 2);
        if (devnull > 2) close(devnull);
      }
      setsid();
      if (cwd && *cwd && chdir(cwd) != 0) _exit(127);
      execvp(argv[0], const_cast<char* const*>(argv.data()));
      _exit(127);
    }
    _exit(0);
  }
  int status = 0;
  while (waitpid(pid, &status, 0) < 0) {
    if (errno != EINTR) break;
  }
  return true;
}
#endif

bool OpenUrl(const std::string& url) {
#ifdef _WIN32
  return reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", Widen(url).c_str(), nullptr,
                                                 nullptr, SW_SHOWNORMAL)) > 32;
#elif defined(__APPLE__)
  return RunDetached({"open", url.c_str(), nullptr}, nullptr);
#else
  return RunDetached({"xdg-open", url.c_str(), nullptr}, nullptr);
#endif
}

bool Launch(const std::string& exe, const std::string& cwd) {
#ifdef _WIN32
  SHELLEXECUTEINFOW info{};
  info.cbSize = sizeof info;
  info.fMask = SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
  info.lpVerb = L"open";
  const std::wstring file = Widen(exe), dir = Widen(cwd);
  info.lpFile = file.c_str();
  info.lpDirectory = dir.empty() ? nullptr : dir.c_str();
  info.nShow = SW_SHOWNORMAL;
  return ShellExecuteExW(&info) != FALSE;
#else
  if (access(exe.c_str(), X_OK) != 0) return false;
  return RunDetached({exe.c_str(), nullptr}, cwd.c_str());
#endif
}

#ifdef _WIN32
// Slint exposes no HWND. The largest visible unowned top-level window on this
// thread is ours (winit also keeps a tiny helper window, hence the floor).
static HWND MainWindow() {
  struct Candidate {
    HWND window = nullptr;
    long long area = 0;
  };
  Candidate best;
  EnumThreadWindows(
      GetCurrentThreadId(),
      [](HWND window, LPARAM out) -> BOOL {
        if (GetWindow(window, GW_OWNER) != nullptr || !IsWindowVisible(window)) return TRUE;
        RECT r{};
        if (!GetWindowRect(window, &r)) return TRUE;
        const long long w = r.right - r.left, h = r.bottom - r.top;
        if (w < 100 || h < 100) return TRUE;
        Candidate* c = reinterpret_cast<Candidate*>(out);
        if (w * h > c->area) {
          c->area = w * h;
          c->window = window;
        }
        return TRUE;
      },
      reinterpret_cast<LPARAM>(&best));
  return best.window;
}
#endif

bool ApplyNativeWindowIcon() {
#ifdef _WIN32
  struct { HWND window; } best{MainWindow()};
  if (!best.window) return false;
  const HINSTANCE self = GetModuleHandleW(nullptr);
  const auto icon_of = [self](int metric) {
    const int size = GetSystemMetrics(metric);
    return static_cast<HICON>(
        LoadImageW(self, MAKEINTRESOURCEW(1), IMAGE_ICON, size, size, LR_DEFAULTCOLOR));
  };
  if (const HICON s = icon_of(SM_CXSMICON))
    SendMessageW(best.window, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(s));
  if (const HICON b = icon_of(SM_CXICON))
    SendMessageW(best.window, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(b));
  return true;
#else
  return true;
#endif
}

// ------------------------------------------------------------ dropped files ---
static std::function<void(std::vector<std::string>)> s_onDrop;
static std::function<void(bool)> s_onHover;

#ifdef _WIN32
// An OLE drop target for files from Explorer (CF_HDROP). winit registers its own, which
// Slint never hears from, so ours replaces it.
class FileDropTarget : public IDropTarget {
  LONG refs_ = 1;
  bool files_ = false;
 public:
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override {
    if (riid == IID_IUnknown || riid == IID_IDropTarget) {
      *out = static_cast<IDropTarget*>(this);
      AddRef();
      return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
  }
  ULONG STDMETHODCALLTYPE AddRef() override { return ULONG(InterlockedIncrement(&refs_)); }
  ULONG STDMETHODCALLTYPE Release() override {
    const LONG n = InterlockedDecrement(&refs_);
    if (n == 0) delete this;
    return ULONG(n);
  }
  HRESULT STDMETHODCALLTYPE DragEnter(IDataObject* data, DWORD, POINTL, DWORD* effect) override {
    FORMATETC f{CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
    files_ = data && data->QueryGetData(&f) == S_OK;
    *effect = files_ ? DROPEFFECT_COPY : DROPEFFECT_NONE;
    if (files_ && s_onHover) s_onHover(true);
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE DragOver(DWORD, POINTL, DWORD* effect) override {
    *effect = files_ ? DROPEFFECT_COPY : DROPEFFECT_NONE;
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE DragLeave() override {
    if (files_ && s_onHover) s_onHover(false);
    files_ = false;
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE Drop(IDataObject* data, DWORD, POINTL, DWORD* effect) override {
    std::vector<std::string> paths;
    FORMATETC f{CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
    STGMEDIUM m{};
    if (data && data->GetData(&f, &m) == S_OK) {
      HDROP drop = static_cast<HDROP>(GlobalLock(m.hGlobal));
      if (drop) {
        const UINT n = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
        for (UINT i = 0; i < n; i++) {
          const UINT len = DragQueryFileW(drop, i, nullptr, 0);
          std::wstring w(len + 1, L'\0');
          DragQueryFileW(drop, i, w.data(), len + 1);
          w.resize(len);
          paths.push_back(Narrow(w));
        }
        GlobalUnlock(m.hGlobal);
      }
      ReleaseStgMedium(&m);
    }
    *effect = paths.empty() ? DROPEFFECT_NONE : DROPEFFECT_COPY;
    if (s_onHover) s_onHover(false);
    files_ = false;
    if (!paths.empty() && s_onDrop) s_onDrop(paths);
    return S_OK;
  }
};
#endif

bool EnableFileDrop(std::function<void(std::vector<std::string>)> onDrop, std::function<void(bool)> onHover) {
#ifdef _WIN32
  s_onDrop = std::move(onDrop);
  s_onHover = std::move(onHover);
  return true;
#else
  (void)onDrop;
  (void)onHover;
  return false;
#endif
}

bool AttachFileDrop() {
#ifdef _WIN32
  const HWND window = MainWindow();
  if (!window) return false;
  RevokeDragDrop(window);   // winit's
  FileDropTarget* target = new FileDropTarget;
  HRESULT hr = RegisterDragDrop(window, target);
  if (hr == CO_E_NOTINITIALIZED || hr == E_OUTOFMEMORY) {
    OleInitialize(nullptr);
    hr = RegisterDragDrop(window, target);
  }
  target->Release();        // OLE holds its own reference
  return SUCCEEDED(hr);
#else
  return true;
#endif
}

void AttachParentConsole() {
#ifdef _WIN32
  if (AttachConsole(ATTACH_PARENT_PROCESS)) {
    FILE* f = nullptr;
    freopen_s(&f, "CONOUT$", "w", stdout);
    freopen_s(&f, "CONOUT$", "w", stderr);
  }
#endif
}

namespace fs = std::filesystem;

static fs::path U8Path(const std::string& s) {
  return fs::path(std::u8string(s.begin(), s.end()));
}

static std::string PathU8(const fs::path& p) {
  const std::u8string u = p.u8string();
  return std::string(u.begin(), u.end());
}

FILE* OpenFile(const std::string& path, const char* mode) {
#ifdef _WIN32
  return _wfopen(Widen(path).c_str(), Widen(mode).c_str());
#else
  return std::fopen(path.c_str(), mode);
#endif
}

bool MakeDirs(const std::string& path) {
  std::error_code ec;
  fs::create_directories(U8Path(path), ec);
  return !ec;
}

bool RemoveTree(const std::string& path) {
  std::error_code ec;
  fs::remove_all(U8Path(path), ec);
  return !ec;
}

bool RenamePath(const std::string& from, const std::string& to) {
  std::error_code ec;
  fs::rename(U8Path(from), U8Path(to), ec);
  return !ec;
}

uint64_t TreeSize(const std::string& dir, const std::string& skipTop) {
  std::error_code ec;
  uint64_t n = 0;
  const fs::path root = U8Path(dir);
  for (fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end;
       it.increment(ec)) {
    if (!skipTop.empty() && it.depth() == 0 && it->is_directory() && PathU8(it->path().filename()) == skipTop) {
      it.disable_recursion_pending();
      continue;
    }
    if (it->is_regular_file(ec)) n += it->file_size(ec);
  }
  return n;
}

bool CopyTree(const std::string& from, const std::string& to, const std::string& skipTop,
              const std::function<void(uint64_t)>& copied, std::string* error) {
  std::error_code ec;
  const fs::path root = U8Path(from), dest = U8Path(to);
  fs::create_directories(dest, ec);
  std::vector<char> buf(1 << 20);
  for (fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end;
       it.increment(ec)) {
    if (!skipTop.empty() && it.depth() == 0 && it->is_directory() && PathU8(it->path().filename()) == skipTop) {
      it.disable_recursion_pending();
      continue;
    }
    const fs::path out = dest / fs::relative(it->path(), root, ec);
    if (it->is_directory()) {
      fs::create_directories(out, ec);
      continue;
    }
    FILE* in = OpenFile(PathU8(it->path()), "rb");
    FILE* o = OpenFile(PathU8(out), "wb");
    bool ok = in && o;
    size_t n;
    while (ok && (n = fread(buf.data(), 1, buf.size(), in)) > 0) {
      ok = fwrite(buf.data(), 1, n, o) == n;
      if (ok && copied) copied(n);
    }
    if (in) fclose(in);
    if (o && fclose(o) != 0) ok = false;
    if (!ok) {
      if (error) *error = "could not copy " + PathU8(it->path()) + " to " + PathU8(out);
      return false;
    }
  }
  if (ec) {
    if (error) *error = ec.message();
    return false;
  }
  return true;
}

std::vector<std::string> VolumeRoots() {
  std::vector<std::string> out;
#ifdef _WIN32
  const DWORD mask = GetLogicalDrives();
  for (int i = 2; i < 26; i++)       // C: on: A: and B: are floppies
    if (mask & (1u << i)) out.push_back(std::string(1, char('A' + i)) + ":/");
#else
  std::vector<std::string> parents;
#ifdef __APPLE__
  parents.push_back("/Volumes");
#else
  const char* user = std::getenv("USER");
  if (user) {
    parents.push_back(std::string("/media/") + user);
    parents.push_back(std::string("/run/media/") + user);
  }
  parents.push_back("/media");
  parents.push_back("/mnt");
#endif
  for (const std::string& p : parents)
    for (const DirEntry& e : ListDir(p))
      if (e.dir) out.push_back(e.path);
#endif
  return out;
}

std::string DownloadsDir() {
#ifdef _WIN32
  PWSTR w = nullptr;
  std::string known;
  if (SHGetKnownFolderPath(FOLDERID_Downloads, 0, nullptr, &w) == S_OK && w) known = Narrow(w);
  if (w) CoTaskMemFree(w);
  if (!known.empty()) return known;
  const char* home = std::getenv("USERPROFILE");
#else
  const char* home = std::getenv("HOME");
#if !defined(__APPLE__)
  // XDG's own name for it, which a translated desktop changes ("Téléchargements").
  if (home) {
    if (FILE* f = std::fopen((std::string(home) + "/.config/user-dirs.dirs").c_str(), "r")) {
      char line[1024];
      std::string dir;
      while (std::fgets(line, sizeof line, f)) {
        if (std::strncmp(line, "XDG_DOWNLOAD_DIR=\"", 18) != 0) continue;
        std::string v(line + 18);
        const size_t q = v.find('"');
        if (q != std::string::npos) v.resize(q);
        if (v.rfind("$HOME", 0) == 0) v = std::string(home) + v.substr(5);
        dir = v;
      }
      std::fclose(f);
      if (!dir.empty() && IsDir(dir)) return dir;
    }
  }
#endif
#endif
  if (!home) return {};
  const std::string dir = std::string(home) + "/Downloads";
  return IsDir(dir) ? dir : std::string();
}

std::vector<DirEntry> ListDir(const std::string& dir) {
  std::vector<DirEntry> out;
  std::error_code ec;
  fs::directory_iterator it(U8Path(dir), fs::directory_options::skip_permission_denied, ec);
  if (ec) return out;
  for (; it != fs::directory_iterator(); it.increment(ec)) {
    if (ec) break;
    const fs::path& p = it->path();
    const std::string name = PathU8(p.filename());
    if (name.empty() || name[0] == '.') continue;
    std::error_code e2;
    DirEntry d;
    d.path = PathU8(p);
    d.dir = it->is_directory(e2);
    d.size = d.dir ? 0 : uint64_t(it->file_size(e2));
    const auto t = it->last_write_time(e2);
    d.mtime = e2 ? 0 : int64_t(t.time_since_epoch().count());
    out.push_back(d);
  }
  return out;
}

std::vector<std::string> GogInstallDirs() {
  std::vector<std::string> out;
#ifdef _WIN32
  // GOG Galaxy and GOG's offline installers both register every game here.
  HKEY games;
  if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\GOG.com\\Games", 0, KEY_READ,
                    &games) == ERROR_SUCCESS) {
    wchar_t sub[256];
    for (DWORD i = 0;; i++) {
      DWORD n = 256;
      if (RegEnumKeyExW(games, i, sub, &n, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS) break;
      wchar_t name[512] = {0}, path[1024] = {0};
      DWORD nb = sizeof name, pb = sizeof path;
      if (RegGetValueW(games, sub, L"gameName", RRF_RT_REG_SZ, nullptr, name, &nb) != ERROR_SUCCESS ||
          RegGetValueW(games, sub, L"path", RRF_RT_REG_SZ, nullptr, path, &pb) != ERROR_SUCCESS)
        continue;
      if (std::wstring(name).find(L"Sensible") != std::wstring::npos) out.push_back(Narrow(path));
    }
    RegCloseKey(games);
  }
  const char* roots[] = {"C:\\GOG Games", "C:\\Program Files (x86)\\GOG Galaxy\\Games",
                         "C:\\Program Files (x86)\\GOG.com", "C:\\Program Files\\GOG Galaxy\\Games"};
#else
  const char* home = std::getenv("HOME");
  const std::string gog = home ? std::string(home) + "/GOG Games" : std::string();
  const char* roots[] = {gog.c_str()};
#endif
  for (const char* root : roots) {
    if (!root[0]) continue;
    for (const DirEntry& d : ListDir(root))
      if (d.dir && d.path.find("Sensible") != std::string::npos) out.push_back(d.path);
  }
  return out;
}

void SetEnvDefault(const char* name, const char* value) {
#ifdef _WIN32
  // Both the C runtime's copy and the process's own block, which Rust code reads.
  if (GetEnvironmentVariableA(name, nullptr, 0) != 0) return;
  _putenv_s(name, value);
  SetEnvironmentVariableA(name, value);
#else
  setenv(name, value, 0);
#endif
}

FILE* OpenLog(const std::string& path) {
  std::error_code ec;
  fs::create_directories(U8Path(ParentDir(path)), ec);
#ifdef _WIN32
  FILE* f = _wfopen(Widen(path).c_str(), L"w");
  if (!f) return nullptr;
  const HANDLE err = GetStdHandle(STD_ERROR_HANDLE);
  if (err == nullptr || err == INVALID_HANDLE_VALUE)
    SetStdHandle(STD_ERROR_HANDLE, reinterpret_cast<HANDLE>(_get_osfhandle(_fileno(f))));
  return f;
#else
  return std::fopen(path.c_str(), "w");
#endif
}

}  // namespace sd
