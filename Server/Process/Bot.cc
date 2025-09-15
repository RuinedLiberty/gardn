#include <Server/Process.hh>

#include <Server/Spawn.hh>
#include <Server/EntityFunctions.hh>
#include <Server/PetalTracker.hh>
#include <Shared/Entity.hh>
#include <Shared/Simulation.hh>
#include <Shared/Map.hh>
#include <Shared/StaticData.hh>
#include <algorithm>

// Rarity base weights; tune as needed
static float rarity_weight(uint8_t rarity) {
    switch (rarity) {
        case RarityID::kCommon:     return 1.0f;
        case RarityID::kUnusual:    return 2.0f;
        case RarityID::kRare:       return 3.5f;
        case RarityID::kEpic:       return 5.0f;
        case RarityID::kLegendary:  return 7.5f;
        case RarityID::kMythic:     return 10.0f;
        case RarityID::kUnique:     return 8.5f; // uniques are special; not always “highest”
        default:                    return 0.0f;
    }
}

// Simple petal score (kept intentionally simple; refine later)
static float petal_score(PetalID::T id) {
    if (id == PetalID::kNone) return 0.0f;
    if (id >= PetalID::kNumPetals) return 0.0f;
    auto const &pd = PETAL_DATA[id];
    float score = rarity_weight(pd.rarity) * 10.0f;
    // reward raw damage and some sustain; weights are small to keep rarity primary
    score += pd.damage * 0.4f;
    score += pd.attributes.constant_heal * 6.0f; // sustain has good value
    // prefer multi-count clumping petals slightly
    if (pd.count > 1) score += (pd.count - 1) * 1.0f;
    return score;
}

// Count whether bot has any petals at all across active+reserve
static uint8_t has_any_petals(Entity &player) {
    uint32_t active = player.get_loadout_count();
    uint32_t total = active + MAX_SLOT_COUNT;
    for (uint32_t i = 0; i < total; ++i)
        if (player.get_loadout_ids(i) != PetalID::kNone) return 1;
    return 0;
}

// Ensure bot can function even if inventory is empty (fill actives with Basic)
static void ensure_basics_if_empty(Entity &player) {
    if (has_any_petals(player)) return;
    uint32_t active = player.get_loadout_count();
    for (uint32_t i = 0; i < active; ++i)
        player.set_loadout_ids(i, PetalID::kBasic);
}

// Rebalance active slots with best available petals (runs periodically)
// Strategy: score every slot in [0, active+reserve), pick top <active> for [0, active)
static void rebalance_loadout(Entity &player) {
    uint32_t active = player.get_loadout_count();
    uint32_t total = active + MAX_SLOT_COUNT;
    if (active == 0 || total == 0) return;

    struct SlotScore { uint32_t idx; float score; PetalID::T id; };
    SlotScore scores[2 * MAX_SLOT_COUNT]; // enough by design
    uint32_t n = 0;

    for (uint32_t i = 0; i < total; ++i) {
        PetalID::T id = player.get_loadout_ids(i);
        float s = petal_score(id);
        scores[n++] = { i, s, id };
    }

    // Sort descending by score
    std::sort(scores, scores + n, [](auto const &a, auto const &b){ return a.score > b.score; });

    // Top 'active' scores should occupy [0, active)
    PetalID::T desired[2 * MAX_SLOT_COUNT] = { PetalID::kNone };
    for (uint32_t pos = 0; pos < active && pos < n; ++pos)
        desired[pos] = scores[pos].id;

    // For reserve positions, just keep the remaining set in any order
    uint32_t write = active;
    for (uint32_t k = active; k < n && write < (active + MAX_SLOT_COUNT); ++k)
        desired[write++] = scores[k].id;

    // Apply
    for (uint32_t i = 0; i < total; ++i)
        player.set_loadout_ids(i, desired[i]);
}

// Delete a reserve petal in slot 'pos' (server-side, mimics client delete semantics without XP award)
static void trash_petal(Simulation *sim, Entity &player, uint32_t pos) {
    if (pos >= player.get_loadout_count() + MAX_SLOT_COUNT) return;
    PetalID::T old_id = player.get_loadout_ids(pos);
    if (old_id == PetalID::kNone) return;
    // push into deleted list; if full, remove the oldest from tracker to prevent leaks (matching overflow handling pattern)
    if (player.deleted_petals.size() == player.deleted_petals.capacity()) {
        PetalTracker::remove_petal(sim, player.deleted_petals[0]);
    }
    player.deleted_petals.push_back(old_id);
    player.set_loadout_ids(pos, PetalID::kNone);
}

// Consider deleting very weak reserve petals compared to current actives (1 per check)
static void maybe_trash_unwanted(Simulation *sim, Entity &player) {
    uint32_t active = player.get_loadout_count();
    uint32_t total = active + MAX_SLOT_COUNT;
    if (active == 0) return;

    // Find worst active score to gauge baseline
    float worst_active = 1e9f;
    for (uint32_t i = 0; i < active; ++i) {
        PetalID::T id = player.get_loadout_ids(i);
        worst_active = std::min(worst_active, petal_score(id));
    }
    if (worst_active == 1e9f) worst_active = 0.0f;

    // Threshold: if reserve petal is far weaker than current worst active, trash it
    float trash_threshold = worst_active * 0.5f; // conservative; keep things that might serve a niche

    for (uint32_t i = active; i < total; ++i) {
        PetalID::T id = player.get_loadout_ids(i);
        if (id == PetalID::kNone) continue;
        float s = petal_score(id);
        if (s < trash_threshold) {
            trash_petal(sim, player, i);
            break; // delete one per check
        }
    }
}

static bool has_empty_slot(Entity &player) {
    uint32_t active = player.get_loadout_count();
    uint32_t total = active + MAX_SLOT_COUNT;
    for (uint32_t i = 0; i < total; ++i)
        if (player.get_loadout_ids(i) == PetalID::kNone) return true;
    return false;
}

static void worst_slots(Entity &player, float &worst_active_score, int &worst_active_idx,
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
        if (id == PetalID::kNone) continue; // empty is not a candidate
        float s = petal_score(id);
        if (s < worst_reserve_score) { worst_reserve_score = s; worst_reserve_idx = (int)i; }
    }
}

// Find nearest mob within radius; returns NULL_ENTITY if none
static EntityID find_nearest_mob(Simulation *sim, Entity const &player, float radius) {
    EntityID best = NULL_ENTITY;
    float min_dist = radius;
    sim->spatial_hash.query(player.get_x(), player.get_y(), radius, radius, [&](Simulation *sim2, Entity &ent){
        if (!sim2->ent_alive(ent.id)) return;
        if (!ent.has_component(kMob)) return;
        if (ent.get_team() == player.get_team()) return;
        if (ent.immunity_ticks > 0) return;
        float d = Vector(ent.get_x() - player.get_x(), ent.get_y() - player.get_y()).magnitude();
        if (d < min_dist) { min_dist = d; best = ent.id; }
    });
    return best;
}

// Search for the best drop nearby and return its entity ID (NULL_ENTITY if none)
static EntityID find_best_drop(Simulation *sim, Entity const &player, float radius, float &best_drop_score) {
    EntityID best = NULL_ENTITY;
    best_drop_score = 0.0f;
    sim->spatial_hash.query(player.get_x(), player.get_y(), radius, radius, [&](Simulation *sim2, Entity &ent){
        if (!sim2->ent_alive(ent.id)) return;
        if (!ent.has_component(kDrop)) return;
        if (ent.immunity_ticks > 0) return;
        PetalID::T drop_id = ent.get_drop_id();
        if (drop_id >= PetalID::kNumPetals || drop_id == PetalID::kNone) return;
        float s = petal_score(drop_id);
        if (s > best_drop_score) {
            best_drop_score = s;
            best = ent.id;
        }
    });
    return best;
}

static void _ensure_spawn(Simulation *sim, Entity &player) {
    if (!sim->ent_alive(player.get_parent())) return;
    Entity &camera = sim->get_ent(player.get_parent());
    (void) camera;
    if (player.get_name().empty()) {
        player.set_name("Bot");
    }
    player.set_loadout_count(loadout_slots_at_level(score_to_level(player.get_score())));
    ensure_basics_if_empty(player);
}

// Petal-centric engagement: avoid body ramming; keep distance and orbit in sweet spot
static void engage_with_petals(Simulation *sim, Entity &player, EntityID target_id) {
    // Offense mode: extend petals (attack)
    BitMath::set(player.input, InputFlags::kAttacking);

    if (!sim->ent_alive(target_id)) return;
    Entity &t = sim->get_ent(target_id);
    Vector delta(t.get_x() - player.get_x(), t.get_y() - player.get_y());
    float dist = delta.magnitude();
    if (dist == 0) {
        player.acceleration = Vector::rand(PLAYER_ACCELERATION);
        return;
    }

    const float desired = player.get_radius() + 100.0f;
    const float too_close = desired - 25.0f;
    const float too_far  = desired + 25.0f;

    Vector dir = delta; dir.normalize();
    Vector tangent(-dir.y, dir.x);
    float orbit_sign = (player.id.id % 2 == 0) ? 1.0f : -1.0f;

    if (dist > too_far) {
        player.acceleration = dir * (PLAYER_ACCELERATION * 1.0f);
    } else if (dist < too_close) {
        player.acceleration = (dir * -1.0f) * (PLAYER_ACCELERATION * 1.0f);
    } else {
        player.acceleration = tangent * (PLAYER_ACCELERATION * 0.8f * orbit_sign);
    }
    player.set_angle(player.acceleration.angle());
}

// Move towards the center of the suitable zone (last priority; no attacking during relocation)
static uint8_t maybe_relocate_to_zone(Simulation *sim, Entity &player) {
    uint32_t level = score_to_level(player.get_score());
    uint32_t desired_difficulty = Map::difficulty_at_level(level);
    uint32_t current_zone_idx = Map::get_zone_from_pos(player.get_x(), player.get_y());
    ZoneDefinition const &curr_zone = MAP_DATA[current_zone_idx];

    if (curr_zone.difficulty >= desired_difficulty) return 0; // already in suitable or harder zone

    // More eager if overlevel penalties building up; otherwise small early probability
    float eager = player.get_overlevel_timer() > 0.5f * TPS ? 1.0f : 0.2f;
    if (frand() > eager) return 0;

    uint32_t target_zone_idx = Map::get_suitable_difficulty_zone(desired_difficulty);
    ZoneDefinition const &target_zone = MAP_DATA[target_zone_idx];
    float tx = 0.5f * (target_zone.left + target_zone.right);
    float ty = 0.5f * (target_zone.top + target_zone.bottom);

    // Path to zone center; do not attack during relocation
    player.input = 0;
    Vector v(tx - player.get_x(), ty - player.get_y());
    if (v.magnitude() > 0) v.set_magnitude(PLAYER_ACCELERATION);
    player.acceleration = v;
    player.set_angle(v.angle());
    return 1;
}

void tick_bot_player_behavior(Simulation *sim, Entity &player) {
    if (player.pending_delete) return;
    if (!player.has_component(kFlower)) return;
    if (!player.is_bot) return;

    // Require a valid camera parent (camera handles respawn)
    if (!sim->ent_alive(player.get_parent())) return;

    _ensure_spawn(sim, player);

    // Periodic equip rebalance (~1s)
    if (player.ai_tick % TPS == 0) {
        rebalance_loadout(player);
        maybe_trash_unwanted(sim, player);
    }

    // Priority 0 safety: ensure we have some petals
    ensure_basics_if_empty(player);

    // Dynamic priority: consider collecting a valuable drop
    float best_drop_score = 0.0f;
    EntityID best_drop = find_best_drop(sim, player, 900.0f, best_drop_score);

    // Compute current worst active score to measure upgrade value
    float worst_active = 1e9f;
    int worst_active_idx = -1;
    float worst_reserve = 1e9f;
    int worst_reserve_idx = -1;
    worst_slots(player, worst_active, worst_active_idx, worst_reserve, worst_reserve_idx);
    if (worst_active == 1e9f) worst_active = 0.0f;
    if (worst_reserve == 1e9f) worst_reserve = 0.0f;

    // Dynamic threshold: if our actives are weak, be eager to upgrade; otherwise require a larger margin
    float base_margin = (worst_active < 25.0f) ? 0.5f : 2.5f; // for deciding drop worthiness overall
    uint8_t prioritize_drop = 0;
    uint8_t room_available = has_empty_slot(player);

    if (!best_drop.null() && best_drop_score > worst_active + base_margin) {
        // Drop is desirable; ensure we can actually take it (create space if needed)
        if (!room_available) {
            // Prefer deleting worst reserve if drop is better than it by a small margin
            if (worst_reserve_idx >= 0 && best_drop_score > worst_reserve + 0.5f) {
                trash_petal(sim, player, (uint32_t)worst_reserve_idx);
                room_available = 1;
            } else if (worst_active_idx >= 0 && best_drop_score > worst_active + 4.0f) {
                // As a last resort, delete the worst active if drop is much better
                trash_petal(sim, player, (uint32_t)worst_active_idx);
                room_available = 1;
            }
        }
        prioritize_drop = room_available ? 1 : 0;
    }

    // Priority 1 (dynamic): collect valuable drop if it’s a meaningful upgrade AND space exists (or was made)
    if (prioritize_drop) {
        player.input = 0; // no need to attack while pathing to a drop
        Entity &d = sim->get_ent(best_drop);
        Vector v(d.get_x() - player.get_x(), d.get_y() - player.get_y());
        if (v.magnitude() > 0) v.set_magnitude(PLAYER_ACCELERATION);
        player.acceleration = v;
        player.set_angle(v.angle());
        ++player.ai_tick;
        return;
    }

    // Priority 2: retaliate against the attacker who dealt the most damage in the last 2 seconds
    EntityID target = NULL_ENTITY;
    float best_damage = 0.0f;
    const game_tick_t now = player.lifetime;
    const game_tick_t window = (game_tick_t)(2 * TPS);

    for (uint32_t i = 0; i < 8; ++i) {
        auto const &rd = player.recent_damage[i];
        if (rd.attacker.null()) continue;
        if (!sim->ent_alive(rd.attacker)) continue;
        if (now - rd.tick > window) continue;
        if (rd.amount > best_damage) {
            best_damage = rd.amount;
            target = rd.attacker;
        }
    }

    // Priority 3: if no recent attacker in the last 2s, target nearest mob
    if (target.null()) {
        target = find_nearest_mob(sim, player, 900.0f);
    }

    // If still nothing to do, idle fallback: gently march right to discover new zones.
    // This is interruptible by higher priorities on subsequent ticks.
    if (target.null() && !prioritize_drop) {
        player.input = 0;
        Vector right(1.0f, 0.0f);
        right.set_magnitude(PLAYER_ACCELERATION * 0.6f);
        player.acceleration = right;
        player.set_angle(0.0f);
        // After setting a right-march fallback, we still allow last-priority relocation to override below.
    }

    // Last priority: relocate toward the appropriate zone if overleveled/likely to be blocked.
    // This can override the right-march idle movement for this tick.
    if (maybe_relocate_to_zone(sim, player)) {
        ++player.ai_tick;
        return;
    }

    // Clear input and set behavior if we have a target at this point
    if (sim->ent_alive(target)) {
        player.input = 0; // set by engage function
        engage_with_petals(sim, player, target);
    }

    ++player.ai_tick;
}