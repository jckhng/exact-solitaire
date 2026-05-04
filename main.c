/*
 * Kindle Aisleriot
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Unofficial Kindle-focused solitaire adaptation inspired by GNOME Aisleriot.
 */

#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include <cairo.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "solitaire_engine.h"

#define APP_TITLE "Kindle Aisleriot"
#define KINDLE_WINDOW_TITLE "L:A_N:application_ID:kindleaisleriot_PC:N_O:URL"
#define KINDLE_WINDOW_TITLE_TOPBAR "L:A_N:application_PC:T_ID:kindleaisleriot_O:URL"
#define LOG_PATH "/mnt/us/kindle-aisleriot.log"
#define SAVE_PATH "/mnt/us/documents/kindle-aisleriot.save"
#define SAVE_MAGIC "KAISLERIOT1"
#define KINDLE_APP_WIDTH 1072
#define KINDLE_APP_HEIGHT 1448

typedef struct {
    double x;
    double y;
    double w;
    double h;
} Rect;

static const char *kindle_window_title(void)
{
    const char *value = g_getenv("KINDLE_SHOW_TOPBAR");
    return (value != NULL && value[0] != '\0' && strcmp(value, "0") != 0) ? KINDLE_WINDOW_TITLE_TOPBAR
                                                                          : KINDLE_WINDOW_TITLE;
}

typedef enum {
    APP_GAME_KLONDIKE_DRAW1 = 0,
    APP_GAME_KLONDIKE_DRAW3,
    APP_GAME_FREECELL
} AppGameMode;

typedef struct {
    GtkWidget *window;
    GtkWidget *board;
    GtkWidget *status;
    GtkWidget *game_combo;
    SolitaireGame game;
    AppGameMode game_mode;
    gboolean suppress_game_mode_cb;
    gboolean has_selection;
    SolLocation selected;
    char message[192];
    Rect stock_rect;
    Rect waste_rect;
    Rect waste_card_rects[3];
    Rect freecell_rects[SOL_FREECELL_COUNT];
    Rect foundation_rects[SOL_FOUNDATION_COUNT];
    Rect tableau_rects[SOL_TABLEAU_COUNT];
    double card_w;
    double card_h;
    double tableau_gap;
} AppState;

typedef struct {
    char magic[16];
    AppGameMode game_mode;
    SolitaireGame game;
} SaveFile;

static AppState app;

static void update_ui(void);
static gboolean board_expose(GtkWidget *widget, GdkEventExpose *event, gpointer data);
static gboolean board_button(GtkWidget *widget, GdkEventButton *event, gpointer data);

static void app_log(const char *message)
{
    FILE *f = fopen(LOG_PATH, "a");
    if (!f)
        return;
    fprintf(f, "[app] %s\n", message);
    fclose(f);
}

static void set_message(const char *message)
{
    g_strlcpy(app.message, message, sizeof(app.message));
}

static gboolean app_is_freecell(void)
{
    return app.game.mode == SOL_GAME_FREECELL;
}

static void app_install_kindle_style(void)
{
    gtk_rc_parse_string(
        "style \"kindle_high_contrast\" {\n"
        "  fg[NORMAL] = \"#000000\"\n"
        "  fg[ACTIVE] = \"#000000\"\n"
        "  fg[PRELIGHT] = \"#ffffff\"\n"
        "  fg[SELECTED] = \"#ffffff\"\n"
        "  text[NORMAL] = \"#000000\"\n"
        "  text[SELECTED] = \"#ffffff\"\n"
        "  base[NORMAL] = \"#ffffff\"\n"
        "  base[SELECTED] = \"#000000\"\n"
        "  bg[NORMAL] = \"#ffffff\"\n"
        "  bg[PRELIGHT] = \"#000000\"\n"
        "  bg[SELECTED] = \"#000000\"\n"
        "}\n"
        "gtk-button-images = 0\n"
        "gtk-menu-images = 0\n"
        "widget_class \"*\" style \"kindle_high_contrast\"\n"
    );
}

static void new_game(void)
{
    guint32 seed = (guint32)time(NULL);

    if (app.game_mode == APP_GAME_FREECELL) {
        solitaire_new_freecell(&app.game, seed);
        set_message("New FreeCell deal. Move cards to free cells, tableau, or foundations.");
    } else {
        int draw_count = app.game_mode == APP_GAME_KLONDIKE_DRAW3 ? 3 : 1;
        solitaire_new_klondike(&app.game, seed, draw_count);
        set_message(draw_count == 3 ?
                    "New Klondike draw-3 deal. Tap the deck to draw." :
                    "New Klondike draw-1 deal. Tap the deck to draw.");
    }
    app.has_selection = FALSE;
    update_ui();
}

static void new_cb(GtkWidget *widget, gpointer data)
{
    (void)widget;
    (void)data;
    new_game();
}

static void undo_cb(GtkWidget *widget, gpointer data)
{
    (void)widget;
    (void)data;

    if (solitaire_undo(&app.game)) {
        app.has_selection = FALSE;
        set_message("Undid the last move.");
    } else {
        set_message("Nothing to undo.");
    }
    update_ui();
}

static void draw_cb(GtkWidget *widget, gpointer data)
{
    SolMoveResult result;
    (void)widget;
    (void)data;

    result = solitaire_draw(&app.game);
    app.has_selection = FALSE;
    if (app_is_freecell())
        set_message("FreeCell has no stock. Use the free cells and tableau.");
    else if (result == SOL_MOVE_INVALID)
        set_message("Stock and waste are empty.");
    else if (app.game.stock.count == 0)
        set_message("Stock empty. Tap stock or Draw to recycle the waste.");
    else if (app.game.draw_count == 3)
        set_message("Drew up to 3 cards. Only the rightmost/top waste card can move.");
    else
        set_message("Drew one card.");
    update_ui();
}

static void save_cb(GtkWidget *widget, gpointer data)
{
    SaveFile save;
    FILE *f;
    (void)widget;
    (void)data;

    memset(&save, 0, sizeof(save));
    g_strlcpy(save.magic, SAVE_MAGIC, sizeof(save.magic));
    save.game_mode = app.game_mode;
    save.game = app.game;

    f = fopen(SAVE_PATH, "wb");
    if (f == NULL) {
        set_message("Could not open save file.");
    } else if (fwrite(&save, sizeof(save), 1, f) != 1) {
        set_message("Could not write save file.");
    } else {
        set_message("Game saved.");
    }
    if (f != NULL)
        fclose(f);
    update_ui();
}

static void load_cb(GtkWidget *widget, gpointer data)
{
    SaveFile save;
    FILE *f;
    (void)widget;
    (void)data;

    f = fopen(SAVE_PATH, "rb");
    if (f == NULL) {
        set_message("No saved game found.");
        update_ui();
        return;
    }
    if (fread(&save, sizeof(save), 1, f) != 1 || strcmp(save.magic, SAVE_MAGIC) != 0 ||
        save.game_mode < APP_GAME_KLONDIKE_DRAW1 || save.game_mode > APP_GAME_FREECELL) {
        set_message("Saved game is not compatible.");
        fclose(f);
        update_ui();
        return;
    }
    fclose(f);

    app.game_mode = save.game_mode;
    app.game = save.game;
    app.has_selection = FALSE;
    if (app.game_combo != NULL) {
        app.suppress_game_mode_cb = TRUE;
        gtk_combo_box_set_active(GTK_COMBO_BOX(app.game_combo), app.game_mode);
        app.suppress_game_mode_cb = FALSE;
    }
    set_message("Game loaded.");
    update_ui();
}

static void game_mode_cb(GtkComboBox *combo, gpointer data)
{
    (void)data;

    if (app.suppress_game_mode_cb)
        return;

    app.game_mode = (AppGameMode)gtk_combo_box_get_active(combo);
    if (app.game_mode < APP_GAME_KLONDIKE_DRAW3 || app.game_mode > APP_GAME_FREECELL)
        app.game_mode = APP_GAME_KLONDIKE_DRAW3;
    app.has_selection = FALSE;
    new_game();
}

static void quit_cb(GtkWidget *widget, gpointer data)
{
    (void)widget;
    (void)data;
    gtk_main_quit();
}

static gboolean key_press(GtkWidget *widget, GdkEventKey *event, gpointer data)
{
    (void)widget;
    (void)data;

    switch (event->keyval) {
    case GDK_n:
    case GDK_N:
        new_game();
        return TRUE;
    case GDK_u:
    case GDK_U:
        undo_cb(NULL, NULL);
        return TRUE;
    case GDK_d:
    case GDK_D:
    case GDK_space:
        draw_cb(NULL, NULL);
        return TRUE;
    case GDK_q:
    case GDK_Q:
    case GDK_Escape:
        gtk_main_quit();
        return TRUE;
    }

    return FALSE;
}

static gboolean point_in_rect(double px, double py, Rect r)
{
    return px >= r.x && px <= r.x + r.w && py >= r.y && py <= r.y + r.h;
}

static void compute_layout(GtkAllocation *a)
{
    double margin = 18.0;
    double gap = 10.0;
    double usable_w = a->width - margin * 2.0;
    int columns = solitaire_tableau_count(&app.game);
    double card_w = (usable_w - gap * (columns - 1)) / (double)columns;
    double card_h = card_w * 1.38;
    double top_y = margin;
    double label_h = 28.0;
    double tableau_y = top_y + label_h + card_h + 36.0;
    int i;

    if (card_w > (app_is_freecell() ? 112.0 : 122.0)) {
        card_w = app_is_freecell() ? 112.0 : 122.0;
        card_h = card_w * 1.38;
        margin = (a->width - (card_w * columns + gap * (columns - 1))) / 2.0;
    }

    app.card_w = card_w;
    app.card_h = card_h;
    app.tableau_gap = MAX(24.0, card_h * 0.28);

    app.stock_rect = (Rect){ margin, top_y + label_h, card_w, card_h };
    app.waste_rect = (Rect){ margin + card_w + gap, top_y + label_h, card_w, card_h };
    for (i = 0; i < 3; i++)
        app.waste_card_rects[i] = (Rect){ app.waste_rect.x + i * (card_w * 0.26), app.waste_rect.y, card_w, card_h };

    for (i = 0; i < SOL_FREECELL_COUNT; i++)
        app.freecell_rects[i] = (Rect){ margin + i * (card_w + gap), top_y + label_h, card_w, card_h };

    for (i = 0; i < SOL_FOUNDATION_COUNT; i++) {
        double x = app_is_freecell() ?
            margin + (SOL_FREECELL_COUNT + i) * (card_w + gap) :
            margin + (3 + i) * (card_w + gap);
        app.foundation_rects[i] = (Rect){ x, top_y + label_h, card_w, card_h };
    }

    for (i = 0; i < columns; i++) {
        double x = margin + i * (card_w + gap);
        app.tableau_rects[i] = (Rect){ x, tableau_y, card_w, card_h };
    }
}

static void rounded_rect(cairo_t *cr, Rect r, double radius)
{
    double x = r.x;
    double y = r.y;
    double w = r.w;
    double h = r.h;
    double a = radius;

    cairo_new_sub_path(cr);
    cairo_arc(cr, x + w - a, y + a, a, -G_PI / 2.0, 0);
    cairo_arc(cr, x + w - a, y + h - a, a, 0, G_PI / 2.0);
    cairo_arc(cr, x + a, y + h - a, a, G_PI / 2.0, G_PI);
    cairo_arc(cr, x + a, y + a, a, G_PI, 3.0 * G_PI / 2.0);
    cairo_close_path(cr);
}

static void draw_centered_text(cairo_t *cr, const char *text, Rect r, double size)
{
    cairo_text_extents_t ext;
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, size);
    cairo_text_extents(cr, text, &ext);
    cairo_move_to(cr, r.x + (r.w - ext.width) / 2.0 - ext.x_bearing, r.y + (r.h - ext.height) / 2.0 - ext.y_bearing);
    cairo_show_text(cr, text);
}

static void set_card_ink(cairo_t *cr, int suit)
{
    if (solitaire_is_red_suit(suit))
        cairo_set_source_rgb(cr, 0.36, 0.36, 0.34);
    else
        cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
}

static void draw_label_above(cairo_t *cr, const char *text, Rect r)
{
    Rect label = { r.x - 4.0, r.y - 29.0, r.w + 8.0, 24.0 };
    cairo_set_source_rgb(cr, 0.03, 0.03, 0.03);
    draw_centered_text(cr, text, label, 16.0);
}

static const char *suit_symbol(int suit)
{
    switch (suit) {
    case SOL_HEARTS:
        return "♥";
    case SOL_DIAMONDS:
        return "♦";
    case SOL_CLUBS:
        return "♣";
    default:
        return "♠";
    }
}

static void build_suit_path(cairo_t *cr, int suit, double cx, double cy, double size)
{
    double s = size;

    cairo_new_path(cr);
    switch (suit) {
    case SOL_HEARTS:
        cairo_move_to(cr, cx, cy + s * 0.42);
        cairo_curve_to(cr, cx - s * 0.75, cy - s * 0.02, cx - s * 0.42, cy - s * 0.58, cx, cy - s * 0.18);
        cairo_curve_to(cr, cx + s * 0.42, cy - s * 0.58, cx + s * 0.75, cy - s * 0.02, cx, cy + s * 0.42);
        cairo_close_path(cr);
        break;
    case SOL_DIAMONDS:
        cairo_move_to(cr, cx, cy - s * 0.62);
        cairo_line_to(cr, cx + s * 0.48, cy);
        cairo_line_to(cr, cx, cy + s * 0.62);
        cairo_line_to(cr, cx - s * 0.48, cy);
        cairo_close_path(cr);
        break;
    case SOL_CLUBS:
        cairo_new_sub_path(cr);
        cairo_arc(cr, cx, cy - s * 0.26, s * 0.26, 0, 2 * G_PI);
        cairo_new_sub_path(cr);
        cairo_arc(cr, cx - s * 0.28, cy + s * 0.05, s * 0.26, 0, 2 * G_PI);
        cairo_new_sub_path(cr);
        cairo_arc(cr, cx + s * 0.28, cy + s * 0.05, s * 0.26, 0, 2 * G_PI);
        cairo_new_sub_path(cr);
        cairo_rectangle(cr, cx - s * 0.08, cy + s * 0.12, s * 0.16, s * 0.44);
        break;
    default:
        cairo_move_to(cr, cx, cy - s * 0.58);
        cairo_curve_to(cr, cx - s * 0.62, cy - s * 0.12, cx - s * 0.42, cy + s * 0.34, cx, cy + s * 0.10);
        cairo_curve_to(cr, cx + s * 0.42, cy + s * 0.34, cx + s * 0.62, cy - s * 0.12, cx, cy - s * 0.58);
        cairo_close_path(cr);
        cairo_new_sub_path(cr);
        cairo_rectangle(cr, cx - s * 0.08, cy + s * 0.08, s * 0.16, s * 0.48);
        break;
    }
}

static void draw_suit_icon(cairo_t *cr, int suit, double cx, double cy, double size)
{
    double s = size;
    gboolean red = solitaire_is_red_suit(suit);

    cairo_save(cr);
    if (suit == SOL_CLUBS && !red) {
        cairo_set_source_rgb(cr, 0.02, 0.02, 0.02);
        cairo_new_path(cr);
        cairo_arc(cr, cx, cy - s * 0.27, s * 0.27, 0, 2 * G_PI);
        cairo_fill(cr);
        cairo_new_path(cr);
        cairo_arc(cr, cx - s * 0.28, cy + s * 0.05, s * 0.27, 0, 2 * G_PI);
        cairo_fill(cr);
        cairo_new_path(cr);
        cairo_arc(cr, cx + s * 0.28, cy + s * 0.05, s * 0.27, 0, 2 * G_PI);
        cairo_fill(cr);
        cairo_new_path(cr);
        cairo_move_to(cr, cx - s * 0.10, cy + s * 0.15);
        cairo_line_to(cr, cx + s * 0.10, cy + s * 0.15);
        cairo_line_to(cr, cx + s * 0.20, cy + s * 0.58);
        cairo_line_to(cr, cx - s * 0.20, cy + s * 0.58);
        cairo_close_path(cr);
        cairo_fill(cr);
        cairo_restore(cr);
        return;
    }

    build_suit_path(cr, suit, cx, cy, size);
    cairo_set_source_rgb(cr, red ? 0.96 : 0.02, red ? 0.96 : 0.02, red ? 0.93 : 0.02);
    cairo_fill_preserve(cr);

    if (red) {
        double x;
        cairo_save(cr);
        cairo_clip_preserve(cr);
        cairo_new_path(cr);
        cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
        cairo_set_line_width(cr, MAX(1.2, size * 0.055));
        for (x = cx - s; x < cx + s; x += MAX(4.0, size * 0.18)) {
            cairo_move_to(cr, x, cy + s);
            cairo_line_to(cr, x + s, cy - s);
        }
        cairo_stroke(cr);
        cairo_restore(cr);
    }

    if (red) {
        build_suit_path(cr, suit, cx, cy, size);
        cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
        cairo_set_line_width(cr, MAX(1.6, size * 0.08));
        cairo_stroke(cr);
    } else {
        cairo_new_path(cr);
    }
    cairo_restore(cr);
}

static void draw_empty_slot(cairo_t *cr, Rect r, const char *label)
{
    rounded_rect(cr, r, 8.0);
    cairo_set_source_rgb(cr, 0.90, 0.90, 0.86);
    cairo_fill_preserve(cr);
    cairo_set_source_rgb(cr, 0.12, 0.12, 0.12);
    cairo_set_line_width(cr, 2.4);
    cairo_stroke(cr);
    cairo_set_source_rgb(cr, 0.18, 0.18, 0.18);
    draw_centered_text(cr, label, r, 20.0);
}

static void draw_foundation_slot(cairo_t *cr, Rect r, int suit)
{
    char label[16];

    snprintf(label, sizeof(label), "ACE %s", solitaire_suit_label(suit));
    draw_empty_slot(cr, r, label);
    draw_suit_icon(cr, suit, r.x + r.w * 0.50, r.y + r.h * 0.66, MIN(r.w, r.h) * 0.22);
}

static void draw_card_back(cairo_t *cr, Rect r)
{
    rounded_rect(cr, r, 8.0);
    cairo_set_source_rgb(cr, 0.12, 0.12, 0.12);
    cairo_fill_preserve(cr);
    cairo_set_source_rgb(cr, 0.02, 0.02, 0.02);
    cairo_set_line_width(cr, 2.6);
    cairo_stroke(cr);

    cairo_set_source_rgb(cr, 0.92, 0.92, 0.86);
    cairo_set_line_width(cr, 3.0);
    cairo_rectangle(cr, r.x + 9, r.y + 9, r.w - 18, r.h - 18);
    cairo_stroke(cr);
    cairo_rectangle(cr, r.x + 18, r.y + 18, r.w - 36, r.h - 36);
    cairo_stroke(cr);

    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
    draw_centered_text(cr, "DECK", r, 22.0);
}

static void draw_card_corner(cairo_t *cr, Rect box, SolCard card, double text_size)
{
    char label[16];

    snprintf(label, sizeof(label), "%s%s", solitaire_rank_label(card.rank), suit_symbol(card.suit));

    rounded_rect(cr, box, 4.0);
    cairo_set_source_rgb(cr, 0.92, 0.92, 0.88);
    cairo_fill_preserve(cr);
    cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
    cairo_set_line_width(cr, 1.4);
    cairo_stroke(cr);

    set_card_ink(cr, card.suit);
    draw_centered_text(cr, label, box, text_size);
}

static void draw_card_pip(cairo_t *cr, SolCard card, Rect area, double nx, double ny, gboolean inverted)
{
    double size = MIN(area.w, area.h) * 0.145;
    double cx = area.x + area.w * nx;
    double cy = area.y + area.h * ny;

    cairo_save(cr);
    if (inverted) {
        cairo_translate(cr, cx, cy);
        cairo_rotate(cr, G_PI);
        draw_suit_icon(cr, card.suit, 0, 0, size);
    } else {
        draw_suit_icon(cr, card.suit, cx, cy, size);
    }
    cairo_restore(cr);
}

static void draw_number_pips(cairo_t *cr, Rect area, SolCard card)
{
    static const double pip_y[5] = { 0.16, 0.31, 0.50, 0.69, 0.84 };
    int rank = card.rank;

    if (rank == 1) {
        draw_card_pip(cr, card, area, 0.50, 0.50, FALSE);
        return;
    }

    if (rank == 2 || rank == 3) {
        draw_card_pip(cr, card, area, 0.50, 0.20, FALSE);
        if (rank == 3)
            draw_card_pip(cr, card, area, 0.50, 0.50, FALSE);
        draw_card_pip(cr, card, area, 0.50, 0.80, TRUE);
        return;
    }

    if (rank >= 4) {
        draw_card_pip(cr, card, area, 0.32, pip_y[0], FALSE);
        draw_card_pip(cr, card, area, 0.68, pip_y[0], FALSE);
        draw_card_pip(cr, card, area, 0.32, pip_y[4], TRUE);
        draw_card_pip(cr, card, area, 0.68, pip_y[4], TRUE);
    }
    if (rank >= 6) {
        draw_card_pip(cr, card, area, 0.32, pip_y[2], FALSE);
        draw_card_pip(cr, card, area, 0.68, pip_y[2], TRUE);
    }
    if (rank == 5 || rank == 7 || rank == 9) {
        draw_card_pip(cr, card, area, 0.50, pip_y[2], FALSE);
    }
    if (rank == 8 || rank == 10) {
        draw_card_pip(cr, card, area, 0.32, pip_y[1], FALSE);
        draw_card_pip(cr, card, area, 0.68, pip_y[1], FALSE);
        draw_card_pip(cr, card, area, 0.32, pip_y[3], TRUE);
        draw_card_pip(cr, card, area, 0.68, pip_y[3], TRUE);
    }
    if (rank == 9 || rank == 10) {
        draw_card_pip(cr, card, area, 0.50, rank == 9 ? pip_y[1] : 0.24, FALSE);
        draw_card_pip(cr, card, area, 0.50, rank == 9 ? pip_y[3] : 0.76, TRUE);
    }
}

static void draw_face_card_art(cairo_t *cr, Rect area, SolCard card)
{
    const char *face = card.rank == 11 ? "J" : (card.rank == 12 ? "Q" : "K");
    double cx = area.x + area.w * 0.50;
    double y = area.y + area.h * 0.18;
    double w = area.w * 0.52;
    double h = area.h * 0.60;

    cairo_save(cr);
    set_card_ink(cr, card.suit);

    if (card.rank == 13 || card.rank == 12) {
        cairo_new_path(cr);
        cairo_move_to(cr, cx - w * 0.42, y + h * 0.22);
        cairo_line_to(cr, cx - w * 0.25, y);
        cairo_line_to(cr, cx, y + h * 0.18);
        cairo_line_to(cr, cx + w * 0.25, y);
        cairo_line_to(cr, cx + w * 0.42, y + h * 0.22);
        cairo_line_to(cr, cx + w * 0.36, y + h * 0.35);
        cairo_line_to(cr, cx - w * 0.36, y + h * 0.35);
        cairo_close_path(cr);
        cairo_set_line_width(cr, 2.0);
        cairo_stroke(cr);
    }

    cairo_new_path(cr);
    cairo_arc(cr, cx, y + h * 0.45, w * 0.25, 0, 2 * G_PI);
    cairo_set_line_width(cr, 2.2);
    cairo_stroke(cr);

    cairo_new_path(cr);
    cairo_move_to(cr, cx - w * 0.42, y + h * 0.95);
    cairo_line_to(cr, cx - w * 0.24, y + h * 0.60);
    cairo_line_to(cr, cx + w * 0.24, y + h * 0.60);
    cairo_line_to(cr, cx + w * 0.42, y + h * 0.95);
    cairo_close_path(cr);
    cairo_set_line_width(cr, 2.4);
    cairo_stroke(cr);

    draw_centered_text(cr, face, (Rect){ area.x, area.y + area.h * 0.31, area.w, area.h * 0.28 }, area.h * 0.28);
    draw_suit_icon(cr, card.suit, cx, area.y + area.h * 0.78, MIN(area.w, area.h) * 0.16);
    cairo_restore(cr);
}

static void draw_card_face(cairo_t *cr, Rect r, SolCard card)
{
    Rect top_box = { r.x + 5.0, r.y + 5.0, r.w * 0.44, r.h * 0.25 };
    Rect pip_area = { r.x + r.w * 0.16, r.y + r.h * 0.28, r.w * 0.68, r.h * 0.50 };

    rounded_rect(cr, r, 8.0);
    cairo_set_source_rgb(cr, 1.0, 1.0, 0.97);
    cairo_fill_preserve(cr);
    cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
    cairo_set_line_width(cr, 2.8);
    cairo_stroke(cr);

    draw_card_corner(cr, top_box, card, 21.0);
    if (card.rank >= 11)
        draw_face_card_art(cr, pip_area, card);
    else
        draw_number_pips(cr, pip_area, card);

    cairo_save(cr);
    cairo_translate(cr, r.x + r.w - 5.0, r.y + r.h - 5.0);
    cairo_rotate(cr, G_PI);
    draw_card_corner(cr, (Rect){ 0, 0, top_box.w, top_box.h }, card, 18.0);
    cairo_restore(cr);
}

static void draw_card(cairo_t *cr, Rect r, SolCard card)
{
    if (card.face_up)
        draw_card_face(cr, r, card);
    else
        draw_card_back(cr, r);
}

static gboolean same_location(SolLocation a, SolLocation b)
{
    return a.type == b.type && a.index == b.index && a.card_index == b.card_index;
}

static void draw_selection(cairo_t *cr, Rect r)
{
    rounded_rect(cr, r, 9.0);
    cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
    cairo_set_line_width(cr, 5.0);
    cairo_stroke(cr);
    rounded_rect(cr, (Rect){ r.x + 5, r.y + 5, r.w - 10, r.h - 10 }, 6.0);
    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
    cairo_set_line_width(cr, 2.0);
    cairo_stroke(cr);
}

static gboolean board_expose(GtkWidget *widget, GdkEventExpose *event, gpointer data)
{
    cairo_t *cr = gdk_cairo_create(widget->window);
    GtkAllocation a;
    int i;

    (void)event;
    (void)data;

    gtk_widget_get_allocation(widget, &a);
    compute_layout(&a);

    cairo_set_source_rgb(cr, 0.76, 0.75, 0.68);
    cairo_paint(cr);

    if (app_is_freecell()) {
        for (i = 0; i < SOL_FREECELL_COUNT; i++)
            draw_label_above(cr, "FREE", app.freecell_rects[i]);
    } else {
        draw_label_above(cr, "STOCK", app.stock_rect);
        draw_label_above(cr, app.game.draw_count == 3 ? "WASTE TOP 3" : "WASTE", app.waste_rect);
    }
    for (i = 0; i < SOL_FOUNDATION_COUNT; i++)
        draw_label_above(cr, "FOUNDATION", app.foundation_rects[i]);

    if (app_is_freecell()) {
        for (i = 0; i < SOL_FREECELL_COUNT; i++) {
            draw_empty_slot(cr, app.freecell_rects[i], "FREE");
            if (app.game.freecells[i].count > 0)
                draw_card(cr, app.freecell_rects[i], app.game.freecells[i].cards[app.game.freecells[i].count - 1]);
        }
    } else {
        draw_empty_slot(cr, app.stock_rect, app.game.stock.count > 0 ? "DRAW" : "RECYCLE");
        if (app.game.stock.count > 0)
            draw_card_back(cr, app.stock_rect);

        draw_empty_slot(cr, app.waste_rect, "TOP ONLY");
        if (app.game.waste.count > 0) {
            int visible = app.game.draw_count == 3 ? MIN(3, app.game.waste.count) : 1;
            int first = app.game.waste.count - visible;
            for (i = 0; i < visible; i++) {
                Rect r = app.game.draw_count == 3 ? app.waste_card_rects[i] : app.waste_rect;
                draw_card(cr, r, app.game.waste.cards[first + i]);
            }
        }
    }

    for (i = 0; i < SOL_FOUNDATION_COUNT; i++) {
        draw_foundation_slot(cr, app.foundation_rects[i], i);
        if (app.game.foundations[i].count > 0)
            draw_card(cr, app.foundation_rects[i], app.game.foundations[i].cards[app.game.foundations[i].count - 1]);
    }

    for (i = 0; i < solitaire_tableau_count(&app.game); i++) {
        SolPile *pile = &app.game.tableau[i];
        int j;
        if (pile->count == 0)
            draw_empty_slot(cr, app.tableau_rects[i], app_is_freecell() ? "ANY" : "K");
        for (j = 0; j < pile->count; j++) {
            Rect r = app.tableau_rects[i];
            r.y += app.tableau_gap * j;
            draw_card(cr, r, pile->cards[j]);
            if (app.has_selection && app.selected.type == SOL_LOC_TABLEAU &&
                app.selected.index == i && app.selected.card_index == j)
                draw_selection(cr, r);
        }
    }

    if (app.has_selection && app.selected.type == SOL_LOC_WASTE) {
        int visible = app.game.draw_count == 3 ? MIN(3, app.game.waste.count) : 1;
        Rect r = visible > 0 && app.game.draw_count == 3 ? app.waste_card_rects[visible - 1] : app.waste_rect;
        draw_selection(cr, r);
    }
    if (app.has_selection && app.selected.type == SOL_LOC_FOUNDATION)
        draw_selection(cr, app.foundation_rects[app.selected.index]);
    if (app.has_selection && app.selected.type == SOL_LOC_FREECELL)
        draw_selection(cr, app.freecell_rects[app.selected.index]);

    cairo_destroy(cr);
    return FALSE;
}

static gboolean locate_click(double x, double y, SolLocation *loc)
{
    int i;

    if (app_is_freecell()) {
        for (i = 0; i < SOL_FREECELL_COUNT; i++) {
            if (point_in_rect(x, y, app.freecell_rects[i])) {
                *loc = (SolLocation){ SOL_LOC_FREECELL, i, app.game.freecells[i].count - 1 };
                return TRUE;
            }
        }
    } else {
        if (point_in_rect(x, y, app.stock_rect)) {
            *loc = (SolLocation){ SOL_LOC_STOCK, 0, -1 };
            return TRUE;
        }

        if (app.game.waste.count > 0) {
            int visible = app.game.draw_count == 3 ? MIN(3, app.game.waste.count) : 1;
            Rect waste_hit = app.waste_rect;
            if (app.game.draw_count == 3 && visible > 0) {
                waste_hit.x = app.waste_card_rects[0].x;
                waste_hit.w = app.waste_card_rects[visible - 1].x + app.card_w - waste_hit.x;
            }
            if (point_in_rect(x, y, waste_hit)) {
                *loc = (SolLocation){ SOL_LOC_WASTE, 0, app.game.waste.count - 1 };
                return TRUE;
            }
        } else if (point_in_rect(x, y, app.waste_rect)) {
            *loc = (SolLocation){ SOL_LOC_WASTE, 0, app.game.waste.count - 1 };
            return FALSE;
        }
    }

    for (i = 0; i < SOL_FOUNDATION_COUNT; i++) {
        if (point_in_rect(x, y, app.foundation_rects[i])) {
            *loc = (SolLocation){ SOL_LOC_FOUNDATION, i, app.game.foundations[i].count - 1 };
            return TRUE;
        }
    }

    for (i = 0; i < solitaire_tableau_count(&app.game); i++) {
        SolPile *pile = &app.game.tableau[i];
        Rect base = app.tableau_rects[i];
        int card = pile->count - 1;
        int j;

        base.h += app.tableau_gap * MAX(0, pile->count - 1);
        if (!point_in_rect(x, y, base))
            continue;

        for (j = 0; j < pile->count; j++) {
            double top = app.tableau_rects[i].y + app.tableau_gap * j;
            double bottom = top + ((j == pile->count - 1) ? app.card_h : app.tableau_gap);
            if (y >= top && y <= bottom) {
                card = j;
                break;
            }
        }
        *loc = (SolLocation){ SOL_LOC_TABLEAU, i, card };
        return TRUE;
    }

    return FALSE;
}

static gboolean auto_foundation_for(SolLocation from)
{
    int i;

    for (i = 0; i < SOL_FOUNDATION_COUNT; i++) {
        SolLocation to = { SOL_LOC_FOUNDATION, i, -1 };
        if (solitaire_can_move(&app.game, from, to)) {
            solitaire_move(&app.game, from, to);
            set_message(solitaire_is_won(&app.game) ? "You won!" : "Moved to foundation.");
            return TRUE;
        }
    }

    return FALSE;
}

static void handle_location(SolLocation loc)
{
    if (loc.type == SOL_LOC_STOCK) {
        draw_cb(NULL, NULL);
        return;
    }

    if (app.has_selection) {
        SolMoveResult result;
        if (same_location(app.selected, loc)) {
            if (auto_foundation_for(loc)) {
                app.has_selection = FALSE;
                update_ui();
                return;
            }
            app.has_selection = FALSE;
            set_message("Selection cleared.");
            update_ui();
            return;
        }

        result = solitaire_move(&app.game, app.selected, loc);
        app.has_selection = FALSE;
        if (result == SOL_MOVE_INVALID) {
            if (loc.type == SOL_LOC_FOUNDATION || loc.type == SOL_LOC_TABLEAU || loc.type == SOL_LOC_FREECELL) {
                set_message(app_is_freecell() ? "That move is not legal in FreeCell." : "That move is not legal in Klondike.");
            } else {
                app.selected = loc;
                app.has_selection = TRUE;
                set_message("Selected a new card.");
            }
        } else if (result == SOL_MOVE_WIN) {
            set_message("You won!");
        } else {
            set_message("Move made.");
        }
        update_ui();
        return;
    }

    if (loc.type == SOL_LOC_FOUNDATION && app.game.foundations[loc.index].count == 0) {
        set_message("Select an ace, then tap an empty foundation.");
        update_ui();
        return;
    }

    if (loc.type == SOL_LOC_TABLEAU && app.game.tableau[loc.index].count == 0) {
        set_message(app_is_freecell() ?
                    "Select any movable card, then tap the empty tableau slot." :
                    "Select a king, then tap the empty tableau slot.");
        update_ui();
        return;
    }

    if (loc.type == SOL_LOC_FREECELL && app.game.freecells[loc.index].count == 0) {
        set_message("Select a card, then tap an empty free cell.");
        update_ui();
        return;
    }

    app.selected = loc;
    app.has_selection = TRUE;
    set_message(app_is_freecell() ?
                "Card selected. Tap a tableau column, free cell, or foundation." :
                "Card selected. Tap a tableau column or foundation.");
    update_ui();
}

static gboolean board_button(GtkWidget *widget, GdkEventButton *event, gpointer data)
{
    GtkAllocation a;
    SolLocation loc;

    (void)widget;
    (void)data;

    gtk_widget_get_allocation(app.board, &a);
    compute_layout(&a);

    if (event->button != 1)
        return FALSE;

    if (locate_click(event->x, event->y, &loc))
        handle_location(loc);
    else {
        app.has_selection = FALSE;
        set_message("Tap stock, waste, a card, or a foundation.");
        update_ui();
    }

    return TRUE;
}

static void update_ui(void)
{
    char text[256];

    if (app_is_freecell()) {
        int free_count = 0;
        int i;
        for (i = 0; i < SOL_FREECELL_COUNT; i++) {
            if (app.game.freecells[i].count > 0)
                free_count++;
        }
        snprintf(text, sizeof(text), "%.120s  FreeCell  Moves %d  Home %d/52  Free %d/%d",
                 app.message,
                 app.game.moves,
                 solitaire_foundation_count(&app.game),
                 free_count,
                 SOL_FREECELL_COUNT);
    } else {
        snprintf(text, sizeof(text), "%.120s  Draw %d  Moves %d  Home %d/52  Hidden %d  Stock %d  Waste %d",
                 app.message,
                 app.game.draw_count,
                 app.game.moves,
                 solitaire_foundation_count(&app.game),
                 solitaire_face_down_count(&app.game),
                 app.game.stock.count,
                 app.game.waste.count);
    }
    gtk_label_set_text(GTK_LABEL(app.status), text);
    gtk_widget_queue_draw(app.board);
}

static gboolean combo_button_press_cb(GtkWidget *widget, GdkEventButton *event, gpointer data)
{
    (void)data;
    if (event->type != GDK_BUTTON_PRESS || event->button != 1)
        return FALSE;
    gtk_widget_grab_focus(widget);
    gtk_combo_box_popup(GTK_COMBO_BOX(widget));
    return TRUE;
}

static GtkWidget *make_game_combo(void)
{
    GtkWidget *combo = gtk_combo_box_new_text();
    gtk_combo_box_append_text(GTK_COMBO_BOX(combo), "Klondike (Draw 1)");
    gtk_combo_box_append_text(GTK_COMBO_BOX(combo), "Klondike (Draw 3)");
    gtk_combo_box_append_text(GTK_COMBO_BOX(combo), "FreeCell");
    gtk_combo_box_set_active(GTK_COMBO_BOX(combo), app.game_mode);
    gtk_widget_set_size_request(combo, 270, -1);
    gtk_widget_add_events(combo, GDK_BUTTON_PRESS_MASK);
    g_signal_connect(combo, "button-press-event", G_CALLBACK(combo_button_press_cb), NULL);
    g_signal_connect(combo, "changed", G_CALLBACK(game_mode_cb), NULL);
    return combo;
}

int main(int argc, char **argv)
{
    GtkWidget *vbox;
    GtkWidget *toolbar;
    GtkWidget *button;
    GtkWidget *title;
    GtkWidget *settings;
    GtkWidget *mode_label;

    gtk_init(&argc, &argv);
    app_install_kindle_style();
    app.game_mode = APP_GAME_KLONDIKE_DRAW1;

    app.window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(app.window), kindle_window_title());
    gtk_window_set_default_size(GTK_WINDOW(app.window), KINDLE_APP_WIDTH, KINDLE_APP_HEIGHT);
    gtk_window_set_resizable(GTK_WINDOW(app.window), TRUE);
    g_signal_connect(app.window, "destroy", G_CALLBACK(gtk_main_quit), NULL);
    g_signal_connect(app.window, "key-press-event", G_CALLBACK(key_press), NULL);

    vbox = gtk_vbox_new(FALSE, 6);
    gtk_container_add(GTK_CONTAINER(app.window), vbox);

    title = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(title), "<b>Kindle Aisleriot</b>");
    gtk_misc_set_alignment(GTK_MISC(title), 0.5, 0.5);
    gtk_box_pack_start(GTK_BOX(vbox), title, FALSE, FALSE, 0);

    app.status = gtk_label_new("");
    gtk_misc_set_alignment(GTK_MISC(app.status), 0.5, 0.5);
    gtk_label_set_ellipsize(GTK_LABEL(app.status), PANGO_ELLIPSIZE_END);
    gtk_box_pack_start(GTK_BOX(vbox), app.status, FALSE, FALSE, 0);

    toolbar = gtk_hbox_new(FALSE, 6);
    gtk_box_pack_start(GTK_BOX(vbox), toolbar, FALSE, FALSE, 0);

    button = gtk_button_new_with_label("New");
    g_signal_connect(button, "clicked", G_CALLBACK(new_cb), NULL);
    gtk_box_pack_start(GTK_BOX(toolbar), button, TRUE, TRUE, 0);

    button = gtk_button_new_with_label("Undo");
    g_signal_connect(button, "clicked", G_CALLBACK(undo_cb), NULL);
    gtk_box_pack_start(GTK_BOX(toolbar), button, TRUE, TRUE, 0);

    button = gtk_button_new_with_label("Save");
    g_signal_connect(button, "clicked", G_CALLBACK(save_cb), NULL);
    gtk_box_pack_start(GTK_BOX(toolbar), button, TRUE, TRUE, 0);

    button = gtk_button_new_with_label("Load");
    g_signal_connect(button, "clicked", G_CALLBACK(load_cb), NULL);
    gtk_box_pack_start(GTK_BOX(toolbar), button, TRUE, TRUE, 0);

    button = gtk_button_new_with_label("Quit");
    g_signal_connect(button, "clicked", G_CALLBACK(quit_cb), NULL);
    gtk_box_pack_start(GTK_BOX(toolbar), button, TRUE, TRUE, 0);

    settings = gtk_hbox_new(FALSE, 8);
    gtk_box_pack_start(GTK_BOX(vbox), settings, FALSE, FALSE, 0);
    mode_label = gtk_label_new("Game");
    gtk_box_pack_start(GTK_BOX(settings), mode_label, FALSE, FALSE, 0);
    app.game_combo = make_game_combo();
    gtk_box_pack_start(GTK_BOX(settings), app.game_combo, FALSE, FALSE, 0);

    app.board = gtk_drawing_area_new();
    gtk_widget_set_size_request(app.board, 700, 900);
    gtk_widget_add_events(app.board, GDK_BUTTON_PRESS_MASK);
    g_signal_connect(app.board, "expose-event", G_CALLBACK(board_expose), NULL);
    g_signal_connect(app.board, "button-press-event", G_CALLBACK(board_button), NULL);
    gtk_box_pack_start(GTK_BOX(vbox), app.board, TRUE, TRUE, 0);

    app_log("starting Kindle Aisleriot");
    new_game();

    gtk_widget_show_all(app.window);
    gtk_main();
    return 0;
}
