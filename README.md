# ChesslabNnet8

**A homebrew 10x12-mailbox chess engine with its own NNUE nets, built from scratch by K7 (K7 ChessLab).**

ChesslabNnet8 is one UCI engine with **seven brains** in a single `.exe`:

- **six neural nets I trained myself**, built into the exe with `incbin`;
- **Homebrew**, the hand-written evaluation from my K7Homebrew line.

You pick the brain from a drop-down list. The engine can also play its brains against each other, and it carries its own Polyglot opening book inside the exe.

**Measured strength: about 3300** (80 games at 5+0, ending with 40 games against Stockfish 4–7). Full PGNs are in [`games/`](games/).

This engine is the end of a road that started with a Python script that played illegal moves at about 600 Elo.

---

## The journey

| Stage | What it was | Strength (my estimate) |
|---|---|---|
| **K7Homebrew v1.0** | 10x12 mailbox board, alpha-beta, transposition table, piece-square tables, UCI. First big bugs fixed: 0x88 logic on a 10x12 board, and a 16-bit move overflowing at square 98 (moves became 32-bit). | — |
| **K7Homebrew v5.1** | PVS, LMR, null move, killer and history moves, quiescence, pawn structure, king safety. The best of many prototypes. | ~2275–2350 |
| **K7Homebrew v5.2 "Tunable"** | Every evaluation weight is a UCI option or a line in `weights.txt`. | ~2350 |
| **CLHBSpeed 1.0** | v5.1 plus speed and strength limits: NodeLimit, MaxDepth, MoveTimeCap. | ~2250–2350 |
| **K7Homebrew v6** | v5.1 search with an `Eval` option: Homebrew, CLCore (a CPW-style eval) or NNUE. | — |
| **ChesslabHB7** | v6 + v5.2 weights + CLHBSpeed options + logging; answers `stop`/`isready` during a search. | ~2350 (Homebrew eval) |
| **K7CustomNetV1** | v6 search with every eval removed except **my own K7 net** (`NetFile`). The experiment: can my own data and trainer make a real net? | 1.5M positions ≈ 2200, 10M ≈ 2425–2450, 54M / 100M (512 hidden) beat CLCoreAVX2 and CLMaster64 |
| **K7CustomNetV2** | Same nets, much stronger search (see below). | ~3300 |
| **ChesslabNnet8** | V2's search with six nets built in, the Homebrew eval as a seventh brain, an embedded book and brain-vs-brain self-play. | Same as V2 with the 31k brain |

### K7CustomNetV2 search upgrades

Each upgrade is a UCI check box, so it can be switched off and tested on its own. The Elo numbers are from my own ablation matches: V2 against V2 with that one feature turned off.

| Feature | What it does | Measured |
|---|---|---|
| RFP | Reverse futility pruning | +154 |
| Ordering+ | Counter-moves, history gravity and malus | +129 |
| NullMove+ | Adaptive null-move reduction, only when eval ≥ beta | +81 |
| LMP | Late move pruning | +44 |
| SEE | Static exchange evaluation for ordering and pruning | +44 |
| LMR+ | Log-table late move reductions | +7 |
| Futility | Futility pruning of quiet moves | +4 |
| IIR | Internal iterative reduction | ≈0 (kept on) |
| Delta | Delta pruning in quiescence | −51 (off by default) |

V2 also updates the net's accumulator in one fused pass and adds up the output in int32 blocks, so the compiler can vectorize both. Against V1 with the same net, V2 scored **+206 Elo at 4+0.05** and **+301 Elo at 12+0.1**.

---

## How the nets were made

```
CLUniMAX (Stockfish 13 based) ── datagen ──►  .k7g game archives (moves + scores)
                                   │
                              expand / shuffle
                                   ▼
                              .k7p  (32-byte packed positions)
                                   │
                    k7_train (C++, CPU, multithreaded)
                                   ▼
                     .bin  ──►  .k7nn  (K7NN format)
                                   │
                                incbin
                                   ▼
                          ChesslabNnet8.exe
```

- **Teacher:** CLUniMAX, built on Stockfish 13, plays self-play games and scores the positions. The nets learn from those scores and from the game results. The K7 nets don't contain or copy any Stockfish net: every weight was trained from scratch on positions generated and scored on my own PC.
- **Trainer:** `k7_train.cpp`, a CPU trainer in C++, because I don't have PyTorch.
  - Adam optimizer, multithreaded.
  - Pause and resume with full checkpoints.
  - A `check` command that finds and repairs damaged training data.
- **NetLab:** a Python/tkinter GUI that runs data generation, expansion, training and net testing, with a training-loss chart.
- **Network:** `768 inputs → H×2 (SCReLU) → 1`, one accumulator per side, quantized to int16 (QA = 255, QB = 64).
- **Training target:** `λ · sigmoid(eval / 400) + (1 − λ) · game result`.

### The six built-in brains

| Brain | Hidden size | File size |
|---|---|---|
| 21k | 128 | 193 KB |
| 22k, 23k, 24k | 256 | 386 KB |
| 25k | 512 | 772 KB |
| **31k** (default) | 512 | 772 KB |

The names are rough strength tags (31k ≈ 3100). The 31k net is the final one: **200 million positions, 512 hidden, λ = 0.5, 20 epochs**.

### What I learned training them

- **λ = 0.5 beat λ = 0.75.** With half the target coming from real game results, the net judged drawn and lost endgames much better.
- **A 512 net fills up at about 200M positions.** Adding 177M more positions made no difference: the validation loss went flat after epoch 5. A 200M / 1024 net beat the 512 net head to head, but it's slower, and against other engines it wasn't better at my time control.
- **Data integrity matters.** A training crash turned out to be my USB hub corrupting data from an SD card during long runs. The trainer now skips damaged positions, and `check` repairs the file.

---

## Results

**Setup:**
- Arena, **5+0** time control (300 s per side), Intel i9-9900K.
- One thread for every engine.
- Brain = 31k (200M positions, 512 hidden, λ0.5).
- Colours alternate every game. All games are in [`games/`](games/).

### Stockfish 4–7 (rating-list ratings, 1 CPU): 40 games

| Opponent | List rating | W / D / L | Score | Performance |
|---|---|---|---|---|
| Stockfish 4 | 3155 | 6 / 2 / 2 | 7 / 10 | ~3300 |
| Stockfish 5 | 3238 | 5 / 1 / 4 | 5.5 / 10 | ~3275 |
| Stockfish 6 | 3280 | 4 / 3 / 3 | 5.5 / 10 | ~3315 |
| Stockfish 7 | 3302 | 3 / 4 / 3 | 5 / 10 | ~3300 |
| **Total** | avg 3244 | **18 / 10 / 12** | **23 / 40 (57.5%)** | **~3300** (95%: 3200–3400) |

### Gauntlet 2900–3150: 40 games

| Opponent | Rating | W / D / L | Score |
|---|---|---|---|
| Demolito 2021 | 3153 | 9 / 0 / 1 | 9 / 10 |
| Crafty 25.3 | 3000 | 8 / 1 / 1 | 8.5 / 10 |
| CL4k64 (4ku derivative) | 2900 | 8 / 0 / 2 | 8 / 10 |
| ChesslabGM64 (Stockfish 2.3 derivative) | 3111 | 4 / 4 / 2 | 6 / 10 |
| **Total** | avg 3041 | **29 / 5 / 6** | **31.5 / 40 (78.8%)**: performance ~3285 |

### All 80 games

**54.5 / 80 (68.1%)** against an average opponent of 3142: **performance ≈ 3290** (95%: about 3205–3355).

The two 40-game sets were played on different days against different opponents, and they agree on about 3300.

**Notes:**
- This is an unofficial rating at 5 minutes per game. At longer time controls it may come out a little lower.
- The book chose mostly French and Ruy Lopez openings in the Stockfish games, so a wider set of openings would make the number more solid.
- Earlier single test games at 120+6 included draws with SLSD (Stockfish in C, ~3400) and losses to Marvin and Stockfish 5. Single games proved to be a poor guide: they suggested ~3100, while the 80-game result is ~3300.

---

## Using ChesslabNnet8

### UCI options

| Option | What it does |
|---|---|
| **Brain** | Homebrew, 21k, 22k, 23k, 24k, 25k, **31k**, NetFile |
| **SelfPlay** | Brain vs brain in a GUI: White plays **Brain**, Black plays **SelfPlayOpponent**. Play ChesslabNnet8 against ChesslabNnet8 with this ticked. |
| SelfPlayOpponent | The second brain |
| UseBook / BookFile | Built-in Polyglot book (`k7Opennings.bin`), or another one |
| NetFile | Load any K7NN net as an eighth brain, without recompiling |
| NodeLimit / MaxDepth / MoveTimeCap | Limit strength or speed |
| RFP … Ordering+ | V2's search switches (the RFP, Futility and Delta boxes affect the net brains only) |
| PawnMG … Tempo, S_*, WeightsFile | The Homebrew eval's weights and search margins (Homebrew brain only) |
| Log / LogFile | Log every command and reply |

Every search starts with a line like `info string playing with Brain 6 = 31k`, so you can always see which brain is playing.

### Console commands

`brains` · `eval` (every brain's opinion of the position) · `book` · `weights` · `bench [depth]` · `perft N` · `selfplay [A] [B] [games] [ms]` · `help`

### Built-in self-play without a GUI

```
ChesslabNnet8.exe selfplay 31k Homebrew 10 1000
```

This plays 10 games with colours alternating, openings from the book and the PGN saved to `ChesslabNnet8_selfplay.pgn`, then prints the final score and an Elo estimate. Or just run **SelfPlay.bat**.

---

## Building

You need MSYS2 MinGW64 gcc. Put these in one folder:

```
ChesslabNnet8.c
k7net21k.k7nn  k7net22k.k7nn  k7net23k.k7nn
k7net24k.k7nn  k7net25k.k7nn  k7net31k.k7nn
k7Opennings.bin
build_nnet8.bat
```

Then run `build_nnet8.bat`, or:

```
gcc -O3 -march=native -static -o ChesslabNnet8.exe ChesslabNnet8.c -lm
```

To check the build, run `ChesslabNnet8.exe`, then type `perft 5`. The answer should be **4865609**.

Build variants:
- `-DN8_NO_EMBED` reads the nets and the book from files next to the exe instead of building them in.
- `-DN8_NO_BOOK` builds without a book.

---

## Credits

- **The Stockfish team**, for the NNUE idea and for Stockfish 13, which CLUniMAX builds on as the teacher.
- **Chess Programming Wiki** and the authors of RapCpw / CPW engine, Polyglot, and Arena.
- The authors of every engine it was tested against.
- Written with AI pair-programming (Claude), with K7 as the coach: chess knowledge, direction, testing and every training run.

**K7 ChessLab**: from a 600-Elo Python script that played illegal moves, to a 2350 hand-written eval, to a ~3300 engine running nets I trained myself.
