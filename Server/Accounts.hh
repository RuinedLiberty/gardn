#pragma once

#include <Shared/Helpers.hh>

#include <cstdint>
#include <string>

class Simulation;
class Entity;
class Client;

namespace Accounts {
    struct AccountStats {
        uint64_t kills = 0;
        uint64_t deaths = 0;
        uint64_t time_alive_ticks = 0;
        uint32_t max_score = 0;
    };

    void init();
    void tick();

    // token format: enc([ver(1)][discord_id(8)][created_at(8)][rand4(4)]) -> base64
    std::string make_account_token(uint64_t discord_id);

    // decode & validate token; returns 0 on failure
    uint64_t parse_account_token(std::string const &token, uint64_t &created_at);

    // session/aggregate tracking
    void on_kill(Simulation *sim, Entity const &killer);
    void on_death(Simulation *sim, Entity const &deceased);

    // link a live client to a discord id via token
    uint8_t link_client_token(Client *client, std::string const &token);

    // read-only snapshot of current session stats for a client
    AccountStats get_session_stats(Client *client);
}

