#include <string.h>
#include <algorithm>
#ifndef UNIT_TEST
#include <esp_log.h>
#include <esp_system.h>
#else
#define ESP_LOGI(args...)
#define ESP_LOGE(args...)
#define ESP_LOGI(args...)
#define ESP_LOGD(args...)
#endif
#include "EpubReader.h"
#include "Epub.h"
#include "../RubbishHtmlParser/RubbishHtmlParser.h"
#include "../Renderer/Renderer.h"

static const char *TAG = "EREADER";

bool EpubReader::load()
{
  ESP_LOGD(TAG, "Before epub load: %d", esp_get_free_heap_size());
  // do we need to load the epub?
  if (!epub || epub->get_path() != state.path)
  {
    renderer->show_busy();
    delete epub;
    delete parser;
    parser = nullptr;
    epub = new Epub(state.path);
    if (!epub->load())
    {
      return false;
    }
    ESP_LOGD(TAG, "After epub load: %d", esp_get_free_heap_size());
  }
  return true;
}

void EpubReader::parse_and_layout_current_section()
{
  if (!parser && epub != nullptr)
  {
    renderer->show_busy();
    if (epub->get_spine_items_count() <= 0)
    {
      state.pages_in_current_section = 0;
      return;
    }
    if (state.current_section >= epub->get_spine_items_count())
    {
      state.current_section = std::max(0, epub->get_spine_items_count() - 1);
    }
    ESP_LOGI(TAG, "Parse and render section %d", state.current_section);
    ESP_LOGD(TAG, "Before read html: %d", esp_get_free_heap_size());

    // if spine item is not found here then it will return get_spine_item(0)
    // so it does not crashes when you want to go after last page (out of vector range)
    std::string item = epub->get_spine_item(state.current_section);
    if (item.empty())
    {
      state.pages_in_current_section = 0;
      return;
    }
    std::string base_path = item.substr(0, item.find_last_of('/') + 1);
    char *html = reinterpret_cast<char *>(epub->get_item_contents(item));
    if (html == nullptr)
    {
      ESP_LOGE(TAG, "Failed to load section %s", item.c_str());
      state.pages_in_current_section = 0;
      return;
    }
    ESP_LOGD(TAG, "After read html: %d", esp_get_free_heap_size());
    parser = new RubbishHtmlParser(html, strlen(html), base_path);
    free(html);
    ESP_LOGD(TAG, "After parse: %d", esp_get_free_heap_size());
    parser->layout(renderer, epub);
    ESP_LOGD(TAG, "After layout: %d", esp_get_free_heap_size());
    state.pages_in_current_section = parser->get_page_count();
  }
}

void EpubReader::next()
{
  if (epub == nullptr || state.pages_in_current_section == 0)
  {
    return;
  }

  state.current_page++;
  if (state.current_page >= state.pages_in_current_section)
  {
    if ((state.current_section + 1) < epub->get_spine_items_count())
    {
      state.current_section++;
      state.current_page = 0;
      delete parser;
      parser = nullptr;
    }
    else
    {
      state.current_page = state.pages_in_current_section - 1;
    }
  }
}

void EpubReader::prev()
{
  if (epub == nullptr)
  {
    return;
  }

  if (state.current_page == 0)
  {
    if (state.current_section > 0)
    {
      delete parser;
      parser = nullptr;
      state.current_section--;
      ESP_LOGD(TAG, "Going to previous section %d", state.current_section);
      parse_and_layout_current_section();
      state.current_page = state.pages_in_current_section - 1;
      return;
    }
    return;
  }
  state.current_page--;
}

void EpubReader::render()
{
  if (!parser)
  {
    parse_and_layout_current_section();
  }
  if (!parser)
  {
    renderer->clear_screen();
    renderer->draw_text_box("Unable to render this section.", 16, renderer->get_page_height() / 2 - 30,
                            renderer->get_page_width() - 32, 80, true, false);
    return;
  }
  ESP_LOGD(TAG, "rendering page %d of %d", state.current_page, parser->get_page_count());
  parser->render_page(state.current_page, renderer, epub);
  ESP_LOGD(TAG, "rendered page %d of %d", state.current_page, parser->get_page_count());
  ESP_LOGD(TAG, "after render: %d", esp_get_free_heap_size());
}

void EpubReader::set_state_section(uint16_t current_section) {
  ESP_LOGI(TAG, "go to section:%d", current_section);
  state.current_section = current_section;
}
