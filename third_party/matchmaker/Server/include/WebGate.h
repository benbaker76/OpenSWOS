// OpenSWOS: the WebSocket gate -- how a BROWSER plays (the web build).
//
// A browser has no UDP. Emscripten's socket layer carries each datagram the game
// sends as ONE WebSocket message to ws://<server>:<port> -- the same port number the
// datagram was for, over TCP -- and delivers each message back as one datagram. So
// this gate takes those connections and gives each browser socket a UDP socket of its
// own on this machine, talking to the server's own UDP port. Matchmaking, the relay
// and GGPO's packets through it then see an ordinary UDP client at 127.0.0.1:<port>,
// and nothing behind the gate knows a browser is there.
//
// One browser socket, one address. Emscripten opens a WebSocket per socket and, when
// the socket was bound, sends its port first ("\xFF\xFF\xFF\xFFport" + 2 bytes). The
// game's match link and then GGPO's socket are bound to the same port, one after the
// other, and the relay knows a player by the address its packets come from -- so a
// tunnel is keyed by (the browser's IP, that port) and outlives its WebSocket for a
// while, and the socket that follows on the same port gets the same address.
//
// TcpGate owns the TCP listener, because the recorder (Recorder.h) listens on the same
// port number: a connection that opens with "GET " is a WebSocket, one that opens with
// "SWMR" a match report (ReportDesk.h), anything else goes to the recorder, whose
// stream opens with its magic "SWRC".
//
// wss://. A page served over https may only open wss://, and Emscripten dials it on the
// same port as ws:// (the datagram's). So TLS is terminated HERE, not by a proxy in
// front: a connection that opens with a TLS record (0x16 0x03) is a secure WebSocket,
// and after the TLS handshake it is exactly a plain one -- same tunnels, same address
// (a proxy would have hidden the browser's), same port. Given --tls-cert/--tls-key
// (OpenSSL; docs/NETPLAY_SETUP.md §10); the files are re-read when they change, so a
// certbot renewal needs no restart.
#pragma once
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

class Recorder;
class ReportDesk;
struct ssl_st;
struct ssl_ctx_st;

class WebGate
{
public:
    // certPath/keyPath empty: no TLS. Otherwise both PEM files, or the gate says why
    // not and serves ws:// only.
    WebGate(bool enabled, uint16_t serverPort, const std::string& certPath = "",
            const std::string& keyPath = "");
    ~WebGate();

    bool Enabled() const { return m_enabled; }
    bool TlsEnabled() const { return m_enabled && m_tls != nullptr; }

    // A connection TcpGate has accepted, whose first bytes are "GET " -- or, with
    // tls, a TLS record.
    void Adopt(int fd, const std::string& ip, uint16_t port, bool tls = false);

    // Once per main-loop pass: read and write every connection and tunnel.
    void Update();
    // A browser is connected: its datagrams arrive over TCP, which the server polls.
    bool Busy() const { return !m_connections.empty(); }

private:
    struct Connection;
    struct Tunnel
    {
        int udp = -1;
        std::string key;                // "ip:port" of a bound browser socket, or unique
        uint16_t localPort = 0;         // the UDP port the server sees it on
        Connection* conn = nullptr;     // the WebSocket its replies go to, if any
        uint32_t idleSinceMs = 0;       // when it was last left without one
        uint64_t toServer = 0, toBrowser = 0;
    };
    struct Connection
    {
        int fd = -1;
        std::string ip, address;
        std::vector<uint8_t> in, out;
        bool upgraded = false;          // the HTTP handshake is done
        bool closing = false;           // flush `out`, then close
        bool first = true;              // no message yet: it may be the port message
        Tunnel* tunnel = nullptr;
        std::vector<uint8_t> message;   // a fragmented message being put together
        uint8_t messageOp = 0;
        uint32_t openedMs = 0;
        uint16_t browserPort = 0;
        ssl_st* ssl = nullptr;          // wss://: everything goes through it
        bool tlsReady = true;           // false until its TLS handshake is done
    };

    bool Handshake(Connection& c);                 // false = close it
    bool Frames(Connection& c);                    // false = close it
    void Deliver(Connection& c, const uint8_t* p, size_t len);
    void SendFrame(Connection& c, uint8_t op, const uint8_t* p, size_t len);
    bool Flush(Connection& c);                     // false = close it
    int Receive(Connection& c, uint8_t* buf, size_t len);   // >0 bytes, 0 none yet, <0 gone
    bool TlsAccept(Connection& c);                 // false = close it
    ssl_ctx_st* LoadTls();                         // nullptr = could not, and says why
    void ReloadTlsIfChanged();
    Tunnel* TunnelFor(const std::string& key);
    void Close(Connection& c, const char* why);
    void PumpTunnels();

    bool m_enabled;
    uint16_t m_serverPort;
    uint32_t m_nextUnique = 1;
    std::vector<std::unique_ptr<Connection>> m_connections;
    std::map<std::string, std::unique_ptr<Tunnel>> m_tunnels;
    std::string m_certPath, m_keyPath;
    ssl_ctx_st* m_tls = nullptr;
    int64_t m_certStamp = 0, m_keyStamp = 0;       // mtimes the loaded context came from
    uint32_t m_tlsCheckedMs = 0;
};

class TcpGate
{
public:
    // Listens only if there is someone to hand a connection to.
    TcpGate(uint16_t port, WebGate* web, Recorder* recorder, ReportDesk* reports = nullptr);
    ~TcpGate();

    void Update();
    // A connection is waiting to say what it is: the server polls TCP every millisecond.
    bool Busy() const { return !m_pending.empty(); }

private:
    struct Pending
    {
        int fd = -1;
        std::string ip;
        uint16_t port = 0;
        uint32_t sinceMs = 0;
    };
    int m_listen = -1;
    WebGate* m_web;
    Recorder* m_recorder;
    ReportDesk* m_reports;
    std::vector<Pending> m_pending;
};
