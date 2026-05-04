/*
 * Kindle Aisleriot solitaire engine
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef KINDLE_AISLERIOT_SOLITAIRE_ENGINE_H
#define KINDLE_AISLERIOT_SOLITAIRE_ENGINE_H

#include <glib.h>

#define SOL_CARD_COUNT 52
#define SOL_KLONDIKE_TABLEAU_COUNT 7
#define SOL_TABLEAU_COUNT 8
#define SOL_FREECELL_COUNT 4
#define SOL_FOUNDATION_COUNT 4
#define SOL_MAX_PILE 52
#define SOL_MAX_HISTORY 256

typedef enum {
    SOL_HEARTS = 0,
    SOL_DIAMONDS = 1,
    SOL_CLUBS = 2,
    SOL_SPADES = 3
} SolSuit;

typedef struct {
    unsigned char rank;
    unsigned char suit;
    unsigned char face_up;
} SolCard;

typedef struct {
    SolCard cards[SOL_MAX_PILE];
    int count;
} SolPile;

typedef enum {
    SOL_LOC_STOCK = 0,
    SOL_LOC_WASTE,
    SOL_LOC_FOUNDATION,
    SOL_LOC_TABLEAU,
    SOL_LOC_FREECELL
} SolLocationType;

typedef struct {
    SolLocationType type;
    int index;
    int card_index;
} SolLocation;

typedef enum {
    SOL_MOVE_INVALID = 0,
    SOL_MOVE_OK,
    SOL_MOVE_WIN
} SolMoveResult;

typedef enum {
    SOL_GAME_KLONDIKE = 0,
    SOL_GAME_FREECELL
} SolGameMode;

typedef struct {
    SolPile stock;
    SolPile waste;
    SolPile freecells[SOL_FREECELL_COUNT];
    SolPile foundations[SOL_FOUNDATION_COUNT];
    SolPile tableau[SOL_TABLEAU_COUNT];
    SolGameMode mode;
    int moves;
    int draw_count;
    guint32 seed;
    int history_count;
    SolPile history_stock[SOL_MAX_HISTORY];
    SolPile history_waste[SOL_MAX_HISTORY];
    SolPile history_freecells[SOL_MAX_HISTORY][SOL_FREECELL_COUNT];
    SolPile history_foundations[SOL_MAX_HISTORY][SOL_FOUNDATION_COUNT];
    SolPile history_tableau[SOL_MAX_HISTORY][SOL_TABLEAU_COUNT];
    SolGameMode history_mode[SOL_MAX_HISTORY];
    int history_moves[SOL_MAX_HISTORY];
} SolitaireGame;

void solitaire_new(SolitaireGame *game, guint32 seed);
void solitaire_new_klondike(SolitaireGame *game, guint32 seed, int draw_count);
void solitaire_new_freecell(SolitaireGame *game, guint32 seed);
void solitaire_restart(SolitaireGame *game);
gboolean solitaire_can_undo(const SolitaireGame *game);
gboolean solitaire_undo(SolitaireGame *game);
SolMoveResult solitaire_draw(SolitaireGame *game);
SolMoveResult solitaire_move(SolitaireGame *game, SolLocation from, SolLocation to);
gboolean solitaire_can_move(const SolitaireGame *game, SolLocation from, SolLocation to);
gboolean solitaire_is_won(const SolitaireGame *game);
int solitaire_tableau_count(const SolitaireGame *game);
int solitaire_foundation_count(const SolitaireGame *game);
int solitaire_face_down_count(const SolitaireGame *game);
const char *solitaire_rank_label(int rank);
const char *solitaire_suit_label(int suit);
gboolean solitaire_is_red_suit(int suit);

#endif
