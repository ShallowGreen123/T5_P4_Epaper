#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <memory>
#include <set>
#include <string>
#include <sys/stat.h>
#include <vector>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include <Arduino.h>
#include <EpdFont.h>
#include <EpdFontFamily.h>
#include <Epub.h>
#include <Epub/Page.h>
#include <Epub/Section.h>
#include <FontCacheManager.h>
#include <FontDecompressor.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <HalStorage.h>
#include <Serialization.h>
#include <Txt.h>
#include <builtinFonts/notosans_14_bold.h>
#include <builtinFonts/notosans_14_regular.h>
#include <builtinFonts/notoserif_18_bold.h>
#include <builtinFonts/notoserif_18_bolditalic.h>
#include <builtinFonts/notoserif_18_italic.h>
#include <builtinFonts/notoserif_18_regular.h>

#include "t5_epub_board.h"

#ifndef CONFIG_EPUB_READER_TASK_STACK_SIZE
#define CONFIG_EPUB_READER_TASK_STACK_SIZE 98304
#endif

namespace {

constexpr char kTag[] = "crosspoint_reader";
constexpr char kSdMountPoint[] = "/sdcard";
constexpr char kCacheDir[] = "/sdcard/.crosspoint_reader";

constexpr int kReaderFontId = -501438527;  // NOTOSERIF_18_FONT_ID
constexpr int kUiFontId = -1589315735;     // NOTOSANS_14_FONT_ID
constexpr int kReaderMargin = 36;
constexpr int kStatusBarHeight = 70;
constexpr int kTopBarHeight = 56;
constexpr int kBottomBarHeight = 80;
constexpr int kListRowHeight = 50;
constexpr int kTocRowHeight = 46;
constexpr int kFullRefreshEvery = 8;
constexpr size_t kTxtChunkSize = 8 * 1024;
constexpr uint32_t kTxtIndexMagic = 0x54585449;  // "TXTI"
constexpr uint8_t kTxtIndexVersion = 1;

enum class BookType {
    Epub,
    Text,
};

enum class Screen {
    Library,
    EpubReader,
    TxtReader,
    Toc,
    Menu,
    Error,
};

struct BookEntry {
    std::string path;
    std::string title;
    BookType type = BookType::Epub;
};

struct ReaderViewport {
    int top = 0;
    int right = 0;
    int bottom = 0;
    int left = 0;
    int width = 0;
    int height = 0;
};

struct AppState {
    Screen screen = Screen::Library;
    Screen previous_screen = Screen::Library;
    std::vector<BookEntry> books;
    int library_page = 0;
    std::string error_message;

    BookEntry current_book;
    std::shared_ptr<Epub> epub;
    std::unique_ptr<Section> section;
    int toc_page = 0;
    int current_spine = 0;
    int next_epub_page = 0;
    int cached_chapter_pages = 0;
    std::string pending_anchor;

    std::unique_ptr<Txt> txt;
    std::vector<size_t> txt_page_offsets;
    std::vector<std::string> txt_page_lines;
    bool txt_initialized = false;
    int txt_current_page = 0;
    int txt_total_pages = 0;
    int txt_lines_per_page = 1;
    int txt_viewport_width = 0;

    int pages_until_full_refresh = kFullRefreshEvery;
    bool needs_redraw = true;
};

int clamp_int(int value, int low, int high)
{
    if (value < low) {
        return low;
    }
    if (value > high) {
        return high;
    }
    return value;
}

std::string to_lower(std::string value)
{
    for (char& ch : value) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return value;
}

std::string extension_of(const std::string& path)
{
    const size_t dot = path.find_last_of('.');
    if (dot == std::string::npos) {
        return {};
    }
    return to_lower(path.substr(dot));
}

std::string basename_of(const std::string& path)
{
    const size_t slash = path.find_last_of('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::string title_from_path(const std::string& path)
{
    std::string title = basename_of(path);
    const size_t dot = title.find_last_of('.');
    if (dot != std::string::npos) {
        title.resize(dot);
    }
    return title.empty() ? basename_of(path) : title;
}

bool directory_exists(const char* path)
{
    struct stat st = {};
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

bool file_exists(const std::string& path)
{
    struct stat st = {};
    return stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

BookType book_type_for_ext(const std::string& ext)
{
    return ext == ".epub" ? BookType::Epub : BookType::Text;
}

bool is_supported_book(const std::string& path)
{
    const std::string ext = extension_of(path);
    return ext == ".epub" || ext == ".txt" || ext == ".md" || ext == ".markdown";
}

void scan_directory(const std::string& root, int depth, std::vector<BookEntry>& out, std::set<std::string>& seen)
{
    DIR* dir = opendir(root.c_str());
    if (dir == nullptr) {
        return;
    }

    while (dirent* entry = readdir(dir)) {
        if (std::strcmp(entry->d_name, ".") == 0 || std::strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        const std::string path = root + "/" + entry->d_name;
        if (path.find("/.crosspoint") != std::string::npos) {
            continue;
        }

        struct stat st = {};
        if (stat(path.c_str(), &st) != 0) {
            continue;
        }

        if (S_ISDIR(st.st_mode)) {
            if (depth > 0) {
                scan_directory(path, depth - 1, out, seen);
            }
            continue;
        }

        if (!S_ISREG(st.st_mode) || !is_supported_book(path) || seen.count(path) != 0) {
            continue;
        }

        seen.insert(path);
        out.push_back(BookEntry{path, title_from_path(path), book_type_for_ext(extension_of(path))});
    }
    closedir(dir);
}

void draw_top_title(GfxRenderer& renderer, const char* title)
{
    renderer.drawText(kUiFontId, kReaderMargin, 20, title, true, EpdFontFamily::BOLD);
    renderer.drawLine(kReaderMargin, kTopBarHeight - 1, renderer.getScreenWidth() - kReaderMargin, kTopBarHeight - 1);
}

void draw_text_block(GfxRenderer& renderer, const std::string& text, int x, int y, int width, int max_lines)
{
    const auto lines = renderer.wrappedText(kUiFontId, text.c_str(), width, max_lines);
    int line_y = y;
    const int line_height = renderer.getLineHeight(kUiFontId) + 3;
    for (const std::string& line : lines) {
        renderer.drawText(kUiFontId, x, line_y, line.c_str());
        line_y += line_height;
    }
}

void display_with_cycle(AppState& app, GfxRenderer& renderer)
{
    HalDisplay::RefreshMode mode = HalDisplay::FAST_REFRESH;
    if (app.pages_until_full_refresh <= 0) {
        mode = HalDisplay::FULL_REFRESH;
        app.pages_until_full_refresh = kFullRefreshEvery;
    } else {
        app.pages_until_full_refresh--;
    }
    renderer.displayBuffer(mode);
}

void draw_message(GfxRenderer& renderer, const char* title, const std::string& message)
{
    renderer.clearScreen();
    draw_top_title(renderer, title);
    draw_text_block(renderer, message, kReaderMargin, 110, renderer.getScreenWidth() - 2 * kReaderMargin, 5);
    renderer.displayBuffer(HalDisplay::FULL_REFRESH);
}

void draw_bottom_buttons(GfxRenderer& renderer, const char* left, const char* center, const char* right)
{
    const int y = renderer.getScreenHeight() - kBottomBarHeight;
    const int cell = renderer.getScreenWidth() / 3;
    const char* labels[3] = {left, center, right};
    for (int i = 0; i < 3; ++i) {
        const int x = i * cell;
        const int w = (i == 2) ? renderer.getScreenWidth() - x : cell;
        renderer.drawRect(x, y, w, kBottomBarHeight);
        const int text_width = renderer.getTextWidth(kUiFontId, labels[i]);
        renderer.drawText(kUiFontId, x + (w - text_width) / 2, y + 21, labels[i], true, EpdFontFamily::BOLD);
    }
}

ReaderViewport reader_viewport(GfxRenderer& renderer)
{
    ReaderViewport vp;
    renderer.getOrientedViewableTRBL(&vp.top, &vp.right, &vp.bottom, &vp.left);
    vp.top += kReaderMargin;
    vp.left += kReaderMargin;
    vp.right += kReaderMargin;
    vp.bottom += std::max(kReaderMargin, kStatusBarHeight);
    vp.width = renderer.getScreenWidth() - vp.left - vp.right;
    vp.height = renderer.getScreenHeight() - vp.top - vp.bottom;
    if (vp.width < 1) {
        vp.width = 1;
    }
    if (vp.height < 1) {
        vp.height = 1;
    }
    return vp;
}

void draw_status_bar(GfxRenderer& renderer, const std::string& title, const std::string& page_label, float progress)
{
    const int y = renderer.getScreenHeight() - kStatusBarHeight;
    const int w = renderer.getScreenWidth();
    renderer.drawLine(kReaderMargin, y, w - kReaderMargin, y);

    const int right_width = renderer.getTextWidth(kUiFontId, page_label.c_str());
    const int title_width = w - 2 * kReaderMargin - right_width - 18;
    const std::string clipped = renderer.truncatedText(kUiFontId, title.c_str(), std::max(20, title_width));
    renderer.drawText(kUiFontId, kReaderMargin, y + 5, clipped.c_str());
    renderer.drawText(kUiFontId, w - kReaderMargin - right_width, y + 5, page_label.c_str());

    progress = std::max(0.0f, std::min(1.0f, progress));
    const int bar_y = renderer.getScreenHeight() - 20;
    const int bar_w = w - 2 * kReaderMargin;
    renderer.drawRect(kReaderMargin, bar_y, bar_w, 4);
    renderer.fillRect(kReaderMargin, bar_y, static_cast<int>(bar_w * progress), 4);
}

void install_fonts(GfxRenderer& renderer, FontCacheManager& font_cache, FontDecompressor& decompressor)
{
    static EpdFont reader_regular(&notoserif_18_regular);
    static EpdFont reader_bold(&notoserif_18_bold);
    static EpdFont reader_italic(&notoserif_18_italic);
    static EpdFont reader_bold_italic(&notoserif_18_bolditalic);
    static EpdFont ui_regular(&notosans_14_regular);
    static EpdFont ui_bold(&notosans_14_bold);

    renderer.insertFont(kReaderFontId, EpdFontFamily(&reader_regular, &reader_bold, &reader_italic, &reader_bold_italic));
    renderer.insertFont(kUiFontId, EpdFontFamily(&ui_regular, &ui_bold));
    font_cache.setFontDecompressor(&decompressor);
    renderer.setFontCacheManager(&font_cache);
}

void reset_current_book(AppState& app)
{
    app.epub.reset();
    app.section.reset();
    app.txt.reset();
    app.txt_page_offsets.clear();
    app.txt_page_lines.clear();
    app.txt_initialized = false;
    app.current_spine = 0;
    app.next_epub_page = 0;
    app.cached_chapter_pages = 0;
    app.pending_anchor.clear();
    app.toc_page = 0;
}

bool rescan_books(AppState& app, T5P4Board& board, GfxRenderer& renderer)
{
    reset_current_book(app);
    app.books.clear();
    app.library_page = 0;

    board.unmount_sd_card(kSdMountPoint);
    if (!board.mount_sd_card(kSdMountPoint)) {
        app.screen = Screen::Error;
        app.error_message = "SD card mount failed.";
        return false;
    }

    Storage.mkdir(kCacheDir, true);

    std::set<std::string> seen;
    if (directory_exists(CONFIG_EPUB_READER_BOOK_DIR)) {
        scan_directory(CONFIG_EPUB_READER_BOOK_DIR, 4, app.books, seen);
    }
    if (directory_exists(CONFIG_EPUB_READER_FALLBACK_DIR)) {
        scan_directory(CONFIG_EPUB_READER_FALLBACK_DIR, 1, app.books, seen);
    }

    std::sort(app.books.begin(), app.books.end(), [](const BookEntry& a, const BookEntry& b) {
        return to_lower(a.title) < to_lower(b.title);
    });

    app.screen = Screen::Library;
    app.needs_redraw = true;
    return true;
}

int library_rows(GfxRenderer& renderer)
{
    const int available = renderer.getScreenHeight() - kTopBarHeight - kBottomBarHeight - 20;
    return std::max(1, available / kListRowHeight);
}

void render_library(AppState& app, GfxRenderer& renderer)
{
    renderer.clearScreen();
    draw_top_title(renderer, "CrossPoint Reader");

    const int rows = library_rows(renderer);
    const int pages = std::max(1, static_cast<int>((app.books.size() + rows - 1) / rows));
    app.library_page = clamp_int(app.library_page, 0, pages - 1);

    if (app.books.empty()) {
        draw_text_block(renderer, "No EPUB, TXT, or Markdown books found.", kReaderMargin, 130,
                        renderer.getScreenWidth() - 2 * kReaderMargin, 3);
    } else {
        const int start = app.library_page * rows;
        for (int row = 0; row < rows; ++row) {
            const int index = start + row;
            if (index >= static_cast<int>(app.books.size())) {
                break;
            }
            const int y = kTopBarHeight + 12 + row * kListRowHeight;
            const BookEntry& book = app.books[index];
            const char* suffix = book.type == BookType::Epub ? "EPUB" : "TXT";
            std::string title = book.title + "  [" + suffix + "]";
            title = renderer.truncatedText(kUiFontId, title.c_str(), renderer.getScreenWidth() - 2 * kReaderMargin);
            renderer.drawText(kUiFontId, kReaderMargin, y + 13, title.c_str());
            renderer.drawLine(kReaderMargin, y + kListRowHeight - 1, renderer.getScreenWidth() - kReaderMargin,
                              y + kListRowHeight - 1);
        }
    }

    draw_bottom_buttons(renderer, "Prev", "Next", "Rescan");
    renderer.displayBuffer(HalDisplay::FULL_REFRESH);
    app.needs_redraw = false;
}

bool load_epub_progress(AppState& app)
{
    if (!app.epub) {
        return false;
    }
    FsFile f;
    if (!Storage.openFileForRead("APP", app.epub->getCachePath() + "/progress.bin", f)) {
        return false;
    }
    uint8_t data[6] = {};
    const int got = f.read(data, sizeof(data));
    if (got < 4) {
        return false;
    }
    app.current_spine = data[0] | (data[1] << 8);
    app.next_epub_page = data[2] | (data[3] << 8);
    if (got >= 6) {
        app.cached_chapter_pages = data[4] | (data[5] << 8);
    }
    return true;
}

void save_epub_progress(AppState& app)
{
    if (!app.epub || !app.section) {
        return;
    }
    FsFile f;
    if (!Storage.openFileForWrite("APP", app.epub->getCachePath() + "/progress.bin", f)) {
        return;
    }
    const int spine = app.current_spine;
    const int page = app.section->currentPage;
    const int page_count = app.section->pageCount;
    const uint8_t data[6] = {
        static_cast<uint8_t>(spine & 0xFF),
        static_cast<uint8_t>((spine >> 8) & 0xFF),
        static_cast<uint8_t>(page & 0xFF),
        static_cast<uint8_t>((page >> 8) & 0xFF),
        static_cast<uint8_t>(page_count & 0xFF),
        static_cast<uint8_t>((page_count >> 8) & 0xFF),
    };
    f.write(data, sizeof(data));
}

bool open_epub(AppState& app, GfxRenderer& renderer, const BookEntry& book)
{
    reset_current_book(app);
    app.current_book = book;
    app.epub = std::make_shared<Epub>(book.path, kCacheDir);

    draw_message(renderer, "CrossPoint Reader", "Loading EPUB...");
    if (!app.epub->load(true, false)) {
        app.screen = Screen::Error;
        app.error_message = "Failed to load EPUB metadata.";
        return false;
    }
    app.epub->setupCacheDir();

    if (!load_epub_progress(app)) {
        app.current_spine = app.epub->getSpineIndexForTextReference();
        if (app.current_spine < 0) {
            app.current_spine = 0;
        }
        app.next_epub_page = 0;
    }

    app.current_spine = clamp_int(app.current_spine, 0, std::max(0, app.epub->getSpineItemsCount() - 1));
    app.screen = Screen::EpubReader;
    app.needs_redraw = true;
    return true;
}

bool open_txt(AppState& app, GfxRenderer& renderer, const BookEntry& book)
{
    reset_current_book(app);
    app.current_book = book;
    app.txt = std::make_unique<Txt>(book.path, kCacheDir);

    draw_message(renderer, "CrossPoint Reader", "Loading text file...");
    if (!app.txt->load()) {
        app.screen = Screen::Error;
        app.error_message = "Failed to load text file.";
        return false;
    }
    app.txt->setupCacheDir();
    app.screen = Screen::TxtReader;
    app.needs_redraw = true;
    return true;
}

bool open_book(AppState& app, GfxRenderer& renderer, const BookEntry& book)
{
    if (!file_exists(book.path)) {
        app.screen = Screen::Error;
        app.error_message = "Book file is no longer readable.";
        app.needs_redraw = true;
        return false;
    }
    return book.type == BookType::Epub ? open_epub(app, renderer, book) : open_txt(app, renderer, book);
}

bool ensure_epub_section(AppState& app, GfxRenderer& renderer)
{
    if (!app.epub) {
        return false;
    }

    const int spine_count = app.epub->getSpineItemsCount();
    if (spine_count <= 0) {
        app.screen = Screen::Error;
        app.error_message = "EPUB has no readable spine items.";
        return false;
    }

    app.current_spine = clamp_int(app.current_spine, 0, spine_count - 1);
    if (app.section) {
        return true;
    }

    const ReaderViewport vp = reader_viewport(renderer);
    app.section = std::make_unique<Section>(app.epub, app.current_spine, renderer);

    const int font_id = kReaderFontId;
    constexpr float line_compression = 1.0f;
    constexpr bool extra_paragraph_spacing = true;
    constexpr uint8_t paragraph_alignment = 0;  // CssTextAlign::Justify
    constexpr bool hyphenation_enabled = false;
    constexpr bool embedded_style = true;
    constexpr uint8_t image_rendering = 0;  // display where supported, fallback to alt/skip otherwise

    if (!app.section->loadSectionFile(font_id, line_compression, extra_paragraph_spacing, paragraph_alignment,
                                      vp.width, vp.height, hyphenation_enabled, embedded_style, image_rendering)) {
        draw_message(renderer, "CrossPoint Reader", "Indexing chapter...");
        if (!app.section->createSectionFile(font_id, line_compression, extra_paragraph_spacing, paragraph_alignment,
                                            vp.width, vp.height, hyphenation_enabled, embedded_style,
                                            image_rendering)) {
            app.section.reset();
            app.screen = Screen::Error;
            app.error_message = "Failed to build chapter cache.";
            return false;
        }
    }

    if (app.next_epub_page < 0 && app.section->pageCount > 0) {
        app.section->currentPage = app.section->pageCount - 1;
    } else {
        app.section->currentPage = clamp_int(app.next_epub_page, 0, std::max(0, static_cast<int>(app.section->pageCount) - 1));
    }

    if (app.cached_chapter_pages > 0 && app.section->pageCount > 0 && app.cached_chapter_pages != app.section->pageCount) {
        const float old_progress = static_cast<float>(app.section->currentPage) / static_cast<float>(app.cached_chapter_pages);
        app.section->currentPage = clamp_int(static_cast<int>(old_progress * app.section->pageCount), 0,
                                             static_cast<int>(app.section->pageCount) - 1);
    }
    app.cached_chapter_pages = 0;

    if (!app.pending_anchor.empty()) {
        if (const auto page = app.section->getPageForAnchor(app.pending_anchor)) {
            app.section->currentPage = clamp_int(*page, 0, std::max(0, static_cast<int>(app.section->pageCount) - 1));
        }
        app.pending_anchor.clear();
    }
    return true;
}

void render_epub_reader(AppState& app, GfxRenderer& renderer)
{
    if (!ensure_epub_section(app, renderer)) {
        app.needs_redraw = true;
        return;
    }

    renderer.clearScreen();
    if (app.section->pageCount == 0) {
        renderer.drawCenteredText(kUiFontId, renderer.getScreenHeight() / 2 - 20, "Empty chapter", true,
                                  EpdFontFamily::BOLD);
        draw_status_bar(renderer, app.epub->getTitle(), "0/0", 0.0f);
        renderer.displayBuffer(HalDisplay::FULL_REFRESH);
        app.needs_redraw = false;
        return;
    }

    app.section->currentPage = clamp_int(app.section->currentPage, 0, app.section->pageCount - 1);
    auto page = app.section->loadPageFromSectionFile();
    if (!page) {
        app.section->clearCache();
        app.section.reset();
        app.screen = Screen::Error;
        app.error_message = "Failed to read chapter cache.";
        app.needs_redraw = true;
        return;
    }

    const ReaderViewport vp = reader_viewport(renderer);
    auto* fcm = renderer.getFontCacheManager();
    if (fcm) {
        auto scope = fcm->createPrewarmScope();
        page->render(renderer, kReaderFontId, vp.left, vp.top);
        scope.endScanAndPrewarm();
        page->render(renderer, kReaderFontId, vp.left, vp.top);
    } else {
        page->render(renderer, kReaderFontId, vp.left, vp.top);
    }
    const int current = app.section->currentPage + 1;
    const int total = app.section->pageCount;
    char page_label[32] = {};
    std::snprintf(page_label, sizeof(page_label), "%d/%d", current, total);
    const float chapter_progress = total > 0 ? static_cast<float>(app.section->currentPage) / total : 0.0f;
    const float book_progress = app.epub->calculateProgress(app.current_spine, chapter_progress);
    draw_status_bar(renderer, app.epub->getTitle(), page_label, book_progress);
    display_with_cycle(app, renderer);
    save_epub_progress(app);
    app.needs_redraw = false;
}

size_t utf8_safe_break(const std::string& text, size_t pos)
{
    if (pos >= text.size()) {
        return text.size();
    }
    while (pos > 0 && (static_cast<unsigned char>(text[pos]) & 0xC0) == 0x80) {
        --pos;
    }
    return pos == 0 ? 1 : pos;
}

bool load_txt_page_at_offset(AppState& app, GfxRenderer& renderer, size_t offset, std::vector<std::string>& out_lines,
                             size_t& next_offset)
{
    out_lines.clear();
    if (!app.txt || offset >= app.txt->getFileSize()) {
        return false;
    }

    const size_t file_size = app.txt->getFileSize();
    const size_t chunk_size = std::min(kTxtChunkSize, file_size - offset);
    std::unique_ptr<uint8_t[]> buffer(new uint8_t[chunk_size + 1]);
    if (!buffer || !app.txt->readContent(buffer.get(), offset, chunk_size)) {
        return false;
    }
    buffer[chunk_size] = '\0';

    size_t pos = 0;
    while (pos < chunk_size && static_cast<int>(out_lines.size()) < app.txt_lines_per_page) {
        size_t line_end = pos;
        while (line_end < chunk_size && buffer[line_end] != '\n') {
            ++line_end;
        }

        const bool line_complete = (line_end < chunk_size) || (offset + line_end >= file_size);
        if (!line_complete && !out_lines.empty()) {
            break;
        }

        size_t content_len = line_end - pos;
        if (content_len > 0 && buffer[pos + content_len - 1] == '\r') {
            --content_len;
        }

        std::string line(reinterpret_cast<char*>(buffer.get() + pos), content_len);
        if (line.empty()) {
            out_lines.emplace_back();
            pos = std::min(chunk_size, line_end + 1);
            continue;
        }

        size_t line_byte_pos = 0;
        while (!line.empty() && static_cast<int>(out_lines.size()) < app.txt_lines_per_page) {
            if (renderer.getTextWidth(kReaderFontId, line.c_str()) <= app.txt_viewport_width) {
                out_lines.push_back(line);
                line_byte_pos = content_len;
                line.clear();
                break;
            }

            size_t break_pos = line.length();
            while (break_pos > 0 &&
                   renderer.getTextWidth(kReaderFontId, line.substr(0, break_pos).c_str()) > app.txt_viewport_width) {
                const size_t space_pos = line.rfind(' ', break_pos - 1);
                if (space_pos != std::string::npos && space_pos > 0) {
                    break_pos = space_pos;
                } else {
                    --break_pos;
                    break_pos = utf8_safe_break(line, break_pos);
                }
            }

            if (break_pos == 0) {
                break_pos = utf8_safe_break(line, 1);
            }

            out_lines.push_back(line.substr(0, break_pos));
            size_t skip_chars = break_pos;
            if (break_pos < line.length() && line[break_pos] == ' ') {
                ++skip_chars;
            }
            line_byte_pos += skip_chars;
            line = line.substr(skip_chars);
        }

        if (line.empty()) {
            pos = std::min(chunk_size, line_end + 1);
        } else {
            pos += line_byte_pos;
            break;
        }
    }

    if (pos == 0 && !out_lines.empty()) {
        pos = 1;
    }

    next_offset = std::min(file_size, offset + pos);
    return !out_lines.empty();
}

bool load_txt_index_cache(AppState& app)
{
    if (!app.txt) {
        return false;
    }
    FsFile f;
    if (!Storage.openFileForRead("TXT", app.txt->getCachePath() + "/index.bin", f)) {
        return false;
    }

    uint32_t magic = 0;
    uint8_t version = 0;
    uint32_t file_size = 0;
    int32_t viewport_width = 0;
    int32_t lines_per_page = 0;
    int32_t font_id = 0;
    uint32_t count = 0;
    serialization::readPod(f, magic);
    serialization::readPod(f, version);
    serialization::readPod(f, file_size);
    serialization::readPod(f, viewport_width);
    serialization::readPod(f, lines_per_page);
    serialization::readPod(f, font_id);
    serialization::readPod(f, count);

    if (magic != kTxtIndexMagic || version != kTxtIndexVersion || file_size != app.txt->getFileSize() ||
        viewport_width != app.txt_viewport_width || lines_per_page != app.txt_lines_per_page ||
        font_id != kReaderFontId || count == 0 || count > 1000000) {
        return false;
    }

    app.txt_page_offsets.clear();
    app.txt_page_offsets.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t off = 0;
        serialization::readPod(f, off);
        app.txt_page_offsets.push_back(off);
    }
    app.txt_total_pages = static_cast<int>(app.txt_page_offsets.size());
    return app.txt_total_pages > 0;
}

void save_txt_index_cache(AppState& app)
{
    if (!app.txt || app.txt_page_offsets.empty()) {
        return;
    }
    FsFile f;
    if (!Storage.openFileForWrite("TXT", app.txt->getCachePath() + "/index.bin", f)) {
        return;
    }
    serialization::writePod(f, kTxtIndexMagic);
    serialization::writePod(f, kTxtIndexVersion);
    serialization::writePod(f, static_cast<uint32_t>(app.txt->getFileSize()));
    serialization::writePod(f, static_cast<int32_t>(app.txt_viewport_width));
    serialization::writePod(f, static_cast<int32_t>(app.txt_lines_per_page));
    serialization::writePod(f, static_cast<int32_t>(kReaderFontId));
    serialization::writePod(f, static_cast<uint32_t>(app.txt_page_offsets.size()));
    for (size_t off : app.txt_page_offsets) {
        serialization::writePod(f, static_cast<uint32_t>(off));
    }
}

void save_txt_progress(AppState& app)
{
    if (!app.txt) {
        return;
    }
    FsFile f;
    if (!Storage.openFileForWrite("TXT", app.txt->getCachePath() + "/progress.bin", f)) {
        return;
    }
    const uint32_t page = static_cast<uint32_t>(std::max(0, app.txt_current_page));
    serialization::writePod(f, page);
}

void load_txt_progress(AppState& app)
{
    if (!app.txt) {
        return;
    }
    FsFile f;
    if (!Storage.openFileForRead("TXT", app.txt->getCachePath() + "/progress.bin", f)) {
        app.txt_current_page = 0;
        return;
    }
    uint32_t page = 0;
    serialization::readPod(f, page);
    app.txt_current_page = clamp_int(static_cast<int>(page), 0, std::max(0, app.txt_total_pages - 1));
}

bool ensure_txt_initialized(AppState& app, GfxRenderer& renderer)
{
    if (!app.txt) {
        return false;
    }
    if (app.txt_initialized) {
        return true;
    }

    const ReaderViewport vp = reader_viewport(renderer);
    app.txt_viewport_width = vp.width;
    const int line_height = std::max(1, renderer.getLineHeight(kReaderFontId));
    app.txt_lines_per_page = std::max(1, vp.height / line_height);

    if (!load_txt_index_cache(app)) {
        draw_message(renderer, "CrossPoint Reader", "Indexing text file...");
        app.txt_page_offsets.clear();
        app.txt_page_offsets.push_back(0);

        size_t offset = 0;
        while (offset < app.txt->getFileSize()) {
            std::vector<std::string> lines;
            size_t next_offset = offset;
            if (!load_txt_page_at_offset(app, renderer, offset, lines, next_offset) || next_offset <= offset) {
                break;
            }
            offset = next_offset;
            if (offset < app.txt->getFileSize()) {
                app.txt_page_offsets.push_back(offset);
            }
            if ((app.txt_page_offsets.size() % 20) == 0) {
                vTaskDelay(1);
            }
        }
        if (app.txt_page_offsets.empty()) {
            app.txt_page_offsets.push_back(0);
        }
        app.txt_total_pages = static_cast<int>(app.txt_page_offsets.size());
        save_txt_index_cache(app);
    }

    load_txt_progress(app);
    app.txt_initialized = true;
    return true;
}

void render_txt_reader(AppState& app, GfxRenderer& renderer)
{
    if (!ensure_txt_initialized(app, renderer)) {
        app.screen = Screen::Error;
        app.error_message = "Text reader is not initialized.";
        app.needs_redraw = true;
        return;
    }

    app.txt_current_page = clamp_int(app.txt_current_page, 0, std::max(0, app.txt_total_pages - 1));
    size_t next_offset = 0;
    app.txt_page_lines.clear();
    if (!load_txt_page_at_offset(app, renderer, app.txt_page_offsets[app.txt_current_page], app.txt_page_lines,
                                 next_offset)) {
        app.txt_page_lines.clear();
    }

    const ReaderViewport vp = reader_viewport(renderer);
    renderer.clearScreen();

    auto render_lines = [&]() {
        int y = vp.top;
        const int line_height = std::max(1, renderer.getLineHeight(kReaderFontId));
        for (const std::string& line : app.txt_page_lines) {
            if (!line.empty()) {
                renderer.drawText(kReaderFontId, vp.left, y, line.c_str());
            }
            y += line_height;
        }
    };

    if (auto* fcm = renderer.getFontCacheManager()) {
        auto scope = fcm->createPrewarmScope();
        render_lines();
        scope.endScanAndPrewarm();
        render_lines();
    } else {
        render_lines();
    }

    char page_label[32] = {};
    std::snprintf(page_label, sizeof(page_label), "%d/%d", app.txt_current_page + 1, app.txt_total_pages);
    const float progress = app.txt_total_pages > 0 ? static_cast<float>(app.txt_current_page + 1) / app.txt_total_pages : 0.0f;
    draw_status_bar(renderer, app.txt->getTitle(), page_label, progress);
    display_with_cycle(app, renderer);
    save_txt_progress(app);
    app.needs_redraw = false;
}

void turn_epub_page(AppState& app, bool forward)
{
    if (!app.epub || !app.section) {
        return;
    }
    if (forward) {
        if (app.section->currentPage < app.section->pageCount - 1) {
            ++app.section->currentPage;
        } else if (app.current_spine + 1 < app.epub->getSpineItemsCount()) {
            ++app.current_spine;
            app.next_epub_page = 0;
            app.section.reset();
        }
    } else {
        if (app.section->currentPage > 0) {
            --app.section->currentPage;
        } else if (app.current_spine > 0) {
            --app.current_spine;
            app.next_epub_page = -1;
            app.section.reset();
        }
    }
    app.needs_redraw = true;
}

void turn_txt_page(AppState& app, bool forward)
{
    if (forward) {
        if (app.txt_current_page + 1 < app.txt_total_pages) {
            ++app.txt_current_page;
        }
    } else if (app.txt_current_page > 0) {
        --app.txt_current_page;
    }
    app.needs_redraw = true;
}

int toc_rows(GfxRenderer& renderer)
{
    const int available = renderer.getScreenHeight() - kTopBarHeight - kBottomBarHeight - 20;
    return std::max(1, available / kTocRowHeight);
}

void render_toc(AppState& app, GfxRenderer& renderer)
{
    renderer.clearScreen();
    draw_top_title(renderer, "Table of Contents");

    if (!app.epub || app.epub->getTocItemsCount() <= 0) {
        draw_text_block(renderer, "No EPUB TOC found.", kReaderMargin, 130, renderer.getScreenWidth() - 2 * kReaderMargin,
                        3);
    } else {
        const int rows = toc_rows(renderer);
        const int pages = std::max(1, (app.epub->getTocItemsCount() + rows - 1) / rows);
        app.toc_page = clamp_int(app.toc_page, 0, pages - 1);
        const int first = app.toc_page * rows;
        for (int row = 0; row < rows; ++row) {
            const int index = first + row;
            if (index >= app.epub->getTocItemsCount()) {
                break;
            }
            const auto item = app.epub->getTocItem(index);
            const int y = kTopBarHeight + 12 + row * kTocRowHeight;
            const int indent = std::min<int>(item.level, 4) * 18;
            std::string label = renderer.truncatedText(kUiFontId, item.title.c_str(),
                                                       renderer.getScreenWidth() - 2 * kReaderMargin - indent);
            renderer.drawText(kUiFontId, kReaderMargin + indent, y + 10, label.c_str());
            renderer.drawLine(kReaderMargin, y + kTocRowHeight - 1, renderer.getScreenWidth() - kReaderMargin,
                              y + kTocRowHeight - 1);
        }
    }

    draw_bottom_buttons(renderer, "Prev", "Next", "Back");
    renderer.displayBuffer(HalDisplay::FULL_REFRESH);
    app.needs_redraw = false;
}

void jump_to_toc(AppState& app, int toc_index)
{
    if (!app.epub || toc_index < 0 || toc_index >= app.epub->getTocItemsCount()) {
        return;
    }

    const auto item = app.epub->getTocItem(toc_index);
    int spine = item.spineIndex;
    if (spine < 0) {
        spine = app.epub->resolveHrefToSpineIndex(item.href);
    }
    if (spine < 0) {
        return;
    }

    app.current_spine = clamp_int(spine, 0, app.epub->getSpineItemsCount() - 1);
    app.pending_anchor = item.anchor;
    app.next_epub_page = 0;
    app.section.reset();
    app.screen = Screen::EpubReader;
    app.needs_redraw = true;
}

void render_menu(AppState& app, GfxRenderer& renderer)
{
    renderer.clearScreen();
    draw_top_title(renderer, "Reader Menu");

    const char* epub_labels[] = {"Back to Library", "Table of Contents", "Refresh Page", "Rescan SD"};
    const char* txt_labels[] = {"Back to Library", "Refresh Page", "Rescan SD"};
    const char** labels = app.epub ? epub_labels : txt_labels;
    const int count = app.epub ? 4 : 3;
    for (int i = 0; i < count; ++i) {
        const int y = kTopBarHeight + 26 + i * 70;
        renderer.drawRect(kReaderMargin, y, renderer.getScreenWidth() - 2 * kReaderMargin, 54);
        renderer.drawText(kUiFontId, kReaderMargin + 20, y + 17, labels[i], true, EpdFontFamily::BOLD);
    }

    renderer.displayBuffer(HalDisplay::FULL_REFRESH);
    app.needs_redraw = false;
}

void render_error(AppState& app, GfxRenderer& renderer)
{
    renderer.clearScreen();
    draw_top_title(renderer, "CrossPoint Reader");
    draw_text_block(renderer, app.error_message.empty() ? "Unknown error." : app.error_message, kReaderMargin, 120,
                    renderer.getScreenWidth() - 2 * kReaderMargin, 5);
    draw_bottom_buttons(renderer, "Back", "", "Retry");
    renderer.displayBuffer(HalDisplay::FULL_REFRESH);
    app.needs_redraw = false;
}

void render_current(AppState& app, GfxRenderer& renderer)
{
    for (int attempt = 0; attempt < 2; ++attempt) {
        const Screen before = app.screen;
        switch (app.screen) {
            case Screen::Library:
                render_library(app, renderer);
                break;
            case Screen::EpubReader:
                render_epub_reader(app, renderer);
                break;
            case Screen::TxtReader:
                render_txt_reader(app, renderer);
                break;
            case Screen::Toc:
                render_toc(app, renderer);
                break;
            case Screen::Menu:
                render_menu(app, renderer);
                break;
            case Screen::Error:
                render_error(app, renderer);
                break;
        }
        if (!app.needs_redraw || app.screen == before) {
            break;
        }
    }
}

void leave_reader_to_library(AppState& app)
{
    reset_current_book(app);
    app.screen = Screen::Library;
    app.needs_redraw = true;
}

void handle_library_touch(AppState& app, GfxRenderer& renderer, int16_t x, int16_t y)
{
    const int rows = library_rows(renderer);
    const int bottom_y = renderer.getScreenHeight() - kBottomBarHeight;
    if (y >= bottom_y) {
        const int third = renderer.getScreenWidth() / 3;
        if (x < third) {
            app.library_page = std::max(0, app.library_page - 1);
        } else if (x < third * 2) {
            const int pages = std::max(1, static_cast<int>((app.books.size() + rows - 1) / rows));
            app.library_page = std::min(pages - 1, app.library_page + 1);
        } else {
            app.screen = Screen::Error;
            app.error_message = "Rescan requested.";
        }
        app.needs_redraw = true;
        return;
    }

    if (y < kTopBarHeight + 12) {
        return;
    }
    const int row = (y - kTopBarHeight - 12) / kListRowHeight;
    if (row < 0 || row >= rows) {
        return;
    }
    const int index = app.library_page * rows + row;
    if (index >= 0 && index < static_cast<int>(app.books.size())) {
        open_book(app, renderer, app.books[index]);
    }
}

void handle_reader_touch(AppState& app, GfxRenderer& renderer, int16_t x)
{
    const int width = renderer.getScreenWidth();
    if (x < width / 3) {
        if (app.screen == Screen::EpubReader) {
            turn_epub_page(app, false);
        } else {
            turn_txt_page(app, false);
        }
    } else if (x > (width * 2) / 3) {
        if (app.screen == Screen::EpubReader) {
            turn_epub_page(app, true);
        } else {
            turn_txt_page(app, true);
        }
    } else {
        app.previous_screen = app.screen;
        app.screen = Screen::Menu;
        app.needs_redraw = true;
    }
}

void handle_toc_touch(AppState& app, GfxRenderer& renderer, int16_t x, int16_t y)
{
    (void)x;
    if (y >= renderer.getScreenHeight() - kBottomBarHeight) {
        const int third = renderer.getScreenWidth() / 3;
        const int rows = toc_rows(renderer);
        const int pages = app.epub ? std::max(1, (app.epub->getTocItemsCount() + rows - 1) / rows) : 1;
        if (x < third) {
            app.toc_page = std::max(0, app.toc_page - 1);
        } else if (x < third * 2) {
            app.toc_page = std::min(pages - 1, app.toc_page + 1);
        } else {
            app.screen = Screen::EpubReader;
        }
        app.needs_redraw = true;
        return;
    }
    const int rows = toc_rows(renderer);
    const int row = (y - kTopBarHeight - 12) / kTocRowHeight;
    if (row >= 0 && row < rows) {
        jump_to_toc(app, app.toc_page * rows + row);
    }
}

void handle_menu_touch(AppState& app, T5P4Board& board, GfxRenderer& renderer, int16_t y)
{
    const int row = (y - kTopBarHeight - 26) / 70;
    if (row < 0) {
        return;
    }

    if (row == 0) {
        leave_reader_to_library(app);
    } else if (row == 1 && app.epub) {
        app.screen = Screen::Toc;
        app.needs_redraw = true;
    } else if ((row == 1 && !app.epub) || (row == 2 && app.epub)) {
        app.screen = app.previous_screen;
        app.needs_redraw = true;
    } else if ((row == 2 && !app.epub) || (row == 3 && app.epub)) {
        rescan_books(app, board, renderer);
    }
}

void handle_error_touch(AppState& app, T5P4Board& board, GfxRenderer& renderer, int16_t x)
{
    const int third = renderer.getScreenWidth() / 3;
    if (x < third) {
        app.screen = Screen::Library;
        app.needs_redraw = true;
    } else if (x > third * 2) {
        rescan_books(app, board, renderer);
    }
}

void handle_touch(AppState& app, T5P4Board& board, GfxRenderer& renderer, int16_t x, int16_t y)
{
    switch (app.screen) {
        case Screen::Library:
            handle_library_touch(app, renderer, x, y);
            break;
        case Screen::EpubReader:
        case Screen::TxtReader:
            handle_reader_touch(app, renderer, x);
            break;
        case Screen::Toc:
            handle_toc_touch(app, renderer, x, y);
            break;
        case Screen::Menu:
            handle_menu_touch(app, board, renderer, y);
            break;
        case Screen::Error:
            handle_error_touch(app, board, renderer, x);
            break;
    }
}

void reader_task(void* arg)
{
    (void)arg;

    static T5P4Board board;
    if (!board.init()) {
        ESP_LOGE(kTag, "Board initialization failed");
        vTaskDelete(nullptr);
        return;
    }

    static HalDisplay hal_display;
    hal_display.attach(&board.display());
    hal_display.begin();

    static GfxRenderer renderer(hal_display);
    renderer.begin();
    renderer.setOrientation(GfxRenderer::LandscapeCounterClockwise);

    static FontDecompressor decompressor;
    static FontCacheManager font_cache(renderer.getFontMap());
    install_fonts(renderer, font_cache, decompressor);

    auto app = std::make_unique<AppState>();
    if (!rescan_books(*app, board, renderer)) {
        render_error(*app, renderer);
    } else {
        render_library(*app, renderer);
    }

    bool touch_latched = false;
    while (true) {
        int16_t x = 0;
        int16_t y = 0;
        const bool touched = board.read_touch_point(&x, &y);
        if (!touched) {
            touch_latched = false;
        } else if (!touch_latched) {
            touch_latched = true;
            handle_touch(*app, board, renderer, x, y);
            if (app->screen == Screen::Error && app->error_message == "Rescan requested.") {
                rescan_books(*app, board, renderer);
            }
            if (app->needs_redraw) {
                render_current(*app, renderer);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(CONFIG_EPUB_READER_TOUCH_POLL_MS));
    }
}

}  // namespace

extern "C" void app_main(void)
{
    esp_log_level_set(kTag, ESP_LOG_INFO);
    esp_log_level_set("cp_storage", ESP_LOG_WARN);
    esp_log_level_set("cp_display", ESP_LOG_INFO);

    const BaseType_t task_ok = xTaskCreate(reader_task, "crosspoint_reader", CONFIG_EPUB_READER_TASK_STACK_SIZE,
                                           nullptr, 5, nullptr);
    if (task_ok != pdPASS) {
        ESP_LOGE(kTag, "Failed to create reader task");
    }
}
