/* See LICENSE file for copyright and license details. */
#include "draw.h"
#include <bits/stdint-intn.h>
#include <ctype.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>

#define INRECT(x, y, rx, ry, rw, rh)                                           \
  ((x) >= (rx) && (x) < (rx) + (rw) && (y) >= (ry) && (y) < (ry) + (rh))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

typedef struct Item Item;
struct Item {
  char *text;
  Item *next;         /* traverses all items */
  Item *left, *right; /* traverses matching items */
  int32_t width;
  int score;
};

typedef enum { LEFT, RIGHT, CENTRE } TextPosition;

struct {
  int32_t width;
  int32_t height;
  int32_t text_height;
  int32_t text_y;
  int32_t input_field;
  int32_t scroll_left;
  int32_t matches;
  int32_t scroll_right;
} window_config;

const char *progname;

static uint32_t color_bg = 0x0f0f0fff;
static uint32_t color_fg = 0xbbbbbbff;
static uint32_t color_input_bg = 0x0f0f0fff;
static uint32_t color_input_fg = 0xbbbbbbff;
static uint32_t color_prompt_bg = 0x924441ff;
static uint32_t color_prompt_fg = 0xeeeeeeff;
static uint32_t color_selected_bg = 0x924441ff;
static uint32_t color_selected_fg = 0xeeeeeeff;

static int32_t line_height = 25;
static int prompt_width = 0;

char *get_clipboard_text(void);
static void appenditem(Item *item, Item **list, Item **last);
static int fuzzy_match(const char *text, const char *pattern);
static void insert(const char *s, ssize_t n, struct dmenu_panel *panel);
static void match(struct dmenu_panel *panel);
static size_t nextrune(int incr);
static void readstdin(void);
void truncate_and_ellipsis(const char *input, char *output, size_t max_len);
static void alarmhandler(int signum);
static void handle_return(char *value, struct dmenu_panel *panel);
static void usage(void);
uint32_t parse_color(char *str);
static int retcode = EXIT_SUCCESS;
static int selected_monitor = 0;
static char *selected_monitor_name = 0;

static char text[BUFSIZ];
static char text_[BUFSIZ];
static int itemcount = 0;
static int lines = 0;
static int gridn = 1;
int grid_width;
static int timeout = 3;
static size_t cursor = 0;
static const char *prompt = "Choose: ";
static bool message = false;
static bool nostdin = false;
static bool returnearly = false;
static bool show_in_bottom = false;
static bool password = false;
static TextPosition messageposition = LEFT;
static Item *items = NULL;
static Item *matches, *sel;
static Item *prev, *curr, *next;
static Item *leftmost, *rightmost;
static char *font = "Hack 11";
static int font_size = 14;
int noOfMatch = 0;
int currentposition = 0;

static int (*fstrncmp)(const char *, const char *, size_t) = strncasecmp;

void insert(const char *s, ssize_t n, struct dmenu_panel *panel) {
  if (strlen(text) + n > sizeof text - 1) {
    return;
  }

  memmove(text + cursor + n, text + cursor, sizeof text - cursor - MAX(n, 0));

  if (n > 0) {
    memcpy(text + cursor, s, n);
  }

  cursor += n;
  match(panel);
}

void keyrepeat(struct dmenu_panel *panel) {
  if (panel->on_keyevent) {
    panel->on_keyevent(panel, panel->repeat_key_state, panel->repeat_sym,
                       panel->keyboard.control, panel->keyboard.shift);
  }
}

void keypress(struct dmenu_panel *panel, enum wl_keyboard_key_state state,
              xkb_keysym_t sym, bool ctrl, bool shft) {
  char buf[8];
  size_t len = strlen(text);

  if (state != WL_KEYBOARD_KEY_STATE_PRESSED)
    return;

  if (ctrl) {
    switch (xkb_keysym_to_lower(sym)) {
    case XKB_KEY_a:
      sym = XKB_KEY_Home;
      break;

    case XKB_KEY_v: {
      char *texts = get_clipboard_text();
      if (texts) {
        size_t len_texts = strlen(texts);
        size_t current_len = strlen(text);

        if (current_len + len_texts < BUFSIZ) {
          strncat(text, texts, BUFSIZ - current_len - 1);
        } else {
          fprintf(stderr, "⚠️ Not enough space in buffer to append full "
                          "clipboard text. Truncating.\n");
          size_t space_left = BUFSIZ - current_len - 1;
          strncat(text, texts, space_left);
        }

        cursor = strlen(text);

        match(panel);
        dmenu_draw(panel);

        free(texts);
        sym = XKB_KEY_NoSymbol;
      }
    } break;
    case XKB_KEY_e:
      sym = XKB_KEY_End;
      break;
    case XKB_KEY_f:
    case XKB_KEY_n:
      sym = XKB_KEY_Right;
      break;
    case XKB_KEY_b:
    case XKB_KEY_p:
      sym = XKB_KEY_Left;
      break;
    case XKB_KEY_h:
      sym = XKB_KEY_BackSpace;
      break;
    case XKB_KEY_j:
      sym = XKB_KEY_Return;
      break;
    case XKB_KEY_g:
    case XKB_KEY_c:
      retcode = EXIT_FAILURE;
      dmenu_close(panel);
      return;
    }
  }

  switch (sym) {
  case XKB_KEY_KP_Enter: /* fallthrough */
  case XKB_KEY_Return:
    handle_return((sel && !shft) ? sel->text : text, panel);
    break;

  case XKB_KEY_Escape:
    retcode = EXIT_FAILURE;
    dmenu_close(panel);
    break;

  case XKB_KEY_Up:
    if (lines > 0) {
      if (sel && sel->left) {
        currentposition--;
        sel = sel->left;
      }
    } else {
      if (sel && sel->left) {
        currentposition--;
        sel = sel->left;
      }
    }
    break;
  case XKB_KEY_Left:
    if (lines > 0) {
      if (sel) {
        Item *t_item = sel;
        int steps = 0;
        // Move exact column widths back relative to vertical list height
        // constraints
        while (steps < lines && t_item->left) {
          t_item = t_item->left;
          steps++;
        }
        if (steps > 0) {
          sel = t_item;
          currentposition -= steps;
        }
      }
    } else {
      if (sel && sel->left) {
        currentposition--;
        sel = sel->left;
      } else if (cursor > 0) {
        cursor = nextrune(-1);
      }
    }
    break;

  case XKB_KEY_Down:
    if (lines > 0) {
      if (sel && sel->right) {
        currentposition++;
        sel = sel->right;
      }
    } else {
      if (sel && sel->right) {
        currentposition++;
        sel = sel->right;
      }
    }
    break;
  case XKB_KEY_Right:
    if (lines > 0) {
      if (sel) {
        Item *t_item = sel;
        int steps = 0;
        // Move exact column widths forward relative to vertical list height
        // constraints
        while (steps < lines && t_item->right) {
          t_item = t_item->right;
          steps++;
        }
        if (steps > 0) {
          sel = t_item;
          currentposition += steps;
        }
      }
    } else {
      if (cursor < len) {
        cursor = nextrune(+1);
      } else if (sel && sel->right) {
        currentposition++;
        sel = sel->right;
      }
    }
    break;
  case XKB_KEY_End:
    if (cursor < len) {
      cursor = len;
      break;
    }
    while (sel && sel->right) {
      sel = sel->right;
      currentposition++;
    }
    break;

  case XKB_KEY_Home:
    if (sel == matches) {
      cursor = 0;
      break;
    }
    sel = curr = matches;
    currentposition = 0;
    break;

  case XKB_KEY_BackSpace:
    if (cursor > 0)
      insert(NULL, nextrune(-1) - cursor, panel);
    break;

  case XKB_KEY_Delete:
    if (cursor == len)
      return;
    cursor = nextrune(+1);
    break;

  case XKB_KEY_Tab:
    if (!sel)
      return;
    strncpy(text, sel->text, sizeof text);
    cursor = strlen(text);
    match(panel);
    break;

  default:
    if (xkb_keysym_to_utf8(sym, buf, 8)) {
      insert(buf, strnlen(buf, 8), panel);
    }
  }

  dmenu_draw(panel);
}

void cairo_set_source_u32(cairo_t *cairo, uint32_t color) {
  cairo_set_source_rgba(cairo, (color >> (3 * 8) & 0xFF) / 255.0,
                        (color >> (2 * 8) & 0xFF) / 255.0,
                        (color >> (1 * 8) & 0xFF) / 255.0,
                        (color >> (0 * 8) & 0xFF) / 255.0);
}

void draw_text(cairo_t *cairo, int32_t width, int32_t height, const char *str,
               int32_t *x, int32_t *y, int32_t *new_x, int32_t *new_y,
               int32_t scale, uint32_t foreground_color,
               uint32_t background_color, int32_t padding) {

  int32_t text_width, text_height;

  get_text_size(cairo, font, &text_width, &text_height, NULL, scale, false,
                str);

  int32_t text_y = (height * scale - text_height) / 2.0 + *y;

  if (background_color) {
    cairo_set_source_u32(cairo, background_color);
    cairo_rectangle(cairo, *x, *y,
                    (lines ? width / gridn : text_width + 2 * padding) * scale,
                    height * scale);
    grid_width =
        (lines ? (width - *x) / gridn : text_width + 2 * padding) * scale;
    cairo_fill(cairo);
  }

  cairo_move_to(cairo, *x + padding * scale, text_y);
  cairo_set_source_u32(cairo, foreground_color);

  pango_printf(cairo, font, scale, false, str);

  *new_x += text_width + 2 * padding * scale;
  *new_y += height * scale;
}

void draw(cairo_t *cairo, int32_t width, int32_t height, int32_t scale) {
  int32_t x = 0;
  int32_t y = 0;
  int32_t bin;

  int32_t item_padding = 10;
  int32_t text_width, text_height;

  get_text_size(cairo, font, &text_width, &text_height, NULL, scale, false,
                "Aj");

  int32_t text_y = (line_height * scale - text_height) / 2.0;

  cairo_set_source_u32(cairo, color_bg);
  cairo_paint(cairo);

  if (prompt) {
    draw_text(cairo, width, line_height, prompt, &x, &y, &x, &bin, scale,
              color_prompt_fg, color_prompt_bg, 6);
    prompt_width = x;
    window_config.input_field = x;
  } else {
    prompt_width = 0;
    window_config.input_field = 0;
  }

  int text_input_x = prompt_width;

  cairo_set_source_u32(cairo, color_input_bg);
  cairo_rectangle(cairo, window_config.input_field, 0,
                  (lines ? width : 300) * scale, line_height * scale);
  cairo_fill(cairo);

  memset(text_, 0, BUFSIZ);

  if (password) {
    size_t char_count = 0;
    for (size_t i = 0; text[i] != '\0'; i++) {
      if ((text[i] & 0xC0) != 0x80)
        char_count++;
    }
    size_t p_len = MIN(char_count, BUFSIZ - 1);
    memset(text_, '*', p_len);
    text_[p_len] = '\0';
  } else {
    strncpy(text_, text, cursor);
    text_[cursor] = '\0';
  }

  int draw_input_x = text_input_x;
  draw_text(cairo, width, line_height, (password) ? text_ : text, &draw_input_x,
            &y, &bin, (lines ? &y : &bin), scale, color_input_fg, 0, 6);

  {
    int32_t text_widths, text_heights;
    get_text_size(cairo, font, &text_widths, &text_heights, NULL, scale, false,
                  text_);

    int32_t padding = 6 * scale;
    cairo_rectangle(cairo, text_input_x + padding + text_widths, text_y, 2,
                    text_heights);
    cairo_fill(cairo);
  }

  if (!lines) {
    x = text_input_x + 320 * scale;
  }
  int32_t scroll_indicator_pos = x;

  if (!matches) {
    return;
  }

  Item *item;
  int gw = (width - prompt_width) / gridn;
  int max_len =
      (gw - (item_padding * 2) - ((gridn == 1) ? 100 : 0)) / (text_width / 2);

  if (!lines) {
    int input_box_end = x;
    if (!curr)
      curr = matches;

    bool sel_before_curr = false;
    for (item = matches; item && item != curr; item = item->right) {
      if (item == sel) {
        sel_before_curr = true;
        break;
      }
    }
    if (sel_before_curr) {
      curr = sel;
    }

    while (curr && curr != sel) {
      int test_x = input_box_end;
      bool fits = false;
      for (item = curr; item; item = item->right) {
        int w, h;
        get_text_size(cairo, font, &w, &h, NULL, scale, false, item->text);
        int alloc_w = w + (item_padding * 2 * scale);

        if (test_x + alloc_w > width - (15 * scale)) {
          break;
        }
        if (item == sel) {
          fits = true;
          break;
        }
        test_x += alloc_w;
      }
      if (fits) {
        break;
      }
      curr = curr->right;
    }

    if (curr != matches) {
      cairo_move_to(cairo, scroll_indicator_pos - (15 * scale), text_y);
      cairo_set_source_u32(cairo, color_fg);
      pango_printf(cairo, font, scale, false, "<");
    }

    x = input_box_end;
    for (item = curr; item; item = item->right) {
      int w, h;
      get_text_size(cairo, font, &w, &h, NULL, scale, false, item->text);
      int alloc_w = w + (item_padding * 2 * scale);

      if (x + alloc_w > width - (15 * scale)) {
        cairo_move_to(cairo, width - (12 * scale), text_y);
        cairo_set_source_u32(cairo, color_fg);
        pango_printf(cairo, font, scale, false, ">");
        break;
      }

      uint32_t bg_color = sel == item ? color_selected_bg : color_bg;
      uint32_t fg_color = sel == item ? color_selected_fg : color_fg;

      draw_text(cairo, width, line_height, item->text, &x, &y, &x, &bin, scale,
                fg_color, bg_color, item_padding);
    }
  } else {
    int display_capacity = lines * gridn;
    if (!curr)
      curr = matches;

    bool sel_before_curr = false;
    for (item = matches; item && item != curr; item = item->right) {
      if (item == sel) {
        sel_before_curr = true;
        break;
      }
    }
    if (sel_before_curr) {
      curr = sel;
    }

    while (curr && curr != sel) {
      int count = 0;
      bool fits = false;
      for (item = curr; item; item = item->right) {
        if (item == sel) {
          fits = true;
          break;
        }
        if (++count >= display_capacity)
          break;
      }
      if (fits)
        break;
      curr = curr->right;
    }

    int i = 0;
    for (item = curr; item; item = item->right) {
      if (i >= display_capacity)
        break;

      uint32_t bg_color = sel == item ? color_selected_bg : color_bg;
      uint32_t fg_color = sel == item ? color_selected_fg : color_fg;

      char newtext[max_len > 0 ? max_len : 1];
      truncate_and_ellipsis(item->text, newtext, max_len > 0 ? max_len : 1);

      if (gridn > 1) {
        int col = i / lines;
        int row = i % lines;
        x = prompt_width + (col * gw);
        y = (row + 1) * line_height * scale;

        if (x >= width || y >= height)
          break;

        draw_text(cairo, width, line_height, newtext, &x, &y, &bin, &bin, scale,
                  fg_color, bg_color, item_padding);
      } else {
        x = prompt_width; // Correct alignment padding relative to prompt width
                          // bounds
        y = (i + 1) * line_height * scale;
        if (y >= height)
          break;
        draw_text(cairo, width, line_height, newtext, &x, &y, &bin, &bin, scale,
                  fg_color, bg_color, item_padding);
      }
      i++;
    }
  }
}

uint32_t parse_color(char *str) {
  if (!str)
    eprintf("NULL as color value\n");

  size_t len = strnlen(str, BUFSIZ);

  if ((len != 7 && len != 9) || str[0] != '#') {
    eprintf("Color format must be '#rrggbb[aa]'\n");
  }

  uint32_t _val = strtol(&str[1], NULL, 16);
  uint32_t color = 0x000000ff;

  if (len == 9) {
    color = _val;
  } else {
    color = (_val << 8) + 0xff;
  }

  return color;
}

int main(int argc, char **argv) {
  int i;
  progname = "dmenu";

  for (i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "-v") || !strcmp(argv[1], "--version")) {
      fputs("dmenu-wl-" VERSION
            ", © 2006-2018 dmenu engineers, see LICENSE for details\n",
            stdout);
      exit(EXIT_SUCCESS);
    } else if (!strcmp(argv[i], "-b") || !strcmp(argv[i], "--bottom"))
      show_in_bottom = true;
    else if (!strcmp(argv[i], "-e") || !strcmp(argv[i], "--echo"))
      message = true;
    else if (!strcmp(argv[i], "-ec") || !strcmp(argv[i], "--echo-centre"))
      message = true, messageposition = CENTRE;
    else if (!strcmp(argv[i], "-er") || !strcmp(argv[i], "--echo-right"))
      message = true, messageposition = RIGHT;
    else if (!strcmp(argv[i], "-i") || !strcmp(argv[i], "--insensitive")) {
      fstrncmp = strncmp;
    } else if (!strcmp(argv[i], "-r") || !strcmp(argv[i], "--return-early"))
      returnearly = true;
    else if (!strcmp(argv[i], "-P"))
      password = true;
    else if (!strcmp(argv[i], "-et") || !strcmp(argv[i], "--echo-timeout"))
      timeout = atoi(argv[++i]);
    else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--height"))
      line_height = atoi(argv[++i]);
    else if (!strcmp(argv[i], "-g") || !strcmp(argv[i], "--grid")) {
      gridn = atoi(argv[++i]);
    } else if (!strcmp(argv[i], "-l") || !strcmp(argv[i], "--lines"))
      lines = atoi(argv[++i]);
    else if (!strcmp(argv[i], "-m") || !strcmp(argv[i], "--monitor")) {
      ++i;
      bool is_num = true;

      for (size_t j = 0; j < strlen(argv[i]); ++j) {
        if (!isdigit(argv[i][j])) {
          is_num = false;
          break;
        }
      }

      if (is_num) {
        selected_monitor = atoi(argv[i]);
      } else {
        selected_monitor = -1;
        selected_monitor_name = argv[i];
      }
    } else if (!strcmp(argv[i], "-p") || !strcmp(argv[i], "--prompt"))
      prompt = argv[++i];
    else if (!strcmp(argv[i], "-P") || !strcmp(argv[i], "--password"))
      password = true;
    else if (!strcmp(argv[i], "-po") || !strcmp(argv[i], "--prompt-only"))
      prompt = argv[++i], nostdin = true;
    else if (!strcmp(argv[i], "-fn") || !strcmp(argv[i], "--font-name"))
      font = argv[++i];
    else if (!strcmp(argv[i], "-fs") || !strcmp(argv[i], "--font-size"))
      font_size = atoi(argv[++i]);
    else if (!strcmp(argv[i], "-nb") || !strcmp(argv[i], "--normal-background"))
      color_bg = color_input_bg = parse_color(argv[++i]);
    else if (!strcmp(argv[i], "-nf") || !strcmp(argv[i], "--normal-foreground"))
      color_fg = color_input_fg = parse_color(argv[++i]);
    else if (!strcmp(argv[i], "-sb") ||
             !strcmp(argv[i], "--selected-background"))
      color_prompt_bg = color_selected_bg = parse_color(argv[++i]);
    else if (!strcmp(argv[i], "-sf") ||
             !strcmp(argv[i], "--selected-foreground"))
      color_prompt_fg = color_selected_fg = parse_color(argv[++i]);
    else {
      usage();
    }
  }

  if (message) {
    signal(SIGALRM, alarmhandler);
    alarm(timeout);
  }

  if (!nostdin) {
    readstdin();
  }

  int32_t panel_height = line_height;
  if (lines && items != NULL) {
    if (itemcount < lines)
      lines = itemcount;
    panel_height *= lines + 1;
  }

  struct dmenu_panel panel;
  panel.selected_monitor = selected_monitor;
  panel.selected_monitor_name = selected_monitor_name;
  dmenu_init_panel(&panel, panel_height, show_in_bottom);

  panel.on_keyevent = keypress;
  panel.on_keyrepeat = keyrepeat;
  panel.draw = draw;

  match(&panel);

  struct monitor_info *monitor = panel.monitor;
  double factor = monitor->scale /
                  ((double)monitor->physical_width / monitor->logical_width);

  window_config.height =
      round_to_int(panel.height /
                   ((double)monitor->physical_width / monitor->logical_width));
  window_config.height *= monitor->scale;
  window_config.width = round_to_int(monitor->physical_width * factor);

  get_text_size(panel.surface.cairo, font, NULL, &window_config.text_height,
                NULL, monitor->scale, false, "Aj");

  window_config.text_y =
      (window_config.height / 2.0) - (window_config.text_height / 2.0);

  dmenu_show(&panel);
  return retcode;
}

void appenditem(Item *item, Item **list, Item **last) {
  if (!*last) {
    *list = item;
  } else {
    (*last)->right = item;
  }

  item->left = *last;
  item->right = NULL;
  *last = item;
}

static int fuzzy_match(const char *text_str, const char *pattern) {
  if (!pattern || !*pattern)
    return 0;

  bool strict_case = (fstrncmp == strncmp);
  int score = 0;
  int penalty = 0;
  const char *t = text_str;
  const char *p = pattern;

  while (*t && *p) {
    char t_char = strict_case ? *t : tolower((unsigned char)*t);
    char p_char = strict_case ? *p : tolower((unsigned char)*p);

    if (t_char == p_char) {
      score += 10;
      if (t == text_str || *(t - 1) == ' ' || *(t - 1) == '-' ||
          *(t - 1) == '_' || *(t - 1) == '/') {
        score += 45;
      }
      score -= penalty;
      penalty = 0;
      p++;
    } else {
      penalty++;
    }
    t++;
  }

  return (*p == '\0') ? score : -1;
}

static int compare_items(const void *a, const void *b) {
  Item *ia = *(Item **)a;
  Item *ib = *(Item **)b;

  if (!ia && !ib)
    return 0;
  if (!ia)
    return 1;
  if (!ib)
    return -1;

  return ib->score - ia->score;
}

void match(struct dmenu_panel *panel) {
  Item *item, *last = NULL;
  int match_count = 0;

  matches = leftmost = rightmost = NULL;

  if (text[0] == '\0') {
    noOfMatch = 0;
    for (item = items; item; item = item->next) {
      item->score = 0;
      appenditem(item, &matches, &last);
      noOfMatch++;
    }
    curr = prev = next = sel = matches;
    leftmost = matches;
    return;
  }

  for (item = items; item; item = item->next) {
    int base_score = fuzzy_match(item->text, text);
    if (base_score >= 0) {
      item->score = base_score - strlen(item->text);

      if (strcasecmp(item->text, text) == 0) {
        item->score += 10000;
      }
      match_count++;
    } else {
      item->score = -1;
    }
  }

  if (match_count == 0) {
    noOfMatch = 0;
    curr = prev = next = sel = NULL;
    return;
  }

  Item **array = malloc(match_count * sizeof(Item *));
  if (!array) {
    eprintf("Allocation failed during fuzzy sort.\n");
  }

  int idx = 0;
  for (item = items; item; item = item->next) {
    if (fuzzy_match(item->text, text) >= 0) {
      array[idx++] = item;
    }
  }

  qsort(array, match_count, sizeof(Item *), compare_items);

  noOfMatch = 0;
  for (int i = 0; i < match_count; i++) {
    if (array[i]) {
      appenditem(array[i], &matches, &last);
      noOfMatch++;
    }
  }

  free(array);

  curr = prev = next = sel = matches;
  leftmost = matches;

  if (returnearly && curr && !curr->right) {
    printf("dieter\n");
    handle_return(curr->text, panel);
  }
}

size_t nextrune(int incr) {
  size_t n, len;
  len = strlen(text);

  for (n = cursor + incr; n >= 0 && n < len && (text[n] & 0xc0) == 0x80;
       n += incr)
    ;

  return n;
}

void readstdin(void) {
  char *line = NULL;
  size_t linecap = 0;
  ssize_t linelen;
  Item *item, **end;

  end = &items;
  while ((linelen = getline(&line, &linecap, stdin)) != -1) {
    itemcount++;

    if (linelen > 0 && line[linelen - 1] == '\n') {
      line[linelen - 1] = '\0';
    }

    if (!(item = malloc(sizeof *item))) {
      eprintf("cannot malloc %zu bytes\n", sizeof *item);
    }
    item->width = -1;
    item->score = 0;

    if (!(item->text = strdup(line))) {
      eprintf("cannot strdup %zu bytes\n", strlen(line) + 1);
    }

    item->next = item->left = item->right = NULL;
    *end = item;
    end = &item->next;
  }
  free(line);
}

void handle_return(char *value, struct dmenu_panel *panel) {
  dmenu_close(panel);
  fputs(value, stdout);
  fflush(stdout);
}

void alarmhandler(int signum) { exit(EXIT_SUCCESS); }

void usage(void) {
  printf("Usage: dmenu [OPTION]...\n");
  printf("Display newline-separated input stdin as a menubar\n");
  printf("\n");
  printf("  -e,  --echo                       display text from stdin with no "
         "user interaction\n");
  printf("  -ec, --echo-centre                same as -e but align text "
         "centrally\n");
  printf(
      "  -er, --echo-right                 same as -e but align text right\n");
  printf("  -et, --echo-timeout SECS          close the message after SEC "
         "seconds when using -e, -ec, or -er\n");
  printf("  -b,  --bottom                     dmenu appears at the bottom of "
         "the screen\n");
  printf("  -h,  --height N                   set dmenu to be N pixels high\n");
  printf("  -i,  --insensitive                dmenu matches menu items case "
         "insensitively (ON by default; flag forces sensitivity)\n");
  printf("  -l,  --lines LINES                dmenu lists items vertically, "
         "within the\n");
  printf("  -g,  --grid N                     dmenu lists items horizontally, "
         "within the given number of lines\n");
  printf("  -m,  --monitor MONITOR            dmenu appears on the given "
         "Xinerama screen (compatibility only)\n");
  printf("  -p,  --prompt  PROMPT             prompt to be displayed to the "
         "left of the input field\n");
  printf("  -P,  --password                   input will be hidden\n");
  printf("  -po, --prompt-only  PROMPT        same as -p but don't wait for "
         "stdin\n");
  printf("  -r,  --return-early               return as soon as a single match "
         "is found\n");
  printf("  -fn, --font-name FONT             font or font set to be used\n");
  printf("  -nb, --normal-background COLOR    normal background color\n");
  printf("  -nf, --normal-foreground COLOR    normal foreground color\n");
  printf("  -sb, --selected-background COLOR  selected background color\n");
  printf("  -sf, --selected-foreground COLOR  selected foreground color\n");
  printf("  -v,  --version                    display version information\n");

  exit(EXIT_FAILURE);
}

void truncate_and_ellipsis(const char *input, char *output, size_t max_len) {
  if (max_len < 4) {
    strncpy(output, "...", max_len - 1);
    output[max_len - 1] = '\0';
    return;
  }

  size_t input_len = strlen(input);

  if (input_len < max_len) {
    strncpy(output, input, max_len - 1);
    output[max_len - 1] = '\0';
  } else {
    strncpy(output, input, max_len - 4);
    output[max_len - 4] = '\0';
    strcat(output, "...");
  }
}

char *get_clipboard_text(void) {
  FILE *fp = popen("wl-paste --no-newline", "r");
  if (!fp) {
    perror("popen failed");
    return NULL;
  }

  size_t bufsize = 8192;
  char *buffer = malloc(bufsize);
  if (!buffer) {
    perror("malloc failed");
    pclose(fp);
    return NULL;
  }

  size_t len = 0;
  int c;

  while ((c = fgetc(fp)) != EOF && len < bufsize - 1) {
    buffer[len++] = (char)c;
  }
  buffer[len] = '\0';

  pclose(fp);
  return buffer;
}
