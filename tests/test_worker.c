#include "harness.h"

#include "mf/arena.h"
#include "mf/digest.h"
#include "mf/artifact.h"
#include "mf/mem.h"
#include "mf/panic.h"
#include "mf/table.h"
#include "mf/worker.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static mf_arena *A;
static const char *PATH = "build/test-worker.jsonl";

#define MF_RUN_A(fn)       \
    do {                   \
        mf_arena_reset(A); \
        MF_RUN(fn);        \
    } while (0)

static mf_config worker_cfg(size_t arena_bytes) {
    mf_config c;
    mf_config_defaults(&c);
    snprintf(c.artifact_path, sizeof c.artifact_path, "%s", PATH);
    c.arena_bytes = arena_bytes;
    return c;
}

MF_TEST(validate_exercises_the_memory_layer_and_records_what_it_used) {
    remove(PATH);
    mf_config c = worker_cfg(4u << 20);

    MF_EQ_INT(mf_worker_run(A, &c, MF_CMD_VALIDATE, NULL), MF_EXIT_OK);

    char *text = mf_mem_read_file(A, PATH, NULL);
    MF_CHECK(text != NULL);
    /* Every run opens with its resolved config, so an artifact reconstructs
       without the config file that produced it. */
    MF_CHECK(strstr(text, "\"record\":\"run_config\"") != NULL);
    MF_CHECK(strstr(text, "\"command\":\"validate\"") != NULL);
    MF_CHECK(strstr(text, "\"arena_bytes\":4194304") != NULL);
    /* And closes with what the memory actually cost, which is the number that
       sizes the next run. */
    MF_CHECK(strstr(text, "\"record\":\"memcheck\"") != NULL);
    MF_CHECK(strstr(text, "\"heap_arena_peak\":1048576") != NULL);
    MF_CHECK(strstr(text, "\"stack_arena_peak\":4096") != NULL);
    remove(PATH);
}

MF_TEST(validate_exercises_both_pool_disciplines) {
    /* The two pools are not interchangeable, so the run has to use both or the
       stack one is scaffolding nobody has ever run. The peaks are what the
       orchestrator would grow, and the acquire counts are the churn signal. */
    remove(PATH);
    mf_config c = worker_cfg(4u << 20);

    MF_EQ_INT(mf_worker_run(A, &c, MF_CMD_VALIDATE, NULL), MF_EXIT_OK);

    char *text = mf_mem_read_file(A, PATH, NULL);
    MF_CHECK(strstr(text, "\"heap_pool_peak\":2") != NULL);
    MF_CHECK(strstr(text, "\"stack_pool_peak\":3") != NULL);
    /* Three borrows: two held together for the whole four-item loop, and one
       for the batch of games. Not one per item — a regression to per-item claiming is
       exactly what this number is here to show. */
    MF_CHECK(strstr(text, "\"heap_pool_acquires\":3") != NULL);
    MF_CHECK(strstr(text, "\"stack_pool_acquires\":3") != NULL);
    remove(PATH);
}

/* An arena too small for the work, and a pool with too few arenas in it, are
   both premises of the two-process split — and both are verified in
   tests/smoke.sh rather than here, deliberately. Catching the fatal with a
   longjmp means mf_pool_destroy never runs, so every pooled arena is abandoned
   exactly as it would be in a dying process. That is correct behaviour and a
   real leak in a suite that refuses to die, and suppressing it would blunt the
   leak check for everything else.

   Nothing is lost by moving them: the lines they would cover here are covered by
   the runs below, the panics themselves belong to mf/arena and mf/pool and are
   tested there, and the smoke test checks the part only a real process can show
   — the exit code, and a report the orchestrator can act on. */

MF_TEST(the_unimplemented_subcommands_fail_rather_than_pretending) {
    remove(PATH);
    mf_config c = worker_cfg(4u << 20);
    MF_EQ_INT(mf_worker_run(A, &c, MF_CMD_EVAL, NULL), MF_EXIT_FAILURE);
    MF_EQ_INT(mf_worker_run(A, &c, MF_CMD_OPTIMIZE, NULL), MF_EXIT_FAILURE);

    /* The run_config record is still written: a run that failed is still a run
       that happened. */
    char *text = mf_mem_read_file(A, PATH, NULL);
    MF_CHECK(strstr(text, "\"command\":\"eval\"") != NULL);
    remove(PATH);
}

MF_TEST(preprocess_reports_the_unmatched_tail_only_when_asked) {
    /* The report goes to stdout — it is a document for whoever decides what to
       build next, not a record of what the run did. Capturing it means taking
       stdout away from the harness for the duration and giving it back. */
    const char *out = "build/test-worker-tail.txt";
    mf_config c = worker_cfg(4u << 20);
    snprintf(c.bulk_path, sizeof c.bulk_path, "tests/fixtures/bulk-unmatched.jsonl");
    c.game = MF_GAME_PAPER;
    c.report_unmatched = true;
    snprintf(c.card_table_path, sizeof c.card_table_path, "build/test-worker-cards.bin");

    int saved = dup(fileno(stdout));
    MF_CHECK(freopen(out, "w", stdout) != NULL);
    int rc = mf_worker_run(A, &c, MF_CMD_PREPROCESS, NULL);
    fflush(stdout);
    dup2(saved, fileno(stdout));
    close(saved);
    MF_EQ_INT(rc, MF_EXIT_OK);

    char *tail = mf_mem_read_file(A, out, NULL);
    MF_CHECK(tail != NULL);
    MF_CHECK(strstr(tail, "unmatched clauses: 3 in 2 distinct shapes") != NULL);
    MF_CHECK(strstr(tail, "If you do, draw a card") != NULL);
    MF_CHECK(strstr(tail, "unless you control a Mountain or a Plains") != NULL);
    /* Inert text is not a failure to model, so it has no business in a list
       whose entire purpose is "what should be built next". */
    MF_CHECK(strstr(tail, "Vigilance") == NULL);

    remove(out);
    remove("build/test-worker-cards.bin");
    remove(PATH);
}

MF_TEST(preprocess_records_the_precon_side_table_when_it_is_given) {
    remove(PATH);
    mf_config c = worker_cfg(4u << 20);
    snprintf(c.bulk_path, sizeof c.bulk_path, "tests/fixtures/bulk-sample.json");
    snprintf(c.precon_path, sizeof c.precon_path, "tests/fixtures/precons-sample.jsonl");
    c.game = MF_GAME_PAPER;
    snprintf(c.card_table_path, sizeof c.card_table_path, "build/test-worker-cards.bin");

    MF_EQ_INT(mf_worker_run(A, &c, MF_CMD_PREPROCESS, NULL), MF_EXIT_OK);
    char *run = mf_mem_read_file(A, PATH, NULL);
    MF_CHECK(strstr(run, "\"decks\":2") != NULL);
    MF_CHECK(strstr(run, "\"distinct_cards\":11") != NULL);
    /* And the skill floors are counted, so a table can be pruned per bracket
       without reading every card again. */
    MF_CHECK(strstr(run, "\"skill_floors\":{\"any\":") != NULL);

    remove("build/test-worker-cards.bin");
    remove(PATH);
}

MF_TEST(preprocess_carries_per_deck_completeness_beside_g1s_fraction) {
    /* Sprint 3.3.1 T4. G1 is a fraction over the candidate pool and per-*deck*
       completeness is a different number; sprint 3.3 read the first as though it
       bounded the second, and 0.9301 per card meant 0 of 190 decks. So the two
       are emitted in the same object, and the one that turned out to matter can
       no longer be quoted without the one that did not.

       Three decks and three answers — the published list, a deck that is
       buildable and is not it, and a deck that is neither — because a block
       whose numbers are all zero proves the key exists and nothing else. */
    remove(PATH);
    mf_config c = worker_cfg(4u << 20);
    snprintf(c.bulk_path, sizeof c.bulk_path, "tests/fixtures/bulk-sample.json");
    snprintf(c.precon_path, sizeof c.precon_path, "tests/fixtures/precons-coverage.jsonl");
    c.game = MF_GAME_PAPER;
    snprintf(c.card_table_path, sizeof c.card_table_path, "build/test-worker-cards.bin");

    MF_EQ_INT(mf_worker_run(A, &c, MF_CMD_PREPROCESS, NULL), MF_EXIT_OK);
    char *run = mf_mem_read_file(A, PATH, NULL);
    MF_CHECK(strstr(run, "\"legal_fraction\":") != NULL);
    MF_CHECK(strstr(run, "\"per_deck\":{\"decks\":3,\"complete\":1,\"buildable\":2,"
                         "\"substituted\":1,\"unresolved\":1,\"commanders_substituted\":1,"
                         "\"min_gap\":0,\"max_gap\":1}") != NULL);

    remove("build/test-worker-cards.bin");
    remove(PATH);
}

MF_TEST(preprocess_writes_the_stand_in_table_without_touching_the_pool) {
    /* Sprint 3.3.1. The commander-legal but unrepresentable cards, written to a
       second table with the same writer and the same format — it IS a card
       table, it is simply not the pool.

       **The assertion that matters is the second one.** §7.9 prunes the search
       space by representability, and a stand-in reaching the pool would undo
       that silently: the optimiser would be free to pick a card the model
       cannot simulate. So the pool table's size is checked to be exactly what
       it was before the stand-in existed. */
    remove(PATH);
    mf_config c = worker_cfg(4u << 20);
    snprintf(c.bulk_path, sizeof c.bulk_path, "tests/fixtures/bulk-sample.json");
    c.game = MF_GAME_PAPER;
    snprintf(c.card_table_path, sizeof c.card_table_path, "build/test-worker-cards.bin");
    MF_EQ_INT(mf_worker_run(A, &c, MF_CMD_PREPROCESS, NULL), MF_EXIT_OK);
    mf_table *pool_only = NULL;
    MF_EQ_INT(mf_table_read(A, "build/test-worker-cards.bin", &pool_only), MF_OK);
    size_t pool_before = mf_table_count(pool_only);
    remove(PATH);

    snprintf(c.standin_table_path, sizeof c.standin_table_path, "build/test-worker-standin.bin");
    MF_EQ_INT(mf_worker_run(A, &c, MF_CMD_PREPROCESS, NULL), MF_EXIT_OK);
    char *run = mf_mem_read_file(A, PATH, NULL);
    MF_CHECK(strstr(run, "\"standins\":") != NULL);

    mf_table *pool = NULL, *standin = NULL;
    MF_EQ_INT(mf_table_read(A, "build/test-worker-cards.bin", &pool), MF_OK);
    MF_EQ_INT(mf_table_read(A, "build/test-worker-standin.bin", &standin), MF_OK);
    MF_EQ_INT(mf_table_count(pool), pool_before); /* the pool is untouched */
    MF_CHECK(mf_table_count(standin) > 0);        /* and the stand-in is not empty */

    /* Disjoint: nothing is in both, or a card would be searchable and
       substitutable at once. */
    for (size_t i = 0; i < mf_table_count(standin); i++) {
        const char *id = mf_table_at(standin, i)->oracle_id;
        for (size_t j = 0; j < mf_table_count(pool); j++) {
            MF_CHECK(strcmp(mf_table_at(pool, j)->oracle_id, id) != 0);
        }
    }

    remove("build/test-worker-standin.bin");
    remove("build/test-worker-cards.bin");
    remove(PATH);
}

MF_TEST(a_stand_in_table_that_cannot_be_written_fails_the_run) {
    /* Optional to ask for, not optional to succeed — the same rule the precon
       side table follows, for the same reason: a run told to produce one and
       silently not producing it would leave G4 grading a corpus it could not
       build, with no sign that anything had gone wrong. */
    remove(PATH);
    mf_config c = worker_cfg(4u << 20);
    snprintf(c.bulk_path, sizeof c.bulk_path, "tests/fixtures/bulk-sample.json");
    c.game = MF_GAME_PAPER;
    snprintf(c.card_table_path, sizeof c.card_table_path, "build/test-worker-cards.bin");
    snprintf(c.standin_table_path, sizeof c.standin_table_path,
             "build/no-such-dir/standin.bin");
    MF_EQ_INT(mf_worker_run(A, &c, MF_CMD_PREPROCESS, NULL), MF_EXIT_FAILURE);
    remove("build/test-worker-cards.bin");
    remove(PATH);
}

/* ---- G4 from the tool (3.3.1 T6) ---------------------------------------- */

/* A corpus built out of the bulk fixture's own cards: `0001 0002 0004 0005
   0007` reach the pool, `0012` is the one card the opcode set cannot represent
   and so is the one the stand-in table holds. Two decks of exactly a hundred,
   because `mf_precon_deck` requires that and a shorter list is a different
   failure. The second is a quarter stand-ins, which is what makes it visible
   whether the stand-in table was passed at all. */
#define G4_CARDS "build/test-worker-cards.bin"
#define G4_STANDIN "build/test-worker-standin.bin"
#define G4_PRECONS "build/test-worker-g4-precons.jsonl"
#define G4_RATES "build/test-worker-g4-rates.tsv"

static void g4_corpus(void) {
    static const char *POOL[4] = {"0001", "0002", "0004", "0005"};
    static const char *GHOST[4] = {"0001", "0002", "0012", "0005"};
    FILE *f = fopen(G4_PRECONS, "w");
    for (int deck = 0; deck < 2; deck++) {
        fprintf(f, "{\"code\":\"G4%c\",\"name\":\"%s\",\"released\":\"2020-01-01\","
                   "\"commanders\":1,\"cards\":[\"0007\"",
                deck ? 'B' : 'A', deck ? "Ghosted" : "Real");
        for (int i = 1; i < 100; i++) {
            fprintf(f, ",\"%s\"", (deck ? GHOST : POOL)[i % 4]);
        }
        fputs("]}\n", f);
    }
    fclose(f);

    f = fopen(G4_RATES, "w");
    fputs("# built by tests/test_worker.c\n"
          "Real\tG4A\t30.0\t200\tReal\n"
          "Ghosted\tG4B\t20.0\t150\tGhosted\n",
          f);
    fclose(f);
}

static mf_config g4_cfg(bool with_standin) {
    mf_config c = worker_cfg(8u << 20);
    snprintf(c.card_table_path, sizeof c.card_table_path, G4_CARDS);
    snprintf(c.precon_path, sizeof c.precon_path, G4_PRECONS);
    snprintf(c.win_rate_path, sizeof c.win_rate_path, G4_RATES);
    if (with_standin) snprintf(c.standin_table_path, sizeof c.standin_table_path, G4_STANDIN);
    return c;
}

/* Both tables, written once by a real preprocess rather than hand-built, so the
   test reads what the tool actually produces. */
static void g4_tables(void) {
    mf_config c = worker_cfg(4u << 20);
    snprintf(c.bulk_path, sizeof c.bulk_path, "tests/fixtures/bulk-sample.json");
    c.game = MF_GAME_PAPER;
    snprintf(c.card_table_path, sizeof c.card_table_path, G4_CARDS);
    snprintf(c.standin_table_path, sizeof c.standin_table_path, G4_STANDIN);
    mf_worker_run(A, &c, MF_CMD_PREPROCESS, NULL);
    remove(PATH);
}

MF_TEST(validate_runs_g4_and_the_thresholds_travel_with_the_verdict) {
    /* Sprint 3.3 measured G4 by hand. G1's and G2's numbers come from the tool
       and are reproducible from (seed, config, card table); this one now is
       too, which is the whole of T6.

       The thresholds are echoed into the record because a bar that lives only
       in a header is one nobody can check the next run against — and all four
       were committed in 3.3 *before the data existed in the tree*. */
    g4_tables();
    g4_corpus();
    remove(PATH);
    mf_config c = g4_cfg(true);

    MF_EQ_INT(mf_worker_run(A, &c, MF_CMD_VALIDATE, NULL), MF_EXIT_OK);
    char *run = mf_mem_read_file(A, PATH, NULL);
    const char *g4 = strstr(run, "\"record\":\"g4\"");
    MF_CHECK(g4 != NULL);
    MF_CHECK(strstr(g4, "\"games_per_deck\":2048") != NULL);
    MF_CHECK(strstr(g4, "\"min_games\":100") != NULL);
    MF_CHECK(strstr(g4, "\"threshold_rho\":0.35") != NULL);
    MF_CHECK(strstr(g4, "\"threshold_z\":2") != NULL);
    MF_CHECK(strstr(g4, "\"min_corpus\":2") != NULL);
    /* Both decks are a hundred cards drawn from the two tables, so both resolve
       — which is what says the tables reached the measurement rather than the
       measurement quietly finding nothing. */
    MF_CHECK(strstr(g4, "\"corpus\":2") != NULL);
    MF_CHECK(strstr(g4, "\"scored\":2") != NULL);
    MF_CHECK(strstr(g4, "\"standin\":true") != NULL);
    /* §14.2: which cards it read. A ρ is comparable only to another ρ measured
       against the same table, and G4 is going to be re-measured. */
    const char *hash = strstr(g4, "\"card_table_hash\":\"");
    MF_CHECK(hash != NULL);
    MF_EQ_INT(strspn(hash + 19, "0123456789abcdef"), MF_DIGEST_HEX - 1);
    /* And the counts are the corpus's own: "Ghosted" carries `0012` at every
       fourth slot, so 25 substitutions, none of them a land and none of them a
       commander. Pinned numerically rather than as "some" — the two tables are
       passed in a fixed order and a swap would still resolve every deck, just
       through the wrong one, which only these numbers can see. */
    MF_CHECK(strstr(g4, "\"substituted\":25") != NULL);
    MF_CHECK(strstr(g4, "\"substituted_lands\":0") != NULL);
    MF_CHECK(strstr(g4, "\"commanders_substituted\":0") != NULL);

    /* **Beside ρ, never after it** — 3.3.1's rule about the substitution's
       footprint, asserted on the byte order of the record rather than trusted
       to whoever wrote it. */
    const char *subs = strstr(g4, "\"substituted\":");
    const char *rho = strstr(g4, "\"rho\":");
    MF_CHECK(subs != NULL && rho != NULL && subs < rho);

    /* Neither fixture deck contains a land — the bulk sample has none the
       opcode set can see — so nothing is ever cast and the two score alike.
       A gate that cannot tell them apart has ordered them no better than
       chance, and says `fail` rather than deferring its way out of a real
       zero. The corpus is degenerate on purpose; this pins the plumbing and
       the branch, not a claim about the objective. */
    MF_CHECK(strstr(g4, "\"verdict\":\"fail\"") != NULL);

    /* **The rows behind the aggregate** (3.3.2). One per scored deck, and the
       two zero-substitution controls on the same axis in the same record —
       they are what separates "the model is broken" from "the corpus is
       broken", so a record carrying the aggregate without them would be the
       one shape this diagnosis cannot use. */
    MF_CHECK(strstr(g4, "\"decks\":[{\"name\":\"Real\"") != NULL); /* corpus order */
    MF_CHECK(strstr(g4, "\"name\":\"Ghosted\",\"win_rate\":20") != NULL);
    MF_CHECK(strstr(g4, "\"lands\":0") != NULL); /* the fixture has none, by construction */
    MF_CHECK(strstr(g4, "\"controls\":[{\"name\":\"g3_forgiving\"") != NULL);
    MF_CHECK(strstr(g4, "\"name\":\"g3_demanding\"") != NULL);
    /* The controls are real decks with real mana bases and they play, which is
       the comparison the whole diagnosis rests on. */
    const char *ctl = strstr(g4, "\"controls\":");
    MF_CHECK(ctl != NULL && strstr(ctl, "\"feasible\":true") != NULL);
    remove(PATH);
}

MF_TEST(without_a_stand_in_table_g4_is_sprint_3_3s_measurement_again) {
    /* The before and after in one pair. Without the stand-in, the deck holding
       `0012` cannot be built at all, the corpus drops below `MF_G4_MIN_CORPUS`,
       and the gate DEFERS rather than reporting a correlation over one deck —
       which is exactly what happened to the real 190 in sprint 3.3. */
    g4_tables();
    g4_corpus();
    remove(PATH);
    mf_config c = g4_cfg(false);

    MF_EQ_INT(mf_worker_run(A, &c, MF_CMD_VALIDATE, NULL), MF_EXIT_OK);
    char *run = mf_mem_read_file(A, PATH, NULL);
    const char *g4 = strstr(run, "\"record\":\"g4\"");
    MF_CHECK(g4 != NULL);
    MF_CHECK(strstr(g4, "\"standin\":false") != NULL);
    MF_CHECK(strstr(g4, "\"corpus\":2") != NULL);
    MF_CHECK(strstr(g4, "\"scored\":1") != NULL);
    MF_CHECK(strstr(g4, "\"unjoined\":1") != NULL);
    MF_CHECK(strstr(g4, "\"verdict\":\"defer\"") != NULL);
    remove(PATH);
}

MF_TEST(validate_without_a_corpus_writes_no_g4_record_at_all) {
    /* 2.1's defect, headed off. A `g4` record that appeared with a `defer`
       verdict because nobody supplied win rates would read exactly like a gate
       that ran and could not decide. Absence is the only reading that cannot
       be faked, so the record is silent rather than zeroed. */
    remove(PATH);
    mf_config c = worker_cfg(4u << 20);
    MF_EQ_INT(mf_worker_run(A, &c, MF_CMD_VALIDATE, NULL), MF_EXIT_OK);
    char *run = mf_mem_read_file(A, PATH, NULL);
    MF_CHECK(strstr(run, "\"record\":\"g4\"") == NULL);
    MF_CHECK(strstr(run, "\"verdict\"") != NULL); /* G3's, so the search is not vacuous */
    remove(PATH);
}

MF_TEST(a_g4_input_that_is_not_there_fails_the_run) {
    /* Optional to ask for, not optional to succeed — the precon side table's
       rule. A run told to measure G4 and silently not measuring it would leave
       the gate looking unrun for a reason nobody could see. Each of the four
       inputs is checked separately, because a single "something went wrong"
       would not say which file to go and fix. */
    g4_tables();
    g4_corpus();
    struct {
        const char *what;
        size_t off;
    } CASES[4] = {
        {"cards", offsetof(mf_config, card_table_path)},
        {"standin", offsetof(mf_config, standin_table_path)},
        {"precons", offsetof(mf_config, precon_path)},
        {"rates", offsetof(mf_config, win_rate_path)},
    };
    for (unsigned i = 0; i < 4; i++) {
        remove(PATH);
        mf_config c = g4_cfg(true);
        snprintf((char *)&c + CASES[i].off, MF_PATH_MAX, "build/no-such-g4-%s", CASES[i].what);
        MF_EQ_INT(mf_worker_run(A, &c, MF_CMD_VALIDATE, NULL), MF_EXIT_FAILURE);
    }
    remove(G4_PRECONS);
    remove(G4_RATES);
    remove(G4_STANDIN);
    remove(G4_CARDS);
    remove(PATH);
}

MF_TEST(a_precon_file_that_is_not_there_fails_the_run) {
    /* Optional to ask for, not optional to find: a run told to use precons and
       silently not using them would price acquisition paths wrongly with no
       sign that it had. */
    remove(PATH);
    mf_config c = worker_cfg(4u << 20);
    snprintf(c.bulk_path, sizeof c.bulk_path, "tests/fixtures/bulk-sample.json");
    snprintf(c.precon_path, sizeof c.precon_path, "build/definitely-no-such-precons.jsonl");
    c.game = MF_GAME_PAPER;
    snprintf(c.card_table_path, sizeof c.card_table_path, "build/test-worker-cards.bin");

    MF_EQ_INT(mf_worker_run(A, &c, MF_CMD_PREPROCESS, NULL), MF_EXIT_FAILURE);
    remove("build/test-worker-cards.bin");
    remove(PATH);
}

MF_TEST(a_card_table_that_cannot_be_written_fails_the_run) {
    remove(PATH);
    mf_config c = worker_cfg(4u << 20);
    snprintf(c.bulk_path, sizeof c.bulk_path, "tests/fixtures/bulk-sample.json");
    c.game = MF_GAME_PAPER;
    snprintf(c.card_table_path, sizeof c.card_table_path, "build/no/such/dir/cards.bin");

    MF_EQ_INT(mf_worker_run(A, &c, MF_CMD_PREPROCESS, NULL), MF_EXIT_FAILURE);
    remove(PATH);
}

MF_TEST(preprocess_without_a_game_is_a_usage_error) {
    /* There is no default, on purpose: paper, Arena and Magic Online are
       different card pools, so picking one silently would answer a question
       that belongs to whoever is building the table. */
    mf_config c = worker_cfg(4u << 20);
    snprintf(c.bulk_path, sizeof c.bulk_path, "tests/fixtures/bulk-sample.json");
    MF_EQ_INT(mf_worker_run(A, &c, MF_CMD_PREPROCESS, NULL), MF_EXIT_USAGE);
    remove(PATH);
}

MF_TEST(preprocess_without_a_bulk_file_is_a_usage_error_not_a_failure) {
    /* It consumes a file and does not fetch one, so "you did not say which"
       is a mistake in the command line rather than something that went wrong
       with the run — and the exit code has to say which. */
    remove(PATH);
    mf_config c = worker_cfg(4u << 20);
    MF_EQ_INT(mf_worker_run(A, &c, MF_CMD_PREPROCESS, NULL), MF_EXIT_USAGE);
    remove(PATH);
}

MF_TEST(preprocess_reads_a_bulk_file_and_writes_a_card_table) {
    remove(PATH);
    mf_config c = worker_cfg(4u << 20);
    snprintf(c.bulk_path, sizeof c.bulk_path, "tests/fixtures/bulk-sample.json");
    c.game = MF_GAME_PAPER;
    snprintf(c.card_table_path, sizeof c.card_table_path, "build/test-worker-cards.bin");

    MF_EQ_INT(mf_worker_run(A, &c, MF_CMD_PREPROCESS, NULL), MF_EXIT_OK);

    char *run = mf_mem_read_file(A, PATH, NULL);
    MF_CHECK(strstr(run, "\"record\":\"preprocess\"") != NULL);
    MF_CHECK(strstr(run, "\"cards\":7") != NULL);
    MF_CHECK(strstr(run, "\"other_games\":2") != NULL);
    MF_CHECK(strstr(run, "\"game\":\"paper\"") != NULL);
    MF_CHECK(strstr(run, "\"no_oracle_id\":1") != NULL);

    /* The card table is binary since 1.3, and reading it back through the same
       reader every other run will use is what proves the file is a file rather
       than only a write. */
    MF_CHECK(strstr(run, "\"card_table\":{\"path\"") != NULL);
    mf_table *table = NULL;
    MF_EQ_INT(mf_table_read(A, "build/test-worker-cards.bin", &table), MF_OK);
    MF_EQ_INT(mf_table_game(table), MF_GAME_PAPER);
    /* The cheapest printing still wins the merge, now asserted on the card the
       table holds rather than on a line of text about it. */
    bool found_sol_ring = false;
    for (size_t i = 0; i < mf_table_count(table); i++) {
        const mf_table_card *tc = mf_table_at(table, i);
        if (strcmp(tc->name, "Sol Ring") == 0) {
            found_sol_ring = true;
            MF_EQ_INT(tc->price_cents, 175);
        }
    }
    MF_CHECK(found_sol_ring);

    remove("build/test-worker-cards.bin");
    remove(PATH);
}

MF_TEST(a_bulk_file_that_is_not_there_is_a_plain_failure) {
    /* A path the user typed wrongly is theirs to fix. */
    mf_config c = worker_cfg(4u << 20);
    snprintf(c.bulk_path, sizeof c.bulk_path, "build/definitely-no-such-bulk.json");
    c.game = MF_GAME_PAPER;
    MF_EQ_INT(mf_worker_run(A, &c, MF_CMD_PREPROCESS, NULL), MF_EXIT_FAILURE);
    remove(PATH);
}

MF_TEST(a_card_table_that_cannot_be_written_stops_the_run) {
    mf_config c = worker_cfg(4u << 20);
    snprintf(c.bulk_path, sizeof c.bulk_path, "tests/fixtures/bulk-sample.json");
    c.game = MF_GAME_PAPER;
    snprintf(c.card_table_path, sizeof c.card_table_path, "build/no/such/dir/cards.jsonl");
    MF_EQ_INT(mf_worker_run(A, &c, MF_CMD_PREPROCESS, NULL), MF_EXIT_FAILURE);
    remove(PATH);
}

MF_TEST(a_truncated_bulk_file_is_not_a_smaller_card_table) {
    /* The failure this whole pipeline is careful about: a half-downloaded file
       must not produce a card table quietly missing its tail. */
    const char *cut = "build/test-worker-cut.json";
    FILE *f = fopen(cut, "wb");
    fputs("[{\"oracle_id\":\"a\",\"name\":\"n\",\"type_line\":\"Land\","
          "\"games\":[\"paper\"]},{\"oracle_id\":\"b\"",
          f);
    fclose(f);

    mf_config c = worker_cfg(4u << 20);
    snprintf(c.bulk_path, sizeof c.bulk_path, "%s", cut);
    c.game = MF_GAME_PAPER;
    snprintf(c.card_table_path, sizeof c.card_table_path, "build/test-worker-cut-cards.jsonl");
    MF_EQ_INT(mf_worker_run(A, &c, MF_CMD_PREPROCESS, NULL), MF_EXIT_FAILURE);

    remove(cut);
    remove(PATH);
}

MF_TEST(an_artifact_that_cannot_be_opened_stops_the_run) {
    mf_config c = worker_cfg(4u << 20);
    snprintf(c.artifact_path, sizeof c.artifact_path, "build/no/such/dir/run.jsonl");
    MF_EQ_INT(mf_worker_run(A, &c, MF_CMD_VALIDATE, NULL), MF_EXIT_FAILURE);
}

/* Fills the artifact's buffer to the brim, so the next record forces a flush
   and the flush is what fails. Records only reach the disk when the buffer
   fills, so nothing smaller would exercise the write path at all. */
static mf_artifact *brimming(void) {
    static char sink[64];
    FILE *f = fmemopen(sink, sizeof sink, "w");
    setvbuf(f, NULL, _IONBF, 0);

    mf_artifact *art = NULL;
    mf_artifact_open_stream(A, f, &art);

    char pad[1024];
    memset(pad, 'x', sizeof pad - 1);
    pad[sizeof pad - 1] = '\0';
    for (int i = 0; i < 64; i++) mf_artifact_write(art, pad); /* 64 x 1024 = full */
    return art;
}

MF_TEST(a_record_that_cannot_be_flushed_is_reported_and_the_run_continues) {
    /* Losing a record is bad; throwing away the work that produced it is worse.
       The run finishes and the failure is reported at the close. */
    mf_config c = worker_cfg(4u << 20);
    MF_EQ_INT(mf_worker_run(A, &c, MF_CMD_VALIDATE, brimming()), MF_EXIT_FAILURE);
}

MF_TEST(a_failing_close_does_not_overwrite_a_failure_already_reported) {
    /* An unimplemented subcommand already failed. The close failing on top of
       that must not relabel it — the first cause is the useful one. */
    mf_config c = worker_cfg(4u << 20);
    MF_EQ_INT(mf_worker_run(A, &c, MF_CMD_EVAL, brimming()), MF_EXIT_FAILURE);
}

MF_TEST(a_failing_close_after_a_failing_run_keeps_the_first_cause) {
    /* An unimplemented subcommand has already decided the exit code. The close
       failing on top must not overwrite it — the first cause is the useful one. */
    const char *path = "build/test-worker-doomed.jsonl";
    FILE *f = fopen(path, "wb");
    mf_artifact *art = NULL;
    mf_artifact_open_stream(A, f, &art);
    close(fileno(f)); /* the disk goes away mid-run */

    mf_config c = worker_cfg(4u << 20);
    MF_EQ_INT(mf_worker_run(A, &c, MF_CMD_EVAL, art), MF_EXIT_FAILURE);
    remove(path);
}

MF_TEST(an_artifact_that_cannot_be_written_is_reported_not_ignored) {
    /* A short stream fails exactly like a full disk. The run still finishes —
       losing the artifact is bad, but it is not a reason to throw away the work
       that produced it — and the close reports the failure. */
    static char sink[64];
    FILE *f = fmemopen(sink, sizeof sink, "w");
    setvbuf(f, NULL, _IONBF, 0);

    mf_artifact *art = NULL;
    MF_EQ_INT(mf_artifact_open_stream(A, f, &art), MF_OK);

    mf_config c = worker_cfg(4u << 20);
    MF_EQ_INT(mf_worker_run(A, &c, MF_CMD_VALIDATE, art), MF_EXIT_FAILURE);
}

void run_worker_tests(void) {
    A = mf_arena_create("worker-test", 4u << 20);

    MF_RUN_A(validate_exercises_the_memory_layer_and_records_what_it_used);
    MF_RUN_A(validate_exercises_both_pool_disciplines);
    MF_RUN_A(the_unimplemented_subcommands_fail_rather_than_pretending);
    MF_RUN_A(preprocess_records_the_precon_side_table_when_it_is_given);
    MF_RUN_A(preprocess_carries_per_deck_completeness_beside_g1s_fraction);
    MF_RUN_A(preprocess_writes_the_stand_in_table_without_touching_the_pool);
    MF_RUN_A(a_stand_in_table_that_cannot_be_written_fails_the_run);
    MF_RUN_A(validate_runs_g4_and_the_thresholds_travel_with_the_verdict);
    MF_RUN_A(without_a_stand_in_table_g4_is_sprint_3_3s_measurement_again);
    MF_RUN_A(validate_without_a_corpus_writes_no_g4_record_at_all);
    MF_RUN_A(a_g4_input_that_is_not_there_fails_the_run);
    MF_RUN_A(a_precon_file_that_is_not_there_fails_the_run);
    MF_RUN_A(a_card_table_that_cannot_be_written_fails_the_run);
    MF_RUN_A(preprocess_without_a_game_is_a_usage_error);
    MF_RUN_A(preprocess_without_a_bulk_file_is_a_usage_error_not_a_failure);
    MF_RUN_A(preprocess_reads_a_bulk_file_and_writes_a_card_table);
    MF_RUN_A(preprocess_reports_the_unmatched_tail_only_when_asked);
    MF_RUN_A(a_bulk_file_that_is_not_there_is_a_plain_failure);
    MF_RUN_A(a_card_table_that_cannot_be_written_stops_the_run);
    MF_RUN_A(a_truncated_bulk_file_is_not_a_smaller_card_table);
    MF_RUN_A(an_artifact_that_cannot_be_opened_stops_the_run);
    MF_RUN_A(a_record_that_cannot_be_flushed_is_reported_and_the_run_continues);
    MF_RUN_A(a_failing_close_does_not_overwrite_a_failure_already_reported);
    MF_RUN_A(a_failing_close_after_a_failing_run_keeps_the_first_cause);
    MF_RUN_A(an_artifact_that_cannot_be_written_is_reported_not_ignored);

    mf_arena_destroy(A);
}
