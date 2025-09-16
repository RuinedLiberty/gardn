#include <Server/Bot.hh>

#include <Shared/Entity.hh>
#include <Shared/Simulation.hh>
#include <Shared/StaticData.hh>
#include <Shared/Map.hh>

// ---------------- basic finders ----------------
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

EntityID find_nearest_player(Simulation *sim, Entity const &player, float radius) {
    EntityID best = NULL_ENTITY;
    float min_dist = radius;
    sim->spatial_hash.query(player.get_x(), player.get_y(), radius, radius, [&](Simulation *sim2, Entity &ent){
        if (!sim2->ent_alive(ent.id)) return;
        if (!ent.has_component(kFlower)) return;
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

EntityID find_best_heal_drop(Simulation *sim, Entity const &player, float radius, float &score) {
    EntityID best = NULL_ENTITY;
    score = 0.0f;
    sim->spatial_hash.query(player.get_x(), player.get_y(), radius, radius, [&](Simulation *sim2, Entity &ent){
        if (!sim2->ent_alive(ent.id)) return;
        if (!ent.has_component(kDrop)) return;
        if (ent.immunity_ticks > 0) return;
        PetalID::T drop_id = ent.get_drop_id();
        if (!is_healing_petal(drop_id)) return;
        float s = petal_score(drop_id);
        if (s > score) { score = s; best = ent.id; }
    });
    return best;
}

EntityID find_nearest_heal_mob(Simulation *sim, Entity const &player, float radius) {
    EntityID best = NULL_ENTITY;
    float min_dist = radius;
    sim->spatial_hash.query(player.get_x(), player.get_y(), radius, radius, [&](Simulation *sim2, Entity &ent){
        if (!sim2->ent_alive(ent.id)) return;
        if (!ent.has_component(kMob)) return;
        if (ent.get_team() == player.get_team()) return;
        if (ent.immunity_ticks > 0) return;

        bool drops_heal = false;
        auto const &md = MOB_DATA[ent.get_mob_id()];
        for (uint32_t i = 0; i < md.drops.size(); ++i) {
            PetalID::T pid = md.drops[i];
            if (is_healing_petal(pid)) { drops_heal = true; break; }
        }
        if (!drops_heal) return;

        float d = Vector(ent.get_x() - player.get_x(), ent.get_y() - player.get_y()).magnitude();
        if (d < min_dist) { min_dist = d; best = ent.id; }
    });
    return best;
}
