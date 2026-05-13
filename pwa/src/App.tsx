import { useEffect, useMemo, useState } from "react";

// --- Types ---
type Suit = 0 | 1 | 2 | 3; // hearts, diamonds, clubs, spades
interface Card { rank: number; suit: Suit; faceUp: boolean; }
interface Pile { cards: Card[]; }
type LocType = "stock" | "waste" | "foundation" | "tableau" | "freecell";
interface Loc { type: LocType; index: number; cardIndex?: number; }
type GameMode = "klondike" | "freecell";
// Card theme: simplified text labels, or the GNOME Aisleriot "bonded" SVG deck.
type CardTheme = "simplified" | "original";

interface GameState {
  stock: Pile;
  waste: Pile;
  freecells: Pile[];
  foundations: Pile[];
  tableau: Pile[];
  mode: GameMode;
  moves: number;
  drawCount: number;
  seed: number;
  history: SnapShot[];
}

interface SnapShot {
  stock: Pile; waste: Pile; freecells: Pile[]; foundations: Pile[]; tableau: Pile[];
  mode: GameMode; moves: number;
}

interface PersistedState { game: GameState; savedGame: GameState | null; cardTheme: CardTheme; }
const STORAGE_KEY = "exact-solitaire-pwa-v2";
const VALID_THEMES = new Set<CardTheme>(["simplified", "original"]);
const PUBLIC_BASE = import.meta.env.BASE_URL;

const SUIT_SYMS = ["♥", "♦", "♣", "♠"];
const RANK_LABELS = ["?", "A", "2", "3", "4", "5", "6", "7", "8", "9", "10", "J", "Q", "K"];
function isRed(suit: Suit): boolean { return suit === 0 || suit === 1; }

// bonded.svg layout: 79×123 unit cells, 13 cols (A-K) × 5 rows (clubs/diamonds/hearts/spades/special).
const CELL_W = 79, CELL_H = 123;
const SUIT_TO_ROW = [2, 1, 0, 3]; // 0=hearts→row2, 1=diamonds→row1, 2=clubs→row0, 3=spades→row3
const BACK_COL = 2, BACK_ROW = 4;

// Inline-SVG region of bonded.svg. Inline <use> renders embedded data: PNG face-card pictures
// reliably; the background-image approach drops them in some browsers' SVG-as-image sandbox.
function BondedRegion({ col, row }: { col: number; row: number }) {
  return (
    <svg
      viewBox={`${col * CELL_W} ${row * CELL_H} ${CELL_W} ${CELL_H}`}
      preserveAspectRatio="xMidYMid meet"
      style={{ width: "100%", height: "100%", display: "block" }}
    >
      <use href={`${PUBLIC_BASE}bonded.svg`} />
    </svg>
  );
}

function lcgNext(state: number): number { return (Math.imul(state, 1664525) + 1013904223) >>> 0; }

function clonePile(p: Pile): Pile { return { cards: p.cards.map(c => ({ ...c })) }; }
function clonePiles(ps: Pile[]): Pile[] { return ps.map(clonePile); }

function snapshot(g: GameState): SnapShot {
  return {
    stock: clonePile(g.stock), waste: clonePile(g.waste),
    freecells: clonePiles(g.freecells), foundations: clonePiles(g.foundations),
    tableau: clonePiles(g.tableau), mode: g.mode, moves: g.moves
  };
}

function shuffle(cards: Card[], seed: number): Card[] {
  let rng = seed || 1;
  const arr = [...cards];
  for (let i = arr.length - 1; i > 0; i--) {
    rng = lcgNext(rng);
    const j = rng % (i + 1);
    [arr[i], arr[j]] = [arr[j], arr[i]];
  }
  return arr;
}

function makeDeck(faceUp: boolean): Card[] {
  const deck: Card[] = [];
  for (let suit = 0; suit < 4; suit++)
    for (let rank = 1; rank <= 13; rank++)
      deck.push({ rank, suit: suit as Suit, faceUp });
  return deck;
}

function emptyPile(): Pile { return { cards: [] }; }

function newKlondike(seed: number, drawCount: number = 1): GameState {
  const s = seed || (Date.now() & 0xffffffff);
  const deck = shuffle(makeDeck(false), s);
  const tableau: Pile[] = Array.from({ length: 7 }, () => emptyPile());
  let n = deck.length;
  for (let col = 0; col < 7; col++) {
    for (let row = 0; row <= col; row++) {
      const card = deck[--n];
      card.faceUp = row === col;
      tableau[col].cards.push(card);
    }
  }
  const stock: Pile = { cards: deck.slice(0, n).reverse() };
  return {
    stock, waste: emptyPile(),
    freecells: Array.from({ length: 4 }, emptyPile),
    foundations: Array.from({ length: 4 }, emptyPile),
    tableau, mode: "klondike", moves: 0,
    drawCount: drawCount === 3 ? 3 : 1,
    seed: s, history: []
  };
}

function newFreecell(seed: number): GameState {
  const s = seed || (Date.now() & 0xffffffff);
  const deck = shuffle(makeDeck(true), s);
  const tableau: Pile[] = Array.from({ length: 8 }, () => emptyPile());
  deck.forEach((card, i) => tableau[i % 8].cards.push(card));
  return {
    stock: emptyPile(), waste: emptyPile(),
    freecells: Array.from({ length: 4 }, emptyPile),
    foundations: Array.from({ length: 4 }, emptyPile),
    tableau, mode: "freecell", moves: 0, drawCount: 0,
    seed: s, history: []
  };
}

function pileAt(g: GameState, loc: Loc): Pile | null {
  switch (loc.type) {
    case "stock": return g.stock;
    case "waste": return g.waste;
    case "freecell": return g.freecells[loc.index] ?? null;
    case "foundation": return g.foundations[loc.index] ?? null;
    case "tableau": return g.tableau[loc.index] ?? null;
  }
}

function freecellEmptyCount(g: GameState): number { return g.freecells.filter(p => p.cards.length === 0).length; }

function validTableauSeq(cards: Card[], from: number): boolean {
  for (let i = from; i + 1 < cards.length; i++) {
    if (!cards[i].faceUp || !cards[i + 1].faceUp) return false;
    if (isRed(cards[i].suit) === isRed(cards[i + 1].suit)) return false;
    if (cards[i].rank !== cards[i + 1].rank + 1) return false;
  }
  return true;
}

function canStackOnTableau(g: GameState, moving: Card, dest: Pile): boolean {
  if (dest.cards.length === 0) return g.mode === "freecell" || moving.rank === 13;
  const top = dest.cards[dest.cards.length - 1];
  return top.faceUp && isRed(top.suit) !== isRed(moving.suit) && top.rank === moving.rank + 1;
}

function canStackOnFoundation(moving: Card, dest: Pile): boolean {
  if (dest.cards.length === 0) return moving.rank === 1;
  const top = dest.cards[dest.cards.length - 1];
  return top.suit === moving.suit && moving.rank === top.rank + 1;
}

function normalizeSource(g: GameState, loc: Loc): { loc: Loc; count: number } | null {
  const pile = pileAt(g, loc);
  if (!pile || pile.cards.length === 0) return null;
  if (loc.type === "stock") return null;
  if (loc.type === "tableau") {
    const cidx = loc.cardIndex !== undefined ? loc.cardIndex : pile.cards.length - 1;
    if (cidx < 0 || cidx >= pile.cards.length) return null;
    if (!pile.cards[cidx].faceUp) return null;
    for (let i = cidx; i < pile.cards.length; i++) if (!pile.cards[i].faceUp) return null;
    if (g.mode === "freecell" && !validTableauSeq(pile.cards, cidx)) return null;
    return { loc: { ...loc, cardIndex: cidx }, count: pile.cards.length - cidx };
  }
  const cidx = pile.cards.length - 1;
  if (!pile.cards[cidx].faceUp) return null;
  return { loc: { ...loc, cardIndex: cidx }, count: 1 };
}

function canMove(g: GameState, from: Loc, to: Loc): boolean {
  const src = normalizeSource(g, from);
  if (!src) return false;
  const srcPile = pileAt(g, from)!;
  const destPile = pileAt(g, to);
  if (!destPile || srcPile === destPile) return false;
  const moving = srcPile.cards[src.loc.cardIndex!];
  if (g.mode === "freecell" && from.type === "tableau" && src.count > freecellEmptyCount(g) + 1) return false;
  if (to.type === "tableau") return canStackOnTableau(g, moving, destPile);
  if (to.type === "foundation") return src.count === 1 && canStackOnFoundation(moving, destPile);
  if (to.type === "freecell") return g.mode === "freecell" && src.count === 1 && destPile.cards.length === 0;
  return false;
}

function doMove(g: GameState, from: Loc, to: Loc): GameState | null {
  if (!canMove(g, from, to)) return null;
  const src = normalizeSource(g, from)!;
  const hist = [...g.history, snapshot(g)];
  const newG: GameState = {
    ...g,
    stock: clonePile(g.stock), waste: clonePile(g.waste),
    freecells: clonePiles(g.freecells), foundations: clonePiles(g.foundations),
    tableau: clonePiles(g.tableau), history: hist, moves: g.moves + 1
  };
  const srcPile = pileAt(newG, from)!;
  const destPile = pileAt(newG, to)!;
  const cidx = src.loc.cardIndex!;
  const moving = srcPile.cards.splice(cidx, srcPile.cards.length - cidx);
  destPile.cards.push(...moving);
  if (g.mode === "klondike" && from.type === "tableau" && srcPile.cards.length > 0)
    srcPile.cards[srcPile.cards.length - 1].faceUp = true;
  return newG;
}

function doDraw(g: GameState): GameState | null {
  if (g.mode === "freecell") return null;
  const hist = [...g.history, snapshot(g)];
  const newG: GameState = {
    ...g,
    stock: clonePile(g.stock), waste: clonePile(g.waste),
    freecells: clonePiles(g.freecells), foundations: clonePiles(g.foundations),
    tableau: clonePiles(g.tableau), history: hist, moves: g.moves + 1
  };
  if (newG.stock.cards.length > 0) {
    const cnt = g.drawCount === 3 ? 3 : 1;
    for (let i = 0; i < cnt && newG.stock.cards.length > 0; i++) {
      const card = newG.stock.cards.pop()!;
      card.faceUp = true;
      newG.waste.cards.push(card);
    }
    return newG;
  }
  if (newG.waste.cards.length > 0) {
    while (newG.waste.cards.length > 0) {
      const card = newG.waste.cards.pop()!;
      card.faceUp = false;
      newG.stock.cards.push(card);
    }
    return newG;
  }
  return null;
}

function doUndo(g: GameState): GameState | null {
  if (g.history.length === 0) return null;
  const snap = g.history[g.history.length - 1];
  return {
    stock: clonePile(snap.stock), waste: clonePile(snap.waste),
    freecells: clonePiles(snap.freecells), foundations: clonePiles(snap.foundations),
    tableau: clonePiles(snap.tableau), mode: snap.mode, moves: snap.moves,
    drawCount: g.drawCount, seed: g.seed,
    history: g.history.slice(0, -1)
  };
}

function isWon(g: GameState): boolean { return g.foundations.reduce((s, f) => s + f.cards.length, 0) === 52; }

function autoComplete(g: GameState): GameState {
  let cur = g;
  const MAX_ITERS = 200;
  for (let iter = 0; iter < MAX_ITERS; iter++) {
    let moved = false;
    const sources: Loc[] = [
      { type: "waste", index: 0 },
      ...Array.from({ length: 4 }, (_, i) => ({ type: "freecell" as LocType, index: i })),
      ...Array.from({ length: cur.mode === "klondike" ? 7 : 8 }, (_, i) => ({ type: "tableau" as LocType, index: i }))
    ];
    for (const from of sources) {
      for (let fi = 0; fi < 4; fi++) {
        const next = doMove(cur, from, { type: "foundation", index: fi });
        if (next) { cur = next; moved = true; break; }
      }
      if (moved) break;
    }
    if (!moved) break;
  }
  return cur;
}

function loadState(): PersistedState {
  const fallback: PersistedState = { game: newKlondike(Date.now() & 0xffffffff), savedGame: null, cardTheme: "simplified" };
  try {
    const raw = localStorage.getItem(STORAGE_KEY);
    if (!raw) return fallback;
    const parsed = JSON.parse(raw) as Partial<PersistedState>;
    return {
      ...fallback,
      ...parsed,
      cardTheme: VALID_THEMES.has(parsed.cardTheme as CardTheme) ? (parsed.cardTheme as CardTheme) : fallback.cardTheme,
    };
  } catch { return fallback; }
}

function gameStatus(g: GameState): string {
  if (isWon(g)) return "You win! Congratulations!";
  const foundCount = g.foundations.reduce((s, f) => s + f.cards.length, 0);
  return `Moves: ${g.moves} | Cards in foundations: ${foundCount}/52`;
}

interface Selection { loc: Loc; }

// Pixels visible of each stacked card. Up overlap raised so suit pip is never hidden behind the next card.
const OVERLAP_DOWN_PX = 18;
const OVERLAP_UP_PX = 52;
const CARD_APPROX_H = 160;

function CardView({ card, onClick, selected, style, className, theme }: {
  card: Card; onClick: () => void; selected?: boolean;
  style?: React.CSSProperties; className?: string; theme: CardTheme;
}) {
  if (!card.faceUp) {
    if (theme === "simplified") {
      return (
        <div
          className={`card face-down simplified-back ${className ?? ""}`}
          style={style}
          onClick={onClick}
        />
      );
    }
    return (
      <div
        className={`card face-down ${className ?? ""}`}
        style={style}
        onClick={onClick}
      >
        <BondedRegion col={BACK_COL} row={BACK_ROW} />
      </div>
    );
  }
  if (theme === "simplified") {
    const sym = SUIT_SYMS[card.suit];
    const rank = RANK_LABELS[card.rank];
    const colorClass = isRed(card.suit) ? "red" : "black";
    return (
      <div
        className={`card simplified-face ${colorClass} ${selected ? "selected" : ""} ${className ?? ""}`}
        style={style}
        onClick={onClick}
      >
        <div className="card-corner top">{rank}<span>{sym}</span></div>
        <div className="card-center">{sym}</div>
        <div className="card-corner bot">{rank}<span>{sym}</span></div>
      </div>
    );
  }
  const col = card.rank - 1;
  const row = SUIT_TO_ROW[card.suit];
  return (
    <div
      className={`card ${selected ? "selected" : ""} ${className ?? ""}`}
      style={style}
      onClick={onClick}
    >
      <BondedRegion col={col} row={row} />
    </div>
  );
}

export default function App() {
  const initial = useMemo(loadState, []);
  const [game, setGame] = useState<GameState>(initial.game);
  const [savedGame, setSavedGame] = useState<GameState | null>(initial.savedGame);
  const [selection, setSelection] = useState<Selection | null>(null);
  const [message, setMessage] = useState("");
  const [page, setPage] = useState<"game" | "about">("game");
  const [drawCount, setDrawCount] = useState(initial.game.drawCount || 1);
  const [cardTheme, setCardTheme] = useState<CardTheme>(initial.cardTheme);

  const won = useMemo(() => isWon(game), [game]);

  useEffect(() => {
    localStorage.setItem(STORAGE_KEY, JSON.stringify({ game, savedGame, cardTheme }));
  }, [game, savedGame, cardTheme]);

  function trySelect(loc: Loc) {
    if (won) return;
    const src = normalizeSource(game, loc);
    if (!src) { setSelection(null); return; }
    if (selection) {
      if (selection.loc.type === loc.type && selection.loc.index === loc.index && selection.loc.cardIndex === loc.cardIndex) { setSelection(null); return; }
      const next = doMove(game, selection.loc, loc);
      if (next) { setGame(next); setSelection(null); setMessage(""); return; }
    }
    setSelection({ loc: src.loc });
  }

  function tapFoundation(idx: number) {
    if (won) return;
    const loc: Loc = { type: "foundation", index: idx };
    if (selection) {
      const next = doMove(game, selection.loc, loc);
      if (next) { setGame(next); setSelection(null); return; }
    }
    setSelection(null);
  }

  function tapTableauCol(col: number, cardIndex?: number) { trySelect({ type: "tableau", index: col, cardIndex }); }
  function tapWaste() { trySelect({ type: "waste", index: 0 }); }

  function tapFreecell(idx: number) {
    const loc: Loc = { type: "freecell", index: idx };
    if (selection) {
      const next = doMove(game, selection.loc, loc);
      if (next) { setGame(next); setSelection(null); return; }
    }
    trySelect(loc);
  }

  function handleDraw() {
    const next = doDraw(game);
    if (next) { setGame(next); setSelection(null); }
  }

  function handleUndo() {
    const next = doUndo(game);
    if (next) { setGame(next); setSelection(null); setMessage("Undone."); }
  }

  function handleAuto() {
    const next = autoComplete(game);
    setGame(next); setSelection(null);
    setMessage(isWon(next) ? "Auto-completed! You win!" : "Auto-completed as far as possible.");
  }

  function handleNew(mode?: GameMode, draw?: number) {
    const m = mode || game.mode;
    const dc = draw ?? drawCount;
    const newG = m === "freecell" ? newFreecell(Date.now() & 0xffffffff) : newKlondike(Date.now() & 0xffffffff, dc);
    setGame(newG); setSelection(null); setMessage("New game.");
  }

  function handleSave() { setSavedGame(game); setMessage("Saved."); }
  function handleLoad() {
    if (!savedGame) { setMessage("No saved game."); return; }
    setGame(savedGame); setSelection(null); setMessage("Loaded.");
  }

  const isSel = (loc: Loc) =>
    selection?.loc.type === loc.type && selection.loc.index === loc.index;

  const tabCount = game.mode === "klondike" ? 7 : 8;

  function renderTableauCol(col: number) {
    const pile = game.tableau[col];
    const colSelected = isSel({ type: "tableau", index: col });
    const selCardIdx = colSelected ? selection?.loc.cardIndex : undefined;
    const topOffsets: number[] = [];
    let offset = 0;
    for (let i = 0; i < pile.cards.length; i++) {
      topOffsets.push(offset);
      offset += pile.cards[i].faceUp ? OVERLAP_UP_PX : OVERLAP_DOWN_PX;
    }
    const totalHeight = Math.max(CARD_APPROX_H, offset + CARD_APPROX_H);
    return (
      <div
        key={col}
        className="tableau-col"
        style={{ position: "relative", minHeight: totalHeight }}
        onClick={() => pile.cards.length === 0 ? tapTableauCol(col, undefined) : undefined}
      >
        {pile.cards.length === 0 && (
          <div className="empty-slot" style={{ position: "absolute", inset: 0, minHeight: CARD_APPROX_H }} />
        )}
        {pile.cards.map((card, i) => {
          const isSelCard = selCardIdx !== undefined && i >= selCardIdx;
          return (
            <CardView
              key={i}
              card={card}
              selected={isSelCard}
              theme={cardTheme}
              style={{ position: "absolute", top: topOffsets[i], left: 0, right: 0, zIndex: i + 1 }}
              onClick={() => card.faceUp ? tapTableauCol(col, i) : undefined}
            />
          );
        })}
      </div>
    );
  }

  return (
    <main className="app">
      <header className="hero">
        <h1>Exact Solitaire</h1>
        <p>{message || gameStatus(game)}</p>
      </header>

      <section className="toolbar" aria-label="Game controls">
        <button onClick={() => handleNew()}>New</button>
        <button onClick={handleUndo} disabled={game.history.length === 0}>Undo</button>
        <button onClick={handleAuto}>Auto</button>
        <button onClick={handleSave}>Save</button>
        <button onClick={handleLoad}>Load</button>
        <button onClick={() => setPage(p => p === "game" ? "about" : "game")}>{page === "game" ? "About" : "Game"}</button>
      </section>

      <section className="settings" aria-label="Settings">
        <label>Mode
          <select value={game.mode === "freecell" ? "freecell" : `klondike-${drawCount}`} onChange={e => {
            const v = e.target.value;
            if (v === "freecell") { handleNew("freecell"); }
            else if (v === "klondike-3") { setDrawCount(3); handleNew("klondike", 3); }
            else { setDrawCount(1); handleNew("klondike", 1); }
          }}>
            <option value="klondike-1">Klondike (Draw 1)</option>
            <option value="klondike-3">Klondike (Draw 3)</option>
            <option value="freecell">FreeCell</option>
          </select>
        </label>
        <label>Theme
          <select value={cardTheme} onChange={e => setCardTheme(e.target.value as CardTheme)}>
            <option value="simplified">Simplified</option>
            <option value="original">Original</option>
          </select>
        </label>
      </section>

      {page === "about" ? (
        <section className="about-page">
          <h2>About Exact Solitaire</h2>
          <p>An installable browser port of Exact Solitaire. Supports Klondike (Draw 1 or Draw 3) and FreeCell.</p>
          <p>Click a card to select it, then click the destination to move it. Click the stock to draw. "Auto" moves cards to foundations automatically.</p>
          <p>Card themes: <em>Simplified</em> uses large rank/suit labels for high contrast; <em>Original</em> uses the GNOME Aisleriot bonded deck.</p>
          <p>Attribution: GNOME Aisleriot / GNOME Games solitaire lineage (engine and bonded deck artwork), KUAL porting project. License: GPL-3.0-or-later.</p>
          <button onClick={() => { localStorage.removeItem(STORAGE_KEY); handleNew(); }}>Clear Browser Save</button>
        </section>
      ) : (
        <section className="table-area">
          {game.mode === "klondike" ? (
            <div className="top-row">
              {/* Stock */}
              <div className="card-slot stock-slot" onClick={handleDraw} title="Draw">
                {(() => {
                  const pile = game.stock;
                  if (pile.cards.length > 0) {
                    return (
                      <div
                        className={`stock-back ${cardTheme === "simplified" ? "simplified-back" : ""}`}
                        style={{ position: "absolute", inset: 0, borderRadius: 6, overflow: "hidden" }}
                      >
                        {cardTheme === "original" && <BondedRegion col={BACK_COL} row={BACK_ROW} />}
                        <div className="card-count">{pile.cards.length}</div>
                      </div>
                    );
                  }
                  return <div className="card-count empty">↺</div>;
                })()}
              </div>
              {/* Waste */}
              <div className="card-slot" style={{ cursor: "default" }}>
                {(() => {
                  const pile = game.waste;
                  if (!pile.cards.length) return null;
                  const card = pile.cards[pile.cards.length - 1];
                  const sel = isSel({ type: "waste", index: 0 });
                  return <CardView card={card} selected={sel} theme={cardTheme} style={{ position: "absolute", inset: 0 }} onClick={tapWaste} />;
                })()}
              </div>
              <div />
              {/* Foundations */}
              {Array.from({ length: 4 }, (_, i) => {
                const pile = game.foundations[i];
                const top = pile.cards[pile.cards.length - 1];
                const sel = isSel({ type: "foundation", index: i });
                return (
                  <div key={i} className={`card-slot ${sel ? "selected" : ""}`} onClick={() => tapFoundation(i)} title="Foundation">
                    {top ? (
                      <CardView card={top} selected={sel} theme={cardTheme} style={{ position: "absolute", inset: 0 }} onClick={() => tapFoundation(i)} />
                    ) : (
                      <div className="slot-placeholder">{SUIT_SYMS[i]}</div>
                    )}
                  </div>
                );
              })}
            </div>
          ) : (
            <div className="fc-top-row">
              {/* Freecells */}
              {Array.from({ length: 4 }, (_, i) => {
                const pile = game.freecells[i];
                const top = pile.cards[pile.cards.length - 1];
                const sel = isSel({ type: "freecell", index: i });
                return (
                  <div key={i} className={`card-slot ${sel ? "selected" : ""}`} onClick={() => tapFreecell(i)} title="Freecell">
                    {top ? (
                      <CardView card={top} selected={sel} theme={cardTheme} style={{ position: "absolute", inset: 0 }} onClick={() => tapFreecell(i)} />
                    ) : (
                      <div className="slot-placeholder small">□</div>
                    )}
                  </div>
                );
              })}
              {/* Foundations */}
              {Array.from({ length: 4 }, (_, i) => {
                const pile = game.foundations[i];
                const top = pile.cards[pile.cards.length - 1];
                const sel = isSel({ type: "foundation", index: i });
                return (
                  <div key={i} className={`card-slot ${sel ? "selected" : ""}`} onClick={() => tapFoundation(i)} title="Foundation">
                    {top ? (
                      <CardView card={top} selected={sel} theme={cardTheme} style={{ position: "absolute", inset: 0 }} onClick={() => tapFoundation(i)} />
                    ) : (
                      <div className="slot-placeholder">{SUIT_SYMS[i]}</div>
                    )}
                  </div>
                );
              })}
            </div>
          )}

          <div className={`tableau-row cols-${tabCount}`}>
            {Array.from({ length: tabCount }, (_, col) => renderTableauCol(col))}
          </div>
        </section>
      )}

      <footer className="notes">
        <p>Click a card to select, click destination to move. State auto-saved. Save/Load for manual restore.</p>
      </footer>
    </main>
  );
}
