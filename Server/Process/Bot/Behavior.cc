#include <Server/Process.hh>
#include <Server/Bot.hh>

#include <Server/Spawn.hh>
#include <Shared/StaticData.hh>
#include <Shared/Entity.hh>
#include <Shared/Simulation.hh>
#include <Shared/Map.hh>

static void ensure_spawn_basics(Entity &player) {
    if (player.get_name().empty()) {
        player.set_name("Bot");
    }
    player.set_loadout_count(loadout_slots_at_level(score_to_level(player.get_score())));
    ensure_basics_if_empty(player);
}

void tick_bot_player_behavior(Simulation *sim, Entity &player) {
    if (player.pending_delete) return;
    if (!player.has_component(kFlower)) return;
    if (!player.is_bot) return;

    if (!sim->ent_alive(player.get_parent())) return;

    ensure_spawn_basics(player);

    // Periodic equip rebalance (~1s)
    if (player.ai_tick % TPS == 0) {
        rebalance_loadout(player);
        maybe_trash_unwanted(sim, player);
    }

    // Safety: ensure some petals if emptied somehow
    ensure_basics_if_empty(player);

    // Priority: consider collecting a valuable drop (with space management)
    float best_drop_score = 0.0f;
    EntityID best_drop = find_best_drop(sim, player, 900.0f, best_drop_score);

    float worst_active = 1e9f; int worst_active_idx = -1;
    float worst_reserve = 1e9f; int worst_reserve_idx = -1;
    worst_slots(player, worst_active, worst_active_idx, worst_reserve, worst_reserve_idx);
    if (worst_active == 1e9f) worst_active = 0.0f;
    if (worst_reserve == 1e9f) worst_reserve = 0.0f;

    float base_margin = (worst_active < 25.0f) ? 0.5f : 2.5f;
    uint8_t prioritize_drop = 0;
    if (!best_drop.null() && best_drop_score > worst_active + base_margin) {
        if (try_ensure_space_for_drop(sim, player, best_drop_score, worst_active, worst_active_idx, worst_reserve, worst_reserve_idx)) {
            prioritize_drop = 1;
        }
    }

    if (prioritize_drop) {
        player.input = 0;
        Entity &d = sim->get_ent(best_drop);
        Vector v(d.get_x() - player.get_x(), d.get_y() - player.get_y());
        if (v.magnitude() > 0) v.set_magnitude(PLAYER_ACCELERATION);
        player.acceleration = v;
        player.set_angle(v.angle());
        ++player.ai_tick;
        return;
    }

    // Retaliation target in last 2 seconds
    EntityID target = select_retaliation_target(sim, player, (game_tick_t)(2 * TPS));

    // Fallback: nearest mob
    if (target.null()) {
        target = find_nearest_mob(sim, player, 900.0f);
    }

    // If still nothing to do, idle fallback: march right (interruptible)
    if (target.null()) {
        apply_idle_march_right(player);
    }

    // Last priority: relocate toward appropriate zone (overrides idle right-march)
    if (maybe_relocate_to_zone(sim, player)) {
        ++player.ai_tick;
        return;
    }

    if (sim->ent_alive(target)) {
        player.input = 0;
        engage_with_petals(sim, player, target);
    }

    ++player.ai_tick;
}