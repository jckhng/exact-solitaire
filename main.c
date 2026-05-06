/*
 * Kindle Aisleriot
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Unofficial Kindle-focused solitaire adaptation inspired by GNOME Aisleriot.
 */

#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include <cairo.h>
#include <librsvg/rsvg.h>
#include <librsvg/rsvg-cairo.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "solitaire_engine.h"

#define APP_TITLE "Kindle Aisleriot"
#define KINDLE_WINDOW_TITLE "L:A_N:application_ID:kindleaisleriot_PC:N_O:URL"
#define KINDLE_WINDOW_TITLE_TOPBAR "L:A_N:application_PC:T_ID:kindleaisleriot_O:URL"
#define LOG_PATH "/mnt/us/kindle-aisleriot.log"
#define SAVE_PATH "/mnt/us/extensions/kindle-aisleriot/kindle-aisleriot.save"
#define LEGACY_SAVE_PATH "/mnt/us/documents/kindle-aisleriot.save"
#define SAVE_MAGIC "KAISLERIOT1"
#define SVG_CARDS_PATH "/mnt/us/extensions/kindle-aisleriot/assets/svg-cards-2.0.svg"
#define SVG_CARDS_DEV_PATH "assets/svg-cards-2.0.svg"
#define BONDED_CARDS_PATH "/mnt/us/extensions/kindle-aisleriot/assets/bonded.svg"
#define BONDED_CARDS_DEV_PATH "assets/bonded.svg"
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

typedef enum {
    CARD_THEME_SIMPLIFIED = 0,
    CARD_THEME_MODERN,
    CARD_THEME_ORIGINAL
} CardTheme;

typedef struct {
    GtkWidget *window;
    GtkWidget *board;
    GtkWidget *status;
    GtkWidget *game_combo;
    GtkWidget *theme_combo;
    SolitaireGame game;
    AppGameMode game_mode;
    CardTheme card_theme;
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
    RsvgHandle *modern_card_svg;
    RsvgHandle *bonded_card_svg;
} AppState;

typedef struct {
    char magic[16];
    AppGameMode game_mode;
    SolitaireGame game;
} SaveFile;

static AppState app;

/* --- Per-card raster cache ---
 *
 * Rendering SVG sub-elements on every paint is far too slow for Kindle's ARM
 * CPU.  For each card we render the sub-element once into a full-document-sized
 * scratch surface (no clip, so <use> cross-references resolve correctly and the
 * content appears at its true SVG position regardless of viewBox offsets).  We
 * then pixel-scan the scratch to find the exact rendered bounds — this sidesteps
 * all get_position_sub / get_dimensions_sub coordinate-space ambiguity that
 * causes face cards to appear distorted or transparent.  The resulting per-card
 * surface is cached; subsequent paints are simple image blits.
 */
static void        app_log(const char *message);
static const char *card_suit_id(int suit);

#define CARD_CACHE_BACK  52
#define CARD_CACHE_TOTAL 53

static cairo_surface_t *s_card_surf[CARD_CACHE_TOTAL];
static double           s_surf_card_w = 0.0;
static double           s_surf_card_h = 0.0;

static int card_surf_key(int suit, int rank)
{
    return suit * 13 + (rank - 1);
}

static void card_surf_clear(void)
{
    int i;
    for (i = 0; i < CARD_CACHE_TOTAL; i++) {
        if (s_card_surf[i]) {
            cairo_surface_destroy(s_card_surf[i]);
            s_card_surf[i] = NULL;
        }
    }
    s_surf_card_w = s_surf_card_h = 0.0;
}

static void svg_atlas_clear(void)
{
    card_surf_clear();
}

/* Render the sub-element into a scratch surface, locate its actual pixel bounds
 * via scan, then scale-extract into a target_w × target_h surface.
 *
 * Rendering into a full-document-sized scratch (with no custom clip) means all
 * <use> cross-references resolve and the viewBox transform is applied normally.
 * The pixel scan then finds the card's true position in the scratch regardless
 * of what get_position_sub / get_dimensions_sub report. */
static RsvgHandle *active_card_svg(void)
{
    if (app.card_theme == CARD_THEME_ORIGINAL)
        return app.bonded_card_svg;
    if (app.card_theme == CARD_THEME_MODERN)
        return app.modern_card_svg;
    return NULL;
}

static cairo_surface_t *make_card_surf(RsvgHandle *handle, const char *id, double target_w, double target_h)
{
    char                   fragment[64];
    RsvgDimensionData      full_dims;
    cairo_surface_t       *scratch, *result;
    cairo_t               *tcr;
    const unsigned char   *data;
    int                    stride, sw, sh;
    int                    x, y, x_min, y_min, x_max, y_max;
    double                 render_scale, sx, sy;

    if (!handle) return NULL;
    snprintf(fragment, sizeof(fragment), "#%s", id);
    if (!rsvg_handle_has_sub(handle, fragment)) return NULL;

    rsvg_handle_get_dimensions(handle, &full_dims);
    if (full_dims.width <= 0 || full_dims.height <= 0) return NULL;

    /* Render scratch at ~800 px tall for good quality after downscale to target. */
    render_scale = 800.0 / (double)full_dims.height;
    if (render_scale > 1.0) render_scale = 1.0;
    sw = (int)(full_dims.width  * render_scale + 0.5);
    sh = (int)(full_dims.height * render_scale + 0.5);

    scratch = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, sw, sh);
    if (cairo_surface_status(scratch) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(scratch); return NULL;
    }

    tcr = cairo_create(scratch);
    cairo_scale(tcr, render_scale, render_scale);
    rsvg_handle_render_cairo_sub(handle, tcr, fragment);
    cairo_destroy(tcr);

    /* Scan for the bounding box of rendered pixels. */
    cairo_surface_flush(scratch);
    data   = cairo_image_surface_get_data(scratch);
    stride = cairo_image_surface_get_stride(scratch);

    x_min = sw; y_min = sh; x_max = -1; y_max = -1;
    for (y = 0; y < sh; y++) {
        const unsigned char *row = data + (size_t)y * stride;
        for (x = 0; x < sw; x++) {
            if (row[x * 4 + 3] > 8) {  /* alpha byte (little-endian ARGB32) */
                if (x < x_min) x_min = x;
                if (x > x_max) x_max = x;
                if (y < y_min) y_min = y;
                if (y > y_max) y_max = y;
            }
        }
    }

    if (x_max < x_min || y_max < y_min) {
        cairo_surface_destroy(scratch); return NULL;
    }

    result = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
                                        (int)(target_w + 0.5), (int)(target_h + 0.5));
    if (cairo_surface_status(result) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(scratch); cairo_surface_destroy(result); return NULL;
    }

    sx = target_w  / (double)(x_max - x_min + 1);
    sy = target_h  / (double)(y_max - y_min + 1);

    tcr = cairo_create(result);
    cairo_scale(tcr, sx, sy);
    cairo_set_source_surface(tcr, scratch, -(double)x_min, -(double)y_min);
    cairo_paint(tcr);
    cairo_destroy(tcr);

    cairo_surface_destroy(scratch);
    return result;
}

/* Return a cached card surface, rendering and scanning on first use. */
static cairo_surface_t *get_card_surf(RsvgHandle *handle, const char *id, int key, double card_w, double card_h)
{
    if (s_surf_card_w != card_w || s_surf_card_h != card_h)
        card_surf_clear();

    if (key < 0 || key >= CARD_CACHE_TOTAL) return NULL;

    if (!s_card_surf[key]) {
        s_card_surf[key] = make_card_surf(handle, id, card_w, card_h);
        s_surf_card_w = card_w;
        s_surf_card_h = card_h;
    }
    return s_card_surf[key];
}

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

static RsvgHandle *load_svg_handle(const char *primary_path, const char *dev_path, const char *name)
{
    GError *error = NULL;
    RsvgHandle *handle;

    handle = rsvg_handle_new_from_file(primary_path, &error);
    if (handle != NULL) {
        app_log(name);
        return handle;
    }
    if (error != NULL) {
        g_error_free(error);
        error = NULL;
    }

    handle = rsvg_handle_new_from_file(dev_path, &error);
    if (handle != NULL) {
        app_log(name);
        return handle;
    }
    if (error != NULL) {
        app_log(error->message);
        g_error_free(error);
    }
    return NULL;
}

static void load_card_svgs(void)
{
    app.modern_card_svg = load_svg_handle(SVG_CARDS_PATH, SVG_CARDS_DEV_PATH, "loaded SVG Cards 2.0 deck");
    app.bonded_card_svg = load_svg_handle(BONDED_CARDS_PATH, BONDED_CARDS_DEV_PATH, "loaded bonded card deck");
    if (app.modern_card_svg == NULL && app.bonded_card_svg == NULL)
        app_log("SVG card decks unavailable; using Cairo fallback cards");
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

static void restart_cb(GtkWidget *widget, gpointer data)
{
    (void)widget;
    (void)data;

    solitaire_restart(&app.game);
    app.has_selection = FALSE;
    set_message("Restarted this deal.");
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
    if (f == NULL)
        f = fopen(LEGACY_SAVE_PATH, "rb");
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

static void theme_cb(GtkComboBox *combo, gpointer data)
{
    (void)data;

    app.card_theme = (CardTheme)gtk_combo_box_get_active(combo);
    if (app.card_theme < CARD_THEME_SIMPLIFIED || app.card_theme > CARD_THEME_ORIGINAL)
        app.card_theme = CARD_THEME_SIMPLIFIED;
    svg_atlas_clear();

    switch (app.card_theme) {
    case CARD_THEME_MODERN:
        set_message("Card theme: Modern SVG.");
        break;
    case CARD_THEME_ORIGINAL:
        set_message("Card theme: Original bonded.");
        break;
    case CARD_THEME_SIMPLIFIED:
    default:
        set_message("Card theme: Simplified large labels.");
        break;
    }
    update_ui();
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
    char             ace_id[16];
    cairo_surface_t *ace_surf;
    RsvgHandle      *handle = active_card_svg();

    /* Draw the empty slot background and border. */
    rounded_rect(cr, r, 8.0);
    cairo_set_source_rgb(cr, 0.90, 0.90, 0.86);
    cairo_fill_preserve(cr);
    cairo_set_source_rgb(cr, 0.12, 0.12, 0.12);
    cairo_set_line_width(cr, 2.4);
    cairo_stroke(cr);

    /* Overlay a faint ace card as a visual hint. */
    if (app.card_theme == CARD_THEME_ORIGINAL)
        snprintf(ace_id, sizeof(ace_id), "1_%s", card_suit_id(suit));
    else
        snprintf(ace_id, sizeof(ace_id), "%s_1", card_suit_id(suit));
    ace_surf = app.card_theme == CARD_THEME_SIMPLIFIED ? NULL : get_card_surf(handle, ace_id, card_surf_key(suit, 1), r.w, r.h);
    if (ace_surf) {
        cairo_save(cr);
        rounded_rect(cr, r, 8.0);
        cairo_clip(cr);
        cairo_set_source_surface(cr, ace_surf, r.x, r.y);
        cairo_paint_with_alpha(cr, 0.35);
        cairo_restore(cr);
    } else {
        char label[16];
        snprintf(label, sizeof(label), "ACE %s", solitaire_suit_label(suit));
        cairo_set_source_rgb(cr, 0.18, 0.18, 0.18);
        draw_centered_text(cr, label, r, 20.0);
        draw_suit_icon(cr, suit, r.x + r.w * 0.50, r.y + r.h * 0.66, MIN(r.w, r.h) * 0.22);
    }
}

static gboolean render_svg_card_sub(RsvgHandle *handle, cairo_t *cr, Rect r, const char *id, int cache_key);

static void draw_card_back(cairo_t *cr, Rect r)
{
    RsvgHandle *handle = active_card_svg();

    if (render_svg_card_sub(handle, cr, r, "back", CARD_CACHE_BACK))
        return;

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

static const char *card_suit_id(int suit)
{
    switch (suit) {
    case SOL_HEARTS:
        return "heart";
    case SOL_DIAMONDS:
        return "diamond";
    case SOL_CLUBS:
        return "club";
    default:
        return "spade";
    }
}

static gboolean render_svg_card_sub(RsvgHandle *handle, cairo_t *cr, Rect r, const char *id, int cache_key)
{
    cairo_surface_t *surf;

    if (handle == NULL || id == NULL)
        return FALSE;

    surf = get_card_surf(handle, id, cache_key, r.w, r.h);
    if (!surf || cairo_surface_status(surf) != CAIRO_STATUS_SUCCESS)
        return FALSE;

    /* White base guarantees a visible card even if SVG content is sparse. */
    rounded_rect(cr, r, 8.0);
    cairo_set_source_rgb(cr, 1.0, 1.0, 0.97);
    cairo_fill(cr);

    cairo_save(cr);
    rounded_rect(cr, r, 8.0);
    cairo_clip(cr);
    cairo_set_source_surface(cr, surf, r.x, r.y);
    cairo_paint(cr);
    cairo_restore(cr);

    rounded_rect(cr, r, 8.0);
    cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
    cairo_set_line_width(cr, 2.4);
    cairo_stroke(cr);
    return TRUE;
}

static gboolean draw_svg_card_face(cairo_t *cr, Rect r, SolCard card)
{
    char id[64];
    int key = card_surf_key(card.suit, card.rank);
    RsvgHandle *handle = active_card_svg();

    if (app.card_theme == CARD_THEME_SIMPLIFIED)
        return FALSE;

    if (card.rank >= 11) {
        const char *face = card.rank == 11 ? "jack" : (card.rank == 12 ? "queen" : "king");
        snprintf(id, sizeof(id), "%s_%s", face, card_suit_id(card.suit));
    } else {
        if (app.card_theme == CARD_THEME_ORIGINAL)
            snprintf(id, sizeof(id), "%d_%s", card.rank, card_suit_id(card.suit));
        else
            snprintf(id, sizeof(id), "%s_%d", card_suit_id(card.suit), card.rank);
    }

    if (!render_svg_card_sub(handle, cr, r, id, key))
        return FALSE;

    return TRUE;
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

static void draw_simplified_card_face(cairo_t *cr, Rect r, SolCard card)
{
    Rect top_box = { r.x + 5.0, r.y + 5.0, r.w * 0.44, r.h * 0.25 };
    Rect center_rank = { r.x + r.w * 0.10, r.y + r.h * 0.24, r.w * 0.80, r.h * 0.30 };
    double suit_size = MIN(r.w, r.h) * 0.22;

    rounded_rect(cr, r, 8.0);
    cairo_set_source_rgb(cr, 1.0, 1.0, 0.97);
    cairo_fill_preserve(cr);
    cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
    cairo_set_line_width(cr, 2.8);
    cairo_stroke(cr);

    draw_card_corner(cr, top_box, card, 21.0);

    set_card_ink(cr, card.suit);
    draw_centered_text(cr,
                       solitaire_rank_label(card.rank),
                       center_rank,
                       MIN(r.h * 0.32, r.w * 0.46));
    draw_suit_icon(cr,
                   card.suit,
                   r.x + r.w * 0.50,
                   r.y + r.h * 0.67,
                   suit_size);

    cairo_save(cr);
    cairo_translate(cr, r.x + r.w - 5.0, r.y + r.h - 5.0);
    cairo_rotate(cr, G_PI);
    draw_card_corner(cr, (Rect){ 0, 0, top_box.w, top_box.h }, card, 18.0);
    cairo_restore(cr);
}

static void draw_card_face(cairo_t *cr, Rect r, SolCard card)
{
    Rect top_box = { r.x + 5.0, r.y + 5.0, r.w * 0.44, r.h * 0.25 };
    Rect pip_area = { r.x + r.w * 0.16, r.y + r.h * 0.28, r.w * 0.68, r.h * 0.50 };

    if (app.card_theme == CARD_THEME_SIMPLIFIED) {
        draw_simplified_card_face(cr, r, card);
        return;
    }

    if (draw_svg_card_face(cr, r, card))
        return;

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

static GtkWidget *make_theme_combo(void)
{
    GtkWidget *combo = gtk_combo_box_new_text();
    gtk_combo_box_append_text(GTK_COMBO_BOX(combo), "Simplified");
    gtk_combo_box_append_text(GTK_COMBO_BOX(combo), "Modern SVG");
    gtk_combo_box_append_text(GTK_COMBO_BOX(combo), "Original Bonded");
    gtk_combo_box_set_active(GTK_COMBO_BOX(combo), app.card_theme);
    gtk_widget_set_size_request(combo, 230, -1);
    gtk_widget_add_events(combo, GDK_BUTTON_PRESS_MASK);
    g_signal_connect(combo, "button-press-event", G_CALLBACK(combo_button_press_cb), NULL);
    g_signal_connect(combo, "changed", G_CALLBACK(theme_cb), NULL);
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
    GtkWidget *theme_label;

    gtk_init(&argc, &argv);
    app_install_kindle_style();
    load_card_svgs();
    app.game_mode = APP_GAME_KLONDIKE_DRAW1;
    app.card_theme = CARD_THEME_SIMPLIFIED;

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

    button = gtk_button_new_with_label("Restart");
    g_signal_connect(button, "clicked", G_CALLBACK(restart_cb), NULL);
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
    theme_label = gtk_label_new("Cards");
    gtk_box_pack_start(GTK_BOX(settings), theme_label, FALSE, FALSE, 0);
    app.theme_combo = make_theme_combo();
    gtk_box_pack_start(GTK_BOX(settings), app.theme_combo, FALSE, FALSE, 0);

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
    svg_atlas_clear();
    if (app.modern_card_svg != NULL)
        g_object_unref(app.modern_card_svg);
    if (app.bonded_card_svg != NULL)
        g_object_unref(app.bonded_card_svg);
    return 0;
}
