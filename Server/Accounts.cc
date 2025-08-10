#include <Server/Accounts.hh>

#include <Server/Server.hh>

#include <openssl/aead.h>
#include <openssl/rand.h>

#include <chrono>
#include <string>
#include <array>
#include <cstdlib>

namespace Accounts {
    static uint8_t const TOKEN_VERSION = 1;
    static uint32_t const NONCE_LEN = 12;
    static uint32_t const TAG_LEN = 16;
    static uint32_t const PLAINTEXT_LEN = 8 + 8 + 4;
    static uint32_t const EXPIRY_SECONDS = 600;

    static std::array<uint8_t, 32> key = { // default key
        0x2f,0x91,0x3a,0xbc,0x44,0xe1,0x77,0x58,
        0x0c,0x22,0x6d,0xa1,0x4b,0x5e,0x7d,0x80,
        0x93,0x12,0x4f,0x6a,0x8c,0xaa,0xcd,0xef,
        0x10,0x39,0x57,0x68,0x90,0xb2,0xd4,0xf6
    };
    static uint8_t key_loaded = 0;

    static uint32_t _rand32() {
        static uint32_t s = 0xA5F1523u ^ (uint32_t)std::chrono::high_resolution_clock::now().time_since_epoch().count();
        s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s;
    }

    static char _rand_alpha() {
        uint32_t r = _rand32() % 52;
        return r < 26 ? char('A' + r) : char('a' + (r - 26));
    }

    static std::string _rand4() {
        std::string s; s.resize(4);
        for (int i = 0; i < 4; ++i) s[i] = _rand_alpha();
        return s;
    }

    static uint8_t _hex(char c) {
        if (c >= '0' && c <= '9') return (uint8_t)(c - '0');
        if (c >= 'a' && c <= 'f') return (uint8_t)(10 + c - 'a');
        if (c >= 'A' && c <= 'F') return (uint8_t)(10 + c - 'A');
        return 0xFF;
    }

    static void _load_key() {
        if (key_loaded) return;
        char const *env = std::getenv("ACCOUNTS_KEY_HEX");
        if (env) {
            std::string s(env);
            if (s.size() == 64) {
                uint8_t tmp[32];
                uint8_t ok = 1;
                for (int i = 0; i < 32; ++i) {
                    uint8_t a = _hex(s[2 * i]);
                    uint8_t b = _hex(s[2 * i + 1]);
                    if (a == 0xFF || b == 0xFF) { ok = 0; break; }
                    tmp[i] = (uint8_t)((a << 4) | b);
                }
                if (ok) for (int i = 0; i < 32; ++i) key[i] = tmp[i];
            }
        }
        key_loaded = 1;
    }

    static std::string _b64_encode(std::string const &in) {
        static char const tbl[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string out; out.reserve((in.size() + 2) / 3 * 4);
        uint32_t val = 0; int valb = -6;
        for (unsigned char c : in) {
            val = (val << 8) + c; valb += 8;
            while (valb >= 0) { out.push_back(tbl[(val >> valb) & 0x3F]); valb -= 6; }
        }
        if (valb > -6) out.push_back(tbl[((val << 8) >> (valb + 8)) & 0x3F]);
        while (out.size() % 4) out.push_back('=');
        return out;
    }

    static uint8_t _b64_index(char c) {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        return 0xFF;
    }

    static uint8_t _b64_decode(std::string const &in, std::string &out) {
        out.clear();
        if (in.empty()) return 0;
        uint32_t val = 0; int valb = -8;
        for (char c : in) {
            if (c == '=') break;
            uint8_t d = _b64_index(c);
            if (d == 0xFF) return 0;
            val = (val << 6) | d; valb += 6;
            if (valb >= 0) { out.push_back(char((val >> valb) & 0xFF)); valb -= 8; }
        }
        return 1;
    }

    static void _be_write8(uint8_t *dst, uint64_t v) {
        for (int i = 0; i < 8; ++i) dst[i] = (uint8_t)((v >> (56 - 8 * i)) & 0xFF);
    }

    static uint64_t _be_read8(uint8_t const *src) {
        uint64_t v = 0; for (int i = 0; i < 8; ++i) v = (v << 8) | src[i]; return v;
    }

    std::string make_account_token(uint64_t discord_id) {
        if (discord_id == 0) return "";
        _load_key();
        using namespace std::chrono;
        uint64_t created = duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
        std::string rand4 = _rand4();

        uint8_t plaintext[PLAINTEXT_LEN];
        _be_write8(&plaintext[0], discord_id);
        _be_write8(&plaintext[8], created);
        for (int i = 0; i < 4; ++i) plaintext[16 + i] = (uint8_t)rand4[i];

        uint8_t nonce[NONCE_LEN];
        RAND_bytes(nonce, NONCE_LEN);

        size_t out_len = 0;
        uint8_t ciphertext[PLAINTEXT_LEN + TAG_LEN];

        EVP_AEAD_CTX ctx;
        EVP_AEAD_CTX_init(&ctx, EVP_aead_chacha20_poly1305(), key.data(), key.size(), TAG_LEN, nullptr);
        if (!EVP_AEAD_CTX_seal(&ctx, ciphertext, &out_len, sizeof(ciphertext), nonce, NONCE_LEN, plaintext, sizeof(plaintext), nullptr, 0)) {
            EVP_AEAD_CTX_cleanup(&ctx);
            return "";
        }
        EVP_AEAD_CTX_cleanup(&ctx);

        std::string raw;
        raw.resize(1 + NONCE_LEN + out_len);
        raw[0] = (char)TOKEN_VERSION;
        for (uint32_t i = 0; i < NONCE_LEN; ++i) raw[1 + i] = (char)nonce[i];
        for (size_t i = 0; i < out_len; ++i) raw[1 + NONCE_LEN + i] = (char)ciphertext[i];

        return _b64_encode(raw);
    }

    uint64_t parse_account_token(std::string const &token, uint64_t &created_at) {
        created_at = 0;
        _load_key();
        std::string raw;
        if (!_b64_decode(token, raw)) return 0;
        if (raw.size() < 1 + NONCE_LEN + TAG_LEN) return 0;
        if ((uint8_t)raw[0] != TOKEN_VERSION) return 0;
        uint8_t const *nonce = (uint8_t const *)&raw[1];
        uint8_t const *cipher = (uint8_t const *)&raw[1 + NONCE_LEN];
        size_t cipher_len = raw.size() - 1 - NONCE_LEN;

        uint8_t plaintext[PLAINTEXT_LEN];
        size_t pt_len = 0;

        EVP_AEAD_CTX ctx;
        EVP_AEAD_CTX_init(&ctx, EVP_aead_chacha20_poly1305(), key.data(), key.size(), TAG_LEN, nullptr);
        uint8_t ok = EVP_AEAD_CTX_open(&ctx, plaintext, &pt_len, sizeof(plaintext), nonce, NONCE_LEN, cipher, cipher_len, nullptr, 0);
        EVP_AEAD_CTX_cleanup(&ctx);
        if (!ok) return 0;
        if (pt_len != PLAINTEXT_LEN) return 0;

        uint64_t discord_id = _be_read8(&plaintext[0]);
        created_at = _be_read8(&plaintext[8]);
        if (discord_id == 0) return 0;

        using namespace std::chrono;
        uint64_t now = (uint64_t)duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
        if (created_at > now) return 0;
        if (now - created_at > EXPIRY_SECONDS) return 0;

        return discord_id;
    }

    // basic in-memory session tracking keyed by base entity id
    struct SessionData {
        uint64_t kills = 0;
        uint64_t deaths = 0;
        uint64_t time_alive_ticks = 0;
        uint32_t max_score = 0;
    };

    static std::unordered_map<uint16_t, SessionData> sessions;

    void on_kill(Simulation *sim, Entity const &killer) {
        if (sim == nullptr) return;
        if (!killer.has_component(kFlower)) return;
        uint16_t key = killer.base_entity.id;
        auto &s = sessions[key];
        ++s.kills;
        if (killer.score > s.max_score) s.max_score = killer.score;
    }

    void on_death(Simulation *sim, Entity const &deceased) {
        if (sim == nullptr) return;
        if (!deceased.has_component(kFlower)) return;
        uint16_t key = deceased.base_entity.id;
        auto &s = sessions[key];
        ++s.deaths;
        if (deceased.score > s.max_score) s.max_score = deceased.score;
    }

    void tick() {
        // accumulate time_alive and track max score
        for (Client *c : Server::clients) {
            if (c == nullptr) continue;
            if (!c->verified) continue;
            if (!Server::simulation.ent_exists(c->camera)) continue;
            Entity &cam = Server::simulation.get_ent(c->camera);
            if (!Server::simulation.ent_exists(cam.player)) continue;
            Entity &pl = Server::simulation.get_ent(cam.player);
            uint16_t key = pl.base_entity.id;
            auto &s = sessions[key];
            ++s.time_alive_ticks;
            if (pl.score > s.max_score) s.max_score = pl.score;
        }
    }

    Accounts::AccountStats get_session_stats(Client *client) {
        AccountStats out;
        if (client == nullptr) return out;
        if (!client->verified) return out;
        if (!Server::simulation.ent_exists(client->camera)) return out;
        Entity &cam = Server::simulation.get_ent(client->camera);
        if (!Server::simulation.ent_exists(cam.player)) return out;
        Entity &pl = Server::simulation.get_ent(cam.player);
        uint16_t key = pl.base_entity.id;
        auto it = sessions.find(key);
        if (it == sessions.end()) return out;
        out.kills = it->second.kills;
        out.deaths = it->second.deaths;
        out.time_alive_ticks = it->second.time_alive_ticks;
        out.max_score = it->second.max_score;
        return out;
    }

    uint8_t link_client_token(Client *client, std::string const &token) {
        if (client == nullptr) return 0;
        uint64_t created = 0;
        uint64_t discord_id = parse_account_token(token, created);
        return discord_id ? 1 : 0;
    }

    void init() {
        sessions.clear();
    }
}

