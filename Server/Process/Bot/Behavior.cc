// File: Server/Behavior.cc
#include <Server/Process.hh>
#include <Server/Bot.hh>

#include <Server/Spawn.hh>
#include <Shared/StaticData.hh>
#include <Shared/Entity.hh>
#include <Shared/Simulation.hh>
#include <Shared/Map.hh>

#include <string>

// Helper: set a single thought (slot 0) and clear all others. Always refresh its timer.
static void add_thought(Entity &player, std::string const &msg) {
    player.set_info(0, msg);
    player.thought_timers[0] = (game_tick_t)(5 * TPS);
    for (uint32_t i = 1; i < 4; ++i) {
        if (!player.get_info(i).empty()) player.set_info(i, "");
        player.thought_timers[i] = 0;
    }
}

static void tick_thoughts(Entity &player) {
    if (player.thought_timers[0] > 0) {
        --player.thought_timers[0];
        if (player.thought_timers[0] == 0) player.set_info(0, "");
    }
    for (uint32_t i = 1; i < 4; ++i) {
        if (player.thought_timers[i] > 0) player.thought_timers[i] = 0;
        if (!player.get_info(i).empty()) player.set_info(i, "");
    }
}

static void ensure_spawn_basics(Entity &player) {
    if (player.get_name().empty()) {
        player.set_name("Bot");
    }
    player.set_loadout_count(loadout_slots_at_level(score_to_level(player.get_score())));
    ensure_basics_if_empty(player);
}

static std::string petal_name(PetalID::T id) {
    if (id < PetalID::kNumPetals) return PETAL_DATA[id].name ? PETAL_DATA[id].name : "Unknown Petal";
    return "Unknown Petal";
}

static std::string mob_name(MobID::T id) {
    if (id < MobID::kNumMobs) return MOB_DATA[id].name ? MOB_DATA[id].name : "Unknown Mob";
    return "Unknown Mob";
}

static std::string entity_display_name(Entity const &e) {
    if (e.has_component(kName) && !e.get_name().empty()) return e.get_name();
    if (e.has_component(kMob)) return mob_name(e.get_mob_id());
    return "target";
}

void tick_bot_player_behavior(Simulation *sim, Entity &player) {
    if (player.pending_delete) return;
    if (!player.has_component(kFlower)) return;
    if (!player.is_bot) return;
    if (!sim->ent_alive(player.get_parent())) return;

    ensure_spawn_basics(player);
    ensure_basics_if_empty(player);

    // Immediately promote any strictly higher-rarity reserve into active (one swap max)
    promote_highest_rarity_once(player);

    // Gather info
    float best_drop_score = 0.0f;
    EntityID best_drop = find_best_drop(sim, player, 900.0f, best_drop_score);

    float worst_active = 1e9f; int worst_active_idx = -1;
    float worst_reserve = 1e9f; int worst_reserve_idx = -1;
    worst_slots(player, worst_active, worst_active_idx, worst_reserve, worst_reserve_idx);
    if (worst_active == 1e9f) worst_active = 0.0f;
    if (worst_reserve == 1e9f) worst_reserve = 0.0f;

    // Compute priorities
    float prio_ovl = priority_overlevel_escape(sim, player);
    float prio_heal = priority_healing(sim, player);
    float prio_ret  = priority_retaliation(sim, player);
    float prio_drop = priority_obtain_drop(sim, player, best_drop_score, worst_active);
    float prio_mob  = priority_aggro_mobs(sim, player);
    float prio_plr  = priority_target_players(sim, player);
    float prio_upg  = priority_upgrade_drop(sim, player, best_drop_score, worst_active); // fixed 0.9 on clear upgrade

    enum Action { kNone, kOverlevel, kHeal, kRetaliate, kUpgradeDrop, kObtainDrop, kAggroMob, kTargetPlayer, kIdle };
    Action act = kNone;
    float bestp = 0.0f;
    auto consider = [&](Action a, float p){ if (p > bestp) { bestp = p; act = a; } };

    consider(kOverlevel,   prio_ovl);
    consider(kHeal,        prio_heal);
    consider(kRetaliate,   prio_ret);
    consider(kUpgradeDrop, prio_upg);   // strong, fixed 0.9 when higher-rarity drop exists
    consider(kObtainDrop,  prio_drop);  // fallback, softer scaling priority
    consider(kAggroMob,    prio_mob);
    consider(kTargetPlayer,prio_plr);

    // Execute chosen action
    if (act == kOverlevel && bestp > 0) {
        if (maybe_relocate_to_zone(sim, player)) {
            add_thought(player, "Relocating to a suitable zone");
            tick_thoughts(player);
            ++player.ai_tick; 
            return;
        }
    }

    if (act == kHeal && bestp > 0) {
        if (!ensure_heal_equipped(player)) {
            float heal_drop_score = 0.0f;
            EntityID heal_drop = find_best_heal_drop(sim, player, 900.0f, heal_drop_score);
            if (sim->ent_alive(heal_drop)) {
                if (try_ensure_space_for_drop(sim, player, heal_drop_score, worst_active, worst_active_idx, worst_reserve, worst_reserve_idx)) {
                    player.input = 0;
                    Entity &d = sim->get_ent(heal_drop);
                    Vector v(d.get_x() - player.get_x(), d.get_y() - player.get_y());
                    if (v.magnitude() > 0) v.set_magnitude(PLAYER_ACCELERATION);
                    player.acceleration = v;
                    player.set_angle(v.angle());
                    PetalID::T pid = d.get_drop_id();
                    add_thought(player, "Going to pick up " + petal_name(pid) + " for healing");
                    tick_thoughts(player);
                    ++player.ai_tick;
                    return;
                }
            } else {
                EntityID heal_mob = find_nearest_heal_mob(sim, player, 900.0f);
                if (sim->ent_alive(heal_mob)) {
                    player.input = 0;
                    Entity &m = sim->get_ent(heal_mob);
                    engage_with_petals(sim, player, heal_mob);
                    add_thought(player, "Farming " + mob_name(m.get_mob_id()) + " for a heal");
                    tick_thoughts(player);
                    ++player.ai_tick; 
                    return;
                }
            }
        }
        // Default healing behavior: fight nearby while regen
        EntityID target = find_nearest_mob(sim, player, 900.0f);
        if (sim->ent_alive(target)) {
            player.input = 0;
            Entity &t = sim->get_ent(target);
            engage_with_petals(sim, player, target);
            add_thought(player, "Fighting while healing");
            tick_thoughts(player);
            ++player.ai_tick; 
            return;
        }
    }

    if (act == kRetaliate && bestp > 0) {
        EntityID target = select_retaliation_target(sim, player, (game_tick_t)(2 * TPS));
        if (sim->ent_alive(target)) {
            player.input = 0;
            Entity &t = sim->get_ent(target);
            engage_with_petals(sim, player, target);
            add_thought(player, "Retaliating against " + entity_display_name(t));
            tick_thoughts(player);
            ++player.ai_tick; 
            return;
        }
    }

    if (act == kUpgradeDrop && bestp > 0 && sim->ent_alive(best_drop)) {
        if (try_ensure_space_for_drop(sim, player, best_drop_score, worst_active, worst_active_idx, worst_reserve, worst_reserve_idx)) {
            player.input = 0;
            Entity &d = sim->get_ent(best_drop);
            Vector v(d.get_x() - player.get_x(), d.get_y() - player.get_y());
            if (v.magnitude() > 0) v.set_magnitude(PLAYER_ACCELERATION);
            player.acceleration = v;
            player.set_angle(v.angle());
            PetalID::T pid = d.get_drop_id();
            add_thought(player, "Upgrading: going for " + petal_name(pid));
            tick_thoughts(player);
            ++player.ai_tick; 
            return;
        }
    }

    if (act == kObtainDrop && bestp > 0 && sim->ent_alive(best_drop)) {
        if (try_ensure_space_for_drop(sim, player, best_drop_score, worst_active, worst_active_idx, worst_reserve, worst_reserve_idx)) {
            player.input = 0;
            Entity &d = sim->get_ent(best_drop);
            Vector v(d.get_x() - player.get_x(), d.get_y() - player.get_y());
            if (v.magnitude() > 0) v.set_magnitude(PLAYER_ACCELERATION);
            player.acceleration = v;
            player.set_angle(v.angle());
            PetalID::T pid = d.get_drop_id();
            add_thought(player, "Going for " + petal_name(pid));
            tick_thoughts(player);
            ++player.ai_tick; 
            return;
        }
    }

    if (act == kAggroMob && bestp > 0) {
        EntityID target = find_nearest_mob(sim, player, 900.0f);
        if (sim->ent_alive(target)) {
            player.input = 0;
            Entity &t = sim->get_ent(target);
            engage_with_petals(sim, player, target);
            add_thought(player, "Engaging " + mob_name(t.get_mob_id()));
            tick_thoughts(player);
            ++player.ai_tick; 
            return;
        }
    }

    if (act == kTargetPlayer && bestp > 0) {
        EntityID target = find_nearest_player(sim, player, 900.0f);
        if (sim->ent_alive(target)) {
            player.input = 0;
            Entity &t = sim->get_ent(target);
            engage_with_petals(sim, player, target);
            add_thought(player, "Engaging player " + entity_display_name(t));
            tick_thoughts(player);
            ++player.ai_tick; 
            return;
        }
    }

    // Final fallback: opportunistic engage or wander
    EntityID nearby_target = find_nearest_mob(sim, player, 700.0f);
    if (sim->ent_alive(nearby_target)) {
        player.input = 0;
        Entity &t = sim->get_ent(nearby_target);
        engage_with_petals(sim, player, nearby_target);
        add_thought(player, "Exploring: engaging " + mob_name(t.get_mob_id()));
        tick_thoughts(player);
        ++player.ai_tick;
        return;
    }

    apply_wander_biased(sim, player, 0.5f, 0.6f);
    add_thought(player, "Exploring the arena");
    if (maybe_relocate_to_zone(sim, player)) {
        add_thought(player, "Relocating to a suitable zone");
    }
    tick_thoughts(player);
    ++player.ai_tick;
}
