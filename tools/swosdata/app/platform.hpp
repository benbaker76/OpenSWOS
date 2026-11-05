// The few things OpenSWOS Setup needs from the operating system, kept out of
// main.cpp so windows.h never meets the generated Slint header.
//
// Every string is UTF-8.
#pragma once

#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace sd {

// The folder the executable is in ("" if it cannot be found).
std::string ExeDir();

// The parent of a path, or "" at the top.
std::string ParentDir(const std::string& path);

bool IsFile(const std::string& path);
bool IsDir(const std::string& path);

// Open a URL in the browser.
bool OpenUrl(const std::string& url);

// Start a program, detached, in `cwd`. False when it could not be started.
bool Launch(const std::string& exe, const std::string& cwd);

// Windows: set the title-bar / taskbar icon from the .exe's icon resource at
// the sizes Windows asks for (Slint hands Windows one bitmap for both). False
// while the window does not exist yet; true elsewhere.
bool ApplyNativeWindowIcon();

// Files dropped on the setup's window: onDrop gets their paths, onHover whether a drag is over
// the window. Windows only (an OLE drop target on the window; Slint's winit backend passes no
// dropped file on): elsewhere false, and nothing happens. AttachFileDrop registers it on the
// window once the window exists -- false until then, call it again.
bool EnableFileDrop(std::function<void(std::vector<std::string>)> onDrop, std::function<void(bool)> onHover);
bool AttachFileDrop();

// Windows: a GUI-subsystem program has no console; attach the parent's so
// --help prints somewhere. No-op elsewhere.
void AttachParentConsole();

// The player's Downloads folder ("" when there is none).
std::string DownloadsDir();

// fopen with a UTF-8 path (Windows' own fopen reads it as the code page); every folder on
// the way to a path made.
FILE* OpenFile(const std::string& path, const char* mode);
bool MakeDirs(const std::string& path);

// A folder and all in it removed; a file or folder renamed (same disk).
bool RemoveTree(const std::string& path);
bool RenamePath(const std::string& from, const std::string& to);

// The bytes of every file in a folder, and a copy of it all into `to` (made), `copied` told of
// each chunk; a top-level folder named `skipTop` is left out of both. False and why on a failure.
uint64_t TreeSize(const std::string& dir, const std::string& skipTop);
bool CopyTree(const std::string& from, const std::string& to, const std::string& skipTop,
              const std::function<void(uint64_t)>& copied, std::string* error);

// The disks a memory card may be: Windows' drive letters from C:, macOS's /Volumes/*, Linux's
// /media/<user>/*, /run/media/<user>/*, /media/*, /mnt/*.
std::vector<std::string> VolumeRoots();

// What is in a folder, not inside its folders. Hidden entries are left out.
struct DirEntry {
  std::string path;
  bool dir;
  uint64_t size;
  int64_t mtime;
};
std::vector<DirEntry> ListDir(const std::string& dir);

// Folders the PC game may have been installed to by GOG: Windows' GOG registry
// entries whose name has "Sensible" in it, and the GOG Games folders.
std::vector<std::string> GogInstallDirs();

// Set an environment variable for this process, unless it is set already.
void SetEnvDefault(const char* name, const char* value);

// The setup's log, `path`, written afresh (its folder made if need be). On Windows, a
// program started from Explorer has no stderr; then stderr goes to the log too, so what
// a library prints before it gives up is kept. NULL when it cannot be opened.
FILE* OpenLog(const std::string& path);

}  // namespace sd
