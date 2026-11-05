#pragma once
// OpenSWOS: match reports (not in upstream). include/report_proto.h in the game tree
// has the protocol, docs/NETPLAY_REPORT.md the reasons.
//
// After an online match each player's game sends one zip -- both players' SWOS.log and
// their HOW WAS THE MATCH? answers -- on the matchmaker's TCP port. The TCP gate
// (WebGate.h) hands a connection that begins "SWMR" here, and it is filed by match id:
//
//     <dir>/<match id>/player1.zip, player2.zip
//
// with a "report" line in the ledger saying who, the rating and the comment. Both
// players send one, so a match is covered even when one of them never gets through.
//
// One thread, polled from the main loop like the recorder: no locks, nothing blocks.
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include "Ledger.h"

class ReportDesk
{
public:
    // Keeps reports in `dir` when `enabled` (--reports DIR).
    ReportDesk(bool enabled, Ledger& ledger, const std::string& dir);
    ~ReportDesk();

    bool Enabled() const { return m_enabled; }

    // A connection the TCP gate accepted that begins "SWMR".
    void Adopt(int fd, const std::string& address);

    // Once per main-loop pass: read what arrived, keep what is whole, answer, close.
    void Update();
    bool Busy() const { return !m_connections.empty(); }    // an upload under way (TCP)

private:
    struct Connection
    {
        int fd = -1;
        std::string address;
        std::vector<uint8_t> in;
        uint32_t sinceMs = 0;
        size_t need = 0;            // the whole message's length, once its head is in
    };

    bool Read(Connection& c);       // false = done with it
    bool Keep(Connection& c);       // the whole message is in: file it
    void Answer(Connection& c, uint8_t status);

    bool m_enabled = false;
    Ledger& m_ledger;
    std::string m_dir;
    std::vector<std::unique_ptr<Connection>> m_connections;
};
