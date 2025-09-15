#include <Server/Bot.hh>

#include <Server/PetalTracker.hh>
#include <Shared/StaticData.hh>
#include <Shared/Entity.hh>

static uint8_t has_any_petals(Entity &player) {
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

float petal_score(PetalID::T id) {
    if (id == PetalID::kNone) return 0.0f;
    if (id >= PetalID::kNumPetals) return 0.0f;
    auto const &pd = PETAL_DATA[id];
    float score = rarity_weight(pd.rarity) * 10.0f;
    score += pd.damage * 0.4f;
    score += pd.attributes.constant_heal * 6.0f;
    if (pd.count > 1) score += (pd.count - 1) * 1.0f;
    return score;
}

void ensure_basics_if_empty(Entity &player) {
    if (has_any_petals(player)) return;
    uint32_t active = player.get_loadout_count();
    for (uint32_t i = 0; i < active; ++i)
        player.set_loadout_ids(i, PetalID::kBasic);
}

void rebalance_loadout(Entity &player) {
    uint32_t active = player.get_loadout_count();
    uint32_t total = active + MAX_SLOT_COUNT;
    if (active == 0 || total == 0) return;

    struct SlotScore { uint32_t idx; float score; PetalID::T id; };
    SlotScore scores[2 * MAX_SLOT_COUNT] = {};
    uint32_t n = 0;

    for (uint32_t i = 0; i < total; ++i) {
        PetalID::T id = player.get_loadout_ids(i);
        scores[n++] = { i, petal_score(id), id };
    }

    std::sort(scores, scores + n, [](auto const &a, auto const &b){ return a.score > b.score; });

    PetalID::T desired[2 * MAX_SLOT_COUNT] = { PetalID::kNone };
    for (uint32_t pos = 0; pos < active && pos < n; ++pos)
        desired[pos] = scores[pos].id;

    uint32_t write = active;
    for (uint32_t k = active; k < n && write < (active + MAX_SLOT_COUNT); ++k)
        desired[write++] = scores[k].id;

    for (uint32_t i = 0; i < total; ++i)
        player.set_loadout_ids(i, desired[i]);
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

void maybe_trash_unwanted(Simulation *sim, Entity &player) {
    uint32_t active = player.get_loadout_count();
    uint32_t total = active + MAX_SLOT_COUNT;
    if (active == 0) return;

    float worst_active = 1e9f;
    for (uint32_t i = 0; i < active; ++i) {
        worst_active = std::min(worst_active, petal_score(player.get_loadout_ids(i)));
    }
    if (worst_active == 1e9f) worst_active = 0.0f;

    float trash_threshold = worst_active * 0.5f;

    for (uint32_t i = active; i < total; ++i) {
        PetalID::T id = player.get_loadout_ids(i);
        if (id == PetalID::kNone) continue;
        if (petal_score(id) < trash_threshold) {
            trash_petal(sim, player, i);
            break;
        }
    }
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

    if (worst_reserve_idx >= 0 && drop_score > worst_reserve_score + 0.5f) {
        trash_petal(sim, player, (uint32_t) worst_reserve_idx);
        return true;
    }
    if (worst_active_idx >= 0 && drop_score > worst_active_score + 4.0f) {
        trash_petal(sim, player, (uint32_t) worst_active_idx);
        return true;
    }
    return false;
}