#include <Client/Ui/InGame/GameInfo.hh>

#include <Client/Ui/Container.hh>

#include <Client/Game.hh>

#include <string>

#include <iostream>

using namespace Ui;

static float const LEADERBOARD_WIDTH = 180;

LeaderboardSlot::LeaderboardSlot(uint8_t p) : Element(LEADERBOARD_WIDTH, 18) , pos(p) {
    ratio.set(1);
    style.animate = [&](Element *, Renderer &){
        if (pos >= Game::simulation.arena_info.player_count) return;
        float r = 1;
        if (((float) Game::simulation.arena_info.scores[0]) != 0) 
            r = (float) Game::simulation.arena_info.scores[pos] / (float) Game::simulation.arena_info.scores[0];
        ratio.set(r);
        ratio.step(Ui::lerp_amount);
    };
}

void LeaderboardSlot::on_render(Renderer &ctx) {
    if (pos >= Game::simulation.arena_info.player_count) return;

    // draw background track
    ctx.set_stroke(0xff222222);
    ctx.set_line_width(height);
    ctx.round_line_cap();
    ctx.begin_path();
    ctx.move_to(-(width-height)/2,0);
    ctx.line_to((width-height)/2,0);
    ctx.stroke();

    // determine this slot's rank (1 = first)
    uint32_t player_count = Game::simulation.arena_info.player_count;
    int rank = 1;
    float my_score = (float) Game::simulation.arena_info.scores[pos];
    for (uint32_t i = 0; i < player_count; ++i) {
        if ((float) Game::simulation.arena_info.scores[i] > my_score) ++rank;
    }

    // pick color: gold/silver/bronze for top 3, otherwise the flower color
    uint32_t bar_color;
    if (rank == 1) bar_color = 0xffffd700;      // gold
    else if (rank == 2) bar_color = 0xffc0c0c0; // silver
    else if (rank == 3) bar_color = 0xffcd7f32; // bronze
    else bar_color = FLOWER_COLORS[Game::simulation.arena_info.colors[pos]];

    // draw the value bar
    ctx.set_stroke(bar_color);
    ctx.set_line_width(height * 0.8);
    ctx.begin_path();
    ctx.move_to(-(width-height)/2,0);
    ctx.line_to(-(width-height)/2 + (width-height) * ((float) ratio), 0);
    ctx.stroke();

    // text
    std::string format_string = std::format("{} - {}",
        Game::simulation.arena_info.names[pos].size() == 0 ? "Unnamed Flower" : Game::simulation.arena_info.names[pos],
        format_score((float) Game::simulation.arena_info.scores[pos]));
    ctx.set_fill(0xffffffff);
    ctx.set_stroke(0xff222222);
    ctx.center_text_align();
    ctx.center_text_baseline();
    ctx.set_text_size(height * 0.75);
    ctx.set_line_width(height * 0.75 * 0.12);
    ctx.stroke_text(format_string.c_str());
    ctx.fill_text(format_string.c_str());
}


Element *Ui::make_leaderboard() {
    Container *lb_header = new Ui::Container({
new Ui::DynamicText(18, [](){
    int count = Game::simulation.arena_info.player_count;
    std::string format_string;

    if (count == 1) {
        format_string = "1 Flower";
    } else {
        int display_count = count;
        if (count > 5) {
            display_count += (count / 4);
        }
        format_string = std::format("{} Flowers", display_count);
    }

    return format_string;
})

    }, LEADERBOARD_WIDTH + 20, 48, { .fill = 0xff55bb55, .line_width = 6, .round_radius = 7 });

    Element *leaderboard = new Ui::VContainer({
        lb_header,
        new Ui::VContainer(
            Ui::make_range(0, LEADERBOARD_SIZE, [](uint32_t i){ return (Element *) (new Ui::LeaderboardSlot(i)); })
        , 10, 4, {})
    }, 0, 0, { 
        .fill = 0xff555555,
        .line_width = 6,
        .round_radius = 7,
        .should_render = [](){ return Game::should_render_game_ui(); },
        .no_polling = 1
    });
    leaderboard->style.h_justify = Style::Right;
    leaderboard->style.v_justify = Style::Top;
    leaderboard->x = -20;
    leaderboard->y = 20;
    return leaderboard;
}
