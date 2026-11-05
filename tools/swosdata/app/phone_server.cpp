// The Wi-Fi copy to a phone: the setup's end (phone_server.hpp, include/phone_copy.h).
//
// One thread waits on the TCP port (HTTP) and the UDP port (a phone asking where the setup is);
// each HTTP connection is answered on a thread of its own, one request, then closed. Only the
// files of the assets folder are served, found by their path in the list, never a path built
// from what the phone sent.
#include "phone_server.hpp"

#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <random>
#include <thread>

#include "../../../include/phone_copy.h"
#include "platform.hpp"

extern "C" {
#include "../../../third_party/qrcodegen/qrcodegen.h"
}

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET sock_t;
#define SOCK_BAD INVALID_SOCKET
#define CloseSock closesocket
typedef int socklen_t_;
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int sock_t;
#define SOCK_BAD (-1)
#define CloseSock close
typedef socklen_t socklen_t_;
#endif

namespace sd {
namespace {

struct Server {
  std::mutex lock;
  PhoneServerState state;
  std::string assets, ini, key, host;
  int port = 0;
  std::map<std::string, uint64_t> files;     // the last list: path -> size
  std::vector<std::pair<std::string, std::string>> mods;   // {slug, folder}: listed as mods/<slug>/
  std::map<std::string, std::string> modFiles;            // a listed mods/ path -> its file
  std::map<std::string, bool> sent;
  sock_t tcp = SOCK_BAD, udp = SOCK_BAD;
  std::atomic<bool> stop{false};
  std::thread thread;
};
Server* g = nullptr;

int SockError() {
#ifdef _WIN32
  return WSAGetLastError();
#else
  return errno;
#endif
}

void SockInit() {
#ifdef _WIN32
  static bool done = false;
  if (!done) {
    WSADATA w;
    WSAStartup(MAKEWORD(2, 2), &w);
    done = true;
  }
#endif
}

// This computer's address on the local network: the one a packet to the internet would leave
// from (a UDP "connect" sends nothing). 127.0.0.1 when there is no network.
std::string LocalAddress() {
  std::string ip = "127.0.0.1";
  sock_t s = socket(AF_INET, SOCK_DGRAM, 0);
  if (s == SOCK_BAD) return ip;
  sockaddr_in a{};
  a.sin_family = AF_INET;
  a.sin_port = htons(53);
  inet_pton(AF_INET, "8.8.8.8", &a.sin_addr);
  if (connect(s, (sockaddr*)&a, sizeof a) == 0) {
    sockaddr_in me{};
    socklen_t_ n = sizeof me;
    if (getsockname(s, (sockaddr*)&me, &n) == 0) {
      char buf[64];
      if (inet_ntop(AF_INET, &me.sin_addr, buf, sizeof buf)) ip = buf;
    }
  }
  CloseSock(s);
  return ip;
}

std::string ComputerName() {
  char buf[256] = "";
  if (gethostname(buf, sizeof buf) != 0 || !buf[0]) return "this computer";
  buf[sizeof buf - 1] = 0;
  return buf;
}

std::string MakeKey() {
  static const char kAlpha[] = "abcdefghijkmnpqrstuvwxyz23456789";
  std::random_device rd;
  std::string k;
  for (int i = 0; i < PHONE_COPY_KEY_LEN; i++) k += kAlpha[rd() % (sizeof kAlpha - 1)];
  return k;
}

void ListFiles(const std::string& dir, const std::string& rel, std::map<std::string, uint64_t>& out) {
  for (const DirEntry& e : ListDir(dir)) {
    const size_t slash = e.path.find_last_of("/\\");
    const std::string name = slash == std::string::npos ? e.path : e.path.substr(slash + 1);
    const std::string r = rel.empty() ? name : rel + "/" + name;
    if (e.dir)
      ListFiles(e.path, r, out);
    else
      out[r] = e.size;
  }
}

std::string Decode(const std::string& s) {
  std::string out;
  for (size_t i = 0; i < s.size(); i++) {
    if (s[i] == '%' && i + 2 < s.size() && isxdigit((unsigned char)s[i + 1]) && isxdigit((unsigned char)s[i + 2])) {
      out += char(std::stoi(s.substr(i + 1, 2), nullptr, 16));
      i += 2;
    } else
      out += s[i];
  }
  return out;
}

std::string Escape(const std::string& s) {     // for the page's HTML
  std::string out;
  for (char c : s) {
    if (c == '&') out += "&amp;";
    else if (c == '<') out += "&lt;";
    else if (c == '>') out += "&gt;";
    else if (c == '"') out += "&quot;";
    else out += c;
  }
  return out;
}

bool SendAll(sock_t s, const char* p, size_t n) {
  while (n > 0) {
    const int k = (int)send(s, p, (int)(n > 65536 ? 65536 : n), 0);
    if (k <= 0) return false;
    p += k;
    n -= size_t(k);
  }
  return true;
}

bool Reply(sock_t s, int code, const char* type, const std::string& body) {
  char head[256];
  snprintf(head, sizeof head, "HTTP/1.0 %d %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n",
           code, code == 200 ? "OK" : "Not Found", type, body.size());
  return SendAll(s, head, strlen(head)) && SendAll(s, body.data(), body.size());
}

// The phone's browser's page: one button that opens the app with the link.
std::string Page(const std::string& userAgent, bool noApp) {
  const std::string link = std::string(PROJECT_URL_SCHEME "://get?h=") + g->host + ":" + std::to_string(g->port) + "&k=" + g->key;
  const bool android = userAgent.find("Android") != std::string::npos;
  const std::string here = "http://" + g->host + ":" + std::to_string(g->port) + "/" + g->key + "/";
  std::string href = link;
  if (android)        // Chrome opens an app by an intent link; back here, saying so, without one
    href = "intent://get?h=" + g->host + ":" + std::to_string(g->port) + "&k=" + g->key +
           "#Intent;scheme=" PROJECT_URL_SCHEME ";package=" PROJECT_APP_ID ";S.browser_fallback_url=" + here + "%3Fnoapp;end";
  std::string html =
      "<!doctype html><html><head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
      "<title>" PROJECT_NAME "</title><style>"
      "body{background:#0d1117;color:#e6edf3;font:17px/1.45 system-ui,sans-serif;margin:0;padding:28px 20px;text-align:center}"
      "h1{font-size:24px;margin:8px 0 6px}p{color:#9aa4ae;margin:10px auto;max-width:28em}"
      "a.b{display:block;margin:26px auto;max-width:20em;background:#3b82f6;color:#fff;text-decoration:none;"
      "font-weight:700;font-size:19px;padding:16px;border-radius:12px}.n{color:#f0b429}</style></head><body>"
      "<h1>" PROJECT_NAME "</h1>";
  if (noApp)
    html += "<p class=\"n\">" PROJECT_NAME " isn't installed on this phone yet. Install it, start it once, "
            "then come back to this page and tap the button again.</p>";
  html += "<p>Copy the game's data from <b>" + Escape(ComputerName()) + "</b> to " PROJECT_NAME " on this phone.</p>"
          "<a class=\"b\" href=\"" + Escape(href) + "\">Open in " PROJECT_NAME "</a>"
          "<p>Keep " PROJECT_SETUP_NAME " open on the computer until " PROJECT_NAME " says the copy is done.</p>"
          "</body></html>";
  return html;
}

void Serve(sock_t s, std::string peer) {
  std::string req;
  char buf[2048];
#ifdef _WIN32
  DWORD wait = 5000;
#else
  timeval wait{5, 0};
#endif
  setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&wait, sizeof wait);   // a silent connection
  while (req.find("\r\n\r\n") == std::string::npos && req.size() < 16384) {
    const int n = (int)recv(s, buf, sizeof buf, 0);
    if (n <= 0) break;
    req.append(buf, size_t(n));
    // A TLS handshake: Chrome trying https:// on this port first ("HTTPS-Upgrades"). Closed at
    // once, it falls back to http://; left waiting, it gave up (ERR_TOO_MANY_RETRIES).
    if ((unsigned char)req[0] == 0x16) {
      fprintf(stderr, "phone copy: %s: a TLS handshake, closed\n", peer.c_str());
      fflush(stderr);
      CloseSock(s);
      return;
    }
  }
  {   // what came (the setup's log, stderr): the first line, or the first bytes when it is not one
    std::string first = req.substr(0, req.find("\r\n"));
    if (first.size() > 120) first = first.substr(0, 120);
    for (char& c : first)
      if ((unsigned char)c < 32 || (unsigned char)c > 126) c = '?';
    fprintf(stderr, "phone copy: %s: %s (%zu bytes)\n", peer.c_str(), first.c_str(), req.size());
    fflush(stderr);
  }
  std::string path;
  if (req.compare(0, 4, "GET ") == 0) {
    const size_t sp = req.find(' ', 4);
    path = req.substr(4, sp == std::string::npos ? std::string::npos : sp - 4);
  }
  std::string ua;
  const size_t u = req.find("User-Agent:");
  if (u != std::string::npos) ua = req.substr(u, req.find("\r\n", u) - u);

  std::unique_lock<std::mutex> lk(g->lock);
  const std::string prefix = "/" + g->key + "/";
  if (path.compare(0, prefix.size(), prefix) != 0) {
    lk.unlock();
    Reply(s, 404, "text/plain", "Not here.\n");
    CloseSock(s);
    return;
  }
  g->state.phone = peer;
  std::string rest = path.substr(prefix.size());
  std::string query;
  const size_t q = rest.find('?');
  if (q != std::string::npos) {
    query = rest.substr(q + 1);
    rest = rest.substr(0, q);
  }
  if (rest.empty()) {
    lk.unlock();
    Reply(s, 200, "text/html; charset=utf-8", Page(ua, query.find("noapp") != std::string::npos));
  } else if (rest == "list") {
    g->files.clear();
    g->sent.clear();
    ListFiles(g->assets, "", g->files);
    for (auto it = g->files.begin(); it != g->files.end();)   // the Amiga drawer's, not a phone's
      it = it->first.compare(0, 6, "amiga/") == 0 || it->first.compare(0, 5, "mods/") == 0 ? g->files.erase(it) : std::next(it);
    g->modFiles.clear();
    for (const auto& m : g->mods) {            // the mods that are on (docs/MODS.md)
      std::map<std::string, uint64_t> mf;
      ListFiles(m.second, "", mf);
      for (const auto& f : mf) {
        const std::string rel = "mods/" + m.first + "/" + f.first;
        g->files[rel] = f.second;
        g->modFiles[rel] = m.second + "/" + f.first;
      }
    }
    std::string body = PHONE_COPY_LIST "\n";
    uint64_t total = 0;
    for (auto& f : g->files) {
      body += std::to_string(f.second) + " " + f.first + "\n";
      total += f.second;
    }
    g->state.files = int(g->files.size());
    g->state.bytes = total;
    g->state.filesSent = 0;
    g->state.bytesSent = 0;
    g->state.finished = false;
    lk.unlock();
    Reply(s, 200, "text/plain; charset=utf-8", body);
  } else if (rest == "ini") {
    const std::string ini = g->ini;
    lk.unlock();
    FILE* f = IsFile(ini) ? OpenFile(ini, "rb") : nullptr;
    if (!f) {
      Reply(s, 404, "text/plain", "No options.\n");
    } else {
      std::string body;
      size_t n;
      while ((n = fread(buf, 1, sizeof buf, f)) > 0) body.append(buf, n);
      fclose(f);
      Reply(s, 200, "text/plain; charset=utf-8", body);
    }
  } else if (rest.compare(0, 2, "f/") == 0) {
    const std::string rel = Decode(rest.substr(2));
    auto it = g->files.find(rel);
    if (it == g->files.end()) {
      lk.unlock();
      Reply(s, 404, "text/plain", "No such file.\n");
    } else {
      const auto mod = g->modFiles.find(rel);
      const std::string full = mod != g->modFiles.end() ? mod->second : g->assets + "/" + rel;
      lk.unlock();
      FILE* f = OpenFile(full, "rb");
      if (!f) {
        Reply(s, 404, "text/plain", "Cannot read it.\n");
      } else {
        fseek(f, 0, SEEK_END);
        const long size = ftell(f);
        fseek(f, 0, SEEK_SET);
        char head[200];
        snprintf(head, sizeof head, "HTTP/1.0 200 OK\r\nContent-Type: application/octet-stream\r\nContent-Length: %ld\r\nConnection: close\r\n\r\n", size);
        bool ok = SendAll(s, head, strlen(head));
        std::vector<char> chunk(65536);
        size_t n;
        uint64_t done = 0;
        while (ok && (n = fread(chunk.data(), 1, chunk.size(), f)) > 0) {
          ok = SendAll(s, chunk.data(), n);
          if (ok) {
            done += n;
            std::lock_guard<std::mutex> l2(g->lock);
            g->state.bytesSent += n;
          }
        }
        fclose(f);
        std::lock_guard<std::mutex> l2(g->lock);
        if (ok && !g->sent[rel]) {
          g->sent[rel] = true;
          g->state.filesSent++;
          if (g->state.filesSent >= g->state.files) g->state.finished = true;
        }
      }
    }
  } else {
    lk.unlock();
    Reply(s, 404, "text/plain", "Not here.\n");
  }
  CloseSock(s);
}

void Loop() {
  while (!g->stop) {
    fd_set rd;
    FD_ZERO(&rd);
    FD_SET(g->tcp, &rd);
    sock_t top = g->tcp;
    if (g->udp != SOCK_BAD) {
      FD_SET(g->udp, &rd);
      if (g->udp > top) top = g->udp;
    }
    timeval tv{0, 200000};
    if (select(int(top + 1), &rd, nullptr, nullptr, &tv) <= 0) continue;
    if (FD_ISSET(g->tcp, &rd)) {
      sockaddr_in a{};
      socklen_t_ n = sizeof a;
      sock_t c = accept(g->tcp, (sockaddr*)&a, &n);
      if (c != SOCK_BAD) {
        char ip[64] = "";
        inet_ntop(AF_INET, &a.sin_addr, ip, sizeof ip);
        std::thread(Serve, c, std::string(ip)).detach();
      }
    }
    if (g->udp != SOCK_BAD && FD_ISSET(g->udp, &rd)) {
      char buf[256];
      sockaddr_in a{};
      socklen_t_ n = sizeof a;
      const int k = (int)recvfrom(g->udp, buf, sizeof buf - 1, 0, (sockaddr*)&a, &n);
      if (k > 0) {
        buf[k] = 0;
        if (strncmp(buf, PHONE_COPY_ASK, strlen(PHONE_COPY_ASK)) == 0) {
          const std::string here = std::string(PHONE_COPY_HERE " ") + std::to_string(g->port) + " " + g->key + " " + ComputerName();
          sendto(g->udp, here.data(), int(here.size()), 0, (sockaddr*)&a, n);
        }
      }
    }
  }
}

}  // namespace

bool PhoneServerStart(const std::string& assetsDir, const std::string& phoneIni,
                      const std::vector<std::pair<std::string, std::string>>& mods) {
  if (g) {           // already serving: the folder may be another one now
    std::lock_guard<std::mutex> lk(g->lock);
    g->assets = assetsDir;
    g->ini = phoneIni;
    g->mods = mods;
    return g->state.running;
  }
  SockInit();
  g = new Server;
  g->assets = assetsDir;
  g->ini = phoneIni;
  g->mods = mods;
  g->key = MakeKey();
  g->host = LocalAddress();
  int why = 0;
  // The usual port or the next nine, else any the system gives (the QR code and the answer to
  // a phone's question both carry it).
  for (int i = 0; i <= 10 && g->tcp == SOCK_BAD; i++) {
    const int p = i < 10 ? PHONE_COPY_PORT + i : 0;
    sock_t s = socket(AF_INET, SOCK_STREAM, 0);
    if (s == SOCK_BAD) {
      why = SockError();
      break;
    }
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(uint16_t(p));
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(s, (sockaddr*)&a, sizeof a) == 0 && listen(s, 16) == 0) {
      g->tcp = s;
      sockaddr_in me{};
      socklen_t_ n = sizeof me;
      g->port = getsockname(s, (sockaddr*)&me, &n) == 0 ? ntohs(me.sin_port) : p;
    } else {
      why = SockError();
      CloseSock(s);
    }
  }
  if (g->tcp == SOCK_BAD) {
    g->state.error = "No network port could be opened for the phone (error " + std::to_string(why) + ").";
    return false;
  }
  sock_t u = socket(AF_INET, SOCK_DGRAM, 0);
  if (u != SOCK_BAD) {
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(PHONE_COPY_PORT);
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(u, (sockaddr*)&a, sizeof a) == 0)
      g->udp = u;
    else
      CloseSock(u);       // (the QR code still works: the phone is told the address)
  }
  g->state.running = true;
  g->state.url = "http://" + g->host + ":" + std::to_string(g->port) + "/" + g->key + "/";
  g->thread = std::thread(Loop);
  return true;
}

void PhoneServerStop() {
  if (!g) return;
  g->stop = true;
  if (g->thread.joinable()) g->thread.join();
  CloseSock(g->tcp);
  if (g->udp != SOCK_BAD) CloseSock(g->udp);
  // (A connection still sending finishes on its own thread: g is not freed for that reason.)
  std::lock_guard<std::mutex> lk(g->lock);
  g->state.running = false;
}

PhoneServerState PhoneServerStatus() {
  if (!g) return {};
  std::lock_guard<std::mutex> lk(g->lock);
  return g->state;
}

std::string PhoneServerLink() {
  if (!g) return "";
  std::lock_guard<std::mutex> lk(g->lock);
  if (!g->state.running) return "";
  return "https://" PROJECT_SITE "/get/#h=" + g->host + ":" + std::to_string(g->port) + "&k=" + g->key;
}

std::vector<bool> QrCode(const std::string& text, int* size) {
  std::vector<uint8_t> qr(qrcodegen_BUFFER_LEN_MAX), tmp(qrcodegen_BUFFER_LEN_MAX);
  *size = 0;
  if (!qrcodegen_encodeText(text.c_str(), tmp.data(), qr.data(), qrcodegen_Ecc_MEDIUM, qrcodegen_VERSION_MIN,
                            qrcodegen_VERSION_MAX, qrcodegen_Mask_AUTO, true))
    return {};
  const int n = qrcodegen_getSize(qr.data());
  std::vector<bool> out(size_t(n * n));
  for (int y = 0; y < n; y++)
    for (int x = 0; x < n; x++) out[size_t(y * n + x)] = qrcodegen_getModule(qr.data(), x, y);
  *size = n;
  return out;
}

}  // namespace sd
