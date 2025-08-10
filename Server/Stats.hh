#pragma once

#include <cstdint>
#include <string>

namespace Stats {
    void init();
    void tick();
    // Trigger an immediate post (e.g., on new connection) in a safe, non-blocking way
    void request_post();
    // helpers exposed for tests
    std::string build_payload_json(uint32_t players_online, uint32_t unique_petals);
}

