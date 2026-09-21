/*
** Copyright 2025 INRIA
**
** Contributors :
** Romain PEREIRA, romain.pereira@inria.fr + rpereira@anl.gov
**
** This software is governed by the CeCILL-C license under French law and
** abiding by the rules of distribution of free software.  You can  use,
** modify and/ or redistribute the software under the terms of the CeCILL-C
** license as circulated by CEA, CNRS and INRIA at the following URL
** "http://www.cecill.info".
**
** The fact that you are presently reading this means that you have had
** knowledge of the CeCILL-C license and that you accept its terms.
*/

/* Unit tests for the disjoint interval tree.
 *
 * Every test both checks the observable behaviour of the tree and its internal
 * coherency through `dit_check()`. The last tests cross-check the tree against
 * a naive O(n) reference implementation on randomized workloads */

#include "dit.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

////////////////////
// TEST HARNESS   //
////////////////////

static int tests_run    = 0;
static int tests_failed = 0;
static int checks_run   = 0;
static const char * current_test = NULL;

#define FAIL(...)                                                           \
    do {                                                                    \
        fprintf(stderr, "    %s:%d: FAILED in `%s`: ",                      \
                __FILE__, __LINE__, current_test);                          \
        fprintf(stderr, __VA_ARGS__);                                       \
        fprintf(stderr, "\n");                                              \
        ++tests_failed;                                                     \
        return ;                                                            \
    } while (0)

#define ASSERT(COND)                                                        \
    do {                                                                    \
        ++checks_run;                                                       \
        if (!(COND))                                                        \
            FAIL("`%s`", #COND);                                            \
    } while (0)

#define ASSERT_EQ(X, Y)                                                     \
    do {                                                                    \
        ++checks_run;                                                       \
        const uint64_t __x = (uint64_t) (X);                                \
        const uint64_t __y = (uint64_t) (Y);                                \
        if (__x != __y)                                                     \
            FAIL("`%s` is %" PRIu64 ", expected %" PRIu64 " (`%s`)",        \
                    #X, __x, __y, #Y);                                      \
    } while (0)

/* a tree is coherent */
#define ASSERT_COHERENT(T)                                                  \
    do {                                                                    \
        ++checks_run;                                                       \
        char __err[512];                                                    \
        if (dit_check((T), __err, sizeof(__err)))                           \
            FAIL("incoherent tree: %s", __err);                             \
    } while (0)

#define RUN(F)                                                              \
    do {                                                                    \
        const int __before = tests_failed;                                  \
        current_test = #F;                                                  \
        ++tests_run;                                                        \
        printf("  %-40s", #F);                                              \
        fflush(stdout);                                                     \
        F();                                                                \
        printf("%s\n", (tests_failed == __before) ? "ok" : "FAILED");       \
    } while (0)

////////////////////////////
// REFERENCE (NAIVE) MODEL //
////////////////////////////

/* A sorted array of disjoint intervals, used to cross-check the tree */

typedef struct
{
    dit_value_t a, b;
}   interval_t;

#define MODEL_CAPACITY 4096

typedef struct
{
    interval_t intervals[MODEL_CAPACITY];
    size_t n;
}   model_t;

static void
model_init(model_t * m)
{
    m->n = 0;
}

static int
model_intersect(const model_t * m, dit_value_t a, dit_value_t b)
{
    for (size_t i = 0 ; i < m->n ; ++i)
        if (a < m->intervals[i].b && m->intervals[i].a < b)
            return 1;
    return 0;
}

/* insert keeping the array sorted, returns 0 if it would overlap */
static int
model_insert(model_t * m, dit_value_t a, dit_value_t b)
{
    if (a >= b || model_intersect(m, a, b))
        return 0;

    if (m->n == MODEL_CAPACITY)
    {
        fprintf(stderr, "model capacity exceeded\n");
        abort();
    }

    size_t i = m->n;
    while (i > 0 && m->intervals[i - 1].a > a)
    {
        m->intervals[i] = m->intervals[i - 1];
        --i;
    }
    m->intervals[i].a = a;
    m->intervals[i].b = b;
    ++m->n;

    return 1;
}

static size_t
model_remove(model_t * m, dit_value_t a, dit_value_t b)
{
    if (a >= b)
        return 0;

    size_t w = 0;
    size_t removed = 0;

    for (size_t i = 0 ; i < m->n ; ++i)
    {
        if (a < m->intervals[i].b && m->intervals[i].a < b)
            ++removed;
        else
            m->intervals[w++] = m->intervals[i];
    }
    m->n = w;

    return removed;
}

////////////////////////
// COLLECTING CALLBACK //
////////////////////////

typedef struct
{
    interval_t intervals[MODEL_CAPACITY];
    size_t n;

    /* stop the traversal after that many intervals, 0 to never stop */
    size_t stop_after;
}   collect_t;

static void
collect_init(collect_t * c)
{
    c->n = 0;
    c->stop_after = 0;
}

static int
collect_cb(dit_value_t a, dit_value_t b, void * user)
{
    collect_t * c = (collect_t *) user;

    if (c->n == MODEL_CAPACITY)
    {
        fprintf(stderr, "collect capacity exceeded\n");
        abort();
    }

    c->intervals[c->n].a = a;
    c->intervals[c->n].b = b;
    ++c->n;

    if (c->stop_after && c->n >= c->stop_after)
        return 42;

    return 0;
}

///////////
// TESTS //
///////////

static void
test_empty(void)
{
    dit_t tree;
    dit_init(&tree);

    ASSERT_COHERENT(&tree);
    ASSERT(dit_empty(&tree));
    ASSERT_EQ(dit_size(&tree), 0);
    ASSERT_EQ(dit_height(&tree), 0);
    ASSERT(dit_at(&tree, 0) == NULL);
    ASSERT(dit_intersecting(&tree, 0, 100) == NULL);
    ASSERT_EQ(dit_intersect_p(&tree, 0, 100), 0);
    ASSERT_EQ(dit_remove(&tree, 0, 100), 0);

    /* an empty tree has no hull, and the out params are left untouched */
    dit_value_t ha = 42;
    dit_value_t hb = 43;
    ASSERT_EQ(dit_hull(&tree, &ha, &hb), 0);
    ASSERT_EQ(ha, 42);
    ASSERT_EQ(hb, 43);

    collect_t c;
    collect_init(&c);
    ASSERT_EQ(dit_intersect(&tree, 0, 100, collect_cb, &c), 0);
    ASSERT_EQ(c.n, 0);
    ASSERT_EQ(dit_each(&tree, collect_cb, &c), 0);
    ASSERT_EQ(c.n, 0);

    dit_destroy(&tree);
}

static void
test_insert_single(void)
{
    dit_t tree;
    dit_init(&tree);

    ASSERT_EQ(dit_insert(&tree, 10, 20), DIT_OK);
    ASSERT_COHERENT(&tree);
    ASSERT_EQ(dit_size(&tree), 1);
    ASSERT_EQ(dit_empty(&tree), 0);
    ASSERT_EQ(dit_height(&tree), 1);

    /* the hull of a single node is the node interval itself */
    ASSERT_EQ(tree.root->augment.hull.a, 10);
    ASSERT_EQ(tree.root->augment.hull.b, 20);
    ASSERT_EQ(tree.root->augment.height, 1);
    ASSERT_EQ(tree.root->augment.size, 1);

    /* half-open interval: 10 is inside, 20 is not */
    ASSERT(dit_at(&tree, 9) == NULL);
    ASSERT(dit_at(&tree, 10) != NULL);
    ASSERT(dit_at(&tree, 19) != NULL);
    ASSERT(dit_at(&tree, 20) == NULL);

    dit_destroy(&tree);
}

static void
test_insert_empty_interval(void)
{
    dit_t tree;
    dit_init(&tree);

    ASSERT_EQ(dit_insert(&tree, 10, 10), DIT_EMPTY);
    ASSERT_EQ(dit_insert(&tree, 20, 10), DIT_EMPTY);
    ASSERT_EQ(dit_size(&tree), 0);
    ASSERT_COHERENT(&tree);

    dit_destroy(&tree);
}

static void
test_insert_overlap_is_rejected(void)
{
    dit_t tree;
    dit_init(&tree);

    ASSERT_EQ(dit_insert(&tree, 10, 20), DIT_OK);
    ASSERT_EQ(dit_insert(&tree, 30, 40), DIT_OK);

    /* every flavour of overlap */
    ASSERT_EQ(dit_insert(&tree, 10, 20), DIT_OVERLAP);  /* identical         */
    ASSERT_EQ(dit_insert(&tree, 12, 18), DIT_OVERLAP);  /* strictly included */
    ASSERT_EQ(dit_insert(&tree,  5, 25), DIT_OVERLAP);  /* strictly includes */
    ASSERT_EQ(dit_insert(&tree,  5, 11), DIT_OVERLAP);  /* overlaps the left */
    ASSERT_EQ(dit_insert(&tree, 19, 25), DIT_OVERLAP);  /* overlaps the right*/
    ASSERT_EQ(dit_insert(&tree,  0, 35), DIT_OVERLAP);  /* spans both        */

    /* and the tree was left untouched */
    ASSERT_EQ(dit_size(&tree), 2);
    ASSERT_COHERENT(&tree);

    /* adjacent intervals do not overlap */
    ASSERT_EQ(dit_insert(&tree,  0, 10), DIT_OK);
    ASSERT_EQ(dit_insert(&tree, 20, 30), DIT_OK);
    ASSERT_EQ(dit_insert(&tree, 40, 50), DIT_OK);
    ASSERT_EQ(dit_size(&tree), 5);
    ASSERT_COHERENT(&tree);

    dit_destroy(&tree);
}

static void
test_ordering(void)
{
    dit_t tree;
    dit_init(&tree);

    /* inserted out of order */
    const dit_value_t starts[] = { 50, 10, 90, 30, 70, 0, 20 };
    for (size_t i = 0 ; i < sizeof(starts) / sizeof(*starts) ; ++i)
        ASSERT_EQ(dit_insert(&tree, starts[i], starts[i] + 5), DIT_OK);
    ASSERT_COHERENT(&tree);

    collect_t c;
    collect_init(&c);
    ASSERT_EQ(dit_each(&tree, collect_cb, &c), 0);

    /* reported in increasing order */
    ASSERT_EQ(c.n, 7);
    for (size_t i = 1 ; i < c.n ; ++i)
        ASSERT(c.intervals[i - 1].b <= c.intervals[i].a);
    ASSERT_EQ(c.intervals[0].a, 0);
    ASSERT_EQ(c.intervals[6].a, 90);

    dit_destroy(&tree);
}

/* the hull augment must follow every mutation, wherever it happens in the
 * tree, and whatever rotations it triggers */
static void
test_hull_augment(void)
{
    dit_t tree;
    dit_init(&tree);

    dit_value_t a, b;

    ASSERT_EQ(dit_insert(&tree, 100, 110), DIT_OK);
    ASSERT_EQ(dit_hull(&tree, &a, &b), 1);
    ASSERT_EQ(a, 100);
    ASSERT_EQ(b, 110);

    /* growing on the left moves the lower bound only */
    ASSERT_EQ(dit_insert(&tree, 10, 20), DIT_OK);
    ASSERT_EQ(dit_hull(&tree, &a, &b), 1);
    ASSERT_EQ(a, 10);
    ASSERT_EQ(b, 110);

    /* growing on the right moves the upper bound only */
    ASSERT_EQ(dit_insert(&tree, 200, 210), DIT_OK);
    ASSERT_EQ(dit_hull(&tree, &a, &b), 1);
    ASSERT_EQ(a, 10);
    ASSERT_EQ(b, 210);

    /* inserting in between changes nothing */
    ASSERT_EQ(dit_insert(&tree, 50, 60), DIT_OK);
    ASSERT_EQ(dit_hull(&tree, &a, &b), 1);
    ASSERT_EQ(a, 10);
    ASSERT_EQ(b, 210);

    /* enough insertions to trigger rotations at the root */
    for (dit_value_t i = 0 ; i < 64 ; ++i)
        ASSERT_EQ(dit_insert(&tree, 1000 + 10 * i, 1000 + 10 * i + 5), DIT_OK);
    ASSERT_COHERENT(&tree);
    ASSERT_EQ(dit_hull(&tree, &a, &b), 1);
    ASSERT_EQ(a, 10);
    ASSERT_EQ(b, 1635);

    /* the hull shrinks back when the extremities are removed */
    ASSERT_EQ(dit_remove(&tree, 10, 20), 1);
    ASSERT_EQ(dit_remove(&tree, 1630, 1635), 1);
    ASSERT_COHERENT(&tree);
    ASSERT_EQ(dit_hull(&tree, &a, &b), 1);
    ASSERT_EQ(a, 50);
    ASSERT_EQ(b, 1625);

    /* the hull of a subtree englobes it, and only it */
    ASSERT(tree.root->augment.hull.a == 50);
    ASSERT(tree.root->augment.hull.b == 1625);
    if (tree.root->left)
        ASSERT(tree.root->left->augment.hull.b <= tree.root->a);
    if (tree.root->right)
        ASSERT(tree.root->right->augment.hull.a >= tree.root->b);

    /* emptying the tree removes the hull */
    dit_clear(&tree);
    ASSERT_EQ(dit_hull(&tree, &a, &b), 0);

    dit_destroy(&tree);
}

static void
test_intersect(void)
{
    dit_t tree;
    dit_init(&tree);

    /* [0..10[ [20..30[ [40..50[ [60..70[ */
    for (dit_value_t i = 0 ; i < 4 ; ++i)
        ASSERT_EQ(dit_insert(&tree, 20 * i, 20 * i + 10), DIT_OK);
    ASSERT_COHERENT(&tree);

    collect_t c;

    /* query strictly inside a hole */
    collect_init(&c);
    dit_intersect(&tree, 12, 18, collect_cb, &c);
    ASSERT_EQ(c.n, 0);

    /* query touching nothing, adjacency is not an intersection */
    collect_init(&c);
    dit_intersect(&tree, 10, 20, collect_cb, &c);
    ASSERT_EQ(c.n, 0);

    /* query overlapping a single interval by one unit */
    collect_init(&c);
    dit_intersect(&tree, 9, 20, collect_cb, &c);
    ASSERT_EQ(c.n, 1);
    ASSERT_EQ(c.intervals[0].a, 0);

    /* query spanning several intervals */
    collect_init(&c);
    dit_intersect(&tree, 5, 45, collect_cb, &c);
    ASSERT_EQ(c.n, 3);
    ASSERT_EQ(c.intervals[0].a, 0);
    ASSERT_EQ(c.intervals[1].a, 20);
    ASSERT_EQ(c.intervals[2].a, 40);

    /* query spanning everything */
    collect_init(&c);
    dit_intersect(&tree, 0, 1000, collect_cb, &c);
    ASSERT_EQ(c.n, 4);

    /* empty query */
    collect_init(&c);
    dit_intersect(&tree, 5, 5, collect_cb, &c);
    ASSERT_EQ(c.n, 0);

    /* out of range queries */
    collect_init(&c);
    dit_intersect(&tree, 1000, 2000, collect_cb, &c);
    ASSERT_EQ(c.n, 0);

    ASSERT_EQ(dit_intersect_p(&tree, 12, 18), 0);
    ASSERT_EQ(dit_intersect_p(&tree, 9, 11), 1);

    dit_destroy(&tree);
}

static void
test_intersect_early_stop(void)
{
    dit_t tree;
    dit_init(&tree);

    for (dit_value_t i = 0 ; i < 100 ; ++i)
        ASSERT_EQ(dit_insert(&tree, 10 * i, 10 * i + 5), DIT_OK);

    collect_t c;
    collect_init(&c);
    c.stop_after = 3;

    /* the callback return value is propagated */
    ASSERT_EQ(dit_intersect(&tree, 0, 10000, collect_cb, &c), 42);
    ASSERT_EQ(c.n, 3);
    ASSERT_EQ(c.intervals[0].a, 0);
    ASSERT_EQ(c.intervals[1].a, 10);
    ASSERT_EQ(c.intervals[2].a, 20);

    /* and the tree is untouched, and still traversable */
    ASSERT_EQ(tree.traversing, 0);
    ASSERT_COHERENT(&tree);

    dit_destroy(&tree);
}

static void
test_remove(void)
{
    dit_t tree;
    dit_init(&tree);

    /* [0..10[ [20..30[ [40..50[ [60..70[ */
    for (dit_value_t i = 0 ; i < 4 ; ++i)
        ASSERT_EQ(dit_insert(&tree, 20 * i, 20 * i + 10), DIT_OK);

    /* removing a hole removes nothing */
    ASSERT_EQ(dit_remove(&tree, 10, 20), 0);
    ASSERT_EQ(dit_size(&tree), 4);
    ASSERT_COHERENT(&tree);

    /* intervals are removed as a whole, never split */
    ASSERT_EQ(dit_remove(&tree, 25, 26), 1);
    ASSERT_EQ(dit_size(&tree), 3);
    ASSERT(dit_at(&tree, 20) == NULL);
    ASSERT(dit_at(&tree, 29) == NULL);
    ASSERT_COHERENT(&tree);

    /* removing a range spanning several intervals */
    ASSERT_EQ(dit_remove(&tree, 5, 45), 2);
    ASSERT_EQ(dit_size(&tree), 1);
    ASSERT(dit_at(&tree, 65) != NULL);
    ASSERT_COHERENT(&tree);

    /* removing everything */
    ASSERT_EQ(dit_remove(&tree, 0, 1000), 1);
    ASSERT_EQ(dit_size(&tree), 0);
    ASSERT(dit_empty(&tree));
    ASSERT_COHERENT(&tree);

    /* removing from an empty tree */
    ASSERT_EQ(dit_remove(&tree, 0, 1000), 0);
    ASSERT_COHERENT(&tree);

    dit_destroy(&tree);
}

static void
test_remove_reinsert(void)
{
    dit_t tree;
    dit_init(&tree);

    for (dit_value_t i = 0 ; i < 256 ; ++i)
        ASSERT_EQ(dit_insert(&tree, 10 * i, 10 * i + 5), DIT_OK);
    ASSERT_COHERENT(&tree);

    /* remove every other interval */
    for (dit_value_t i = 0 ; i < 256 ; i += 2)
        ASSERT_EQ(dit_remove(&tree, 10 * i, 10 * i + 5), 1);
    ASSERT_EQ(dit_size(&tree), 128);
    ASSERT_COHERENT(&tree);

    /* the removed slots are now free again */
    for (dit_value_t i = 0 ; i < 256 ; i += 2)
        ASSERT_EQ(dit_insert(&tree, 10 * i, 10 * i + 5), DIT_OK);
    ASSERT_EQ(dit_size(&tree), 256);
    ASSERT_COHERENT(&tree);

    /* the others are not */
    for (dit_value_t i = 1 ; i < 256 ; i += 2)
        ASSERT_EQ(dit_insert(&tree, 10 * i, 10 * i + 5), DIT_OVERLAP);
    ASSERT_COHERENT(&tree);

    dit_destroy(&tree);
}

static void
test_clear(void)
{
    dit_t tree;
    dit_init(&tree);

    for (dit_value_t i = 0 ; i < 100 ; ++i)
        ASSERT_EQ(dit_insert(&tree, 10 * i, 10 * i + 5), DIT_OK);

    dit_clear(&tree);
    ASSERT(dit_empty(&tree));
    ASSERT_EQ(dit_size(&tree), 0);
    ASSERT_COHERENT(&tree);

    /* the tree is reusable */
    ASSERT_EQ(dit_insert(&tree, 0, 1000), DIT_OK);
    ASSERT_EQ(dit_size(&tree), 1);
    ASSERT_COHERENT(&tree);

    dit_destroy(&tree);
}

/* sequential insertions are the worst case for an unbalanced BST */
static void
test_balance_sorted_insertions(void)
{
    dit_t tree;
    dit_init(&tree);

    const dit_value_t n = 4095;

    for (dit_value_t i = 0 ; i < n ; ++i)
        ASSERT_EQ(dit_insert(&tree, 2 * i, 2 * i + 1), DIT_OK);

    ASSERT_EQ(dit_size(&tree), n);
    ASSERT_COHERENT(&tree);

    /* 4095 nodes fit in a perfectly balanced tree of height 12, an AVL tree
     * is at most ~1.44*log2(n) deep */
    ASSERT(dit_height(&tree) <= 18);

    /* and reverse sorted insertions too */
    dit_clear(&tree);
    for (dit_value_t i = n ; i > 0 ; --i)
        ASSERT_EQ(dit_insert(&tree, 2 * i, 2 * i + 1), DIT_OK);
    ASSERT_EQ(dit_size(&tree), n);
    ASSERT(dit_height(&tree) <= 18);
    ASSERT_COHERENT(&tree);

    /* sequential deletions from the left */
    for (dit_value_t i = 1 ; i <= n ; ++i)
    {
        ASSERT_EQ(dit_remove(&tree, 2 * i, 2 * i + 1), 1);
        ASSERT_EQ(dit_size(&tree), n - i);
    }
    ASSERT(dit_empty(&tree));
    ASSERT_COHERENT(&tree);

    dit_destroy(&tree);
}

/* the whole [0..UINT64_MAX[ range must be usable */
static void
test_extreme_values(void)
{
    dit_t tree;
    dit_init(&tree);

    ASSERT_EQ(dit_insert(&tree, 0, 1), DIT_OK);
    ASSERT_EQ(dit_insert(&tree, DIT_VALUE_MAX - 1, DIT_VALUE_MAX), DIT_OK);
    ASSERT_EQ(dit_insert(&tree, 1, DIT_VALUE_MAX - 1), DIT_OK);
    ASSERT_COHERENT(&tree);

    ASSERT_EQ(tree.root->augment.hull.a, 0);
    ASSERT_EQ(tree.root->augment.hull.b, DIT_VALUE_MAX);

    /* DIT_VALUE_MAX can never be covered by a half-open interval */
    ASSERT(dit_at(&tree, DIT_VALUE_MAX) == NULL);
    ASSERT(dit_at(&tree, DIT_VALUE_MAX - 1) != NULL);
    ASSERT(dit_at(&tree, 0) != NULL);

    collect_t c;
    collect_init(&c);
    dit_intersect(&tree, 0, DIT_VALUE_MAX, collect_cb, &c);
    ASSERT_EQ(c.n, 3);

    ASSERT_EQ(dit_remove(&tree, 0, DIT_VALUE_MAX), 3);
    ASSERT(dit_empty(&tree));
    ASSERT_COHERENT(&tree);

    dit_destroy(&tree);
}

/* `dit_check()` must actually detect a corrupted tree */
static void
test_check_detects_corruption(void)
{
    char err[512];

    dit_t tree;
    dit_init(&tree);

    for (dit_value_t i = 0 ; i < 32 ; ++i)
        ASSERT_EQ(dit_insert(&tree, 10 * i, 10 * i + 5), DIT_OK);
    ASSERT_EQ(dit_check(&tree, err, sizeof(err)), 0);

    /* corrupt the cardinality */
    tree.n += 1;
    ASSERT(dit_check(&tree, err, sizeof(err)) != 0);
    tree.n -= 1;
    ASSERT_EQ(dit_check(&tree, err, sizeof(err)), 0);

    /* corrupt the hull augment */
    const dit_value_t saved_hull_b = tree.root->augment.hull.b;
    tree.root->augment.hull.b -= 1;
    ASSERT(dit_check(&tree, err, sizeof(err)) != 0);
    tree.root->augment.hull.b = saved_hull_b;
    ASSERT_EQ(dit_check(&tree, err, sizeof(err)), 0);

    /* corrupt the height augment */
    tree.root->augment.height += 1;
    ASSERT(dit_check(&tree, err, sizeof(err)) != 0);
    tree.root->augment.height -= 1;
    ASSERT_EQ(dit_check(&tree, err, sizeof(err)), 0);

    /* corrupt the size augment */
    tree.root->augment.size += 1;
    ASSERT(dit_check(&tree, err, sizeof(err)) != 0);
    tree.root->augment.size -= 1;
    ASSERT_EQ(dit_check(&tree, err, sizeof(err)), 0);

    /* make the root interval empty */
    const dit_value_t saved_a = tree.root->a;
    tree.root->a = tree.root->b;
    ASSERT(dit_check(&tree, err, sizeof(err)) != 0);
    tree.root->a = saved_a;
    ASSERT_EQ(dit_check(&tree, err, sizeof(err)), 0);

    /* break the ordering, the root now overlaps its whole left subtree */
    ASSERT(tree.root->left != NULL);
    tree.root->a = 0;
    ASSERT(dit_check(&tree, err, sizeof(err)) != 0);
    tree.root->a = saved_a;
    ASSERT_EQ(dit_check(&tree, err, sizeof(err)), 0);

    dit_destroy(&tree);
}

static void
test_dump_dot(void)
{
    dit_t tree;
    dit_init(&tree);

    FILE * f = fopen("/dev/null", "w");
    if (f == NULL)
        FAIL("could not open /dev/null");

    /* an empty tree is dumpable */
    dit_dump_dot(&tree, f);

    for (dit_value_t i = 0 ; i < 16 ; ++i)
        ASSERT_EQ(dit_insert(&tree, 10 * i, 10 * i + 5), DIT_OK);

    dit_dump_dot(&tree, f);
    dit_dump_dot(NULL, f);

    fclose(f);

    ASSERT_COHERENT(&tree);
    dit_destroy(&tree);
}

///////////////////////
// RANDOMIZED TESTS  //
///////////////////////

/* Seeds, sizes and intervals are decimal everywhere in this file. The few
 * constants below are the published bit patterns of splitmix64 and
 * xorshift64*, and are the only thing left in hexadecimal: they are chosen for
 * their bits, not for their value, and writing them in decimal would only make
 * them unrecognizable */
static uint64_t rng_state = 0x853c49e6748fea9bULL;

/* xorshift64* needs a non-zero, well spread state: run the seed through a
 * splitmix64 avalanche so that consecutive seeds give unrelated streams.
 * Simply forcing the state odd would make seeds 2k and 2k+1 equivalent, and
 * halve the coverage of a `rake test:seeds` sweep */
static uint64_t
rng_seed(uint64_t seed)
{
    seed += 0x9E3779B97F4A7C15ULL;
    seed = (seed ^ (seed >> 30)) * 0xBF58476D1CE4E5B9ULL;
    seed = (seed ^ (seed >> 27)) * 0x94D049BB133111EBULL;
    seed ^=  seed >> 31;
    return seed ? seed : 1;
}

static uint64_t
rng_next(void)
{
    /* xorshift64* */
    rng_state ^= rng_state >> 12;
    rng_state ^= rng_state << 25;
    rng_state ^= rng_state >> 27;
    return rng_state * 0x2545F4914F6CDD1DULL;
}

static uint64_t
rng_below(uint64_t n)
{
    return rng_next() % n;
}

/* compare the tree content against the model, in-order */
static int
same_as_model(dit_t * tree, const model_t * m, collect_t * c)
{
    collect_init(c);
    dit_each(tree, collect_cb, c);

    if (c->n != m->n)
        return 0;

    for (size_t i = 0 ; i < m->n ; ++i)
        if (c->intervals[i].a != m->intervals[i].a ||
            c->intervals[i].b != m->intervals[i].b)
            return 0;

    return 1;
}

static void
test_random_against_model(void)
{
    const uint64_t universe   = 512;
    const int      iterations = 20000;

    dit_t tree;
    dit_init(&tree);

    model_t model;
    model_init(&model);

    collect_t c;

    for (int it = 0 ; it < iterations ; ++it)
    {
        const dit_value_t a   = rng_below(universe);
        const dit_value_t len = 1 + rng_below(8);
        const dit_value_t b   = a + len;

        switch (rng_below(3))
        {
            /* insert, only when the model says it does not overlap */
            case 0:
            {
                const int insertable = !model_intersect(&model, a, b);
                const dit_status_t status = dit_insert(&tree, a, b);

                if (insertable)
                {
                    if (status != DIT_OK)
                        FAIL("insert([%" PRIu64 "..%" PRIu64 "[) returned %d, "
                             "expected DIT_OK", a, b, (int) status);
                    model_insert(&model, a, b);
                }
                else
                {
                    if (status != DIT_OVERLAP)
                        FAIL("insert([%" PRIu64 "..%" PRIu64 "[) returned %d, "
                             "expected DIT_OVERLAP", a, b, (int) status);
                }
                break ;
            }

            /* remove */
            case 1:
            {
                const size_t expected = model_remove(&model, a, b);
                const size_t got      = dit_remove(&tree, a, b);
                if (got != expected)
                    FAIL("remove([%" PRIu64 "..%" PRIu64 "[) removed %zu "
                         "intervals, expected %zu", a, b, got, expected);
                break ;
            }

            /* intersect */
            default:
            {
                collect_init(&c);
                dit_intersect(&tree, a, b, collect_cb, &c);

                size_t expected = 0;
                for (size_t i = 0 ; i < model.n ; ++i)
                    if (a < model.intervals[i].b && model.intervals[i].a < b)
                    {
                        if (expected >= c.n ||
                            c.intervals[expected].a != model.intervals[i].a ||
                            c.intervals[expected].b != model.intervals[i].b)
                            FAIL("intersect([%" PRIu64 "..%" PRIu64 "[) "
                                 "mismatch at %zu", a, b, expected);
                        ++expected;
                    }

                if (c.n != expected)
                    FAIL("intersect([%" PRIu64 "..%" PRIu64 "[) reported %zu "
                         "intervals, expected %zu", a, b, c.n, expected);
                break ;
            }
        }

        /* the structure stays coherent at every single step */
        {
            char err[512];
            if (dit_check(&tree, err, sizeof(err)))
                FAIL("iteration %d: incoherent tree: %s", it, err);
        }

        if (dit_size(&tree) != model.n)
            FAIL("iteration %d: tree holds %zu intervals, expected %zu",
                    it, dit_size(&tree), model.n);

        if (!same_as_model(&tree, &model, &c))
            FAIL("iteration %d: tree content differs from the model", it);
    }

    ++checks_run;
    dit_destroy(&tree);
}

/* same thing, but on a large tree, checking coherency only from time to time
 * so that the test remains fast */
static void
test_random_large(void)
{
    /* the paranoid build checks the whole structure after every mutation,
     * which is O(n): keep the tree small enough for the suite to stay fast */
    const uint64_t universe   = DIT_PARANOID ?  20000 :  1000000;
    const int      iterations = DIT_PARANOID ?  20000 :   200000;

    dit_t tree;
    dit_init(&tree);

    size_t inserted = 0;
    size_t removed  = 0;

    for (int it = 0 ; it < iterations ; ++it)
    {
        const dit_value_t a = rng_below(universe);
        const dit_value_t b = a + 1 + rng_below(64);

        if (rng_below(4) == 0)
            removed += dit_remove(&tree, a, b);
        else if (dit_insert(&tree, a, b) == DIT_OK)
            ++inserted;

        if ((it % 5000) == 0)
        {
            char err[512];
            if (dit_check(&tree, err, sizeof(err)))
                FAIL("iteration %d: incoherent tree: %s", it, err);
        }
    }

    ASSERT_COHERENT(&tree);
    ASSERT_EQ(dit_size(&tree), inserted - removed);
    ASSERT(inserted > 1000);
    ASSERT(removed > 1000);

    /* removing everything must empty the tree */
    dit_remove(&tree, 0, DIT_VALUE_MAX);
    ASSERT(dit_empty(&tree));
    ASSERT_COHERENT(&tree);

    dit_destroy(&tree);
}

/* insert n intervals in a random order, then remove them in another random
 * order, checking the structure at every step */
static void
test_random_insert_then_remove(void)
{
    const size_t n = 1024;

    dit_value_t * order = (dit_value_t *) malloc(n * sizeof(dit_value_t));
    if (order == NULL)
        FAIL("out of memory");

    for (size_t i = 0 ; i < n ; ++i)
        order[i] = (dit_value_t) i;

    /* fisher-yates */
    for (size_t i = n - 1 ; i > 0 ; --i)
    {
        const size_t j = (size_t) rng_below(i + 1);
        const dit_value_t tmp = order[i];
        order[i] = order[j];
        order[j] = tmp;
    }

    dit_t tree;
    dit_init(&tree);

    for (size_t i = 0 ; i < n ; ++i)
    {
        if (dit_insert(&tree, 10 * order[i], 10 * order[i] + 7) != DIT_OK)
        {
            free(order);
            FAIL("insertion %zu failed", i);
        }
        if (dit_size(&tree) != i + 1)
        {
            free(order);
            FAIL("tree holds %zu intervals after %zu insertions",
                    dit_size(&tree), i + 1);
        }
    }

    {
        char err[512];
        if (dit_check(&tree, err, sizeof(err)))
        {
            free(order);
            FAIL("incoherent tree after insertions: %s", err);
        }
    }

    /* reshuffle, then remove */
    for (size_t i = n - 1 ; i > 0 ; --i)
    {
        const size_t j = (size_t) rng_below(i + 1);
        const dit_value_t tmp = order[i];
        order[i] = order[j];
        order[j] = tmp;
    }

    for (size_t i = 0 ; i < n ; ++i)
    {
        if (dit_remove(&tree, 10 * order[i], 10 * order[i] + 7) != 1)
        {
            free(order);
            FAIL("removal %zu failed", i);
        }
        if (dit_size(&tree) != n - i - 1)
        {
            free(order);
            FAIL("tree holds %zu intervals after %zu removals",
                    dit_size(&tree), i + 1);
        }
        if ((i % 64) == 0)
        {
            char err[512];
            if (dit_check(&tree, err, sizeof(err)))
            {
                free(order);
                FAIL("incoherent tree after %zu removals: %s", i, err);
            }
        }
    }

    free(order);

    ASSERT(dit_empty(&tree));
    ASSERT_COHERENT(&tree);

    dit_destroy(&tree);
}

//////////
// MAIN //
//////////

int
main(int argc, char ** argv)
{
    uint64_t seed = 0;
    if (argc > 1)
    {
        /* base 10 explicitly: a `0`-prefixed seed is a decimal one, not octal */
        seed = strtoull(argv[1], NULL, 10);
        rng_state = rng_seed(seed);
    }

    printf("running dit tests (seed=%" PRIu64 ", state=%" PRIu64 ", paranoid=%d)\n",
            seed, rng_state, DIT_PARANOID);

    RUN(test_empty);
    RUN(test_insert_single);
    RUN(test_insert_empty_interval);
    RUN(test_insert_overlap_is_rejected);
    RUN(test_ordering);
    RUN(test_hull_augment);
    RUN(test_intersect);
    RUN(test_intersect_early_stop);
    RUN(test_remove);
    RUN(test_remove_reinsert);
    RUN(test_clear);
    RUN(test_balance_sorted_insertions);
    RUN(test_extreme_values);
    RUN(test_check_detects_corruption);
    RUN(test_dump_dot);
    RUN(test_random_against_model);
    RUN(test_random_large);
    RUN(test_random_insert_then_remove);

    printf("\n%d tests, %d checks, %d failures\n",
            tests_run, checks_run, tests_failed);

    return tests_failed ? 1 : 0;
}
