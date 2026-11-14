// OpenSWOS: the team check. TeamDesk.h says what it is for.
#include "TeamDesk.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>

#include "team_proto.h"

namespace
{
    struct Row { const char* uuid; const char* checksum; const char* name; };
    const Row kRegistry[] = {
#include "TeamRegistry.inc"
#include "TeamRegistryPacks.inc"
    };
    // A world's clubs' players 17.. (a long bench brings them on): its id, the checksum of those
    // players in their order. team_proto.h, TEAMCHECK_PROTOCOL_EXTRAS.
    struct ExtrasRow { const char* uuid; const char* checksum; };
    const ExtrasRow kExtras[] = {
#include "TeamRegistryExtras.inc"
        {nullptr, nullptr}
    };

    std::string Hex(const uint8_t* bytes, size_t count)
    {
        static const char digits[] = "0123456789abcdef";
        std::string out;
        for (size_t i = 0; i < count; i++)
        {
            out += digits[bytes[i] >> 4];
            out += digits[bytes[i] & 15];
        }
        return out;
    }

    std::string Uuid(const uint8_t* b)
    {
        std::string h = Hex(b, 16);
        return h.substr(0, 8) + "-" + h.substr(8, 4) + "-" + h.substr(12, 4) + "-" + h.substr(16, 4) + "-" +
               h.substr(20, 12);
    }

    std::string Address(const ENetAddress& a)
    {
        char ip[64] = "?";
        enet_address_get_host_ip(&a, ip, sizeof ip);
        return std::string(ip) + ":" + std::to_string(a.port);
    }
}

TeamDesk::TeamDesk(ENetHost* host, Ledger& ledger, const std::string& extraFile) :
    m_host(host),
    m_ledger(ledger)
{
    for (const Row& row : kRegistry)
        Add(row.uuid, row.checksum, row.name);
    size_t extras = 0;
    for (const ExtrasRow& row : kExtras)
        if (row.uuid && AddExtras(row.uuid, row.checksum))
            extras++;
    size_t builtIn = m_teams.size();

    if (!extraFile.empty())
    {
        std::ifstream in(extraFile);
        std::string line;
        size_t bad = 0;
        while (in && std::getline(in, line))
        {
            std::istringstream fields(line);
            std::string uuid, checksum, name;
            if (!(fields >> uuid) || uuid[0] == '#')
                continue;
            fields >> checksum;
            std::getline(fields, name);
            if (!name.empty() && name[0] == ' ')
                name.erase(0, 1);
            if (!Add(uuid, checksum, name))
                bad++;
        }
        std::cout << "[teams] " << extraFile << ": " << (m_teams.size() - builtIn) << " more teams"
                  << (bad ? ", " + std::to_string(bad) + " lines not understood" : "")
                  << (in.eof() ? "" : " (could not read it)") << "\n";
    }
    std::cout << "[teams] " << m_teams.size() << " canonical teams (" << extras
              << " with their players 17..): a game may ask whether its teams are on the list\n";
}

bool TeamDesk::Add(const std::string& uuid, const std::string& checksum, const std::string& name)
{
    Id id;
    Team team;
    if (TeamCheck_Hex(uuid.c_str(), id.data(), 16) != 0 || TeamCheck_Hex(checksum.c_str(), team.checksum.data(), 8) != 0)
        return false;
    team.name = name;
    m_teams[id] = team;
    return true;
}

bool TeamDesk::AddExtras(const std::string& uuid, const std::string& checksum)
{
    Id id;
    std::array<uint8_t, 8> sum;
    if (TeamCheck_Hex(uuid.c_str(), id.data(), 16) != 0 || TeamCheck_Hex(checksum.c_str(), sum.data(), 8) != 0)
        return false;
    auto it = m_teams.find(id);
    if (it == m_teams.end())
        return false;
    it->second.hasExtras = true;
    it->second.extras = sum;
    return true;
}

int TeamDesk::OnDatagram(const ENetAddress& from, const uint8_t* data, size_t len)
{
    TeamCheck c;
    if (len > TEAMCHECK_MAX_LEN || TeamCheck_Read(data, (int32_t)len, &c) != 0 || c.type != TEAMCHECK_ASK)
        return 0;

    for (int i = 0; i < c.count; i++)
    {
        Id id;
        memcpy(id.data(), c.team[i].uuid, 16);
        auto it = m_teams.find(id);
        const bool extras = c.protocol == TEAMCHECK_PROTOCOL_EXTRAS;
        m_asked++;
        if (it == m_teams.end() || (extras && !it->second.hasExtras))
            c.team[i].status = TEAMCHECK_UNKNOWN;
        else if (memcmp((extras ? it->second.extras : it->second.checksum).data(), c.team[i].checksum, 8) != 0)
            c.team[i].status = TEAMCHECK_CHANGED;
        else
            c.team[i].status = TEAMCHECK_OK;

        if (c.team[i].status != TEAMCHECK_OK)
        {
            // Worth a line and a ledger entry: it is either a doctored team or a
            // database this server has not been told about.
            m_refused++;
            const std::string uuid = Uuid(c.team[i].uuid), sum = Hex(c.team[i].checksum, 8);
            const bool unknown = c.team[i].status == TEAMCHECK_UNKNOWN;
            if (extras && unknown && it != m_teams.end())
            {   // (a list without its players 17..: unverified on the game's side, not refused)
                m_refused--;
                std::cout << "[teams] " << Address(from) << ": " << it->second.name
                          << "'s players 17.. are not on this list\n";
                continue;
            }
            std::cout << "[teams] " << Address(from) << ": REFUSED " << uuid << (extras ? " players 17.." : "")
                      << " (checksum " << sum << "): "
                      << (unknown ? "no team with that id"
                                  : it->second.name + " has checksum " +
                                        Hex((extras ? it->second.extras : it->second.checksum).data(), 8))
                      << "\n";
            std::ostringstream json;
            json << "{\"type\":\"teamcheck\",\"at\":\"" << ServerTime::WallClock() << "\",\"from\":\"" << Address(from)
                 << "\",\"uuid\":\"" << uuid << "\",\"checksum\":\"" << sum << "\",\"why\":\""
                 << (unknown ? "unknown" : extras ? "extras changed" : "changed") << "\"}";
            m_ledger.Write(json.str());
        }
    }

    c.type = TEAMCHECK_ANSWER;
    uint8_t out[TEAMCHECK_MAX_LEN];
    ENetBuffer buffer;
    buffer.data = out;
    buffer.dataLength = (size_t)TeamCheck_Write(out, &c);
    enet_socket_send(m_host->socket, &from, &buffer, 1);
    return 1;
}
