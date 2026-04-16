#include "t5_epub_renderer.h"

#include <algorithm>

#include "sdkconfig.h"

#include "../../../components/fastepd/Fonts/Lora_24.h"
#include "../../../components/fastepd/Fonts/Roboto_Black_24.h"

namespace {

bool point_in_triangle(int px, int py, int x0, int y0, int x1, int y1, int x2, int y2)
{
    const int d1 = (px - x1) * (y0 - y1) - (x0 - x1) * (py - y1);
    const int d2 = (px - x2) * (y1 - y2) - (x1 - x2) * (py - y2);
    const int d3 = (px - x0) * (y2 - y0) - (x2 - x0) * (py - y0);
    const bool has_neg = (d1 < 0) || (d2 < 0) || (d3 < 0);
    const bool has_pos = (d1 > 0) || (d2 > 0) || (d3 > 0);
    return !(has_neg && has_pos);
}

}  // namespace

T5EpubRenderer::T5EpubRenderer(FASTEPD &display, int screen_width, int screen_height, int toolbar_height)
    : display_(display), screen_width_(screen_width), screen_height_(screen_height), toolbar_height_(toolbar_height)
{
    display_.setMode(BB_MODE_4BPP);
    display_.setTextWrap(false);
    body_font_ = measure_font(display_, Lora_24);
    heading_font_ = measure_font(display_, Roboto_Black_24);
}

T5EpubRenderer::FontMetrics T5EpubRenderer::measure_font(FASTEPD &display, const void *font)
{
    BB_RECT rect = {};
    display.setFont(font, true);
    display.getStringBox("Tj", &rect);

    FontMetrics metrics = {};
    metrics.font = font;
    metrics.baseline = std::max(0, -rect.y);
    metrics.line_height = std::max(24, rect.h + CONFIG_EPUB_READER_BODY_LINE_GAP);
    return metrics;
}

void T5EpubRenderer::force_full_refresh()
{
    first_refresh_ = true;
}

void T5EpubRenderer::mark_dirty_absolute(int x, int y, int width, int height)
{
    mark_dirty_internal(x, y, width, height);
}

void T5EpubRenderer::refresh_absolute_region(int x, int y, int width, int height)
{
    BB_RECT rect = {.x = x, .y = y, .w = width, .h = height};
    display_.fullUpdate(CLEAR_NONE, true, &rect);
}

uint8_t T5EpubRenderer::map_color(uint8_t color) const
{
    if (color <= 15) {
        return color;
    }
    return (uint8_t)std::clamp<int>((color + 8) / 17, 0, 15);
}

void T5EpubRenderer::select_font(bool bold, bool italic)
{
    const FontMetrics &font = font_for(bold, italic);
    display_.setFont(font.font, true);
    display_.setTextColor(BBEP_BLACK, BBEP_TRANSPARENT);
}

const T5EpubRenderer::FontMetrics &T5EpubRenderer::font_for(bool bold, bool italic) const
{
    (void)italic;
    return bold ? heading_font_ : body_font_;
}

void T5EpubRenderer::mark_dirty_relative(int x, int y, int width, int height)
{
    mark_dirty_internal(origin_x() + x, origin_y() + y, width, height);
}

void T5EpubRenderer::mark_dirty_internal(int x, int y, int width, int height)
{
    if (width <= 0 || height <= 0) {
        return;
    }

    const int clamped_x1 = std::clamp(x, 0, screen_width_ - 1);
    const int clamped_y1 = std::clamp(y, 0, screen_height_ - 1);
    const int clamped_x2 = std::clamp(x + width - 1, 0, screen_width_ - 1);
    const int clamped_y2 = std::clamp(y + height - 1, 0, screen_height_ - 1);

    if (clamped_x2 < clamped_x1 || clamped_y2 < clamped_y1) {
        return;
    }

    if (!dirty_) {
        dirty_rect_.x = clamped_x1;
        dirty_rect_.y = clamped_y1;
        dirty_rect_.w = clamped_x2 - clamped_x1 + 1;
        dirty_rect_.h = clamped_y2 - clamped_y1 + 1;
        dirty_ = true;
        return;
    }

    const int x1 = std::min(dirty_rect_.x, clamped_x1);
    const int y1 = std::min(dirty_rect_.y, clamped_y1);
    const int x2 = std::max(dirty_rect_.x + dirty_rect_.w - 1, clamped_x2);
    const int y2 = std::max(dirty_rect_.y + dirty_rect_.h - 1, clamped_y2);

    dirty_rect_.x = x1;
    dirty_rect_.y = y1;
    dirty_rect_.w = x2 - x1 + 1;
    dirty_rect_.h = y2 - y1 + 1;
}

void T5EpubRenderer::reset_dirty()
{
    dirty_ = false;
    dirty_rect_ = {};
}

void T5EpubRenderer::draw_pixel(int x, int y, uint8_t color)
{
    if (x < 0 || y < 0 || x >= content_width() || y >= content_draw_height()) {
        return;
    }
    display_.drawPixel(origin_x() + x, origin_y() + y, map_color(color));
}

int T5EpubRenderer::get_text_width(const char *text, bool bold, bool italic)
{
    select_font(bold, italic);
    BB_RECT rect = {};
    display_.getStringBox(text, &rect);
    return rect.w;
}

void T5EpubRenderer::draw_text(int x, int y, const char *text, bool bold, bool italic)
{
    const FontMetrics &font = font_for(bold, italic);
    select_font(bold, italic);
    BB_RECT rect = {};
    display_.getStringBox(text, &rect);
    const int draw_x = origin_x() + x;
    const int draw_y = origin_y() + y + font.baseline;
    display_.drawString(text, draw_x, draw_y);
    mark_dirty_internal(draw_x + rect.x, draw_y + rect.y, rect.w, rect.h);
}

void T5EpubRenderer::draw_rect(int x, int y, int width, int height, uint8_t color)
{
    display_.drawRect(origin_x() + x, origin_y() + y, width, height, map_color(color));
    mark_dirty_relative(x, y, width, height);
}

void T5EpubRenderer::draw_triangle(int x0, int y0, int x1, int y1, int x2, int y2, uint8_t color)
{
    const uint8_t mapped = map_color(color);
    display_.drawLine(origin_x() + x0, origin_y() + y0, origin_x() + x1, origin_y() + y1, mapped);
    display_.drawLine(origin_x() + x1, origin_y() + y1, origin_x() + x2, origin_y() + y2, mapped);
    display_.drawLine(origin_x() + x2, origin_y() + y2, origin_x() + x0, origin_y() + y0, mapped);

    const int min_x = std::min({x0, x1, x2});
    const int min_y = std::min({y0, y1, y2});
    const int max_x = std::max({x0, x1, x2});
    const int max_y = std::max({y0, y1, y2});
    mark_dirty_relative(min_x, min_y, max_x - min_x + 1, max_y - min_y + 1);
}

void T5EpubRenderer::draw_circle(int x, int y, int r, uint8_t color)
{
    display_.drawCircle(origin_x() + x, origin_y() + y, r, map_color(color));
    mark_dirty_relative(x - r, y - r, 2 * r + 1, 2 * r + 1);
}

void T5EpubRenderer::fill_triangle(int x0, int y0, int x1, int y1, int x2, int y2, uint8_t color)
{
    const int min_x = std::min({x0, x1, x2});
    const int min_y = std::min({y0, y1, y2});
    const int max_x = std::max({x0, x1, x2});
    const int max_y = std::max({y0, y1, y2});
    const uint8_t mapped = map_color(color);

    for (int py = min_y; py <= max_y; ++py) {
        for (int px = min_x; px <= max_x; ++px) {
            if (point_in_triangle(px, py, x0, y0, x1, y1, x2, y2)) {
                display_.drawPixel(origin_x() + px, origin_y() + py, mapped);
            }
        }
    }
    mark_dirty_relative(min_x, min_y, max_x - min_x + 1, max_y - min_y + 1);
}

void T5EpubRenderer::fill_rect(int x, int y, int width, int height, uint8_t color)
{
    display_.fillRect(origin_x() + x, origin_y() + y, width, height, map_color(color));
    mark_dirty_relative(x, y, width, height);
}

void T5EpubRenderer::fill_circle(int x, int y, int r, uint8_t color)
{
    display_.fillCircle(origin_x() + x, origin_y() + y, r, map_color(color));
    mark_dirty_relative(x - r, y - r, 2 * r + 1, 2 * r + 1);
}

void T5EpubRenderer::needs_gray(uint8_t color)
{
    (void)color;
}

bool T5EpubRenderer::has_gray()
{
    return false;
}

void T5EpubRenderer::show_busy()
{
    const int size = 18;
    const int x = screen_width_ - size - 12;
    const int y = 12;
    display_.fillRect(x, y, size, size, 4);
    mark_dirty_internal(x, y, size, size);
}

void T5EpubRenderer::show_img(int x, int y, int width, int height, const uint8_t *img_buffer)
{
    (void)x;
    (void)y;
    (void)width;
    (void)height;
    (void)img_buffer;
}

void T5EpubRenderer::clear_screen()
{
    display_.fillRect(0, 0, screen_width_, content_height(), 15);
    mark_dirty_internal(0, 0, screen_width_, content_height());
}

void T5EpubRenderer::flush_display()
{
    if (!dirty_) {
        return;
    }

    const int dirty_area = dirty_rect_.w * dirty_rect_.h;
    const int screen_area = screen_width_ * screen_height_;
    const bool use_region_update = !first_refresh_ && dirty_area < (screen_area / 3);

    if (use_region_update) {
        display_.fullUpdate(CLEAR_NONE, true, &dirty_rect_);
    } else {
        display_.fullUpdate(first_refresh_ ? CLEAR_SLOW : CLEAR_NONE, true);
        first_refresh_ = false;
    }

    reset_dirty();
}

void T5EpubRenderer::flush_area(int x, int y, int width, int height)
{
    mark_dirty_relative(x, y, width, height);
}

int T5EpubRenderer::get_page_width()
{
    return content_width();
}

int T5EpubRenderer::get_page_height()
{
    return content_draw_height();
}

int T5EpubRenderer::get_space_width()
{
    const int base_space_width = get_text_width(" ", false, false);
    if (base_space_width <= 0) {
        return 0;
    }

    const int configured_space = (base_space_width * CONFIG_EPUB_READER_SPACE_WIDTH_PERCENT) / 100;
    const int tightened_space = configured_space - CONFIG_EPUB_READER_INTER_WORD_TIGHTEN_PX;
    const int min_space = -std::max(1, base_space_width / 2);
    return std::max(min_space, tightened_space);
}

int T5EpubRenderer::get_line_height()
{
    return body_font_.line_height;
}
