// The Wi-Fi copy to a phone (include/phone_copy.h, docs/MOBILE.md "Copying over Wi-Fi"): while
// the wizard's phone screen is open, the assets folder served on the local network, and the QR
// code of the page a phone opens. Its own file so the sockets' headers never meet the generated
// Slint header.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace sd {

struct PhoneServerState {
  bool running = false;
  std::string url;          // the page: http://<ip>:<port>/<key>/
  std::string error;        // why it is not running
  std::string phone;        // the address of the last phone that asked, "" before one
  int files = 0, filesSent = 0;
  uint64_t bytes = 0, bytesSent = 0;
  bool finished = false;    // a phone asked for every file of the last list
};

// Serves `assetsDir` (and `phoneIni`, the phone's GAME OPTIONS, when it exists) until Stop, and
// each of `mods` ({slug, folder}: the mods that are on, docs/MODS.md) as files listed under
// mods/<slug>/, which the phone puts in its own mods folder, not its assets.
// False, and why in PhoneServerStatus().error, when no port could be opened.
bool PhoneServerStart(const std::string& assetsDir, const std::string& phoneIni,
                      const std::vector<std::pair<std::string, std::string>>& mods = {});
void PhoneServerStop();
PhoneServerState PhoneServerStatus();

// What the QR code holds: https://<PROJECT_SITE>/get/#h=<ip>:<port>&k=<key> -- a public page the
// camera opens without reaching the local network (Android 17 lets a browser reach it only with a
// permission), and on Android an App Link straight into the app. "" when not serving.
std::string PhoneServerLink();

// A QR code of `text`: `size` modules a side, row by row (true = dark). Empty when it does not fit.
std::vector<bool> QrCode(const std::string& text, int* size);

}  // namespace sd
