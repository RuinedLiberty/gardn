#include <Client/Render/RenderEntity.hh>

#include <Client/Render/Renderer.hh>

#include <Client/Game.hh>

#include <Shared/Entity.hh>

void render_name(Renderer &ctx, Entity const &ent) {
    if (!ent.get_nametag_visible()) return;
    if (ent.id == Game::player_id) return;

    // Original name rendering
    ctx.translate(0, -ent.get_radius() - 18);
    ctx.set_global_alpha(1 - ent.deletion_animation);
    float scaleFactor = 1 + 0.5f * ent.deletion_animation;
    ctx.scale(scaleFactor);
    ctx.draw_text(ent.get_name().c_str(), { .size = 18 });

    // Thought bubble: single line (latest message only)
    ctx.scale(1.0f / scaleFactor);
    ctx.translate(0, -26.0f);

    // Styling
    float text_size = 16.0f;
    float pad_x = 8.0f;
    float pad_y = 6.0f;
    uint32_t bg = 0xeeffffff;
    uint32_t stroke = 0x55000000;

    ctx.center_text_align();
    ctx.center_text_baseline();

    std::string s = ent.get_info(0);
    if (!s.empty()) {
        ctx.set_text_size(text_size);
        float text_w = ctx.get_text_size(s.c_str());
        float bubble_w = text_w + 2 * pad_x;
        float bubble_h = text_size + 2 * pad_y;

        float left = -bubble_w * 0.5f;
        float top = 0.0f;

        // Bubble background (fresh path to avoid bleeding into other shapes)
        ctx.begin_path();
        ctx.set_fill(bg);
        ctx.set_stroke(stroke);
        ctx.set_line_width(1.0f);
        ctx.round_rect(left, top, bubble_w, bubble_h, 7.0f);
        ctx.fill(1);
        ctx.stroke();

        // Bubble text (centered)
        ctx.translate(0, top + bubble_h * 0.5f);
        ctx.draw_text(s.c_str(), {
            .fill = 0xff000000,   // black text
            .stroke = 0x00000000,
            .size = text_size,
            .stroke_scale = 0.0f
        });
        ctx.translate(0, -(top + bubble_h * 0.5f));
    }
}