#pragma once

#include <Shared/Entity.hh>
#include <Shared/Simulation.hh>

// Inventory/equip management
float rarity_weight(uint8_t rarity);
float petal_score(PetalID::T id);
void ensure_basics_if_empty(Entity &player);
void rebalance_loadout(Entity &player);
void maybe_trash_unwanted(Simulation *sim, Entity &player);
bool has_empty_slot(Entity &player);
void worst_slots(Entity &player,
                 float &worst_active_score, int &worst_active_idx,
                 float &worst_reserve_score, int &worst_reserve_idx);
// Try to ensure space for a desirable drop (may delete reserve/active petals); returns true if space exists/was made
bool try_ensure_space_for_drop(Simulation *sim, Entity &player,
                               float drop_score,
                               float worst_active_score, int worst_active_idx,
                               float worst_reserve_score, int worst_reserve_idx);

// Targeting
EntityID find_nearest_mob(Simulation *sim, Entity const &player, float radius);
EntityID find_best_drop(Simulation *sim, Entity const &player, float radius, float &best_drop_score);
EntityID select_retaliation_target(Simulation *sim, Entity const &player, game_tick_t window_ticks);

// Movement/combat
void engage_with_petals(Simulation *sim, Entity &player, EntityID target_id);
uint8_t maybe_relocate_to_zone(Simulation *sim, Entity &player);
void apply_idle_march_right(Entity &player);

// Behavior entrypoint (called by Simulation::on_tick)
void tick_bot_player_behavior(Simulation *sim, Entity &player);