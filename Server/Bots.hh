#pragma once

#include <Shared/StaticData.hh>
#include <Shared/Entity.hh>
#include <Shared/Simulation.hh>

// Utilities to spawn and manage bot players server-side.
namespace Bots {
    // Spawns a camera + player pair like a real client would get, but marks the player as bot.
    // Returns camera Entity& and writes out the player id.
    inline Entity &spawn_bot(Simulation *sim, uint8_t color = ColorID::kGray, const char *name = "Bot") {
        Entity &cam = sim->alloc_ent();
        cam.add_component(kCamera);
        cam.add_component(kRelations);
        cam.set_team(cam.id);
        cam.set_color(color);
        cam.set_fov(BASE_FOV);
        cam.set_respawn_level(1);
        for (uint32_t i = 0; i < loadout_slots_at_level(cam.get_respawn_level()); ++i)
            cam.set_inventory(i, PetalID::kBasic);
        // allocate the player
        Entity &player = alloc_player(sim, cam.get_team());
        player.is_bot = 1;
        player.set_name(name);
        player_spawn(sim, cam, player);
        return cam;
    }
}
