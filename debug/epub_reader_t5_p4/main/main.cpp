#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include <memory>
#include <string>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include "EpubList/EpubList.h"
#include "EpubList/EpubReader.h"
#include "EpubList/EpubToc.h"
#include "t5_epub_board.h"
#include "t5_epub_renderer.h"

#ifndef CONFIG_EPUB_READER_TASK_STACK_SIZE
#define CONFIG_EPUB_READER_TASK_STACK_SIZE 65536
#endif

namespace {

constexpr char kTag[] = "epub_reader";
constexpr char kSdMountPoint[] = "/sdcard";
constexpr int kToolbarHeight = 96;
constexpr int kToolbarPadding = 12;

enum class UiMode {
    BookList,
    Toc,
    Reader,
    Error,
};

enum class UiAction {
    None,
    Prev,
    Next,
    Select,
};

struct ToolbarButton {
    BB_RECT rect;
    const char *label;
    UiAction action;
};

struct ToolbarLayout {
    ToolbarButton prev;
    ToolbarButton next;
    ToolbarButton select;
};

struct AppState {
    UiMode mode = UiMode::BookList;
    EpubListState list_state = {};
    EpubTocState toc_state = {};
    std::unique_ptr<EpubList> list;
    std::unique_ptr<EpubToc> toc;
    std::unique_ptr<EpubReader> reader;
    std::string library_path;
    std::string error_message;
};

bool directory_exists(const char *path)
{
    struct stat st = {};
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

void reset_list_state(EpubListState &state)
{
    memset(&state, 0, sizeof(state));
    state.previous_rendered_page = -1;
    state.previous_selected_item = -1;
}

void reset_toc_state(EpubTocState &state)
{
    memset(&state, 0, sizeof(state));
    state.previous_rendered_page = -1;
    state.previous_selected_item = -1;
}

ToolbarLayout make_toolbar_layout(int screen_width, int screen_height, UiMode mode)
{
    const int top = screen_height - kToolbarHeight;
    const int cell_width = screen_width / 3;
    const char *select_label = (mode == UiMode::Reader) ? "Back" : (mode == UiMode::Error) ? "Retry" : "Select";

    return {
        .prev = {.rect = {.x = 0, .y = top, .w = cell_width, .h = kToolbarHeight}, .label = "Prev", .action = UiAction::Prev},
        .next = {.rect = {.x = cell_width, .y = top, .w = cell_width, .h = kToolbarHeight}, .label = "Next", .action = UiAction::Next},
        .select = {.rect = {.x = cell_width * 2, .y = top, .w = screen_width - (cell_width * 2), .h = kToolbarHeight}, .label = select_label, .action = UiAction::Select},
    };
}

void draw_toolbar_button(T5EpubRenderer &renderer, const ToolbarButton &button, bool pressed)
{
    FASTEPD &display = renderer.display();
    const uint8_t bg = pressed ? 2 : 15;
    const uint8_t fg = pressed ? 15 : 0;

    display.fillRect(button.rect.x, button.rect.y, button.rect.w, button.rect.h, bg);
    display.drawRect(button.rect.x, button.rect.y, button.rect.w, button.rect.h, 0);
    renderer.mark_dirty_absolute(button.rect.x, button.rect.y, button.rect.w, button.rect.h);

    display.setFont(FONT_12x16);
    display.setTextWrap(false);
    display.setTextColor(fg, BBEP_TRANSPARENT);
    display.setCursor(0, 0);
    BB_RECT text_rect = {};
    display.getStringBox(button.label, &text_rect);
    const int text_x = button.rect.x + (button.rect.w - text_rect.w) / 2;
    const int text_y = button.rect.y + (button.rect.h - text_rect.h) / 2;
    display.drawString(button.label, text_x, text_y);
    renderer.mark_dirty_absolute(text_x, text_y, text_rect.w, text_rect.h);
}

void draw_toolbar(T5EpubRenderer &renderer, UiMode mode, UiAction pressed_action)
{
    const ToolbarLayout layout = make_toolbar_layout(renderer.screen_width(), renderer.screen_height(), mode);
    draw_toolbar_button(renderer, layout.prev, pressed_action == UiAction::Prev);
    draw_toolbar_button(renderer, layout.next, pressed_action == UiAction::Next);
    draw_toolbar_button(renderer, layout.select, pressed_action == UiAction::Select);
}

UiAction hit_test_toolbar(const T5EpubRenderer &renderer, UiMode mode, int16_t touch_x, int16_t touch_y)
{
    const ToolbarLayout layout = make_toolbar_layout(renderer.screen_width(), renderer.screen_height(), mode);
    const ToolbarButton buttons[] = {layout.prev, layout.next, layout.select};

    for (const ToolbarButton &button : buttons) {
        if (touch_x >= button.rect.x && touch_x < (button.rect.x + button.rect.w) &&
            touch_y >= button.rect.y && touch_y < (button.rect.y + button.rect.h)) {
            return button.action;
        }
    }
    return UiAction::None;
}

void flash_toolbar_action(T5EpubRenderer &renderer, UiMode mode, UiAction action)
{
    if (action == UiAction::None) {
        return;
    }
    const ToolbarLayout layout = make_toolbar_layout(renderer.screen_width(), renderer.screen_height(), mode);
    const ToolbarButton *button = nullptr;

    switch (action) {
        case UiAction::Prev:
            button = &layout.prev;
            break;
        case UiAction::Next:
            button = &layout.next;
            break;
        case UiAction::Select:
            button = &layout.select;
            break;
        case UiAction::None:
        default:
            return;
    }

    draw_toolbar_button(renderer, *button, true);
    renderer.refresh_absolute_region(button->rect.x, button->rect.y, button->rect.w, button->rect.h);
}

void render_error_screen(T5EpubRenderer &renderer, const std::string &message)
{
    renderer.clear_screen();
    renderer.draw_text_box("EPUB Reader", kToolbarPadding, 24, renderer.get_page_width() - 24, 60, true, false);
    renderer.draw_text_box(message, kToolbarPadding, 110, renderer.get_page_width() - 24, 240, false, false);
}

bool choose_library_path(AppState &app)
{
    if (directory_exists(CONFIG_EPUB_READER_BOOK_DIR)) {
        app.library_path = CONFIG_EPUB_READER_BOOK_DIR;
        return true;
    }
    if (directory_exists(CONFIG_EPUB_READER_FALLBACK_DIR)) {
        app.library_path = CONFIG_EPUB_READER_FALLBACK_DIR;
        return true;
    }
    app.library_path.clear();
    return false;
}

void reset_navigation_state(AppState &app)
{
    app.list.reset();
    app.toc.reset();
    app.reader.reset();
    reset_list_state(app.list_state);
    reset_toc_state(app.toc_state);
}

bool ensure_book_list_loaded(AppState &app, T5EpubRenderer &renderer)
{
    if (!app.list) {
        app.list = std::make_unique<EpubList>(&renderer, app.list_state);
    }
    return app.list->load(app.library_path.c_str());
}

bool rescan_books(AppState &app, T5P4Board &board, T5EpubRenderer &renderer)
{
    reset_navigation_state(app);

    board.unmount_sd_card(kSdMountPoint);
    if (!board.mount_sd_card(kSdMountPoint)) {
        app.mode = UiMode::Error;
        app.error_message = "Insert an SD card, then tap Retry.";
        return false;
    }

    if (!choose_library_path(app)) {
        app.mode = UiMode::Error;
        app.error_message = "SD card mounted, but neither /sdcard/books nor /sdcard is readable.";
        return false;
    }

    if (!ensure_book_list_loaded(app, renderer)) {
        app.mode = UiMode::Error;
        app.error_message = "Failed to scan the EPUB directory. Check the SD card and tap Retry.";
        return false;
    }

    app.mode = UiMode::BookList;
    return true;
}

bool render_book_list(AppState &app, T5EpubRenderer &renderer, UiAction action, bool force_redraw)
{
    if (!ensure_book_list_loaded(app, renderer)) {
        app.mode = UiMode::Error;
        app.error_message = "Failed to open the EPUB library.";
        return false;
    }

    if (force_redraw) {
        app.list->set_needs_redraw();
    }

    switch (action) {
        case UiAction::Prev:
            app.list->prev();
            break;
        case UiAction::Next:
            app.list->next();
            break;
        case UiAction::Select:
            if (app.list_state.num_epubs > 0) {
                reset_toc_state(app.toc_state);
                app.reader.reset();
                app.toc = std::make_unique<EpubToc>(app.list_state.epub_list[app.list_state.selected_item], app.toc_state, &renderer);
                if (!app.toc->load()) {
                    app.mode = UiMode::Error;
                    app.error_message = "Failed to load the selected book.";
                    return false;
                }
                app.toc->set_needs_redraw();
                app.mode = UiMode::Toc;
                return true;
            }
            break;
        case UiAction::None:
        default:
            break;
    }

    app.list->render();
    return true;
}

bool render_toc(AppState &app, T5EpubRenderer &renderer, UiAction action, bool force_redraw)
{
    if (!app.toc) {
        app.toc = std::make_unique<EpubToc>(app.list_state.epub_list[app.list_state.selected_item], app.toc_state, &renderer);
        if (!app.toc->load()) {
            app.mode = UiMode::Error;
            app.error_message = "Failed to load the book table of contents.";
            return false;
        }
        force_redraw = true;
    }

    if (force_redraw) {
        app.toc->set_needs_redraw();
    }

    switch (action) {
        case UiAction::Prev:
            app.toc->prev();
            break;
        case UiAction::Next:
            app.toc->next();
            break;
        case UiAction::Select:
            app.reader = std::make_unique<EpubReader>(app.list_state.epub_list[app.list_state.selected_item], &renderer);
            app.reader->set_state_section(app.toc->get_selected_toc());
            if (!app.reader->load()) {
                app.mode = UiMode::Error;
                app.error_message = "Failed to open the selected section.";
                return false;
            }
            app.mode = UiMode::Reader;
            return true;
        case UiAction::None:
        default:
            break;
    }

    app.toc->render();
    return true;
}

bool render_reader(AppState &app, T5EpubRenderer &renderer, UiAction action)
{
    if (!app.reader) {
        app.reader = std::make_unique<EpubReader>(app.list_state.epub_list[app.list_state.selected_item], &renderer);
        if (!app.reader->load()) {
            app.mode = UiMode::Error;
            app.error_message = "Failed to open the selected book.";
            return false;
        }
    }

    switch (action) {
        case UiAction::Prev:
            app.reader->prev();
            break;
        case UiAction::Next:
            app.reader->next();
            break;
        case UiAction::Select:
            app.reader.reset();
            app.toc.reset();
            app.mode = UiMode::BookList;
            if (app.list) {
                app.list->set_needs_redraw();
            }
            return true;
        case UiAction::None:
        default:
            break;
    }

    app.reader->render();
    return true;
}

void render_ui(AppState &app, T5P4Board &board, T5EpubRenderer &renderer, UiAction action, bool force_redraw)
{
    bool ok = true;

    switch (app.mode) {
        case UiMode::BookList:
            ok = render_book_list(app, renderer, action, force_redraw);
            break;
        case UiMode::Toc:
            ok = render_toc(app, renderer, action, force_redraw);
            break;
        case UiMode::Reader:
            ok = render_reader(app, renderer, action);
            if (ok && app.mode == UiMode::BookList) {
                ok = render_book_list(app, renderer, UiAction::None, true);
            }
            break;
        case UiMode::Error:
            if (action == UiAction::Select) {
                rescan_books(app, board, renderer);
            }
            render_error_screen(renderer, app.error_message);
            break;
    }

    if (!ok) {
        render_error_screen(renderer, app.error_message);
        app.mode = UiMode::Error;
    }

    draw_toolbar(renderer, app.mode, UiAction::None);
    renderer.flush_display();
}

void epub_reader_task(void *arg)
{
    (void)arg;

    static T5P4Board board;
    if (!board.init()) {
        ESP_LOGE(kTag, "Board initialization failed");
        vTaskDelete(nullptr);
        return;
    }

    // Keep long-lived state off the small task stack and run the whole reader on
    // a larger dedicated task because miniz's unzip path needs tens of KB of stack.
    auto renderer = std::make_unique<T5EpubRenderer>(board.display(), board.screen_width(), board.screen_height(), kToolbarHeight);
    renderer->set_margin_top(20);
    renderer->set_margin_bottom(18);
    renderer->set_margin_left(26);
    renderer->set_margin_right(26);

    auto app = std::make_unique<AppState>();
    reset_list_state(app->list_state);
    reset_toc_state(app->toc_state);
    if (!rescan_books(*app, board, *renderer)) {
        render_error_screen(*renderer, app->error_message);
    }

    draw_toolbar(*renderer, app->mode, UiAction::None);
    renderer->flush_display();

    bool touch_latched = false;
    while (true) {
        int16_t touch_x = 0;
        int16_t touch_y = 0;
        const bool touched = board.read_touch_point(&touch_x, &touch_y);

        if (!touched) {
            touch_latched = false;
        } else if (!touch_latched) {
            touch_latched = true;
            const UiAction action = hit_test_toolbar(*renderer, app->mode, touch_x, touch_y);
            if (action != UiAction::None) {
                flash_toolbar_action(*renderer, app->mode, action);
                render_ui(*app, board, *renderer, action, false);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(CONFIG_EPUB_READER_TOUCH_POLL_MS));
    }
}

}  // namespace

extern "C" void app_main(void)
{
    esp_log_level_set(kTag, ESP_LOG_INFO);
    esp_log_level_set("EPUB", ESP_LOG_INFO);
    esp_log_level_set("PUBLIST", ESP_LOG_INFO);
    esp_log_level_set("PUBINDEX", ESP_LOG_INFO);
    esp_log_level_set("EREADER", ESP_LOG_INFO);
    esp_log_level_set("HTML", ESP_LOG_WARN);

    BaseType_t task_ok = xTaskCreate(
        epub_reader_task,
        "epub_reader",
        CONFIG_EPUB_READER_TASK_STACK_SIZE,
        nullptr,
        5,
        nullptr);
    if (task_ok != pdPASS) {
        ESP_LOGE(kTag, "Failed to create reader task with %d-byte stack", CONFIG_EPUB_READER_TASK_STACK_SIZE);
        return;
    }
}
