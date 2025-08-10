#include <Server/Stats.hh>

#include <Server/Server.hh>
#include <Server/PetalTracker.hh>
#include <Shared/StaticData.hh>

#include <cstdlib>
#include <cstring>
#include <string>
#include <sstream>
#include <vector>
#include <thread>
#include <atomic>

#ifdef HAS_LIBCURL
#include <curl/curl.h>
#endif

namespace Stats {
    static std::string webhook_url;
    static double last_post_time = 0; // in seconds since epoch
    static bool pending_post = false;

    static std::string getenv_str(const char *name) {
        const char *v = std::getenv(name);
        return v ? std::string(v) : std::string();
    }

    std::string build_payload_json(uint32_t players_online, uint32_t unique_petals) {
        std::ostringstream oss;
        oss << "{\"content\":\""
            << "Players online: " << players_online << "\\n"
            << "Unique rarity petals held: " << unique_petals
            << "\"}";
        return oss.str();
    }

    void init() {
        webhook_url = getenv_str("GARDN_STATS_WEBHOOK_URL");
    }

    static uint32_t count_unique_rarity_petals() {
        uint32_t sum = 0;
        for (uint32_t i = 0; i < PetalID::kNumPetals; ++i) {
            if (PETAL_DATA[i].rarity == RarityID::kUnique)
                sum += PetalTracker::get_count(static_cast<PetalID::T>(i));
        }
        return sum;
    }

    static void do_post() {
        if (webhook_url.empty()) return;
        uint32_t players_online = (uint32_t)Server::clients.size();
        uint32_t unique_count = count_unique_rarity_petals();
        std::string payload = build_payload_json(players_online, unique_count);

#ifdef HAS_LIBCURL
        CURL *curl = curl_easy_init();
        if (!curl) return;
        struct curl_slist *headers = nullptr;
        headers = curl_slist_append(headers, "Content-Type: application/json");
        curl_easy_setopt(curl, CURLOPT_URL, webhook_url.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, payload.size());
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);
        curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
        CURLcode res = curl_easy_perform(curl);
        (void)res;
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
#endif
    }

    void tick() {
        double now = (double)std::time(nullptr);
        if (pending_post || now - last_post_time >= 60.0) {
            last_post_time = now;
            std::thread([](){ do_post(); }).detach();
            pending_post = false;
        }
    }

    void request_post() {
        pending_post = true;
    }
}
