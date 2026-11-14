// OpenSWOS: the team check (include/team_proto.h, docs/TEAM_IDS.md section 5).
//
// Before an online match each game asks whether the two teams are canonical: every
// team has an id and a checksum of what it plays by (positions, skills, values), and
// this is the list of both that no player can edit. A game that is told one of them is
// not on it does not start the match.
//
// The list is compiled in (TeamRegistry.inc, written by scripts/assign_team_uuids.py
// --registry from the game's own team database), so updating the server updates it;
// `--teams FILE` adds more, one "uuid checksum name" a line -- for teams published
// after the build (downloadable ones).
//
// It answers a datagram on the matchmaker's own UDP port, told apart by its magic and
// exact length, as the relay's control packets are; main.cpp's intercept tries this
// desk first and the relay second.
#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <string>

#include <enet/enet.h>

#include "Ledger.h"

class TeamDesk
{
public:
    TeamDesk(ENetHost* host, Ledger& ledger, const std::string& extraFile);

    // 1 if the datagram was a team check (and has been answered).
    int OnDatagram(const ENetAddress& from, const uint8_t* data, size_t len);

    size_t Teams() const { return m_teams.size(); }

private:
    struct Team { std::array<uint8_t, 8> checksum; std::string name; bool hasExtras = false;
                  std::array<uint8_t, 8> extras{}; };   // (its players 17..: TEAMCHECK_PROTOCOL_EXTRAS)
    using Id = std::array<uint8_t, 16>;

    bool Add(const std::string& uuid, const std::string& checksum, const std::string& name);
    bool AddExtras(const std::string& uuid, const std::string& checksum);

    ENetHost* m_host;
    Ledger&   m_ledger;
    std::map<Id, Team> m_teams;
    uint64_t  m_asked = 0, m_refused = 0;
};
