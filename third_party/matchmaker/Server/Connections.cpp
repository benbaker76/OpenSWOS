#include "Connections.h"
#include "../../../include/project_identity.h"   // PROJECT_NETPLAY_TAG: OpenSWOS's (a fork's own)
#include "User.h"
#include "Message.h"
#include "Sender.h"
#include "Utils.h"
#include "States.h"
#include <algorithm>
#include <iostream>
#include "Sender.h"
#include "UserChangedStateVisitor.h"
#include "Relay.h"

Connections::Connections(uint16_t port) : 
    address{ ENET_HOST_ANY,port },
    m_host(enet_host_create(&address, ENET_PROTOCOL_MAXIMUM_PEER_ID, 0, 0, 0), enet_host_destroy)
{  
    if (!m_host) {
        throw "An error occurred while trying to create an ENet local.\n";
    }
}

void Connections::OnUserChangeState(class User* user)
{
    UserChangedStateVisitor v(*this);
    user->Visit(v);
}

void Connections::NewConnection(ENetPeer* peer)
{
    if (!m_users.count(peer))
        m_users.insert({ peer, std::make_unique<User>(peer,this) });

    std::cout << "added user, currently " << m_users.size() << " connected\n";
}

void Connections::LostConnection(ENetPeer* peer)
{
    if (m_users.count(peer) == 0)
        return;
    auto& u = m_users.at(peer);

    RemoveUserFromAnyGames(u.get());

    m_users.erase(peer);
    
    BroadcastMessage(Message::Make(MessageType::Info, "Player disconnected"));
    BroadcastActiveUsers();
    std::cout << "removed user, currently " << m_users.size() << " connected\n";
}

void Connections::RemoveUserFromAnyGames(User* user)
{
    auto it = std::find_if(RANGE(m_games), [&](const Game& g) {return g.WasCreatedBy(user); });
    if (it != std::end(m_games))
    {
        it->KillGame();
        Message::Make(MessageType::Leave, "").OnData(SendTo(user->Peer()));
        m_games.erase(it);
        std::cout << "Closed game created by " << user->Name() << "\n";
    }
    else
    {
        for (auto& game : m_games)
        {
            if (game.RemoveJoinedOrPending(user))
            {
                Message::Make(MessageType::Leave, "").OnData(SendTo(user->Peer()));
                return;
            }
        }
    }
}

void Connections::ReceiveMessage(ENetPeer* peer, const unsigned char* data, size_t len)
{
    if (!m_users.count(peer))
        return;
    auto& user = m_users.at(peer);
    try
    {
        user->OnMessage(Message::Parse(data, len));
    }
    catch (BadMessageException&)
    {
        // Tell the client their message was bogus, then kick them off. Should never happen if the client works properly
        user->DisconnectUser("Badly formed message.");
    }

}



bool Connections::VerifyName(const std::string& name)
{
    auto success = std::count_if(m_users.begin(), m_users.end(), [&](auto& p) {
        return p.second->Name() == name; }) == 0;
     
    return success;
}

bool Connections::VerifyVersion(const std::string& version)
{
    // TODO check vesion
    return true;
}

class IsUserActive
{
    bool m_bActive;
public:
    bool operator()() const {return m_bActive; }
    void Visit(const JoinedOpenGame&){ m_bActive = true; }
    void Visit(const OpenedGameState&){ m_bActive = true; }
    void Visit(const LoggedInState&){ m_bActive = true; }
    void Visit(const PendingJoinState&) { m_bActive = true; }
    void Visit(const WatingForLocalIPState&){ m_bActive = false; }
    void Visit(const WatingForLoginState&){ m_bActive = false; }
    void Visit(const WatingForVersionState&){ m_bActive = false; }
    void Visit(const KickedOffState&){ m_bActive = false; }

};

class ActiveUsersListMaker
{
public:
    std::vector<User*> m_ActiveUsers;
    template <class T> void Visit(const T& s) {
        IsUserActive isActive;
        isActive.Visit(s);
        if(isActive())
            m_ActiveUsers.push_back(s.m_user);
    };
};

std::vector<User*> Connections::ActivePlayers() const
{
    ActiveUsersListMaker v;
    return VisitMap(m_users, v).m_ActiveUsers;
}

std::vector<ENetPeer*> AsPeers(const std::vector<User*>& users)
{
    std::vector<ENetPeer*> peers(users.size());
    std::transform(users.begin(), users.end(), peers.begin(), [](User* u) {return u->Peer(); });
    return peers;
}

void Connections::BroadcastMessage(const Message& m) const
{
    auto activeUsers = AsPeers(ActivePlayers());
    m.OnData(SendTo(activeUsers));
    std::cout << "Broadcast message: " << (int)m.Type() << "\n";
}
void Connections::BroadcastActiveUsers() const
{
    auto activeUsers = ActivePlayers();

    std::string userList;
    userList += std::to_string(activeUsers.size())+":";
    for (const auto& u : activeUsers)
        userList += u->Name() + ":" + std::to_string(u->Data().size())+":"+u->Data();

    BroadcastMessage(Message::Make(MessageType::PlayersActive, userList));
    BroadcastOpenGames();
}
// OpenSWOS: who can play whom, from the VERSION: tag. OPENSWOS/<n> and OPENSWOS-WEB/<n>
// play any OpenSWOS with the same <n> (NETPLAY_COMPAT, include/netplay_compat.h): the
// games' handshake refuses any other, so a player is never shown a game they cannot
// join. An OpenSWOS from before the number ("OPENSWOS", its handshake compares the
// commit) plays only those; any other client (SWOS 2020's) only its own tag.
static std::string PlayKey(const std::string& version)
{
    const std::string ours = PROJECT_NETPLAY_TAG;
    if (version.compare(0, ours.size(), ours) != 0)
        return version;
    const auto slash = version.rfind('/');
    return slash == std::string::npos ? ours : ours + "/" + version.substr(slash + 1);
}

void Connections::BroadcastOpenGames() const
{
    // Each player gets the games they could play.
    for (User* u : ActivePlayers())
    {
        const std::string key = PlayKey(u->Version());
        std::string gamelist;
        for (const auto& g : m_games)
            if (PlayKey(g.CreatedBy()->Version()) == key)
                gamelist += g.ShortInfo() + ",";
        Message::Make(MessageType::GamesOpen, gamelist).OnData(SendTo(u->Peer()));
    }
    std::cout << "Broadcast message: " << (int)MessageType::GamesOpen << "\n";
}
void Connections::Update()
{
   
}
bool Connections::OpenGame(User* creator, int min_players, int max_players)
{
    m_games.push_back(Game(creator, min_players, max_players));
    return true;
}

bool Connections::RequestToJoin(User* requestor, const std::string& nameOfGame) 
{
    auto it  = std::find_if(RANGE(m_games), [&](const Game& g) {return g.WasCreatedBy(nameOfGame); });
    if (it == std::end(m_games))
        return false;
    // Not a game this player was shown (BroadcastOpenGames): the two would not play.
    if (PlayKey(it->CreatedBy()->Version()) != PlayKey(requestor->Version()))
        return false;
    auto joined = it->RequestUserJoin(requestor);
    if (joined)
    {
        auto msg = Message::Make(MessageType::Join, requestor->Name()); 
        msg.OnData(SendTo(it->CreatedBy()->Peer()));
        msg.OnData(SendTo(requestor->Peer()));
    }
    return joined;
}

User* Connections::UserByName(const std::string& name) 
{
    auto users = ActivePlayers();
    for (auto it : users)
        if (it->Name() == name)
            return it;

    return nullptr;
}

// owner wants to remove the player specified in the message
void Connections::Eject(User* owner, const std::string& other) 
{
    auto p = UserByName(other);
    if (!p)
        return;
    auto it = std::find_if(RANGE(m_games), [&](const Game& g) {return g.WasCreatedBy(owner); });
    if (it == std::end(m_games))
        return;

    if (it->RemoveJoinedOrPending(p)) {
        Message::Make(MessageType::Eject, owner->Name()).OnData(SendTo(p->Peer()));
        p->ChangeState<LoggedInState>(p);
    }
}

bool Connections::Approve(User* owner, const std::string& other) 
{
    auto it = std::find_if(RANGE(m_games), [&](const Game& g) {return g.WasCreatedBy(owner); });
    if (it == std::end(m_games))
        return false;
   return it->Approve(other);
}

bool Connections::StartGame(User* owner)
{
    auto it = std::find_if(RANGE(m_games), [&](const Game& g) {return g.WasCreatedBy(owner); });
    if (it == std::end(m_games))
        return false;
    bool canStart = it->CanStart();
    
    if (canStart)
    {
        User* p2 = it->FirstJoiner();
        std::string gameInfo1 = std::string("1") + "," + p2->Name() + "," + ToString(p2->Peer()->address);
        for(const auto& ip : p2->LocalIPs())
            gameInfo1 += "," + ToString(ip);
        std::string gameInfo2 = std::string("2") + "," + owner->Name() + "," + ToString(owner->Peer()->address);// +"," + ToString(owner->LocalIP());
        for (const auto& ip : owner->LocalIPs())
            gameInfo2 += "," + ToString(ip);
        std::cout << "Users starting a game, sending\n";
        // OpenSWOS: a relayed match (Relay.h). Only between two OpenSWOS
        // clients -- the SWOS 2020 client's parser throws on a header it does not
        // know, and could not use a relay anyway. RELAY: goes first, on the same
        // reliable channel, so it is in hand when START arrives.
        // A browser (VERSION OPENSWOS-WEB, through the WebSocket gate) can reach
        // nothing but this server, so its match is relayed whether or not the server
        // relays everyone's.
        const std::string ours = PROJECT_NETPLAY_TAG;
        const std::string web = PROJECT_NETPLAY_TAG "-WEB";
        bool anyWeb = owner->Version().compare(0, web.size(), web) == 0 ||
                      p2->Version().compare(0, web.size(), web) == 0;
        if (m_relay && (m_relay->Enabled() || (anyWeb && m_relay->ServesWeb())) &&
            owner->Version().compare(0, ours.size(), ours) == 0 &&
            p2->Version().compare(0, ours.size(), ours) == 0)
        {
            uint64_t token = m_relay->CreateSession(owner->Name(), owner->Peer()->address,
                                                    p2->Name(), p2->Peer()->address, anyWeb);
            if (token)
            {
                char hex[17];
                snprintf(hex, sizeof hex, "%016llx", (unsigned long long)token);
                SendTo(owner->Peer())(std::string(RELAY_MM_HEADER) + hex + ",1");
                SendTo(p2->Peer())(std::string(RELAY_MM_HEADER) + hex + ",2");
            }
        }
        // OpenSWOS, --relay-fallback only (off by default: the bandwidth is the
        // server's): not relayed, but a session in case the two cannot punch to each
        // other -- a VPN, or a router that gives every destination its own port. The
        // game punches direct and moves to it only when no round trip works after a few
        // seconds (p2p_link.c, relay_fallback). A different header, RELAYFB:, so a game
        // from before it ignores it (an unknown header is skipped) and plays as it did.
        else if (m_relay && m_relay->Fallback() &&
                 owner->Version().compare(0, ours.size(), ours) == 0 &&
                 p2->Version().compare(0, ours.size(), ours) == 0)
        {
            uint64_t token = m_relay->CreateSession(owner->Name(), owner->Peer()->address,
                                                    p2->Name(), p2->Peer()->address, false, true);
            if (token)
            {
                char hex[17];
                snprintf(hex, sizeof hex, "%016llx", (unsigned long long)token);
                SendTo(owner->Peer())(std::string(RELAY_FALLBACK_MM_HEADER) + hex + ",1");
                SendTo(p2->Peer())(std::string(RELAY_FALLBACK_MM_HEADER) + hex + ",2");
            }
        }
        auto m1 = Message::Make(MessageType::Start, gameInfo1); 
        m1.OnData(SendTo(owner->Peer()));
        m1.OnData([&](const std::string& s) {std::cout << "To " << owner->Name() << " send " << s << "\n"; });

        auto m2 = Message::Make(MessageType::Start, gameInfo2);
        m2.OnData(SendTo(p2->Peer()));
        m2.OnData([&](const std::string& s) {std::cout << "To " << p2->Name() << " send " << s << "\n"; });
        RemoveUserFromAnyGames(owner);        
        BroadcastOpenGames();
    }
    return canStart;
}