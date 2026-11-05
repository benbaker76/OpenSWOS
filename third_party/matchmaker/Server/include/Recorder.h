#pragma once
// OpenSWOS: the recorder (not in upstream). include/record_proto.h in the game
// tree has the protocol and the reasons for it; this is the server's half.
//
// Players stream a live match here over TCP -- the matchmaker's port number, TCP
// instead of UDP -- whether the match is played through the relay or peer to peer,
// and whether this server is the one that introduced them or just one they were
// pointed at (SWOS_RECORD=host:port). Both players' streams are paired by match id
// and compared frame by frame as they arrive; the first frame whose SIM-tier hashes
// differ is a desync, reported the moment it is seen -- on the console, and as a
// "desync" line in the ledger -- with the two builds, platforms, teams and times.
//
// One thread, polled from the main loop like the relay: no locks, nothing blocks.
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "Ledger.h"
#include "srp_writer.h"

class Recorder
{
public:
    // Takes players' streams on TCP `port` when `enabled` -- the TCP gate (WebGate.h)
    // listens, since browsers' WebSockets arrive on the same port, and hands each
    // stream over (Adopt). `ledger` gets the match and desync lines, and a full
    // recording's .SRP goes into `recordingsDir`.
    Recorder(bool enabled, uint16_t port, Ledger& ledger, const std::string& recordingsDir);
    ~Recorder();

    bool Enabled() const { return m_enabled; }

    // A connection the TCP gate accepted and found to be a player's stream.
    void Adopt(int fd, const std::string& address);

    // Once per main-loop pass: read what arrived, compare, and close matches that
    // are over.
    void Update();
    // A recording stream is open (TCP, which the server polls).
    bool Busy() const { return !m_connections.empty(); }

private:
    struct Side
    {
        bool present = false;       // its HELLO has arrived
        bool open = false;          // its connection is still up
        bool ended = false;         // its END has arrived
        std::string address;
        std::string build, platform, name, peer, team1, team2;
        int gameplay = 0, mode = 0, delay = 0, simWidth = 0;
        bool noWatch = false;       // LIVE CAST: its player keeps the match from viewers
        uint64_t relayToken = 0;
        std::vector<uint64_t> hashes;   // [frame]
        int outcome = -1;
        uint32_t endFrame = 0;
        uint64_t finalHash = 0;
        std::string why;

        // A FULL recording: the match itself, laid out as a .SRP by srp_writer.h --
        // the same code that wrote the player's own copy.
        bool replay = false;                    // its META has arrived
        uint16_t tickRate = 0, quirks = 0;
        std::vector<uint8_t> meta, frms;
        std::vector<uint32_t> offsets;
        std::vector<SrpAudioEvent> audio;
        std::vector<SrpChapter> chapters;
        bool haveScore = false;
        uint16_t score[2] = { 0, 0 };
        uint32_t writtenFrames = 0;             // frames in the last file written

        // A RESUMABLE recording (replay_v2.h): the parts SIMV, SETP, STAT, INPT hold.
        // HASH comes from `hashes` above -- the frames it compares.
        bool resume = false;
        uint64_t layout = 0;
        uint8_t rules = 0;
        uint8_t style = 0;          // the gameplay style (GAMEPLAY_*): the RESUME message's byte 9
        uint16_t resumeSimWidth = 0;
        std::vector<uint8_t> setup, kickoff, inputs;   // inputs: SRP_INPUT_BYTES a frame
        bool inputsBroken = false;              // a batch came without its inputs
        // What this player SAW (RECORD_REPLAY_PRED): its half of PRED and SEEN.
        std::vector<uint8_t> pred, seen;
        // What this player's game READ (RECORD_REPLAY_READ): SRP_READ_BYTES a frame.
        std::vector<uint8_t> read;
        uint16_t readDelay = 0;
        uint8_t readBalance = 0;
    };
    struct Match
    {
        uint64_t id = 0;
        Side side[2];
        std::string started;
        uint32_t lastMs = 0;
        uint32_t compared = 0;
        int64_t desyncFrame = -1;
        uint64_t desyncHash[2] = { 0, 0 };
        std::string desyncAt;
        std::string stamp;                      // "20260919-001308", for the file name
        std::string recording;                  // the .SRP written, once there is one
    };
    struct Connection
    {
        int fd = -1;
        std::string address;
        std::vector<uint8_t> in;
        Match* match = nullptr;
        int player = 0;             // 1 or 2 once its HELLO has arrived

        // A VIEWER (LIVE CAST, record_proto.h RECORD_WATCH): what it has been sent of the
        // match it watches -- by id, so a match that finishes meanwhile is simply gone.
        bool viewer = false;
        bool closing = false;       // close once `out` has gone
        uint64_t watchId = 0;
        int watchSide = -1;         // the side whose stream it is sent
        bool metaSent = false;
        uint32_t sentFrames = 0, sentAudio = 0, sentChapters = 0;
        std::vector<uint8_t> out;
    };

    bool Read(Connection& c);                       // false = close it
    bool OnWatch(Connection& c, uint8_t type, const uint8_t* p, uint32_t len);
    void ServeViewer(Connection& c);
    static const Side* WatchedSide(const Match& m, int* which);
    static void Queue(Connection& c, uint8_t type, const std::vector<uint8_t>& payload);
    static bool Flush(Connection& c);               // false = the viewer has gone
    bool Handle(Connection& c, uint8_t type, const uint8_t* p, uint32_t len);
    bool OnHello(Connection& c, const uint8_t* p, uint32_t len);
    void OnFrames(Connection& c, const uint8_t* p, uint32_t len);
    void OnEnd(Connection& c, const uint8_t* p, uint32_t len);
    void OnReplayMeta(Connection& c, const uint8_t* p, uint32_t len);
    bool OnReplayFrames(Connection& c, const uint8_t* p, uint32_t len);
    void OnReplayPred(Connection& c, const uint8_t* p, uint32_t len);
    void OnReplayRead(Connection& c, const uint8_t* p, uint32_t len);
    void OnReplayScore(Connection& c, const uint8_t* p, uint32_t len);
    void OnReplayResume(Connection& c, const uint8_t* p, uint32_t len);
    static uint32_t Usable(const Side& s);
    static bool Whole(const Side& s);
    void WriteReplay(Match& m, bool last);
    // Who played whom, beside the .SRP (<name>.json) and all of them in index.json: what
    // the playback page lists (docs/PLAYBACK.md, "Who played whom").
    void WriteMatchCard(const Match& m, const std::string& why);
    void WriteIndex();
    void Compare(Match& m, int from, uint32_t frame);
    void CloseConnection(Connection& c, const char* why);
    void Finish(Match& m, const char* why);
    std::string Label(const Match& m) const;

    bool m_enabled = false;
    Ledger& m_ledger;
    std::string m_dir;
    std::vector<std::unique_ptr<Connection>> m_connections;
    std::map<uint64_t, std::unique_ptr<Match>> m_matches;   // by match id
    uint32_t m_lastSweepMs = 0;
};
