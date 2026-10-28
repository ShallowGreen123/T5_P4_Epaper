#pragma once

#include <stdint.h>

#include <FastEPD.h>
#include "Renderer/Renderer.h"

class T5EpubRenderer : public Renderer {
public:
    T5EpubRenderer(FASTEPD &display, int screen_width, int screen_height, int toolbar_height);

    void force_full_refresh();
    void mark_dirty_absolute(int x, int y, int width, int height);
    void refresh_absolute_region(int x, int y, int width, int height);

    FASTEPD &display() { return display_; }
    int screen_width() const { return screen_width_; }
    int screen_height() const { return screen_height_; }
    int toolbar_height() const { return toolbar_height_; }
    int content_height() const { return screen_height_ - toolbar_height_; }

    void draw_pixel(int x, int y, uint8_t color) override;
    int get_text_width(const char *text, bool bold = false, bool italic = false) override;
    void draw_text(int x, int y, const char *text, bool bold = false, bool italic = false) override;
    void draw_rect(int x, int y, int width, int height, uint8_t color = 0) override;
    void draw_triangle(int x0, int y0, int x1, int y1, int x2, int y2, uint8_t color) override;
    void draw_circle(int x, int y, int r, uint8_t color = 0) override;
    void fill_triangle(int x0, int y0, int x1, int y1, int x2, int y2, uint8_t color) override;
    void fill_rect(int x, int y, int width, int height, uint8_t color = 0) override;
    void fill_circle(int x, int y, int r, uint8_t color = 0) override;
    void needs_gray(uint8_t color) override;
    bool has_gray() override;
    void show_busy() override;
    void show_img(int x, int y, int width, int height, const uint8_t *img_buffer) override;
    void clear_screen() override;
    void flush_display() override;
    void flush_area(int x, int y, int width, int height) override;
    int get_page_width() override;
    int get_page_height() override;
    int get_space_width() override;
    int get_line_height() override;

private:
    struct FontMetrics {
        const void *font;
        int baseline;
        int line_height;
    };

    FASTEPD &display_;
    int screen_width_;
    int screen_height_;
    int toolbar_height_;
    bool first_refresh_ = true;
    bool dirty_ = false;
    BB_RECT dirty_rect_ = {};
    FontMetrics body_font_ = {};
    FontMetrics heading_font_ = {};

    static FontMetrics measure_font(FASTEPD &display, const void *font);

    uint8_t map_color(uint8_t color) const;
    int origin_x() const { return margin_left; }
    int origin_y() const { return margin_top; }
    int content_width() const { return screen_width_ - margin_left - margin_right; }
    int content_draw_height() const { return content_height() - margin_top - margin_bottom; }

    void select_font(bool bold, bool italic);
    const FontMetrics &font_for(bool bold, bool italic) const;
    void mark_dirty_relative(int x, int y, int width, int height);
    void mark_dirty_internal(int x, int y, int width, int height);
    void reset_dirty();
};
