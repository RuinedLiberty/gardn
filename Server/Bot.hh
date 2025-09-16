// File: Server/Bot.hh
#pragma once

#include <Shared/Entity.hh>
#include <Shared/Simulation.hh>

// ---------- Scoring / heuristics ----------
float rarity_weight(uint8_t rarity);
float petal_score(PetalID::T id);    // now rarity-only
bool  is_healing_petal(PetalID::T id);

// ---------- Inventory / equip helpers ----------
void ensure_basics_if_empty(Entity &player);

bool has_healing_petal(Entity &player);
bool ensure_heal_equipped(Entity &player);

bool has_empty_slot(Entity &player);
void worst_slots(Entity &player,
                 float &worst_active_score, int &worst_active_idx,
                 float &worst_reserve_score, int &worst_reserve_idx);

bool try_ensure_space_for_drop(Simulation *sim, Entity &player,
                               float drop_score,
                               float worst_active_score, int worst_active_idx,
                               float worst_reserve_score, int worst_reserve_idx);

// Promote a single highest-rarity reserve petal into the worst active slot (one swap max)
bool promote_highest_rarity_once(Entity &player);

// ---------- Targeting ----------
EntityID find_nearest_mob(Simulation *sim, Entity const &player, float radius);
EntityID find_nearest_player(Simulation *sim, Entity const &player, float radius);
EntityID find_best_drop(Simulation *sim, Entity const &player, float radius, float &best_drop_score);

EntityID find_best_heal_drop(Simulation *sim, Entity const &player, float radius, float &score);
EntityID find_nearest_heal_mob(Simulation *sim, Entity const &player, float radius);

// ---------- Movement / combat ----------
void engage_with_petals(Simulation *sim, Entity &player, EntityID target_id);
uint8_t maybe_relocate_to_zone(Simulation *sim, Entity &player);
void apply_idle_march_right(Entity &player);
void apply_wander_biased(Simulation *sim, Entity &player, float bias_right, float speed_scale);

// ---------- Priorities ----------
float priority_retaliation(Simulation *sim, Entity const &player);
float priority_obtain_drop(Simulation *sim, Entity const &player, float best_drop_score, float worst_active_score);
float priority_aggro_mobs(Simulation *sim, Entity const &player);
float priority_target_players(Simulation *sim, Entity const &player);
float priority_healing(Simulation *sim, Entity const &player);
float priority_overlevel_escape(Simulation *sim, Entity const &player);

// Fixed, high-priority upgrade when a drop's rarity beats worst active
float priority_upgrade_drop(Simulation *sim, Entity const &player, float best_drop_score, float worst_active_score);

// ---------- Behavior entry ----------
void tick_bot_player_behavior(Simulation *sim, Entity &player);

// ---------- Inline retaliation selection ----------
inline EntityID select_retaliation_target(Simulation *sim, Entity const &player, game_tick_t window_ticks) {
    EntityID target = NULL_ENTITY;
    float best_damage = 0.0f;
    const game_tick_t now = player.lifetime;
    for (uint32_t i = 0; i < 8; ++i) {
        auto const &rd = player.recent_damage[i];
        if (rd.attacker.null()) continue;
        if (!sim->ent_alive(rd.attacker)) continue;
        if (now - rd.tick > window_ticks) continue;
        if (rd.amount > best_damage) {
            best_damage = rd.amount;
            target = rd.attacker;
        }
    }
    return target;
}
