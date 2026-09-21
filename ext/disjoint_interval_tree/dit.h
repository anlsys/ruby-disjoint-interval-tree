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

/*
**  dit - disjoint interval tree
**
**  A self-balancing (AVL) binary search tree whose nodes are pairwise
**  disjoint half-open intervals `[a..b[`, augmented - as in the lp-tree - with
**  the hull of the subtree they root (`augment.hull`), so that intersection
**  queries can prune entire subtrees in O(1).
**
**  Ordering
**      Since stored intervals are pairwise disjoint, ordering them by `a` also
**      orders them by `b`, and the tree is a plain BST on `a`. Searching for
**      the (unique) interval intersecting a point or a range is therefore a
**      deterministic O(log n) descent.
**
**  Contract
**      `dit_insert()` must not be given an interval intersecting an already
**      inserted one. That contract is *checked* at no extra cost during the
**      insertion descent: `DIT_OVERLAP` is returned and the tree is left
**      untouched.
**
**  Objects
**      Every interval carries an opaque object, given at insertion and handed
**      back by every query and traversal. The tree never owns it: it only
**      stores the value, and never reads, copies nor releases whatever it
**      points to. See `dit_remove()` to reclaim objects as intervals go away.
**
**  Complexities, with `n` intervals stored and `k` intervals reported
**      dit_insert      O(log n)
**      dit_intersect   O(k + log n)
**      dit_remove      O(k.log n)
**      dit_at          O(log n)
**      dit_each        O(n)
**      dit_check       O(n)
*/

#ifndef __DIT_H__
# define __DIT_H__

# include <inttypes.h>
# include <stddef.h>
# include <stdint.h>

# ifdef __cplusplus
extern "C" {
# endif

/* Interval bound type. Unsigned so that the whole address space is usable
 * when intervals are memory ranges */
# ifndef DIT_VALUE_T
#  define DIT_VALUE_T       uint64_t
#  define DIT_VALUE_MIN     ((dit_value_t) 0)
#  define DIT_VALUE_MAX     ((dit_value_t) UINT64_MAX)
#  define DIT_VALUE_FMT     PRIu64
# endif /* DIT_VALUE_T */

typedef DIT_VALUE_T dit_value_t;

/* Type of the object associated with each interval. It is opaque to the tree,
 * which only ever stores and hands it back */
# ifndef DIT_OBJECT_T
#  define DIT_OBJECT_T      void *
#  define DIT_OBJECT_NULL   ((dit_object_t) NULL)
# endif /* DIT_OBJECT_T */

typedef DIT_OBJECT_T dit_object_t;

/* Internal consistency assertions. Those only ever fire on a dit bug, never on
 * a caller mistake - caller mistakes are reported through `dit_status_t`.
 * Define `DIT_ASSERT` to override, or `NDEBUG` to compile them out */
# ifndef DIT_ASSERT
#  include <assert.h>
#  define DIT_ASSERT(X) assert(X)
# endif /* DIT_ASSERT */

/* When set, `dit_check()` runs after every mutation: this makes every
 * operation O(n) but catches structural corruptions at the exact call that
 * caused them. Only meant for tests and debugging */
# ifndef DIT_PARANOID
#  define DIT_PARANOID 0
# endif /* DIT_PARANOID */

/* Allocation hooks */
# ifndef DIT_MALLOC
#  include <stdlib.h>
#  define DIT_MALLOC(S) malloc(S)
#  define DIT_FREE(P)   free(P)
# endif /* DIT_MALLOC */

typedef enum
{
    DIT_LEFT        = 0,
    DIT_RIGHT       = 1,
    DIT_N_CHILDREN  = 2
}   dit_direction_t;

typedef enum
{
    /* the operation succeeded */
    DIT_OK          = 0,

    /* `a >= b`: the interval is empty, nothing was done */
    DIT_EMPTY       = 1,

    /* the interval intersects an already inserted one: contract violation,
     * nothing was done */
    DIT_OVERLAP     = 2,

    /* out of memory */
    DIT_NOMEM       = 3
}   dit_status_t;

/* Everything a node caches about the subtree it roots.
 *
 * Augments are derived from the subtree only: they are recomputed bottom-up
 * after every structural change, and are what makes the queries sublinear */
typedef struct  dit_augment_s
{
    /* the englobing interval of the subtree, i.e. the smallest interval
     * including every interval stored in that subtree. Since stored intervals
     * are pairwise disjoint and ordered, it spans exactly from the `a` of the
     * leftmost descendant to the `b` of the rightmost one.
     *
     * This is what lets a query prune a whole subtree in O(1) */
    struct {
        dit_value_t a, b;
    } hull;

    /* height of the subtree, a leaf has 1 */
    int32_t height;

    /* number of nodes in the subtree. 32 bits caps a tree to 2^32 intervals,
     * which already is 224 GiB of nodes */
    uint32_t size;
}               dit_augment_t;

typedef struct  dit_node_s
{
    /* the interval [a..b[ represented by this node, with a < b */
    dit_value_t a, b;

    /* the object associated with that interval, as given to `dit_insert()`.
     * Opaque to the tree, which never dereferences nor releases it */
    dit_object_t obj;

    /* children - `child[DIT_LEFT]` holds intervals entirely before `a`,
     * `child[DIT_RIGHT]` holds intervals entirely after `b` */
    union {
        struct dit_node_s * child[DIT_N_CHILDREN];
        struct {
            struct dit_node_s * left;
            struct dit_node_s * right;
        };
    };

    /* what this node caches about the subtree it roots */
    dit_augment_t augment;
}               dit_node_t;

typedef struct  dit_s
{
    dit_node_t * root;

    /* number of intervals stored */
    size_t n;

    /* >0 while a traversal is in progress: mutating the tree from within an
     * `dit_intersect()` or `dit_each()` callback is forbidden and detected */
    int traversing;
}               dit_t;

/* Interval callback.
 * `[a..b[` is the stored interval and `obj` its associated object, while
 * `user` is the opaque pointer given to the traversal. Return 0 to keep going,
 * non-zero to stop the traversal early - that value is then returned by the
 * traversal routine */
typedef int (*dit_cb_t)(dit_value_t a, dit_value_t b, dit_object_t obj, void * user);

/* Initialize an empty tree. `dit_t` may also be zero-initialized */
void dit_init(dit_t * tree);

/* Free every node. The tree is left initialized and empty.
 * Objects are *not* released: walk the tree with `dit_each()` first if they
 * need to be reclaimed */
void dit_clear(dit_t * tree);

/* Alias of `dit_clear()`, for symmetry with `dit_init()` */
void dit_destroy(dit_t * tree);

/* Number of intervals stored */
size_t dit_size(const dit_t * tree);

/* 1 if no interval is stored, 0 otherwise */
int dit_empty(const dit_t * tree);

/* Height of the tree, 0 if empty */
int dit_height(const dit_t * tree);

/* The englobing interval of the whole tree, read from the root augment.
 * Returns 1 and writes it to `a` and `b`, or returns 0 and leaves them
 * untouched when the tree is empty. O(1) */
int dit_hull(const dit_t * tree, dit_value_t * a, dit_value_t * b);

/* Insert `[a..b[`, associated with the object `obj`.
 * Returns DIT_OK, DIT_EMPTY if `a >= b`, DIT_OVERLAP if `[a..b[` intersects an
 * already inserted interval, DIT_NOMEM on allocation failure. The tree is left
 * unchanged unless DIT_OK is returned */
dit_status_t dit_insert(dit_t * tree, dit_value_t a, dit_value_t b, dit_object_t obj);

/* Return the stored interval intersecting `[a..b[`, or NULL if there is none.
 * If several intervals intersect `[a..b[`, which one is returned is
 * unspecified. The returned node is owned by the tree and invalidated by the
 * next mutation */
const dit_node_t * dit_intersecting(const dit_t * tree, dit_value_t a, dit_value_t b);

/* Return the stored interval containing the point `x`, or NULL */
const dit_node_t * dit_at(const dit_t * tree, dit_value_t x);

/* 1 if at least one stored interval intersects `[a..b[`, 0 otherwise */
int dit_intersect_p(const dit_t * tree, dit_value_t a, dit_value_t b);

/* Invoke `cb` on every stored interval intersecting `[a..b[`, in increasing
 * order. Returns 0, or the first non-zero value returned by `cb`.
 * `cb` must not mutate the tree */
int dit_intersect(dit_t * tree, dit_value_t a, dit_value_t b, dit_cb_t cb, void * user);

/* Invoke `cb` on every stored interval, in increasing order. Returns 0, or the
 * first non-zero value returned by `cb`. `cb` must not mutate the tree */
int dit_each(dit_t * tree, dit_cb_t cb, void * user);

/* Remove every stored interval intersecting `[a..b[`. Returns how many
 * intervals were removed. Removed intervals are removed as a whole: an
 * interval merely overlapping `[a..b[` is *not* split.
 *
 * `cb`, when not NULL, is invoked on each interval just before its node is
 * freed, so that its object can be reclaimed. It is called in increasing
 * order, must not mutate the tree, and - unlike a traversal callback - its
 * return value is ignored: a removal cannot be interrupted halfway */
size_t dit_remove(dit_t * tree, dit_value_t a, dit_value_t b, dit_cb_t cb, void * user);

/* Verify every structural invariant of the tree.
 * Returns 0 if the tree is coherent. Otherwise returns a non-zero value and,
 * if `err` is not NULL, writes a NUL-terminated description of the first
 * violation found into it.
 * This never aborts, so that it can be used from test suites */
int dit_check(const dit_t * tree, char * err, size_t errlen);

/* Dump the tree to `f` (a `FILE *`, void * here to avoid <stdio.h>) in
 * graphviz dot format */
void dit_dump_dot(const dit_t * tree, void * f);

# ifdef __cplusplus
}
# endif

#endif /* __DIT_H__ */
