#include <Server/Bot.hh>

#include <Shared/Entity.hh>
#include <Shared/Simulation.hh>
#include <Shared/StaticData.hh>
#include <Shared/Map.hh>
#include <Helpers/Math.hh>

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

// Helper: nearest hostile mob within radius, returns pointer or nullptr
static Entity *nearest_hostile_mob(Simulation *sim, Entity const &player, float radius, float &out_dist) {
    Entity *best = nullptr;
    float min_dist = radius;
    sim->spatial_hash.query(player.get_x(), player.get_y(), radius, radius, [&](Simulation *sim2, Entity &ent){
        if (!sim2->ent_alive(ent.id)) return;
        if (!ent.has_component(kMob)) return;
        if (ent.get_team() == player.get_team()) return;
        if (ent.immunity_ticks > 0) return;
        float d = Vector(ent.get_x() - player.get_x(), ent.get_y() - player.get_y()).magnitude();
        if (d < min_dist) { min_dist = d; best = &ent; }
    });
    out_dist = min_dist;
    return best;
}

// Helper: smooth steering towards target angle, with optional avoidance repulsion
// IMPORTANT: This function MUST NOT write to player.heading_angle (that controls petal orbit).
static void steer_with_avoidance(Simulation *sim, Entity &player, float target_angle, float speed_scale) {
    // Base desired direction from target angle
    Vector desired_dir(cosf(target_angle), sinf(target_angle));

    // Awareness: avoid getting too close to mobs while wandering/searching
    float dist = 0.0f;
    Entity *threat = nearest_hostile_mob(sim, player, 220.0f, dist);
    if (threat) {
        Vector away(player.get_x() - threat->get_x(), player.get_y() - threat->get_y());
        float safe = player.get_radius() + threat->get_radius() + 30.0f;
        float soft = player.get_radius() + threat->get_radius() + 100.0f;

        float repel = 0.0f;
        if (dist < safe && dist > 1.0f) {
            repel = fclamp((safe - dist) / safe, 0.0f, 1.0f) * 1.5f; // strong push if too close
        } else if (dist < soft && dist > 1.0f) {
            repel = fclamp((soft - dist) / soft, 0.0f, 1.0f) * 0.6f; // gentle steer if a bit close
        }

        if (repel > 0.0f) {
            away.normalize();
            desired_dir = desired_dir * 1.0f + away * repel;
        }
    }

    // Smooth heading change using the body's current angle (NOT heading_angle)
    float target = desired_dir.angle();
    float current = player.get_angle();
    float new_angle = angle_lerp(current, target, 0.12f);

    // Apply acceleration and visual facing
    Vector final_dir(cosf(new_angle), sinf(new_angle));
    player.input = 0;
    player.acceleration = final_dir * (PLAYER_ACCELERATION * speed_scale);
    player.set_angle(new_angle);
}

// Public: wandering that meanders around (no “marching right”), with mild bias if desired
void apply_wander_biased(Simulation *sim, Entity &player, float bias_right, float speed_scale) {
    // Build a smooth pseudo-noise heading from lifetime and a stable per-entity phase
    float t = (float)player.lifetime / (float)TPS;
    float phase1 = (player.id.id * 0.123f) + 1.234f;
    float phase2 = (player.id.id * 0.321f) + 4.321f;

    // Two sines at incommensurate frequencies => gentle, continuous direction changes
    float n = sinf(t * 0.7f + phase1) * 0.7f + sinf(t * 1.3f + phase2) * 0.3f; // [-1..1]
    float bias = (bias_right - 0.5f) * 0.6f; // map [0..1] -> ~[-0.3..+0.3] rad
    float target_angle = n * (float)M_PI + bias;

    steer_with_avoidance(sim, player, target_angle, speed_scale);
}
