#include <Server/Process.hh>

#include <Server/Spawn.hh>
#include <Server/EntityFunctions.hh>
#include <Shared/Entity.hh>
#include <Shared/Simulation.hh>
#include <Shared/Map.hh>
#include <Shared/StaticData.hh>
#include <algorithm>

// Simple server-side bot controller that makes Flower entities marked as is_bot behave like players.
// It only runs on server and uses existing Flower/Camera/AI systems, so clients see bots as normal players.

static void _ensure_spawn(Simulation *sim, Entity &player) {
    if (!sim->ent_alive(player.get_parent())) return;
    Entity &camera = sim->get_ent(player.get_parent());
    (void) camera;
    if (player.get_name().empty()) {
        // give a simple name
        player.set_name("Bot");
    }
    // Keep loadout_count consistent with level
    player.set_loadout_count(loadout_slots_at_level(score_to_level(player.get_score())));
}

void tick_bot_player_behavior(Simulation *sim, Entity &player) {
    if (player.pending_delete) return;
    if (!player.has_component(kFlower)) return;
    if (!player.is_bot) return;

    // Require a valid camera parent (camera manages respawn)
    if (!sim->ent_alive(player.get_parent())) return;

    _ensure_spawn(sim, player);

    // Behavior: simple chase nearest enemy like aggro mobs, with attack/defend toggles
    player.input = 0;

    // Locate nearest enemy flower or mob
    EntityID target = find_nearest_enemy(sim, player, 900);
    if (sim->ent_alive(target)) {
        Entity &t = sim->get_ent(target);
        Vector v(t.get_x() - player.get_x(), t.get_y() - player.get_y());
        float dist = v.magnitude();
        float speed = 1.0f;
        v.set_magnitude(PLAYER_ACCELERATION * speed);
        player.acceleration = v;
        player.set_angle(v.angle());
        // Toggle attack or defend based on health and distance
        if (player.health / std::max(player.max_health, 1.0f) > 0.35f && dist < 700) {
            BitMath::set(player.input, InputFlags::kAttacking);
        } else if (dist < 250) {
            BitMath::set(player.input, InputFlags::kDefending);
        }
    } else {
        // Wander idly:
        if ((player.lifetime % (TPS * 2)) == 0) {
            player.set_angle(frand() * 2 * M_PI);
        }
        Vector v; v.unit_normal(player.get_angle());
        v.set_magnitude(PLAYER_ACCELERATION * 0.5f);
        player.acceleration = v;
    }
}