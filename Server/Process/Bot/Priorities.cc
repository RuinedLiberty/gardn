#include <Server/Bot.hh>

#include <Shared/StaticData.hh>
#include <Shared/Entity.hh>
#include <Shared/Map.hh>

float priority_retaliation(Simulation *sim, Entity const &player) {
    const game_tick_t window = (game_tick_t)(2 * TPS);
    const game_tick_t now = player.lifetime;
    for (uint32_t i = 0; i < 8; ++i) {
        auto const &rd = player.recent_damage[i];
        if (rd.attacker.null()) continue;
        if (!sim->ent_alive(rd.attacker)) continue;
        if (now - rd.tick > window) continue;
        return 0.8f;
    }
    return 0.0f;
}

float priority_obtain_drop(Simulation * /*sim*/, Entity const & /*player*/, float best_drop_score, float worst_active_score) {
    if (best_drop_score <= 0.0f) return 0.0f;
    float delta = best_drop_score - worst_active_score;
    float p = delta / 10.0f;
    if (p < 0) p = 0;
    if (p > 1) p = 1;
    return p;
}

float priority_aggro_mobs(Simulation *sim, Entity const &player) {
    EntityID mob = find_nearest_mob(sim, player, 900.0f);
    return sim->ent_alive(mob) ? 0.7f : 0.0f;
}

float priority_target_players(Simulation *sim, Entity const &player) {
    EntityID p = find_nearest_player(sim, player, 900.0f);
    if (!sim->ent_alive(p)) return 0.0f;
    return 0.6f;
}

float priority_healing(Simulation * /*sim*/, Entity const &player) {
    float health_ratio = player.max_health > 0 ? (player.health / player.max_health) : 0.0f;
    // Simple, conservative: the lower the health, the higher the priority (cap at 0.9)
    float need = 1.0f - health_ratio;
    float p = need * 0.9f;
    if (p < 0) p = 0;
    if (p > 0.9f) p = 0.9f;
    return p;
}

float priority_overlevel_escape(Simulation * /*sim*/, Entity const &player) {
    float t = player.get_overlevel_timer();
    float deadline = PETAL_DISABLE_DELAY * TPS;
    if (deadline <= 0) return 0.0f;
    float p = t / deadline;
    if (p < 0) p = 0;
    if (p > 1) p = 1;
    return p * 0.9f;
}

// NEW: fixed, high priority to chase a clear rarity upgrade
float priority_upgrade_drop(Simulation * /*sim*/, Entity const & /*player*/, float best_drop_score, float worst_active_score) {
    // small epsilon so equal-rarity items don't trigger
    if (best_drop_score > worst_active_score + 0.05f) return 0.9f;
    return 0.0f;
}
