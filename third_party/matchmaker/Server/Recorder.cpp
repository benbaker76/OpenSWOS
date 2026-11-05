// OpenSWOS: the recorder. See include/Recorder.h, and include/record_proto.h in
// the game tree for the protocol.
#include "Recorder.h"
#include "record_proto.h"
#include <iostream>

#ifndef _WIN32
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <dirent.h>
#include <algorithm>
#include <fstream>
#include <sstream>
#endif
#include <ctime>

namespace
{
    // A match whose streams have all closed is over. One whose other player never
    // recorded is given this long after the last stream closes before it is written.
    const uint32_t kLoneSideGraceMs = 15000;
    // Nothing from anyone for this long: over, whatever state it is in.
    const uint32_t kIdleTimeoutMs = 120000;
    // A full recording is written to disk as it goes, every this many frames (a
    // minute or so), so a server that dies mid-match keeps most of it.
    const uint32_t kCheckpointFrames = 3000;

    std::string CompactStamp()
    {
        char buf[32];
        std::time_t t = std::time(nullptr);
        std::strftime(buf, sizeof buf, "%Y%m%d-%H%M%S", std::localtime(&t));
        return buf;
    }

    std::string Tag()
    {
        return "[record] " + ServerTime::TimeOfDay() + " ";
    }

    // A length-prefixed string at p[*at], bounded by len. False if it runs off the end.
    bool TakeString(const uint8_t* p, uint32_t len, uint32_t* at, std::string* out)
    {
        if (*at >= len)
            return false;
        uint32_t n = p[*at];
        if (*at + 1 + n > len)
            return false;
        out->assign((const char*)p + *at + 1, n);
        *at += 1 + n;
        return true;
    }
}

#ifdef _WIN32

// The server has only ever been run on Linux (WSL included); the recorder is POSIX.
Recorder::Recorder(bool enabled, uint16_t, Ledger& ledger, const std::string& dir) :
    m_ledger(ledger), m_dir(dir)
{
    if (enabled)
        std::cout << "[record] not available on a Windows build of the server\n";
}
Recorder::~Recorder() {}
void Recorder::Adopt(int, const std::string&) {}
void Recorder::Update() {}

#else

Recorder::Recorder(bool enabled, uint16_t port, Ledger& ledger, const std::string& dir) :
    m_enabled(enabled), m_ledger(ledger), m_dir(dir)
{
    if (!enabled)
        return;
    if (!m_dir.empty())
        mkdir(m_dir.c_str(), 0755);             // one level; EEXIST is the usual answer
    std::cout << "[record] on: players stream their matches to TCP " << port
              << ", full recordings kept in " << m_dir << "/\n";
}

Recorder::~Recorder()
{
    for (auto& c : m_connections)
        if (c->fd >= 0)
            CloseConnection(*c, "server stopping");
    while (!m_matches.empty())
        Finish(*m_matches.begin()->second, "server stopping");
}

std::string Recorder::Label(const Match& m) const
{
    std::string n1 = m.side[0].present ? m.side[0].name : m.side[1].peer;
    std::string n2 = m.side[1].present ? m.side[1].name : m.side[0].peer;
    return Hex64(m.id) + " " + n1 + " vs " + n2;
}

void Recorder::Adopt(int fd, const std::string& address)
{
    if (!m_enabled)
    {
        close(fd);
        return;
    }
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
    auto c = std::make_unique<Connection>();
    c->fd = fd;
    c->address = address;
    m_connections.push_back(std::move(c));
}

void Recorder::Update()
{
    if (!m_enabled)
        return;
    for (auto& c : m_connections)
        if (c->fd >= 0 && !Read(*c) && c->fd >= 0)
            CloseConnection(*c, "closed");
    for (auto& c : m_connections)                   // LIVE CAST: what the viewers have not had
    {
        if (c->fd < 0 || !c->viewer)
            continue;
        if (!c->closing)
            ServeViewer(*c);
        if (!Flush(*c) || (c->closing && c->out.empty()))
        {
            close(c->fd);
            c->fd = -1;
        }
    }
    for (size_t i = 0; i < m_connections.size();)
    {
        if (m_connections[i]->fd < 0)
            m_connections.erase(m_connections.begin() + i);
        else
            i++;
    }

    uint32_t now = ServerTime::NowMs();
    if (now - m_lastSweepMs < 1000)
        return;
    m_lastSweepMs = now;
    std::vector<Match*> done;
    for (auto& entry : m_matches)
    {
        Match& m = *entry.second;
        bool anyOpen = (m.side[0].present && m.side[0].open) || (m.side[1].present && m.side[1].open);
        bool both = m.side[0].present && m.side[1].present;
        if (!anyOpen && (both || now - m.lastMs > kLoneSideGraceMs))
            done.push_back(&m);
        else if (now - m.lastMs > kIdleTimeoutMs)
            done.push_back(&m);
    }
    for (Match* m : done)
        Finish(*m, "over");
}

bool Recorder::Read(Connection& c)
{
    uint8_t buf[16384];
    bool closed = false;
    for (;;)
    {
        ssize_t n = recv(c.fd, buf, sizeof buf, 0);
        if (n > 0)
        {
            c.in.insert(c.in.end(), buf, buf + n);
            continue;
        }
        if (n == 0)
            closed = true;                          // the player closed it: parse, then close
        else if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
            closed = true;
        break;
    }
    size_t at = 0;
    while (c.in.size() - at >= RECORD_HEADER_LEN)
    {
        const uint8_t* h = c.in.data() + at;
        uint32_t len = ((uint32_t)h[1] << 16) | ((uint32_t)h[2] << 8) | h[3];
        if (len > RECORD_MAX_PAYLOAD)
        {
            c.in.clear();
            return false;
        }
        if (c.in.size() - at < RECORD_HEADER_LEN + len)
            break;
        if (!Handle(c, h[0], h + RECORD_HEADER_LEN, len))
        {
            c.in.clear();
            return false;
        }
        at += RECORD_HEADER_LEN + len;
    }
    c.in.erase(c.in.begin(), c.in.begin() + at);
    return !closed;
}

bool Recorder::Handle(Connection& c, uint8_t type, const uint8_t* p, uint32_t len)
{
    if (c.player == 0 && !c.viewer && (type == RECORD_WATCH_LIST || type == RECORD_WATCH))
        return OnWatch(c, type, p, len);
    if (c.viewer)
        return true;                                // a viewer says nothing more
    if (type == RECORD_HELLO)
        return c.player == 0 && OnHello(c, p, len);
    if (c.player == 0 || !c.match)
        return false;                               // nothing before the HELLO
    if (type == RECORD_FRAMES)
        OnFrames(c, p, len);
    else if (type == RECORD_END)
        OnEnd(c, p, len);
    else if (type == RECORD_REPLAY_META)
        OnReplayMeta(c, p, len);
    else if (type == RECORD_REPLAY_FRAMES)
    {
        if (!OnReplayFrames(c, p, len))
            return false;                           // a malformed stream: stop listening to it
    }
    else if (type == RECORD_REPLAY_SCORE)
        OnReplayScore(c, p, len);
    else if (type == RECORD_REPLAY_RESUME)
        OnReplayResume(c, p, len);
    else if (type == RECORD_REPLAY_PRED)
        OnReplayPred(c, p, len);
    else if (type == RECORD_REPLAY_READ)
        OnReplayRead(c, p, len);
    // Anything newer than us is skipped: a player may be a later build.
    c.match->lastMs = ServerTime::NowMs();
    return true;
}

bool Recorder::OnHello(Connection& c, const uint8_t* p, uint32_t len)
{
    if (len < RECORD_HELLO_FIXED || memcmp(p, RECORD_MAGIC, 4) != 0 || p[4] != RECORD_PROTOCOL)
        return false;
    int player = p[5];
    if (player != 1 && player != 2)
        return false;
    uint64_t id = Record_Get64(p + 8);
    if (id == 0)
    {
        // No packet key: nothing to pair by. Its own match, then.
        static uint64_t lone = 0;
        id = 0x8000000000000000ULL | ++lone;
    }
    auto& slot = m_matches[id];
    if (!slot)
    {
        slot = std::make_unique<Match>();
        slot->id = id;
        slot->started = ServerTime::WallClock();
        slot->stamp = CompactStamp();
    }
    Match& m = *slot;
    Side& s = m.side[player - 1];
    if (s.present)
    {
        std::cout << Tag() << Hex64(id) << " a second stream for player " << player
                  << " from " << c.address << ": refused\n";
        return false;
    }
    s.present = s.open = true;
    s.address = c.address;
    s.mode = p[6];
    s.gameplay = p[7];
    s.relayToken = Record_Get64(p + 16);
    s.simWidth = (int)Record_Get16(p + 24);
    s.delay = p[26];
    s.noWatch = (p[27] & RECORD_HELLO_NO_WATCH) != 0;  // (LIVE CAST: this player said no)
    uint32_t at = RECORD_HELLO_FIXED;
    if (!TakeString(p, len, &at, &s.build) || !TakeString(p, len, &at, &s.platform) ||
        !TakeString(p, len, &at, &s.name) || !TakeString(p, len, &at, &s.peer) ||
        !TakeString(p, len, &at, &s.team1) || !TakeString(p, len, &at, &s.team2))
        return false;
    c.match = &m;
    c.player = player;
    m.lastMs = ServerTime::NowMs();

    std::cout << Tag() << Hex64(id) << " player " << player << " (" << s.name << ", "
              << s.build << ", " << s.platform << ", rules 0x" << std::hex << s.gameplay << std::dec
              << ") recording from " << c.address << ": " << s.team1 << " v " << s.team2
              << (s.relayToken ? ", through the relay " + Hex64(s.relayToken) : std::string(", peer to peer"))
              << "\n";
    // Two players on different rules cannot stay in step -- say so now, not at the
    // desync. The handshake refuses such a pair since 2026-09-19; a build older than
    // that does not, and one such pair (a stale tackle_foul.o) is how this was found.
    const Side& other = m.side[2 - player];
    if (other.present && other.gameplay != s.gameplay)
        std::cout << Tag() << Hex64(id) << " WARNING: the two players play by different rules"
                  << " (player 1 0x" << std::hex << m.side[0].gameplay << ", player 2 0x"
                  << m.side[1].gameplay << std::dec << ") -- this match cannot stay in step\n";
    return true;
}

void Recorder::OnFrames(Connection& c, const uint8_t* p, uint32_t len)
{
    if (len < 6)
        return;
    uint32_t first = Record_Get32(p);
    uint32_t count = Record_Get16(p + 4);
    if (6 + (uint64_t)count * 8 > len)
        return;
    Match& m = *c.match;
    int i = c.player - 1;
    Side& s = m.side[i];
    for (uint32_t k = 0; k < count; k++)
    {
        uint32_t f = first + k;
        if (f < s.hashes.size())
            continue;                               // a repeat
        if (f > s.hashes.size())
            s.hashes.resize(f, 0);                  // a gap -- a player bug, but keep the frame numbers
        s.hashes.push_back(Record_Get64(p + 6 + 8 * k));
        Compare(m, i, f);
    }
}

void Recorder::Compare(Match& m, int from, uint32_t frame)
{
    const Side& other = m.side[1 - from];
    if (frame >= other.hashes.size())
        return;                                     // compared when the other one arrives
    m.compared++;
    if (m.side[from].hashes[frame] == other.hashes[frame])
        return;
    if (m.desyncFrame >= 0)
        return;                                     // the first is the one that matters
    m.desyncFrame = frame;
    m.desyncHash[0] = m.side[0].hashes[frame];
    m.desyncHash[1] = m.side[1].hashes[frame];
    m.desyncAt = ServerTime::WallClock();
    const Side& a = m.side[0];
    const Side& b = m.side[1];
    std::cout << Tag() << Label(m) << " DESYNC at frame " << frame << ": player 1 "
              << Hex64(m.desyncHash[0]) << " (" << a.build << ", " << a.platform << "), player 2 "
              << Hex64(m.desyncHash[1]) << " (" << b.build << ", " << b.platform << "); "
              << (frame ? "frame " + std::to_string(frame - 1) + " was the last they agreed on"
                        : std::string("they never agreed"))
              << "\n";
    m_ledger.Write("{\"kind\":\"desync\",\"match\":\"" + Hex64(m.id) + "\""
        + ",\"at\":" + JsonString(m.desyncAt)
        + ",\"frame\":" + std::to_string(frame)
        + ",\"hashes\":[\"" + Hex64(m.desyncHash[0]) + "\",\"" + Hex64(m.desyncHash[1]) + "\"]"
        + ",\"players\":[" + JsonString(a.name) + "," + JsonString(b.name) + "]"
        + ",\"builds\":[" + JsonString(a.build) + "," + JsonString(b.build) + "]"
        + ",\"platforms\":[" + JsonString(a.platform) + "," + JsonString(b.platform) + "]"
        + ",\"rules\":[" + std::to_string(a.gameplay) + "," + std::to_string(b.gameplay) + "]"
        + ",\"teams\":[" + JsonString(a.team1) + "," + JsonString(a.team2) + "]"
        + ",\"relay\":" + (a.relayToken ? "\"" + Hex64(a.relayToken) + "\"" : std::string("null"))
        + "}");
}

void Recorder::OnEnd(Connection& c, const uint8_t* p, uint32_t len)
{
    if (len < 13)
        return;
    Side& s = c.match->side[c.player - 1];
    s.ended = true;
    s.outcome = p[0];
    s.endFrame = Record_Get32(p + 1);
    s.finalHash = Record_Get64(p + 5);
    uint32_t at = 13;
    TakeString(p, len, &at, &s.why);
}

void Recorder::OnReplayMeta(Connection& c, const uint8_t* p, uint32_t len)
{
    if (len < 4)
        return;
    Side& s = c.match->side[c.player - 1];
    s.tickRate = (uint16_t)Record_Get16(p);
    s.quirks = (uint16_t)Record_Get16(p + 2);
    s.meta.assign(p + 4, p + len);
    s.replay = true;
}

bool Recorder::OnReplayFrames(Connection& c, const uint8_t* p, uint32_t len)
{
    Side& s = c.match->side[c.player - 1];
    if (!s.replay || len < 6)
        return false;
    uint32_t first = Record_Get32(p);
    uint32_t count = Record_Get16(p + 4);
    if (first != s.offsets.size())
        return false;                               // frames arrive in order, each once
    uint32_t at = 6;
    for (uint32_t k = 0; k < count; k++)
    {
        if (at + FRAME_HEAD_BYTES > len)
            return false;
        uint32_t n = FRAME_HEAD_BYTES + SPRITE_BYTES * Record_Get16(p + at);
        if (at + n > len)
            return false;
        s.offsets.push_back((uint32_t)s.frms.size());
        s.frms.insert(s.frms.end(), p + at, p + at + n);
        at += n;
    }
    if (at + 2 > len)
        return false;
    uint32_t nAudio = Record_Get16(p + at);
    at += 2;
    if (at + nAudio * AUDIO_EVENT_BYTES + 2 > len)
        return false;
    for (uint32_t k = 0; k < nAudio; k++, at += AUDIO_EVENT_BYTES)
    {
        SrpAudioEvent e;
        e.frame = Record_Get32(p + at);
        e.kind = p[at + 4];
        e.channel = p[at + 5];
        e.id = (uint16_t)Record_Get16(p + at + 6);
        s.audio.push_back(e);
    }
    uint32_t nChap = Record_Get16(p + at);
    at += 2;
    if (at + nChap * CHAPTER_BYTES > len)
        return false;
    for (uint32_t k = 0; k < nChap; k++, at += CHAPTER_BYTES)
    {
        SrpChapter ch;
        ch.frame = Record_Get32(p + at);
        ch.kind = p[at + 4];
        ch.team = p[at + 5];
        ch.value = (uint16_t)Record_Get16(p + at + 6);
        s.chapters.push_back(ch);
    }
    // Each frame's input (a resumable recording), or none. A batch without them leaves
    // the recording watchable but not resumable.
    uint32_t nInputs = at + 2 <= len ? Record_Get16(p + at) : 0;
    at += at + 2 <= len ? 2 : 0;
    if (nInputs == count && at + nInputs * SRP_INPUT_BYTES <= len)
        s.inputs.insert(s.inputs.end(), p + at, p + at + nInputs * SRP_INPUT_BYTES);
    else if (count)
        s.inputsBroken = true;
    // A checkpoint when the stream the file is WRITTEN from has grown enough -- player 1's
    // when it has one (WriteReplay). Checking the arriving stream instead fired on every
    // batch of the other one once it passed 3000 frames, since only the writer's count
    // moves: with both players streaming (recording on by default since 2026-09-30) the
    // server rebuilt and deflated the whole file many times a second, stopped answering the
    // matchmaker ("NO ANSWER FROM SERVER"), fell behind the second stream and never closed
    // the match.
    const Side* writer = nullptr;
    for (int i = 0; i < 2 && !writer; i++)
        if (c.match->side[i].replay && !c.match->side[i].offsets.empty())
            writer = &c.match->side[i];
    if (writer == &s && s.offsets.size() - s.writtenFrames >= kCheckpointFrames)
        WriteReplay(*c.match, false);
    return true;
}

void Recorder::OnReplayResume(Connection& c, const uint8_t* p, uint32_t len)
{
    if (len < 16)
        return;
    Side& s = c.match->side[c.player - 1];
    uint32_t setupLen = Record_Get32(p + 12);
    if (16 + (uint64_t)setupLen > len)
        return;
    s.layout = Record_Get64(p);
    s.rules = p[8];
    s.style = p[9];
    s.resumeSimWidth = (uint16_t)Record_Get16(p + 10);
    s.setup.assign(p + 16, p + 16 + setupLen);
    s.kickoff.assign(p + 16 + setupLen, p + len);
    s.resume = !s.setup.empty() && !s.kickoff.empty();
}

// A player's half of PRED and SEEN, batch by batch, in order. A batch that does not
// follow on is dropped: the file keeps what arrived whole.
void Recorder::OnReplayPred(Connection& c, const uint8_t* p, uint32_t len)
{
    if (len < 6)
        return;
    Side& s = c.match->side[c.player - 1];
    uint32_t first = Record_Get32(p), count = Record_Get16(p + 4);
    if (first != s.pred.size() || 6 + count + 4 > len)
        return;
    uint32_t seenLen = Record_Get32(p + 6 + count);
    if (6 + count + 4 + (uint64_t)seenLen != len)
        return;
    s.pred.insert(s.pred.end(), p + 6, p + 6 + count);
    s.seen.insert(s.seen.end(), p + 10 + count, p + len);
}

// What this player's game read from its controls on those frames, and when.
void Recorder::OnReplayRead(Connection& c, const uint8_t* p, uint32_t len)
{
    if (len < 8)
        return;
    Side& s = c.match->side[c.player - 1];
    uint32_t first = Record_Get32(p), count = Record_Get16(p + 4);
    if (first * SRP_READ_BYTES != s.read.size() || 8 + (uint64_t)count * SRP_READ_BYTES != len)
        return;
    s.readDelay = p[6];
    s.readBalance = p[7];
    s.read.insert(s.read.end(), p + 8, p + len);
}

void Recorder::OnReplayScore(Connection& c, const uint8_t* p, uint32_t len)
{
    if (len < 4)
        return;
    Side& s = c.match->side[c.player - 1];
    s.score[0] = (uint16_t)Record_Get16(p);
    s.score[1] = (uint16_t)Record_Get16(p + 2);
    s.haveScore = true;
}

// How many of a player's frames came WHOLE: drawn, with their input and their hash.
uint32_t Recorder::Usable(const Side& s)
{
    if (!s.replay || !s.resume || s.inputsBroken)
        return 0;
    size_t n = s.offsets.size();
    n = std::min(n, s.inputs.size() / SRP_INPUT_BYTES);
    n = std::min(n, s.hashes.size());
    return (uint32_t)n;
}
// Every frame it drew can be played again.
bool Recorder::Whole(const Side& s)
{
    return !s.offsets.empty() && Usable(s) == s.offsets.size() &&
           s.inputs.size() == s.offsets.size() * SRP_INPUT_BYTES;
}

// The .SRP, from player 1's stream when it has one (the host, the top team), else
// player 2's. `last` = the match is over: the final score goes into META.
void Recorder::WriteReplay(Match& m, bool last)
{
    Side* s = nullptr;
    for (int i = 0; i < 2 && !s; i++)
        if (m.side[i].replay && !m.side[i].offsets.empty())
            s = &m.side[i];
    if (!s || m_dir.empty())
        return;
    std::vector<uint8_t> meta = s->meta;
    if (s->haveScore && meta.size() >= SRP_META_SCORE_OFF + 4)
    {
        Record_Put16(meta.data() + SRP_META_SCORE_OFF, s->score[0]);
        Record_Put16(meta.data() + SRP_META_SCORE_OFF + 2, s->score[1]);
    }
    SrpContent c{};
    c.meta = meta.data();
    c.metaLen = (uint32_t)meta.size();
    c.frms = s->frms.data();
    c.frmsLen = (uint32_t)s->frms.size();
    c.offsets = s->offsets.data();
    c.frameCount = (uint32_t)s->offsets.size();
    c.audio = s->audio.data();
    c.audioCount = (uint32_t)s->audio.size();
    c.chapters = s->chapters.data();
    c.chapterCount = (uint32_t)s->chapters.size();
    c.tickRate = s->tickRate;
    c.quirks = s->quirks;
    // A stream that was CUT -- the game closed, crashed or lost its connection, so no END
    // came -- has frames whose input or hash never arrived: the hashes travel in batches
    // and the last one was still in the game. Written as it stood, the whole match lost
    // its inputs for the sake of the last half second (8 of the first 153 recordings,
    // four of them full matches cut at the final whistle). So when the match is over and
    // this side is not whole, keep the frames that ARE whole -- drawn, with input and
    // hash -- and drop the few after them: a match that can be played again to within a
    // second of where it stopped. A whole stream is untouched, so its file is still the
    // host's own, byte for byte.
    if (last && !Whole(*s))
    {
        Side* o = s == &m.side[0] ? &m.side[1] : &m.side[0];
        if (o->replay && !o->offsets.empty() && (Whole(*o) || Usable(*o) > Usable(*s)))
        {
            s = o;                          // the other player's stream got further
            meta = s->meta;
            if (s->haveScore && meta.size() >= SRP_META_SCORE_OFF + 4)
            {
                Record_Put16(meta.data() + SRP_META_SCORE_OFF, s->score[0]);
                Record_Put16(meta.data() + SRP_META_SCORE_OFF + 2, s->score[1]);
            }
            c.meta = meta.data();
            c.metaLen = (uint32_t)meta.size();
            c.frms = s->frms.data();
            c.frmsLen = (uint32_t)s->frms.size();
            c.offsets = s->offsets.data();
            c.frameCount = (uint32_t)s->offsets.size();
            c.audio = s->audio.data();
            c.audioCount = (uint32_t)s->audio.size();
            c.chapters = s->chapters.data();
            c.chapterCount = (uint32_t)s->chapters.size();
            c.tickRate = s->tickRate;
            c.quirks = s->quirks;
        }
        uint32_t keep = Usable(*s);
        if (keep > 0 && keep < c.frameCount)
        {
            std::cout << Tag() << Hex64(m.id) << " player " << (s == &m.side[0] ? 1 : 2)
                      << "'s stream was cut: keeping the " << keep << " frames that came whole of "
                      << c.frameCount << "\n";
            c.frmsLen = s->offsets[keep];
            c.frameCount = keep;
            while (c.audioCount > 0 && s->audio[c.audioCount - 1].frame >= keep)
                c.audioCount--;
            while (c.chapterCount > 0 && s->chapters[c.chapterCount - 1].frame >= keep)
                c.chapterCount--;
        }
    }
    // Resumable when every frame has its input and its hash -- as the player's game
    // decides it (replay_v2.c write_recording), so the two files still agree.
    std::vector<uint32_t> hashes;
    if (s->resume && !s->inputsBroken && s->inputs.size() >= (size_t)c.frameCount * SRP_INPUT_BYTES &&
        (last ? true : s->inputs.size() == (size_t)c.frameCount * SRP_INPUT_BYTES) &&
        s->hashes.size() >= c.frameCount)
    {
        hashes.resize(c.frameCount);
        for (uint32_t f = 0; f < c.frameCount; f++)
            hashes[f] = (uint32_t)s->hashes[f];
        c.setup = s->setup.data();
        c.setupLen = (uint32_t)s->setup.size();
        c.kickoff = s->kickoff.data();
        c.kickoffLen = (uint32_t)s->kickoff.size();
        c.inputs = s->inputs.data();
        c.hashes = hashes.data();
        c.layout = s->layout;
        c.rules = s->rules;
        c.style = s->style;
        c.simWidth = s->resumeSimWidth;
    }
    // Who played (PLYR), as the players' games write it: player 1 (the host) first, each
    // named by its own HELLO or, if it did not record, by the other's.
    std::string names[2] = {
        m.side[0].present ? m.side[0].name : m.side[1].peer,
        m.side[1].present ? m.side[1].name : m.side[0].peer,
    };
    if (!names[0].empty() || !names[1].empty())
    {
        c.players[0] = names[0].c_str();
        c.players[1] = names[1].c_str();
    }
    // What each player saw, from each player's own stream: both halves in one file.
    SrpPrediction halves[2] = {};
    for (int i = 0; i < 2; i++)
    {
        const Side& h = m.side[i];
        if (h.pred.empty())
            continue;
        halves[i].pred = h.pred.data();
        halves[i].predFrames = (uint32_t)h.pred.size();
        halves[i].seen = h.seen.data();
        halves[i].seenLen = (uint32_t)h.seen.size();
        if (!h.read.empty())
        {
            halves[i].read = h.read.data();
            halves[i].readFrames = (uint32_t)(h.read.size() / SRP_READ_BYTES);
            halves[i].delay = h.readDelay;
            halves[i].balance = h.readBalance;
        }
        c.prediction[i] = &halves[i];
    }
    uint32_t len = 0;
    uint8_t* file = Srp_Build(&c, &len);
    if (!file)
        return;
    std::string path = m_dir + "/" + m.stamp + "-" + Hex64(m.id).substr(0, 8) + ".SRP";
    std::string tmp = path + ".part";
    FILE* f = fopen(tmp.c_str(), "wb");
    bool ok = f && fwrite(file, 1, len, f) == len;
    if (f)
        ok = (fclose(f) == 0) && ok;
    free(file);
    // Written whole, then renamed over the last one: a reader never sees half a file.
    if (ok && rename(tmp.c_str(), path.c_str()) == 0)
    {
        m.recording = path;
        s->writtenFrames = c.frameCount;
        if (last)
            std::cout << Tag() << Hex64(m.id) << " wrote " << path << " (" << c.frameCount
                      << " frames, " << len << " bytes)\n";
    }
    else
    {
        std::cout << Tag() << Hex64(m.id) << " cannot write " << path << "\n";
        remove(tmp.c_str());
    }
}

void Recorder::CloseConnection(Connection& c, const char* why)
{
    if (c.fd >= 0)
        close(c.fd);
    c.fd = -1;
    if (!c.match)
        return;
    Side& s = c.match->side[c.player - 1];
    s.open = false;
    c.match->lastMs = ServerTime::NowMs();
    std::cout << Tag() << Hex64(c.match->id) << " player " << c.player << " stream " << why
              << " after " << s.hashes.size() << " frames"
              << (s.ended ? (s.outcome == 0 ? ", the match finished" : ", ended early: " + s.why)
                          : std::string(", with no END"))
              << "\n";
    c.match = nullptr;
}

void Recorder::Finish(Match& m, const char* why)
{
    // A connection still pointing at it (only when the server stops) lets go first.
    for (auto& c : m_connections)
        if (c->match == &m)
            c->match = nullptr;

    WriteReplay(m, true);

    const Side& a = m.side[0];
    const Side& b = m.side[1];
    bool both = a.present && b.present;
    std::string finalAgree = "null";
    if (both && a.ended && b.ended && a.endFrame == b.endFrame)
        finalAgree = a.finalHash == b.finalHash ? "true" : "false";

    std::cout << Tag() << Label(m) << " recorded (" << why << "): "
              << (both ? std::to_string(m.compared) + " frames compared, " : std::string("one side only, "))
              << (m.desyncFrame >= 0 ? "DESYNC at frame " + std::to_string(m.desyncFrame)
                                     : std::string(both ? "no desync" : "nothing to compare with"))
              << "\n";

    auto sideJson = [](int player, const Side& s) {
        if (!s.present)
            return std::string("null");
        return "{\"player\":" + std::to_string(player)
            + ",\"name\":" + JsonString(s.name)
            + ",\"peer\":" + JsonString(s.peer)          // the other player, as this one named them
            + ",\"address\":" + JsonString(s.address)
            + ",\"build\":" + JsonString(s.build)
            + ",\"platform\":" + JsonString(s.platform)
            + ",\"frames\":" + std::to_string(s.hashes.size())
            + ",\"replayFrames\":" + std::to_string(s.offsets.size())
            + ",\"ended\":" + (s.ended ? "true" : "false")
            + ",\"outcome\":" + JsonString(!s.ended ? "none" : s.outcome == 0 ? "finished" : "ended early")
            + ",\"endFrame\":" + std::to_string(s.endFrame)
            + ",\"finalSimHash\":\"" + Hex64(s.finalHash) + "\""
            + ",\"why\":" + JsonString(s.why)
            + "}";
    };
    const Side& any = a.present ? a : b;
    m_ledger.Write("{\"kind\":\"match\",\"match\":\"" + Hex64(m.id) + "\""
        + ",\"started\":" + JsonString(m.started)
        + ",\"closed\":" + JsonString(ServerTime::WallClock())
        + ",\"why\":" + JsonString(why)
        + ",\"teams\":[" + JsonString(any.team1) + "," + JsonString(any.team2) + "]"
        + ",\"gameplay\":" + std::to_string(any.gameplay)
        + ",\"delay\":" + std::to_string(any.delay)
        + ",\"simWidth\":" + std::to_string(any.simWidth)
        + ",\"relay\":" + (any.relayToken ? "\"" + Hex64(any.relayToken) + "\"" : std::string("null"))
        + ",\"players\":[" + sideJson(1, a) + "," + sideJson(2, b) + "]"
        + ",\"compared\":" + std::to_string(m.compared)
        + ",\"desync\":" + (m.desyncFrame < 0 ? std::string("null")
            : "{\"frame\":" + std::to_string(m.desyncFrame) + ",\"at\":" + JsonString(m.desyncAt)
              + ",\"hashes\":[\"" + Hex64(m.desyncHash[0]) + "\",\"" + Hex64(m.desyncHash[1]) + "\"]}")
        + ",\"finalAgree\":" + finalAgree
        + ",\"recording\":" + (m.recording.empty() ? std::string("null") : JsonString(m.recording))
        + "}");
    WriteMatchCard(m, why);
    m_matches.erase(m.id);
}

// The match's card: <recording>.json beside it, for the playback page. Names, teams,
// score, builds and how it ended -- who played whom, never where from (no addresses:
// the page is public). scripts/recordings_index.py writes the same card from the ledger
// for recordings made before this, and the same index.
void Recorder::WriteMatchCard(const Match& m, const std::string& why)
{
    if (m.recording.empty() || m_dir.empty())
        return;
    const Side& a = m.side[0];
    const Side& b = m.side[1];
    const Side& any = a.present ? a : b;
    const Side& scored = a.haveScore ? a : b;
    // A player who did not record ([netplay] record off, or an older build) never streams:
    // their name still comes from the other one's HELLO, which names both.
    auto player = [](int n, const Side& s, const Side& other) {
        if (!s.present)
            return other.present && !other.peer.empty()
                ? "{\"player\":" + std::to_string(n) + ",\"name\":" + JsonString(other.peer)
                  + ",\"recorded\":false}"
                : std::string("null");
        return "{\"player\":" + std::to_string(n) + ",\"name\":" + JsonString(s.name)
            + ",\"build\":" + JsonString(s.build) + ",\"platform\":" + JsonString(s.platform)
            + ",\"outcome\":" + JsonString(!s.ended ? "none" : s.outcome == 0 ? "finished" : "ended early")
            + "}";
    };
    std::string file = m.recording.substr(m.recording.find_last_of('/') + 1);
    std::string card = "{\"file\":" + JsonString(file)
        + ",\"match\":\"" + Hex64(m.id) + "\""
        + ",\"started\":" + JsonString(m.started)
        + ",\"teams\":[" + JsonString(any.team1) + "," + JsonString(any.team2) + "]"
        + ",\"score\":" + (scored.haveScore ? "[" + std::to_string(scored.score[0]) + ","
                                                  + std::to_string(scored.score[1]) + "]"
                                               : std::string("null"))
        + ",\"players\":[" + player(1, a, b) + "," + player(2, b, a) + "]"
        + ",\"frames\":" + std::to_string(any.offsets.size())
        + ",\"gameplay\":" + std::to_string(any.gameplay)
        + ",\"relayed\":" + (any.relayToken ? "true" : "false")
        + ",\"desync\":" + (m.desyncFrame < 0 ? std::string("null") : std::to_string(m.desyncFrame))
        + ",\"why\":" + JsonString(why)
        + "}\n";
    std::string path = m.recording.substr(0, m.recording.size() - 4) + ".json";
    std::string tmp = path + ".part";
    FILE* f = fopen(tmp.c_str(), "wb");
    bool ok = f && fwrite(card.data(), 1, card.size(), f) == card.size();
    if (f)
        ok = (fclose(f) == 0) && ok;
    if (!ok || rename(tmp.c_str(), path.c_str()) != 0)
    {
        std::cout << Tag() << Hex64(m.id) << " cannot write " << path << "\n";
        remove(tmp.c_str());
        return;
    }
    WriteIndex();
}

// index.json: every card in the folder, newest first -- the cards' own bytes, so no
// JSON is parsed here. A card whose .SRP has gone is left out.
void Recorder::WriteIndex()
{
    DIR* d = opendir(m_dir.c_str());
    if (!d)
        return;
    std::vector<std::string> names;
    while (dirent* e = readdir(d))
    {
        std::string n = e->d_name;
        if (n.size() > 5 && n.compare(n.size() - 5, 5, ".json") == 0 && n != "index.json")
            names.push_back(n);
    }
    closedir(d);
    std::sort(names.rbegin(), names.rend());
    std::string out = "[\n";
    bool first = true;
    for (const std::string& n : names)
    {
        struct stat st;
        std::string srp = m_dir + "/" + n.substr(0, n.size() - 5) + ".SRP";
        if (stat(srp.c_str(), &st) != 0)
            continue;
        std::ifstream in(m_dir + "/" + n, std::ios::binary);
        std::stringstream ss;
        ss << in.rdbuf();
        std::string card = ss.str();
        while (!card.empty() && (card.back() == '\n' || card.back() == '\r' || card.back() == ' '))
            card.pop_back();
        if (card.empty() || card[0] != '{')
            continue;
        out += (first ? "" : ",\n") + card;
        first = false;
    }
    out += "\n]\n";
    std::string path = m_dir + "/index.json", tmp = path + ".part";
    FILE* f = fopen(tmp.c_str(), "wb");
    bool ok = f && fwrite(out.data(), 1, out.size(), f) == out.size();
    if (f)
        ok = (fclose(f) == 0) && ok;
    if (!ok || rename(tmp.c_str(), path.c_str()) != 0)
        remove(tmp.c_str());
}

// ---- LIVE CAST: viewers (record_proto.h RECORD_WATCH_LIST, RECORD_WATCH) ----

namespace {
void PutString(std::vector<uint8_t>& v, const std::string& str)
{
    size_t n = str.size() > 255 ? 255 : str.size();
    v.push_back((uint8_t)n);
    v.insert(v.end(), str.begin(), str.begin() + n);
}
}

const Recorder::Side* Recorder::WatchedSide(const Match& m, int* which)
{
    // A match either player keeps from viewers ([netplay] live_cast = off) is never listed or sent.
    if ((m.side[0].present && m.side[0].noWatch) || (m.side[1].present && m.side[1].noWatch))
        return nullptr;
    for (int i = 0; i < 2; i++)                     // the host's stream when it has one
        if (m.side[i].replay && !m.side[i].offsets.empty())
        {
            *which = i;
            return &m.side[i];
        }
    return nullptr;
}

void Recorder::Queue(Connection& c, uint8_t type, const std::vector<uint8_t>& payload)
{
    uint8_t h[RECORD_HEADER_LEN];
    Record_PutHeader(h, type, (uint32_t)payload.size());
    c.out.insert(c.out.end(), h, h + RECORD_HEADER_LEN);
    c.out.insert(c.out.end(), payload.begin(), payload.end());
}

bool Recorder::Flush(Connection& c)
{
    while (!c.out.empty())
    {
        ssize_t n = send(c.fd, c.out.data(), c.out.size(), MSG_NOSIGNAL);
        if (n > 0)
        {
            c.out.erase(c.out.begin(), c.out.begin() + n);
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR))
            return true;                            // the rest at the next pass
        return false;
    }
    return true;
}

bool Recorder::OnWatch(Connection& c, uint8_t type, const uint8_t* p, uint32_t len)
{
    if (len < 5 || memcmp(p, RECORD_VIEWER_MAGIC, 4) != 0)
        return false;
    c.viewer = true;
    if (type == RECORD_WATCH_LIST)
    {
        std::vector<uint8_t> v(2, 0);
        uint32_t count = 0;
        for (auto& entry : m_matches)
        {
            const Match& m = *entry.second;
            int which = 0;
            const Side* s = WatchedSide(m, &which);
            if (!s || s->ended || count == 255)
                continue;
            uint8_t b[12];
            Record_Put64(b, m.id);
            Record_Put32(b + 8, (uint32_t)s->offsets.size());
            v.insert(v.end(), b, b + 12);
            PutString(v, m.side[0].present ? m.side[0].name : m.side[1].peer);
            PutString(v, m.side[1].present ? m.side[1].name : m.side[0].peer);
            PutString(v, s->team1);
            PutString(v, s->team2);
            count++;
        }
        Record_Put16(v.data(), count);
        Queue(c, RECORD_WATCH_MATCHES, v);
        c.closing = true;
        std::cout << Tag() << "a viewer (" << c.address << ") listed " << count << " live match(es)\n";
        return true;
    }
    if (len < 13)
        return false;
    c.watchId = Record_Get64(p + 5);
    if (c.watchId == 0)                             // the one recorded the longest
    {
        size_t best = 0;
        for (auto& entry : m_matches)
        {
            int which = 0;
            const Side* s = WatchedSide(*entry.second, &which);
            if (s && !s->ended && s->offsets.size() > best)
            {
                best = s->offsets.size();
                c.watchId = entry.first;
            }
        }
    }
    std::cout << Tag() << "a viewer (" << c.address << ") watches " << Hex64(c.watchId) << "\n";
    return true;
}

void Recorder::ServeViewer(Connection& c)
{
    auto it = c.watchId ? m_matches.find(c.watchId) : m_matches.end();
    const Side* s = nullptr;
    if (it != m_matches.end())
    {
        int which = -1;
        if (c.watchSide >= 0)
        {
            if (it->second->side[c.watchSide].replay)
                s = &it->second->side[c.watchSide];
        }
        else if ((s = WatchedSide(*it->second, &which)) != nullptr)
            c.watchSide = which;
    }
    if (it == m_matches.end() || (!s && c.metaSent))
    {
        std::vector<uint8_t> end(13, 0);
        end[0] = c.metaSent ? 0 : 2;                // 2: no such match (or none is live)
        Record_Put32(end.data() + 1, c.sentFrames);
        Queue(c, RECORD_END, end);
        c.closing = true;
        return;
    }
    if (!s)
        return;                                     // its stream has not begun: wait
    if (!c.metaSent)
    {
        std::vector<uint8_t> v(4);
        Record_Put16(v.data(), s->tickRate);
        Record_Put16(v.data() + 2, s->quirks);
        v.insert(v.end(), s->meta.begin(), s->meta.end());
        Queue(c, RECORD_REPLAY_META, v);
        c.metaSent = true;
    }
    // The frames since the last pass, a batch at most kViewerBatch -- with the sounds and the
    // chapters they played -- while the viewer keeps up (no more than ~1 MB waiting).
    const uint32_t kViewerBatch = 256;
    while (c.sentFrames < s->offsets.size() && c.out.size() < (1u << 20))
    {
        uint32_t first = c.sentFrames;
        uint32_t upto = (uint32_t)s->offsets.size();
        if (upto - first > kViewerBatch)
            upto = first + kViewerBatch;
        uint32_t from = s->offsets[first];
        uint32_t to = upto < s->offsets.size() ? s->offsets[upto] : (uint32_t)s->frms.size();
        std::vector<uint8_t> v(6);
        Record_Put32(v.data(), first);
        Record_Put16(v.data() + 4, upto - first);
        v.insert(v.end(), s->frms.begin() + from, s->frms.begin() + to);
        uint32_t a0 = c.sentAudio, a1 = a0;
        while (a1 < s->audio.size() && s->audio[a1].frame < upto)
            a1++;
        uint8_t b[8];
        Record_Put16(b, a1 - a0);
        v.insert(v.end(), b, b + 2);
        for (uint32_t k = a0; k < a1; k++)
        {
            Record_Put32(b, s->audio[k].frame);
            b[4] = s->audio[k].kind;
            b[5] = s->audio[k].channel;
            Record_Put16(b + 6, s->audio[k].id);
            v.insert(v.end(), b, b + 8);
        }
        uint32_t c0 = c.sentChapters, c1 = c0;
        while (c1 < s->chapters.size() && s->chapters[c1].frame < upto)
            c1++;
        Record_Put16(b, c1 - c0);
        v.insert(v.end(), b, b + 2);
        for (uint32_t k = c0; k < c1; k++)
        {
            Record_Put32(b, s->chapters[k].frame);
            b[4] = s->chapters[k].kind;
            b[5] = s->chapters[k].team;
            Record_Put16(b + 6, s->chapters[k].value);
            v.insert(v.end(), b, b + 8);
        }
        Record_Put16(b, 0);                         // (no inputs: a viewer only watches)
        v.insert(v.end(), b, b + 2);
        Queue(c, RECORD_REPLAY_FRAMES, v);
        c.sentFrames = upto;
        c.sentAudio = a1;
        c.sentChapters = c1;
    }
    if (s->ended && c.sentFrames == s->offsets.size())
    {
        if (s->haveScore)
        {
            std::vector<uint8_t> v(4);
            Record_Put16(v.data(), s->score[0]);
            Record_Put16(v.data() + 2, s->score[1]);
            Queue(c, RECORD_REPLAY_SCORE, v);
        }
        std::vector<uint8_t> end(13, 0);
        end[0] = (uint8_t)(s->outcome < 0 ? 1 : s->outcome);
        Record_Put32(end.data() + 1, c.sentFrames);
        Queue(c, RECORD_END, end);
        c.closing = true;
    }
}

#endif

