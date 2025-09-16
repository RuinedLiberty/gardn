#include <Server/Bot.hh>

#include <Server/PetalTracker.hh>
#include <Shared/StaticData.hh>
#include <Shared/Entity.hh>
#include <algorithm>
#include <cstring>

static uint8_t _has_any_petals(Entity &player) {
    uint32_t active = player.get_loadout_count();
    uint32_t total = active + MAX_SLOT_COUNT;
    for (uint32_t i = 0; i < total; ++i)
        if (player.get_loadout_ids(i) != PetalID::kNone) return 1;
    return 0;
}

float rarity_weight(uint8_t rarity) {
    switch (rarity) {
        case RarityID::kCommon:     return 1.0f;
        case RarityID::kUnusual:    return 2.0f;
        case RarityID::kRare:       return 3.5f;
        case RarityID::kEpic:       return 5.0f;
        case RarityID::kLegendary:  return 7.5f;
        case RarityID::kMythic:     return 10.0f;
        case RarityID::kUnique:     return 8.5f;
        default:                    return 0.0f;
    }
}

// Rarity-only score: higher rarity strictly preferred.
float petal_score(PetalID::T id) {
    if (id == PetalID::kNone) return 0.0f;
    if (id >= PetalID::kNumPetals) return 0.0f;
    auto const &pd = PETAL_DATA[id];
    return rarity_weight(pd.rarity);
}

bool is_healing_petal(PetalID::T id) {
    if (id == PetalID::kNone || id >= PetalID::kNumPetals) return false;
    auto const &pd = PETAL_DATA[id];
    return pd.attributes.constant_heal > 0.0f || std::strcmp(pd.name ? pd.name : "", "Rose") == 0;
}

bool has_healing_petal(Entity &player) {
    uint32_t active = player.get_loadout_count();
    uint32_t total = active + MAX_SLOT_COUNT;
    for (uint32_t i = 0; i < total; ++i) {
        if (is_healing_petal(player.get_loadout_ids(i))) return true;
    }
    return false;
}

bool ensure_heal_equipped(Entity &player) {
    uint32_t active = player.get_loadout_count();
    for (uint32_t i = 0; i < active; ++i)
        if (is_healing_petal(player.get_loadout_ids(i))) return true;

    int reserve_idx = -1;
    for (uint32_t i = active; i < active + MAX_SLOT_COUNT; ++i) {
        if (is_healing_petal(player.get_loadout_ids(i))) { reserve_idx = (int)i; break; }
    }
    if (reserve_idx < 0) return false;

    PetalID::T heal = player.get_loadout_ids((uint32_t)reserve_idx);
    PetalID::T displaced = player.get_loadout_ids(0);
    player.set_loadout_ids(0, heal);
    player.set_loadout_ids((uint32_t)reserve_idx, displaced);
    return true;
}

void ensure_basics_if_empty(Entity &player) {
    if (_has_any_petals(player)) return;
    uint32_t active = player.get_loadout_count();
    for (uint32_t i = 0; i < active; ++i)
        player.set_loadout_ids(i, PetalID::kBasic);
}

static void trash_petal(Simulation *sim, Entity &player, uint32_t pos) {
    if (pos >= player.get_loadout_count() + MAX_SLOT_COUNT) return;
    PetalID::T old_id = player.get_loadout_ids(pos);
    if (old_id == PetalID::kNone) return;
    if (player.deleted_petals.size() == player.deleted_petals.capacity())
        PetalTracker::remove_petal(sim, player.deleted_petals[0]);
    player.deleted_petals.push_back(old_id);
    player.set_loadout_ids(pos, PetalID::kNone);
}

bool has_empty_slot(Entity &player) {
    uint32_t active = player.get_loadout_count();
    uint32_t total = active + MAX_SLOT_COUNT;
    for (uint32_t i = 0; i < total; ++i)
        if (player.get_loadout_ids(i) == PetalID::kNone) return true;
    return false;
}

void worst_slots(Entity &player,
                 float &worst_active_score, int &worst_active_idx,
                 float &worst_reserve_score, int &worst_reserve_idx) {
    uint32_t active = player.get_loadout_count();
    uint32_t total = active + MAX_SLOT_COUNT;
    worst_active_score = 1e9f; worst_active_idx = -1;
    worst_reserve_score = 1e9f; worst_reserve_idx = -1;

    for (uint32_t i = 0; i < active; ++i) {
        float s = petal_score(player.get_loadout_ids(i));
        if (s < worst_active_score) { worst_active_score = s; worst_active_idx = (int)i; }
    }
    for (uint32_t i = active; i < total; ++i) {
        PetalID::T id = player.get_loadout_ids(i);
        if (id == PetalID::kNone) continue;
        float s = petal_score(id);
        if (s < worst_reserve_score) { worst_reserve_score = s; worst_reserve_idx = (int)i; }
    }
}

bool try_ensure_space_for_drop(Simulation *sim, Entity &player,
                               float drop_score,
                               float worst_active_score, int worst_active_idx,
                               float worst_reserve_score, int worst_reserve_idx) {
    if (has_empty_slot(player)) return true;

    if (worst_reserve_idx >= 0) {
        PetalID::T id = player.get_loadout_ids((uint32_t) worst_reserve_idx);
        if (!is_healing_petal(id) && drop_score > worst_reserve_score + 0.1f) {
            trash_petal(sim, player, (uint32_t) worst_reserve_idx);
            return true;
        }
    }
    if (worst_active_idx >= 0) {
        PetalID::T id = player.get_loadout_ids((uint32_t) worst_active_idx);
        if (!is_healing_petal(id) && drop_score > worst_active_score + 0.5f) {
            trash_petal(sim, player, (uint32_t) worst_active_idx);
            return true;
        }
    }
    return false;
}

// Promote exactly one reserve petal (highest rarity) into the worst active slot if it's strictly better.
// No global resorting; at most one swap per call to avoid churn.
bool promote_highest_rarity_once(Entity &player) {
    uint32_t active = player.get_loadout_count();
    uint32_t total  = active + MAX_SLOT_COUNT;
    if (active == 0) return false;

    // Find worst active by rarity
    int worst_active_idx = -1;
    float worst_active_score = 1e9f;
    for (uint32_t i = 0; i < active; ++i) {
        PetalID::T id = player.get_loadout_ids(i);
        float s = petal_score(id);
        if (s < worst_active_score) { worst_active_score = s; worst_active_idx = (int)i; }
    }

    // Find best reserve by rarity
    int best_reserve_idx = -1;
    float best_reserve_score = -1.0f;
    for (uint32_t i = active; i < total; ++i) {
        PetalID::T id = player.get_loadout_ids(i);
        if (id == PetalID::kNone) continue;
        float s = petal_score(id);
        if (s > best_reserve_score) { best_reserve_score = s; best_reserve_idx = (int)i; }
    }

    if (worst_active_idx < 0 || best_reserve_idx < 0) return false;

    // Small epsilon so equal-rarity swaps don't occur
    if (best_reserve_score > worst_active_score + 0.05f) {
        PetalID::T a = player.get_loadout_ids((uint32_t)worst_active_idx);
        PetalID::T b = player.get_loadout_ids((uint32_t)best_reserve_idx);
        player.set_loadout_ids((uint32_t)worst_active_idx, b);
        player.set_loadout_ids((uint32_t)best_reserve_idx, a);
        return true;
    }
    return false;
}
