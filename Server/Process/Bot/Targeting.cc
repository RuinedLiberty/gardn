#include <Server/Bot.hh>

#include <Shared/Entity.hh>
#include <Shared/Simulation.hh>
#include <Shared/StaticData.hh>
#include <Shared/Map.hh>

EntityID find_nearest_mob(Simulation *sim, Entity const &player, float radius) {
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

EntityID find_best_drop(Simulation *sim, Entity const &player, float radius, float &best_drop_score) {
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

EntityID select_retaliation_target(Simulation *sim, Entity const &player, game_tick_t window_ticks) {
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