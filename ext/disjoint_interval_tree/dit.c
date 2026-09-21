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

#include "dit.h"

#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define DIT_MIN(X, Y) (((X) < (Y)) ? (X) : (Y))
#define DIT_MAX(X, Y) (((X) > (Y)) ? (X) : (Y))

/* [a..b[ and [c..d[ intersect - both must be non-empty */
#define DIT_INTERSECTS(A, B, C, D) ((A) < (D) && (C) < (B))

/* run the full coherency check after each mutation, see `DIT_PARANOID` */
#if DIT_PARANOID
# define DIT_CHECK_PARANOID(T)                                              \
    do {                                                                    \
        char __err[512];                                                    \
        if (dit_check((T), __err, sizeof(__err)))                           \
        {                                                                   \
            fprintf(stderr, "%s:%d: incoherent tree in `%s`: %s\n",         \
                    __FILE__, __LINE__, __func__, __err);                   \
            DIT_ASSERT(0 && "incoherent tree");                             \
        }                                                                   \
    } while (0)
#else /* DIT_PARANOID */
# define DIT_CHECK_PARANOID(T) ((void) 0)
#endif /* DIT_PARANOID */

////////////
// NODES  //
////////////

static inline int32_t
dit_node_height(const dit_node_t * node)
{
    return node ? node->augment.height : 0;
}

static inline uint32_t
dit_node_size(const dit_node_t * node)
{
    return node ? node->augment.size : 0;
}

/* balance factor: >0 means the left subtree is the deepest */
static inline int32_t
dit_node_balance(const dit_node_t * node)
{
    DIT_ASSERT(node);
    return dit_node_height(node->left) - dit_node_height(node->right);
}

/* Recompute every augment of `node` from its - assumed up to date - children.
 * Must be called bottom-up after any structural change */
static inline void
dit_node_refresh_augment(dit_node_t * node)
{
    DIT_ASSERT(node);
    DIT_ASSERT(node->a < node->b);

    const dit_node_t * l = node->left;
    const dit_node_t * r = node->right;

    const int32_t hl = dit_node_height(l);
    const int32_t hr = dit_node_height(r);

    node->augment.height = 1 + DIT_MAX(hl, hr);
    node->augment.size   = 1 + dit_node_size(l) + dit_node_size(r);

    /* englobing interval of the subtree, as the lp-tree `includes.hyperrect` */
    node->augment.hull.a = node->a;
    node->augment.hull.b = node->b;
    if (l)
    {
        node->augment.hull.a = DIT_MIN(node->augment.hull.a, l->augment.hull.a);
        node->augment.hull.b = DIT_MAX(node->augment.hull.b, l->augment.hull.b);
    }
    if (r)
    {
        node->augment.hull.a = DIT_MIN(node->augment.hull.a, r->augment.hull.a);
        node->augment.hull.b = DIT_MAX(node->augment.hull.b, r->augment.hull.b);
    }

    /* intervals being disjoint and ordered on `a`, they are also ordered on
     * `b`: the hull is exactly [leftmost->a .. rightmost->b[ */
    DIT_ASSERT(node->augment.hull.a == (l ? l->augment.hull.a : node->a));
    DIT_ASSERT(node->augment.hull.b == (r ? r->augment.hull.b : node->b));
    DIT_ASSERT(node->augment.hull.a <= node->a);
    DIT_ASSERT(node->b <= node->augment.hull.b);
}

static inline dit_node_t *
dit_node_new(dit_value_t a, dit_value_t b, dit_object_t obj)
{
    DIT_ASSERT(a < b);

    dit_node_t * node = (dit_node_t *) DIT_MALLOC(sizeof(dit_node_t));
    if (node == NULL)
        return NULL;

    node->a              = a;
    node->b              = b;
    node->obj            = obj;
    node->left           = NULL;
    node->right          = NULL;
    node->augment.hull.a = a;
    node->augment.hull.b = b;
    node->augment.height = 1;
    node->augment.size   = 1;

    return node;
}

static void
dit_node_release(dit_node_t * node)
{
    if (node == NULL)
        return ;
    dit_node_release(node->left);
    dit_node_release(node->right);
    DIT_FREE(node);
}

///////////////
// ROTATIONS //
///////////////

/**
 *      A              B
 *     / \            / \
 *    B   C    ->    D   A
 *   / \                / \
 *  D   E              E   C
 *
 *  Returns the new subtree root `B`
 */
static inline dit_node_t *
dit_rotate_right(dit_node_t * A)
{
    DIT_ASSERT(A && A->left);

    dit_node_t * B = A->left;
    dit_node_t * E = B->right;

    B->right = A;
    A->left  = E;

    dit_node_refresh_augment(A);
    dit_node_refresh_augment(B);

    return B;
}

/**
 *      C              A
 *     / \            / \
 *    A   E    <-    B   C
 *   / \                / \
 *  B   D              D   E
 *
 *  Returns the new subtree root `C`
 */
static inline dit_node_t *
dit_rotate_left(dit_node_t * A)
{
    DIT_ASSERT(A && A->right);

    dit_node_t * C = A->right;
    dit_node_t * D = C->left;

    C->left  = A;
    A->right = D;

    dit_node_refresh_augment(A);
    dit_node_refresh_augment(C);

    return C;
}

/* Restore the AVL invariant on `node`, whose augments must already be up to
 * date and whose children must be valid AVL subtrees. Returns the new root of
 * the subtree */
static inline dit_node_t *
dit_rebalance(dit_node_t * node)
{
    DIT_ASSERT(node);

    const int32_t bf = dit_node_balance(node);

    /* a single insertion/deletion can only break the balance by 1 */
    DIT_ASSERT(-2 <= bf && bf <= 2);

    if (bf > 1)
    {
        DIT_ASSERT(node->left);
        if (dit_node_balance(node->left) < 0)
            node->left = dit_rotate_left(node->left);
        return dit_rotate_right(node);
    }

    if (bf < -1)
    {
        DIT_ASSERT(node->right);
        if (dit_node_balance(node->right) > 0)
            node->right = dit_rotate_right(node->right);
        return dit_rotate_left(node);
    }

    return node;
}

//////////////////
// CONSTRUCTION //
//////////////////

void
dit_init(dit_t * tree)
{
    DIT_ASSERT(tree);
    tree->root       = NULL;
    tree->n          = 0;
    tree->traversing = 0;
}

void
dit_clear(dit_t * tree)
{
    DIT_ASSERT(tree);
    DIT_ASSERT(tree->traversing == 0 && "cannot mutate the tree while traversing it");

    dit_node_release(tree->root);
    tree->root = NULL;
    tree->n    = 0;
}

void
dit_destroy(dit_t * tree)
{
    dit_clear(tree);
}

size_t
dit_size(const dit_t * tree)
{
    DIT_ASSERT(tree);
    DIT_ASSERT(tree->n == (size_t) dit_node_size(tree->root));
    return tree->n;
}

int
dit_empty(const dit_t * tree)
{
    DIT_ASSERT(tree);
    return tree->root == NULL;
}

int
dit_height(const dit_t * tree)
{
    DIT_ASSERT(tree);
    return (int) dit_node_height(tree->root);
}

int
dit_hull(const dit_t * tree, dit_value_t * a, dit_value_t * b)
{
    DIT_ASSERT(tree);
    DIT_ASSERT(a && b);

    if (tree->root == NULL)
        return 0;

    /* the root augment already englobes every stored interval */
    *a = tree->root->augment.hull.a;
    *b = tree->root->augment.hull.b;

    DIT_ASSERT(*a < *b);

    return 1;
}

////////////
// INSERT //
////////////

static dit_node_t *
dit_insert_from(
    dit_node_t * node,
    dit_value_t a,
    dit_value_t b,
    dit_object_t obj,
    dit_status_t * status
) {
    if (node == NULL)
    {
        dit_node_t * created = dit_node_new(a, b, obj);
        *status = created ? DIT_OK : DIT_NOMEM;
        return created;
    }

    /* case (1) - [a..b[ entirely before this node */
    if (b <= node->a)
        node->left = dit_insert_from(node->left, a, b, obj, status);

    /* case (2) - [a..b[ entirely after this node */
    else if (a >= node->b)
        node->right = dit_insert_from(node->right, a, b, obj, status);

    /* case (3) - contract violation, [a..b[ intersect this node */
    else
    {
        DIT_ASSERT(DIT_INTERSECTS(a, b, node->a, node->b));
        *status = DIT_OVERLAP;
        return node;
    }

    /* nothing was inserted below: no augment to refresh, no rebalancing */
    if (*status != DIT_OK)
        return node;

    dit_node_refresh_augment(node);
    return dit_rebalance(node);
}

dit_status_t
dit_insert(dit_t * tree, dit_value_t a, dit_value_t b, dit_object_t obj)
{
    DIT_ASSERT(tree);
    DIT_ASSERT(tree->traversing == 0 && "cannot mutate the tree while traversing it");

    if (a >= b)
        return DIT_EMPTY;

    dit_status_t status = DIT_OK;
    dit_node_t * root = dit_insert_from(tree->root, a, b, obj, &status);

    if (status != DIT_OK)
        return status;

    tree->root = root;
    tree->n   += 1;

    DIT_ASSERT(tree->n == (size_t) dit_node_size(tree->root));
    DIT_CHECK_PARANOID(tree);

    return DIT_OK;
}

////////////
// SEARCH //
////////////

/* Intervals being pairwise disjoint, they are totally ordered: the descent
 * towards an interval intersecting [a..b[ is deterministic */
static inline dit_node_t *
dit_intersecting_from(dit_node_t * node, dit_value_t a, dit_value_t b)
{
    while (node)
    {
        if (b <= node->a)
            node = node->left;
        else if (a >= node->b)
            node = node->right;
        else
        {
            DIT_ASSERT(DIT_INTERSECTS(a, b, node->a, node->b));
            return node;
        }
    }
    return NULL;
}

/* Same, but always returning the *smallest* such interval. It cannot stop as
 * soon as it finds a match, so it always walks a full root-to-leaf path -
 * still O(log n), just without the early exit */
static inline dit_node_t *
dit_leftmost_intersecting_from(dit_node_t * node, dit_value_t a, dit_value_t b)
{
    dit_node_t * leftmost = NULL;

    /* leftmost node ending after `a`. Intervals being disjoint and ordered,
     * those intersecting [a..b[ form a contiguous run, so that node is the
     * first of the run - when it starts before `b` */
    while (node)
    {
        if (node->b > a)
        {
            leftmost = node;
            node = node->left;
        }
        else
            node = node->right;
    }

    if (leftmost && leftmost->a < b)
    {
        DIT_ASSERT(DIT_INTERSECTS(a, b, leftmost->a, leftmost->b));
        return leftmost;
    }

    return NULL;
}

const dit_node_t *
dit_intersecting(const dit_t * tree, dit_value_t a, dit_value_t b)
{
    DIT_ASSERT(tree);
    if (a >= b)
        return NULL;
    return dit_intersecting_from(tree->root, a, b);
}

const dit_node_t *
dit_at(const dit_t * tree, dit_value_t x)
{
    DIT_ASSERT(tree);

    /* intervals are half-open and bounded by DIT_VALUE_MAX, so no stored
     * interval [a..b[ may ever contain DIT_VALUE_MAX */
    if (x == DIT_VALUE_MAX)
        return NULL;

    return dit_intersecting_from(tree->root, x, x + 1);
}

int
dit_intersect_p(const dit_t * tree, dit_value_t a, dit_value_t b)
{
    return dit_intersecting(tree, a, b) != NULL;
}

///////////////
// TRAVERSAL //
///////////////

static int
dit_intersect_from(
    dit_node_t * node,
    dit_value_t a,
    dit_value_t b,
    dit_cb_t cb,
    void * user
) {
    if (node == NULL)
        return 0;

    /* augment pruning: no interval of this subtree can intersect [a..b[ */
    if (!DIT_INTERSECTS(a, b, node->augment.hull.a, node->augment.hull.b))
        return 0;

    int r;

    /* in-order traversal, so that intervals are reported in increasing order */
    if ((r = dit_intersect_from(node->left, a, b, cb, user)) != 0)
        return r;

    if (DIT_INTERSECTS(a, b, node->a, node->b))
    {
        if ((r = cb(node->a, node->b, node->obj, user)) != 0)
            return r;
    }

    return dit_intersect_from(node->right, a, b, cb, user);
}

int
dit_intersect(dit_t * tree, dit_value_t a, dit_value_t b, dit_cb_t cb, void * user)
{
    DIT_ASSERT(tree);
    DIT_ASSERT(cb);

    if (a >= b)
        return 0;

    tree->traversing += 1;
    const int r = dit_intersect_from(tree->root, a, b, cb, user);
    tree->traversing -= 1;

    DIT_ASSERT(tree->traversing >= 0);

    return r;
}

static int
dit_each_from(dit_node_t * node, dit_cb_t cb, void * user)
{
    if (node == NULL)
        return 0;

    int r;

    if ((r = dit_each_from(node->left, cb, user)) != 0)
        return r;

    if ((r = cb(node->a, node->b, node->obj, user)) != 0)
        return r;

    return dit_each_from(node->right, cb, user);
}

int
dit_each(dit_t * tree, dit_cb_t cb, void * user)
{
    DIT_ASSERT(tree);
    DIT_ASSERT(cb);

    tree->traversing += 1;
    const int r = dit_each_from(tree->root, cb, user);
    tree->traversing -= 1;

    DIT_ASSERT(tree->traversing >= 0);

    return r;
}

////////////
// REMOVE //
////////////

/* Detach the leftmost node of the subtree, store it into `*out`, and return
 * the new subtree root */
static dit_node_t *
dit_detach_min(dit_node_t * node, dit_node_t ** out)
{
    DIT_ASSERT(node);

    if (node->left == NULL)
    {
        *out = node;
        return node->right;
    }

    node->left = dit_detach_min(node->left, out);
    dit_node_refresh_augment(node);
    return dit_rebalance(node);
}

/* Remove the node whose interval starts at `key`, which must exist, and return
 * the new subtree root */
static dit_node_t *
dit_remove_from(dit_node_t * node, dit_value_t key)
{
    DIT_ASSERT(node && "removing an interval that is not in the tree");

    if (key < node->a)
        node->left = dit_remove_from(node->left, key);
    else if (key > node->a)
        node->right = dit_remove_from(node->right, key);
    else
    {
        /* at most one child: splice it in */
        if (node->left == NULL || node->right == NULL)
        {
            dit_node_t * child = node->left ? node->left : node->right;

            /* an AVL node with a single child has a leaf as child */
            DIT_ASSERT(child == NULL || (child->left == NULL && child->right == NULL));

            DIT_FREE(node);
            return child;
        }

        /* two children: replace the interval with its in-order successor's,
         * then remove that successor from the right subtree.
         *
         * The object travels with the interval it belongs to: forgetting it
         * here would silently hand the successor's interval the object of the
         * node being deleted */
        dit_node_t * successor;
        node->right = dit_detach_min(node->right, &successor);

        DIT_ASSERT(successor && successor->left == NULL);
        DIT_ASSERT(node->a < successor->a);

        node->a   = successor->a;
        node->b   = successor->b;
        node->obj = successor->obj;

        DIT_FREE(successor);
    }

    dit_node_refresh_augment(node);
    return dit_rebalance(node);
}

size_t
dit_remove(dit_t * tree, dit_value_t a, dit_value_t b, dit_cb_t cb, void * user)
{
    DIT_ASSERT(tree);
    DIT_ASSERT(tree->traversing == 0 && "cannot mutate the tree while traversing it");

    if (a >= b)
        return 0;

    size_t n = 0;

    /* each iteration is a O(log n) descent plus a O(log n) deletion. Removing
     * the smallest match every time reports them in increasing order */
    for (;;)
    {
        dit_node_t * node = dit_leftmost_intersecting_from(tree->root, a, b);
        if (node == NULL)
            break ;

        if (cb)
        {
            /* the callback may read the tree, but not mutate it: it would
             * free the very node about to be deleted */
            tree->traversing += 1;
            cb(node->a, node->b, node->obj, user);
            tree->traversing -= 1;
        }

        tree->root = dit_remove_from(tree->root, node->a);

        DIT_ASSERT(tree->n > 0);
        tree->n -= 1;
        n       += 1;
    }

    DIT_ASSERT(tree->n == (size_t) dit_node_size(tree->root));
    DIT_ASSERT(dit_intersecting_from(tree->root, a, b) == NULL);
    DIT_CHECK_PARANOID(tree);

    return n;
}

///////////////////////
// COHERENCY CHECKS  //
///////////////////////

typedef struct
{
    char * err;
    size_t errlen;
    int failed;

    /* number of nodes visited so far */
    size_t count;

    /* end of the previously visited interval, in-order */
    dit_value_t prev_b;
    int has_prev;
}   dit_check_t;

#ifdef __GNUC__
__attribute__((format(printf, 2, 3)))
#endif /* __GNUC__ */
static void
dit_check_fail(dit_check_t * ctx, const char * fmt, ...)
{
    if (ctx->failed)
        return ;

    ctx->failed = 1;

    if (ctx->err && ctx->errlen)
    {
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(ctx->err, ctx->errlen, fmt, ap);
        va_end(ap);
    }
}

#define DIT_CHECK_THAT(CTX, COND, ...)                  \
    do {                                                \
        if (!(COND))                                    \
        {                                               \
            dit_check_fail(CTX, __VA_ARGS__);           \
            return ;                                    \
        }                                               \
    } while (0)

/* Recursively check `node`, knowing every interval of that subtree must be
 * included in [lo..hi[ */
static void
dit_check_from(
    dit_check_t * ctx,
    const dit_node_t * node,
    dit_value_t lo,
    dit_value_t hi,
    int depth
) {
    if (node == NULL || ctx->failed)
        return ;

    DIT_CHECK_THAT(ctx, depth < 128,
            "tree is deeper than 128, it is probably cyclic");

    /* 1. intervals are non-empty */
    DIT_CHECK_THAT(ctx, node->a < node->b,
            "node [%" DIT_VALUE_FMT "..%" DIT_VALUE_FMT "[ is empty",
            node->a, node->b);

    /* 2. binary search tree ordering, which - given the bounds narrowing at
     * each level - also proves that stored intervals are pairwise disjoint */
    DIT_CHECK_THAT(ctx, lo <= node->a && node->b <= hi,
            "node [%" DIT_VALUE_FMT "..%" DIT_VALUE_FMT "[ is not within its "
            "expected bounds [%" DIT_VALUE_FMT "..%" DIT_VALUE_FMT "[",
            node->a, node->b, lo, hi);

    dit_check_from(ctx, node->left, lo, node->a, depth + 1);
    if (ctx->failed)
        return ;

    /* 3. in-order traversal yields increasing, non-overlapping intervals */
    if (ctx->has_prev)
    {
        DIT_CHECK_THAT(ctx, ctx->prev_b <= node->a,
                "node [%" DIT_VALUE_FMT "..%" DIT_VALUE_FMT "[ overlaps its "
                "in-order predecessor, which ends at %" DIT_VALUE_FMT,
                node->a, node->b, ctx->prev_b);
    }
    ctx->has_prev = 1;
    ctx->prev_b   = node->b;
    ctx->count   += 1;

    dit_check_from(ctx, node->right, node->b, hi, depth + 1);
    if (ctx->failed)
        return ;

    const dit_node_t * l = node->left;
    const dit_node_t * r = node->right;

    /* 4. height augment */
    const int32_t hl = dit_node_height(l);
    const int32_t hr = dit_node_height(r);
    DIT_CHECK_THAT(ctx, node->augment.height == 1 + DIT_MAX(hl, hr),
            "node [%" DIT_VALUE_FMT "..%" DIT_VALUE_FMT "[ has height %d "
            "instead of %d",
            node->a, node->b, (int) node->augment.height, (int) (1 + DIT_MAX(hl, hr)));

    /* 5. AVL balance */
    DIT_CHECK_THAT(ctx, -1 <= hl - hr && hl - hr <= 1,
            "node [%" DIT_VALUE_FMT "..%" DIT_VALUE_FMT "[ is unbalanced "
            "(left height %d, right height %d)",
            node->a, node->b, (int) hl, (int) hr);

    /* 6. size augment */
    const uint32_t size = 1 + dit_node_size(l) + dit_node_size(r);
    DIT_CHECK_THAT(ctx, node->augment.size == size,
            "node [%" DIT_VALUE_FMT "..%" DIT_VALUE_FMT "[ has size %u "
            "instead of %u",
            node->a, node->b, node->augment.size, size);

    /* 7. the hull augment englobes the whole subtree */
    dit_value_t ha = node->a;
    dit_value_t hb = node->b;
    if (l)
    {
        ha = DIT_MIN(ha, l->augment.hull.a);
        hb = DIT_MAX(hb, l->augment.hull.b);
    }
    if (r)
    {
        ha = DIT_MIN(ha, r->augment.hull.a);
        hb = DIT_MAX(hb, r->augment.hull.b);
    }
    DIT_CHECK_THAT(ctx, node->augment.hull.a == ha && node->augment.hull.b == hb,
            "node [%" DIT_VALUE_FMT "..%" DIT_VALUE_FMT "[ has hull "
            "[%" DIT_VALUE_FMT "..%" DIT_VALUE_FMT "[ instead of "
            "[%" DIT_VALUE_FMT "..%" DIT_VALUE_FMT "[",
            node->a, node->b, node->augment.hull.a, node->augment.hull.b, ha, hb);

    /* 8. the hull spans exactly from the leftmost to the rightmost interval */
    DIT_CHECK_THAT(ctx, node->augment.hull.a == (l ? l->augment.hull.a : node->a),
            "node [%" DIT_VALUE_FMT "..%" DIT_VALUE_FMT "[ does not start its "
            "hull at its leftmost descendant",
            node->a, node->b);
    DIT_CHECK_THAT(ctx, node->augment.hull.b == (r ? r->augment.hull.b : node->b),
            "node [%" DIT_VALUE_FMT "..%" DIT_VALUE_FMT "[ does not end its "
            "hull at its rightmost descendant",
            node->a, node->b);
}

int
dit_check(const dit_t * tree, char * err, size_t errlen)
{
    if (err && errlen)
        err[0] = '\0';

    if (tree == NULL)
    {
        if (err && errlen)
            snprintf(err, errlen, "tree is NULL");
        return 1;
    }

    dit_check_t ctx;
    ctx.err      = err;
    ctx.errlen   = errlen;
    ctx.failed   = 0;
    ctx.count    = 0;
    ctx.prev_b   = 0;
    ctx.has_prev = 0;

    dit_check_from(&ctx, tree->root, DIT_VALUE_MIN, DIT_VALUE_MAX, 0);
    if (ctx.failed)
        return 1;

    /* 9. the cached cardinality matches the actual number of nodes */
    if (ctx.count != tree->n)
    {
        dit_check_fail(&ctx, "tree holds %zu nodes but reports %zu",
                ctx.count, tree->n);
        return 1;
    }

    if (tree->root && (size_t) tree->root->augment.size != tree->n)
    {
        dit_check_fail(&ctx, "root size augment is %u but the tree holds %zu nodes",
                tree->root->augment.size, tree->n);
        return 1;
    }

    /* 10. an AVL tree of n nodes is at most 1.4405*log2(n+2)-0.3277 deep, the
     * bound below is looser but does not need any floating point arithmetic */
    {
        int log2n = 0;
        while (((size_t) 1 << (log2n + 1)) <= ctx.count + 1)
            ++log2n;

        const int height = (int) dit_node_height(tree->root);
        if (height > 2 * (log2n + 1))
        {
            dit_check_fail(&ctx, "tree of %zu nodes is %d deep, expected at most %d",
                    ctx.count, height, 2 * (log2n + 1));
            return 1;
        }
    }

    /* 11. no traversal may be leaking */
    if (tree->traversing < 0)
    {
        dit_check_fail(&ctx, "negative traversal counter (%d)", tree->traversing);
        return 1;
    }

    return 0;
}

//////////
// DUMP //
//////////

static void
dit_dump_dot_from(const dit_node_t * node, FILE * f)
{
    if (node == NULL)
        return ;

    fprintf(f, "    N%p[shape=record, label=\"{[%" DIT_VALUE_FMT "..%" DIT_VALUE_FMT "[",
            (const void *) node, node->a, node->b);
    fprintf(f, "|obj %p", (const void *) (uintptr_t) node->obj);
    fprintf(f, "|hull [%" DIT_VALUE_FMT "..%" DIT_VALUE_FMT "[",
            node->augment.hull.a, node->augment.hull.b);
    fprintf(f, "|h=%d, n=%u}\"] ;\n", (int) node->augment.height, node->augment.size);

    for (int dir = DIT_LEFT ; dir < DIT_N_CHILDREN ; ++dir)
    {
        const dit_node_t * child = node->child[dir];
        if (child)
        {
            dit_dump_dot_from(child, f);
            fprintf(f, "    N%p->N%p [label=\"%s\"] ;\n",
                    (const void *) node, (const void *) child,
                    (dir == DIT_LEFT) ? "l" : "r");
        }
    }
}

void
dit_dump_dot(const dit_t * tree, void * f)
{
    FILE * stream = (FILE *) f;

    fprintf(stream, "digraph dit {\n");
    if (tree && tree->root)
        dit_dump_dot_from(tree->root, stream);
    fprintf(stream, "}\n");
}
