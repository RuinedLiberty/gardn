#pragma once

#include <cstdint>
#include <string>

namespace Stats {
    void init();
    void tick();
    void request_post();
    std::string build_payload_json(uint32_t players_online, uint32_t unique_petals);
}

