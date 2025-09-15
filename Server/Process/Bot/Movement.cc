#include <Server/Bot.hh>

#include <Shared/Entity.hh>
#include <Shared/Simulation.hh>
#include <Shared/StaticData.hh>
#include <Shared/Map.hh>

void engage_with_petals(Simulation *sim, Entity &player, EntityID target_id) {
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

uint8_t maybe_relocate_to_zone(Simulation *sim, Entity &player) {
    uint32_t level = score_to_level(player.get_score());
    uint32_t desired_difficulty = Map::difficulty_at_level(level);
    uint32_t current_zone_idx = Map::get_zone_from_pos(player.get_x(), player.get_y());
    ZoneDefinition const &curr_zone = MAP_DATA[current_zone_idx];

    if (curr_zone.difficulty >= desired_difficulty) return 0;

    float eager = player.get_overlevel_timer() > 0.5f * TPS ? 1.0f : 0.2f;
    if (frand() > eager) return 0;

    uint32_t target_zone_idx = Map::get_suitable_difficulty_zone(desired_difficulty);
    ZoneDefinition const &target_zone = MAP_DATA[target_zone_idx];
    float tx = 0.5f * (target_zone.left + target_zone.right);
    float ty = 0.5f * (target_zone.top + target_zone.bottom);

    player.input = 0;
    Vector v(tx - player.get_x(), ty - player.get_y());
    if (v.magnitude() > 0) v.set_magnitude(PLAYER_ACCELERATION);
    player.acceleration = v;
    player.set_angle(v.angle());
    return 1;
}

void apply_idle_march_right(Entity &player) {
    player.input = 0;
    Vector right(1.0f, 0.0f);
    right.set_magnitude(PLAYER_ACCELERATION * 0.6f);
    player.acceleration = right;
    player.set_angle(0.0f);
}