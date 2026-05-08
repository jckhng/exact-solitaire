/*
 * Exact Solitaire smoke tests
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "solitaire_engine.h"

#include <stdio.h>

static int fail(const char *message)
{
    fprintf(stderr, "smoke-test: %s\n", message);
    return 1;
}

static int total_cards(const SolitaireGame *game)
{
    int total = game->stock.count + game->waste.count;
    int i;

    for (i = 0; i < SOL_FREECELL_COUNT; i++)
        total += game->freecells[i].count;
    for (i = 0; i < SOL_FOUNDATION_COUNT; i++)
        total += game->foundations[i].count;
    for (i = 0; i < SOL_TABLEAU_COUNT; i++)
        total += game->tableau[i].count;
    return total;
}

int main(void)
{
    SolitaireGame game;
    int i;

    solitaire_new(&game, 12345u);

    if (total_cards(&game) != SOL_CARD_COUNT)
        return fail("deal lost cards");
    if (game.stock.count != 24)
        return fail("stock should start with 24 cards");
    for (i = 0; i < SOL_KLONDIKE_TABLEAU_COUNT; i++) {
        if (game.tableau[i].count != i + 1)
            return fail("tableau column has wrong height");
        if (!game.tableau[i].cards[i].face_up)
            return fail("top tableau card should be face up");
    }

    if (solitaire_draw(&game) != SOL_MOVE_OK || game.stock.count != 23 || game.waste.count != 1)
        return fail("draw from stock failed");
    if (!solitaire_undo(&game) || game.stock.count != 24 || game.waste.count != 0)
        return fail("undo draw failed");

    game.draw_count = 3;
    if (solitaire_draw(&game) != SOL_MOVE_OK || game.stock.count != 21 || game.waste.count != 3)
        return fail("draw-3 from stock failed");
    if (!solitaire_undo(&game) || game.stock.count != 24 || game.waste.count != 0)
        return fail("undo draw-3 failed");

    game.draw_count = 1;
    for (i = 0; i < 24; i++) {
        if (solitaire_draw(&game) != SOL_MOVE_OK)
            return fail("stock draw sequence failed");
    }
    if (game.stock.count != 0 || game.waste.count != 24)
        return fail("draw sequence ended with wrong pile sizes");
    if (solitaire_draw(&game) != SOL_MOVE_OK || game.stock.count != 24 || game.waste.count != 0)
        return fail("waste recycle failed");

    solitaire_new_freecell(&game, 12345u);
    if (total_cards(&game) != SOL_CARD_COUNT)
        return fail("freecell deal lost cards");
    if (game.stock.count != 0 || game.waste.count != 0)
        return fail("freecell should not use stock or waste");
    for (i = 0; i < SOL_TABLEAU_COUNT; i++) {
        if (game.tableau[i].count < 6 || game.tableau[i].count > 7)
            return fail("freecell tableau column has wrong height");
    }
    if (solitaire_draw(&game) != SOL_MOVE_INVALID)
        return fail("freecell draw should be invalid");

    puts("smoke-test: ok");
    return 0;
}
