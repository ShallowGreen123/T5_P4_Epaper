#include <string.h>
#include <stdlib.h>
#include <algorithm>
#include <limits.h>
#include <vector>
#include "sdkconfig.h"
#include "TextBlock.h"
#ifndef UNIT_TEST
#include <esp_log.h>
#else
#define ESP_LOGI(args...)
#define ESP_LOGE(args...)
#define ESP_LOGD(args...)
#define ESP_LOGW(args...)
#endif

// TODO - is there any more whitespace we should consider?
static bool is_whitespace(char c)
{
  return (c == ' ' || c == '\r' || c == '\n');
}

static bool is_ascii_letter(unsigned char c)
{
  return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

// move past anything that should be considered part of a work
static int skip_word(const char *text, int index, int length)
{
  while (index < length && !is_whitespace(text[index]))
  {
    index++;
  }
  return index;
}

// skip past any white space characters
static int skip_whitespace(const char *html, int index, int length)
{
  while (index < length && is_whitespace(html[index]))
  {
    index++;
  }
  return index;
}

void TextBlock::add_span(const char *span, bool is_bold, bool is_italic)
{
  // adding a span to text block
  // make a copy of the text as we'll modify it
  int length = strlen(span);
  char *text = new char[length + 1];
  strcpy(text, span);
  spans.push_back(text);
  // work out where each word is in the span
  int index = 0;
  while (index < length)
  {
    // skip past any whitespace to the start of a word
    index = skip_whitespace(span, index, length);
    int word_start = index;
    // find the end of the word
    index = skip_word(span, index, length);
    int word_length = index - word_start;
    if (word_length > 0)
    {
      // null terminate the word
      text[word_start + word_length] = '\0';
      // store the information about the word for later
      words.push_back(text + word_start);
      // store the style for the word
      word_styles.push_back((is_bold ? BOLD_SPAN : 0) | (is_italic ? ITALIC_SPAN : 0));
    }
  }

  for (int i = 0; i < length; ++i)
  {
    const unsigned char c = static_cast<unsigned char>(span[i]);
    if (is_ascii_letter(c))
    {
      ascii_letter_count++;
    }
    else if (c >= 0x80)
    {
      non_ascii_byte_count++;
    }
  }
}
// given a renderer works out where to break the words into lines
void TextBlock::layout(Renderer *renderer, Epub *epub, int max_width)
{
  (void)epub;
  word_widths.clear();
  word_xpos.clear();
  line_breaks.clear();

  // measure each word
  for (int i = 0; i < words.size(); i++)
  {
    // measure the word
    int width = renderer->get_text_width(words[i], word_styles[i] & BOLD_SPAN, word_styles[i] & ITALIC_SPAN);
    word_widths.push_back(width);
  }

  int page_width = max_width != -1 ? max_width : renderer->get_page_width();
  int space_width = renderer->get_space_width();
  const float max_justified_spacing = (space_width * CONFIG_EPUB_READER_JUSTIFY_MAX_SPACE_PERCENT) / 100.0f;
  const bool is_english_like = ascii_letter_count >= 4 && ascii_letter_count >= (non_ascii_byte_count * 2);
  const int first_line_indent =
      (first_line_indent_enabled && is_english_like) ? CONFIG_EPUB_READER_ENGLISH_FIRST_LINE_INDENT : 0;

  // now apply the dynamic programming algorithm to find the best line breaks
  int n = word_widths.size();
  if (n <= 0)
  {
    return;
  }

  // DP table in which dp[i] represents cost of line starting with word words[i]
  std::vector<int> dp(n);

  // Array in which ans[i] store index of last word in line starting with word word[i]
  std::vector<size_t> ans(n);

  // If only one word is present then only one line is required. Cost of last line is zero. Hence cost
  // of this line is zero. Ending point is also n-1 as single word is present
  dp[n - 1] = 0;
  ans[n - 1] = n - 1;

  // Make each word first word of line by iterating over each index in arr.
  for (int i = n - 2; i >= 0; i--)
  {
    const int available_width = (i == 0) ? std::max(1, page_width - first_line_indent) : page_width;
    int currlen = 0;
    dp[i] = INT_MAX;

    // Variable to store possible minimum cost of line.
    int cost;

    // Keep on adding words in current line by iterating from starting word upto last word in arr.
    for (int j = i; j < n; j++)
    {
      if (j > i)
      {
        currlen += space_width;
      }
      currlen += word_widths[j];

      // If we're bigger than the current pagewidth then we can't add more words
      if (currlen > available_width)
        break;

      // if we've run out of words then this is last line and the cost should be 0
      // Otherwise the cost is the sqaure of the left over space + the costs of all the previous lines
      if (j == n - 1)
        cost = 0;
      else
        cost = (available_width - currlen) * (available_width - currlen) + dp[j + 1];

      // Check if this arrangement gives minimum cost for line starting with word words[i].
      if (cost < dp[i])
      {
        dp[i] = cost;
        ans[i] = j;
      }
    }

    if (dp[i] == INT_MAX)
    {
      dp[i] = (i == n - 1) ? 0 : dp[i + 1];
      ans[i] = i;
    }
  }
  // We can now iterate through the answer to find the line break positions
  size_t i = 0;
  while (i < n)
  {
    const size_t next_break = ans[i] + 1;
    if (next_break <= i || next_break > static_cast<size_t>(n))
    {
      line_breaks.push_back(i + 1);
      i = i + 1;
      continue;
    }

    i = next_break;
    if (i > n)
    {
      ESP_LOGI("TextBlock", "fallen off the end of the words");
      dump();

      for (int x = 0; x < n; x++)
      {
        ESP_LOGI("TextBlock", "line break %d=>%d", x, ans[x]);
      }
      break;
    }
    line_breaks.push_back(i);
    if (line_breaks.size() > 1000)
    {
      ESP_LOGE("TextBlock", "too many line breaks");
      dump();

      for (int x = 0; x < n; x++)
      {
        ESP_LOGI("TextBlock", "line break %d=>%d", x, ans[x]);
      }
      break;
    }
  }
  // With the page breaks calculated we can now position the words along the line
  int start_word = 0;
  word_xpos.resize(words.size());
  for (int i = 0; i < line_breaks.size(); i++)
  {
    const int line_indent = (i == 0) ? first_line_indent : 0;
    const int available_width = page_width - line_indent;
    int total_word_width = 0;
    for (int word_index = start_word; word_index < line_breaks[i]; word_index++)
    {
      total_word_width += word_widths[word_index];
    }
    int number_words = line_breaks[i] - start_word;
    const float line_width = total_word_width + std::max(0, number_words - 1) * static_cast<float>(space_width);
    float spare_space = available_width - line_width;
    float actual_spacing = static_cast<float>(space_width);
    // don't add space if we are on the last line and we are not justified text
    if (i != line_breaks.size() - 1 && style == JUSTIFIED)
    {
      if (number_words > 1)
      {
        actual_spacing = std::min((available_width - total_word_width) / float(number_words - 1), max_justified_spacing);
        spare_space = available_width - (total_word_width + (number_words - 1) * actual_spacing);
      }
    }
    float xpos = static_cast<float>(line_indent);
    if (style == RIGHT_ALIGN)
    {
      xpos = line_indent + std::max(0.0f, spare_space);
    }
    if (style == CENTER_ALIGN)
    {
      xpos = line_indent + std::max(0.0f, spare_space) / 2;
    }
    for (int word_index = start_word; word_index < line_breaks[i]; word_index++)
    {
      word_xpos[word_index] = xpos;
      xpos += word_widths[word_index] + actual_spacing;
    }
    start_word = line_breaks[i];
  }
  spans.shrink_to_fit();
  words.shrink_to_fit();
  word_widths.shrink_to_fit();
  word_xpos.shrink_to_fit();
  word_styles.shrink_to_fit();
}
void TextBlock::render(Renderer *renderer, int line_break_index, int x_pos, int y_pos)
{
  int start = line_break_index == 0 ? 0 : line_breaks[line_break_index - 1];
  int end = line_breaks[line_break_index];
  for (int i = start; i < end; i++)
  {
    // get the style
    uint8_t style = word_styles[i];
    // render the word
    renderer->draw_text(x_pos + word_xpos[i], y_pos, words[i], style & BOLD_SPAN, style & ITALIC_SPAN);
  }
}
// debug helper - dumps out the contents of the block with line breaks
void TextBlock::dump()
{
  for (int i = 0; i < words.size(); i++)
  {
    printf("##%d#%s## ", word_widths[i], words[i]);
  }
}
