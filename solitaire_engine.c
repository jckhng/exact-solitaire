/*
 * Kindle Aisleriot solitaire engine
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "solitaire_engine.h"

#include <string.h>

static gboolean pile_push(SolPile *pile, SolCard card)
{
    if (pile->count >= SOL_MAX_PILE)
        return FALSE;
    pile->cards[pile->count++] = card;
    return TRUE;
}

static SolCard pile_pop(SolPile *pile)
{
    SolCard empty = { 0, 0, 0 };
    if (pile->count <= 0)
        return empty;
    return pile->cards[--pile->count];
}

static SolPile *pile_for_location(SolitaireGame *game, SolLocation loc)
{
    switch (loc.type) {
    case SOL_LOC_STOCK:
        return &game->stock;
    case SOL_LOC_WASTE:
        return &game->waste;
    case SOL_LOC_FOUNDATION:
        if (loc.index >= 0 && loc.index < SOL_FOUNDATION_COUNT)
            return &game->foundations[loc.index];
        break;
    case SOL_LOC_TABLEAU:
        if (loc.index >= 0 && loc.index < SOL_TABLEAU_COUNT)
            return &game->tableau[loc.index];
        break;
    }

    return NULL;
}

static const SolPile *const_pile_for_location(const SolitaireGame *game, SolLocation loc)
{
    return pile_for_location((SolitaireGame *)game, loc);
}

static void save_history(SolitaireGame *game)
{
    int slot;

    if (game->history_count >= SOL_MAX_HISTORY) {
        memmove(game->history_stock, game->history_stock + 1, sizeof(game->history_stock[0]) * (SOL_MAX_HISTORY - 1));
        memmove(game->history_waste, game->history_waste + 1, sizeof(game->history_waste[0]) * (SOL_MAX_HISTORY - 1));
        memmove(game->history_foundations, game->history_foundations + 1, sizeof(game->history_foundations[0]) * (SOL_MAX_HISTORY - 1));
        memmove(game->history_tableau, game->history_tableau + 1, sizeof(game->history_tableau[0]) * (SOL_MAX_HISTORY - 1));
        memmove(game->history_moves, game->history_moves + 1, sizeof(game->history_moves[0]) * (SOL_MAX_HISTORY - 1));
        game->history_count = SOL_MAX_HISTORY - 1;
    }

    slot = game->history_count++;
    game->history_stock[slot] = game->stock;
    game->history_waste[slot] = game->waste;
    memcpy(game->history_foundations[slot], game->foundations, sizeof(game->foundations));
    memcpy(game->history_tableau[slot], game->tableau, sizeof(game->tableau));
    game->history_moves[slot] = game->moves;
}

static void restore_history(SolitaireGame *game, int slot)
{
    game->stock = game->history_stock[slot];
    game->waste = game->history_waste[slot];
    memcpy(game->foundations, game->history_foundations[slot], sizeof(game->foundations));
    memcpy(game->tableau, game->history_tableau[slot], sizeof(game->tableau));
    game->moves = game->history_moves[slot];
}

static guint32 next_random(guint32 *state)
{
    *state = (*state * 1664525u) + 1013904223u;
    return *state;
}

static void shuffle(SolCard cards[SOL_CARD_COUNT], guint32 seed)
{
    int i;
    guint32 state = seed ? seed : 1u;

    for (i = SOL_CARD_COUNT - 1; i > 0; i--) {
        int j = (int)(next_random(&state) % (guint32)(i + 1));
        SolCard tmp = cards[i];
        cards[i] = cards[j];
        cards[j] = tmp;
    }
}

void solitaire_new(SolitaireGame *game, guint32 seed)
{
    SolCard deck[SOL_CARD_COUNT];
    int n = 0;
    int suit;
    int rank;
    int col;

    memset(game, 0, sizeof(*game));
    game->seed = seed ? seed : 1u;
    game->draw_count = 1;

    for (suit = 0; suit < 4; suit++) {
        for (rank = 1; rank <= 13; rank++) {
            deck[n].rank = (unsigned char)rank;
            deck[n].suit = (unsigned char)suit;
            deck[n].face_up = FALSE;
            n++;
        }
    }

    shuffle(deck, game->seed);

    for (col = 0; col < SOL_TABLEAU_COUNT; col++) {
        int row;
        for (row = 0; row <= col; row++) {
            SolCard card = deck[--n];
            card.face_up = (row == col);
            pile_push(&game->tableau[col], card);
        }
    }

    while (n > 0)
        pile_push(&game->stock, deck[--n]);
}

void solitaire_restart(SolitaireGame *game)
{
    guint32 seed = game->seed;
    solitaire_new(game, seed);
}

gboolean solitaire_can_undo(const SolitaireGame *game)
{
    return game->history_count > 0;
}

gboolean solitaire_undo(SolitaireGame *game)
{
    if (!solitaire_can_undo(game))
        return FALSE;

    game->history_count--;
    restore_history(game, game->history_count);
    return TRUE;
}

static gboolean can_stack_on_tableau(SolCard moving, const SolPile *dest)
{
    SolCard top;

    if (dest->count == 0)
        return moving.rank == 13;

    top = dest->cards[dest->count - 1];
    return top.face_up && solitaire_is_red_suit(top.suit) != solitaire_is_red_suit(moving.suit) && top.rank == moving.rank + 1;
}

static gboolean can_stack_on_foundation(SolCard moving, const SolPile *dest)
{
    SolCard top;

    if (dest->count == 0)
        return moving.rank == 1;

    top = dest->cards[dest->count - 1];
    return top.suit == moving.suit && moving.rank == top.rank + 1;
}

static gboolean normalize_source(const SolitaireGame *game, SolLocation *from, int *move_count)
{
    const SolPile *src = const_pile_for_location(game, *from);
    int i;

    *move_count = 0;
    if (!src || src->count <= 0)
        return FALSE;

    if (from->type == SOL_LOC_STOCK)
        return FALSE;

    if (from->type == SOL_LOC_TABLEAU) {
        if (from->card_index < 0 || from->card_index >= src->count)
            from->card_index = src->count - 1;
        if (!src->cards[from->card_index].face_up)
            return FALSE;
        for (i = from->card_index; i < src->count; i++) {
            if (!src->cards[i].face_up)
                return FALSE;
        }
        *move_count = src->count - from->card_index;
        return TRUE;
    }

    from->card_index = src->count - 1;
    if (!src->cards[from->card_index].face_up)
        return FALSE;
    *move_count = 1;
    return TRUE;
}

gboolean solitaire_can_move(const SolitaireGame *game, SolLocation from, SolLocation to)
{
    const SolPile *src;
    const SolPile *dest;
    SolCard moving;
    int move_count;

    if (!normalize_source(game, &from, &move_count))
        return FALSE;

    src = const_pile_for_location(game, from);
    dest = const_pile_for_location(game, to);
    if (!src || !dest || src == dest)
        return FALSE;

    moving = src->cards[from.card_index];
    if (to.type == SOL_LOC_TABLEAU)
        return can_stack_on_tableau(moving, dest);
    if (to.type == SOL_LOC_FOUNDATION)
        return move_count == 1 && can_stack_on_foundation(moving, dest);

    return FALSE;
}

SolMoveResult solitaire_move(SolitaireGame *game, SolLocation from, SolLocation to)
{
    SolPile *src;
    SolPile *dest;
    SolCard moving[SOL_MAX_PILE];
    int move_count;
    int i;

    if (!solitaire_can_move(game, from, to))
        return SOL_MOVE_INVALID;

    normalize_source(game, &from, &move_count);
    src = pile_for_location(game, from);
    dest = pile_for_location(game, to);
    save_history(game);

    for (i = 0; i < move_count; i++)
        moving[i] = src->cards[from.card_index + i];
    src->count = from.card_index;
    for (i = 0; i < move_count; i++)
        pile_push(dest, moving[i]);

    if (from.type == SOL_LOC_TABLEAU && src->count > 0)
        src->cards[src->count - 1].face_up = TRUE;

    game->moves++;
    return solitaire_is_won(game) ? SOL_MOVE_WIN : SOL_MOVE_OK;
}

SolMoveResult solitaire_draw(SolitaireGame *game)
{
    int i;

    save_history(game);

    if (game->stock.count > 0) {
        int draw_count = game->draw_count == 3 ? 3 : 1;
        for (i = 0; i < draw_count && game->stock.count > 0; i++) {
            SolCard card = pile_pop(&game->stock);
            card.face_up = TRUE;
            pile_push(&game->waste, card);
        }
        game->moves++;
        return SOL_MOVE_OK;
    }

    if (game->waste.count > 0) {
        while (game->waste.count > 0) {
            SolCard card = pile_pop(&game->waste);
            card.face_up = FALSE;
            pile_push(&game->stock, card);
        }
        game->moves++;
        return SOL_MOVE_OK;
    }

    solitaire_undo(game);
    return SOL_MOVE_INVALID;
}

gboolean solitaire_is_won(const SolitaireGame *game)
{
    return solitaire_foundation_count(game) == SOL_CARD_COUNT;
}

int solitaire_foundation_count(const SolitaireGame *game)
{
    int total = 0;
    int i;

    for (i = 0; i < SOL_FOUNDATION_COUNT; i++)
        total += game->foundations[i].count;
    return total;
}

int solitaire_face_down_count(const SolitaireGame *game)
{
    int total = 0;
    int col;
    int i;

    for (col = 0; col < SOL_TABLEAU_COUNT; col++) {
        for (i = 0; i < game->tableau[col].count; i++) {
            if (!game->tableau[col].cards[i].face_up)
                total++;
        }
    }
    return total + game->stock.count;
}

const char *solitaire_rank_label(int rank)
{
    static const char *labels[] = { "?", "A", "2", "3", "4", "5", "6", "7", "8", "9", "10", "J", "Q", "K" };
    if (rank < 1 || rank > 13)
        return "?";
    return labels[rank];
}

const char *solitaire_suit_label(int suit)
{
    static const char *labels[] = { "H", "D", "C", "S" };
    if (suit < 0 || suit > 3)
        return "?";
    return labels[suit];
}

gboolean solitaire_is_red_suit(int suit)
{
    return suit == SOL_HEARTS || suit == SOL_DIAMONDS;
}
