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

/* Ruby bindings for the disjoint interval tree */

#include <ruby.h>

#include <inttypes.h>

#include "dit.h"

static VALUE cTree;
static VALUE eError;
static VALUE eOverlapError;
static VALUE eCorruptedError;

static VALUE rb_dit_size(VALUE self);

/* not exposed as such by every supported ruby version */
#ifndef RBIGNUM_NEGATIVE_P
# define RBIGNUM_NEGATIVE_P(B) (rb_big_sign(B) == 0)
#endif /* RBIGNUM_NEGATIVE_P */

/////////////////////
// TYPED DATA GLUE //
/////////////////////

static void
rb_dit_free(void * ptr)
{
    dit_t * tree = (dit_t *) ptr;

    /* a traversal cannot be in progress here: the tree is unreachable */
    tree->traversing = 0;
    dit_destroy(tree);
    xfree(tree);
}

static size_t
rb_dit_memsize(const void * ptr)
{
    const dit_t * tree = (const dit_t *) ptr;
    return sizeof(dit_t) + tree->n * sizeof(dit_node_t);
}

static const rb_data_type_t rb_dit_type = {
    .wrap_struct_name = "DisjointIntervalTree",
    .function = {
        .dmark    = NULL,
        .dfree    = rb_dit_free,
        .dsize    = rb_dit_memsize,
    },
    .parent   = NULL,
    .data     = NULL,
    .flags    = RUBY_TYPED_FREE_IMMEDIATELY,
};

static inline dit_t *
rb_dit_get(VALUE self)
{
    dit_t * tree;
    TypedData_Get_Struct(self, dit_t, &rb_dit_type, tree);
    return tree;
}

/* Mutating the tree from within `each`/`intersect` would invalidate the
 * traversal cursor, and possibly free the node being visited */
static inline dit_t *
rb_dit_get_mutable(VALUE self)
{
    dit_t * tree = rb_dit_get(self);

    rb_check_frozen(self);

    if (tree->traversing)
        rb_raise(eError, "cannot modify a DisjointIntervalTree while traversing it");

    return tree;
}

/* Interval bounds are unsigned 64 bits integers.
 *
 * `NUM2ULL` cannot be used as-is: it mirrors C conversion rules and silently
 * wraps negative values around, which would turn a `-1` typo into a
 * `18446744073709551615` bound. Floats are rejected too, as truncating them
 * silently would be just as surprising */
static inline dit_value_t
rb_dit_value(VALUE v)
{
    if (RB_FIXNUM_P(v))
    {
        const long l = FIX2LONG(v);
        if (l < 0)
            rb_raise(rb_eRangeError,
                    "interval bounds must be in [0..2**64[, got %ld", l);
        return (dit_value_t) l;
    }

    if (RB_TYPE_P(v, T_BIGNUM))
    {
        if (RBIGNUM_NEGATIVE_P(v))
            rb_raise(rb_eRangeError,
                    "interval bounds must be in [0..2**64[, got %"PRIsVALUE, v);

        /* raises RangeError if it does not fit in 64 bits */
        return (dit_value_t) rb_big2ull(v);
    }

    rb_raise(rb_eTypeError, "no implicit conversion of %"PRIsVALUE" into Integer",
            rb_obj_class(v));
}

static inline VALUE
rb_dit_interval(dit_value_t a, dit_value_t b)
{
    return rb_assoc_new(ULL2NUM(a), ULL2NUM(b));
}

static VALUE
rb_dit_alloc(VALUE klass)
{
    dit_t * tree;
    VALUE self = TypedData_Make_Struct(klass, dit_t, &rb_dit_type, tree);
    dit_init(tree);
    return self;
}

////////////////
// CALLBACKS  //
////////////////

static int
rb_dit_cb_yield(dit_value_t a, dit_value_t b, void * user)
{
    (void) user;
    rb_yield_values(2, ULL2NUM(a), ULL2NUM(b));
    return 0;
}

static int
rb_dit_cb_push(dit_value_t a, dit_value_t b, void * user)
{
    rb_ary_push((VALUE) user, rb_dit_interval(a, b));
    return 0;
}

/* Traversals must restore `tree->traversing` even if the block raises or
 * breaks, hence the `rb_ensure` dance */
typedef struct
{
    dit_t * tree;
    dit_value_t a, b;
    dit_cb_t cb;
    void * user;

    /* whole tree instead of the [a..b[ range */
    int all;

    /* value of `tree->traversing` before the traversal started */
    int traversing;
}   rb_dit_traversal_t;

static VALUE
rb_dit_traverse_body(VALUE arg)
{
    rb_dit_traversal_t * t = (rb_dit_traversal_t *) arg;

    if (t->all)
        dit_each(t->tree, t->cb, t->user);
    else
        dit_intersect(t->tree, t->a, t->b, t->cb, t->user);

    return Qnil;
}

static VALUE
rb_dit_traverse_ensure(VALUE arg)
{
    rb_dit_traversal_t * t = (rb_dit_traversal_t *) arg;
    t->tree->traversing = t->traversing;
    return Qnil;
}

static void
rb_dit_traverse(dit_t * tree, int all, dit_value_t a, dit_value_t b, dit_cb_t cb, void * user)
{
    rb_dit_traversal_t t;
    t.tree       = tree;
    t.a          = a;
    t.b          = b;
    t.cb         = cb;
    t.user       = user;
    t.all        = all;
    t.traversing = tree->traversing;

    rb_ensure(rb_dit_traverse_body, (VALUE) &t, rb_dit_traverse_ensure, (VALUE) &t);
}

/////////////
// METHODS //
/////////////

/*
 *  call-seq:
 *      DisjointIntervalTree.new                      -> tree
 *      DisjointIntervalTree.new([[a, b], [c, d]])    -> tree
 *
 *  Create a tree, optionally filled with the given intervals.
 */
static VALUE
rb_dit_initialize(int argc, VALUE * argv, VALUE self)
{
    VALUE intervals;
    rb_scan_args(argc, argv, "01", &intervals);

    if (!NIL_P(intervals))
    {
        intervals = rb_check_array_type(intervals);
        if (NIL_P(intervals))
            rb_raise(rb_eTypeError, "expected an array of [a, b] intervals");

        for (long i = 0 ; i < RARRAY_LEN(intervals) ; ++i)
        {
            VALUE interval = rb_check_array_type(rb_ary_entry(intervals, i));
            if (NIL_P(interval) || RARRAY_LEN(interval) != 2)
                rb_raise(rb_eArgError, "expected an [a, b] interval at index %ld", i);
            rb_funcall(self, rb_intern("insert"), 2,
                    rb_ary_entry(interval, 0), rb_ary_entry(interval, 1));
        }
    }

    return self;
}

/* Insert `[a..b[`, returns `DIT_OK`, or raises unless `soft` is set */
static dit_status_t
rb_dit_do_insert(VALUE self, VALUE va, VALUE vb, int soft)
{
    dit_t * tree = rb_dit_get_mutable(self);

    const dit_value_t a = rb_dit_value(va);
    const dit_value_t b = rb_dit_value(vb);

    const dit_status_t status = dit_insert(tree, a, b);

    switch (status)
    {
        case DIT_OK:
            break ;

        case DIT_EMPTY:
            rb_raise(rb_eArgError,
                    "empty interval [%"PRIu64"..%"PRIu64"[, expected a < b", a, b);

        case DIT_OVERLAP:
            if (!soft)
            {
                const dit_node_t * other = dit_intersecting(tree, a, b);
                rb_raise(eOverlapError,
                        "[%"PRIu64"..%"PRIu64"[ overlaps [%"PRIu64"..%"PRIu64"[",
                        a, b, other ? other->a : 0, other ? other->b : 0);
            }
            break ;

        case DIT_NOMEM:
            rb_memerror();

        default:
            rb_raise(eError, "unexpected insertion status %d", (int) status);
    }

    return status;
}

/*
 *  call-seq:
 *      tree.insert(a, b) -> self
 *
 *  Insert the half-open interval `[a..b[`.
 *
 *  It is a usage contract that `[a..b[` must not overlap an already inserted
 *  interval: an OverlapError is raised - and the tree is left unchanged - if
 *  it does. Adjacent intervals such as `[0..10[` and `[10..20[` do not
 *  overlap.
 */
static VALUE
rb_dit_insert(VALUE self, VALUE va, VALUE vb)
{
    rb_dit_do_insert(self, va, vb, 0);
    return self;
}

/*
 *  call-seq:
 *      tree.insert?(a, b) -> true or false
 *
 *  Same as #insert, but returns false instead of raising when `[a..b[`
 *  overlaps an already inserted interval.
 */
static VALUE
rb_dit_insert_p(VALUE self, VALUE va, VALUE vb)
{
    return (rb_dit_do_insert(self, va, vb, 1) == DIT_OK) ? Qtrue : Qfalse;
}

/*
 *  call-seq:
 *      tree.intersect(a, b) { |x, y| ... } -> self
 *      tree.intersect(a, b)                -> array
 *
 *  Yield every stored interval intersecting `[a..b[`, in increasing order.
 *  Without a block, return them as an array of `[x, y]` pairs.
 *
 *  The tree must not be modified from within the block.
 */
static VALUE
rb_dit_intersect(VALUE self, VALUE va, VALUE vb)
{
    dit_t * tree = rb_dit_get(self);

    const dit_value_t a = rb_dit_value(va);
    const dit_value_t b = rb_dit_value(vb);

    if (rb_block_given_p())
    {
        rb_dit_traverse(tree, 0, a, b, rb_dit_cb_yield, NULL);
        return self;
    }

    VALUE ary = rb_ary_new();
    rb_dit_traverse(tree, 0, a, b, rb_dit_cb_push, (void *) ary);
    return ary;
}

/*
 *  call-seq:
 *      tree.intersect?(a, b) -> true or false
 *
 *  Whether at least one stored interval intersect `[a..b[`. O(log n).
 */
static VALUE
rb_dit_intersect_p(VALUE self, VALUE va, VALUE vb)
{
    const dit_t * tree = rb_dit_get(self);
    return dit_intersect_p(tree, rb_dit_value(va), rb_dit_value(vb)) ? Qtrue : Qfalse;
}

/*
 *  call-seq:
 *      tree.remove(a, b)                    -> integer
 *      tree.remove(a, b) { |x, y| ... }     -> integer
 *
 *  Remove every stored interval intersecting `[a..b[` and return how many were
 *  removed. If a block is given, it is called with each removed interval once
 *  the removal is done.
 *
 *  Intervals are removed as a whole: an interval only partially covered by
 *  `[a..b[` is removed entirely, never split.
 */
static VALUE
rb_dit_remove(VALUE self, VALUE va, VALUE vb)
{
    dit_t * tree = rb_dit_get_mutable(self);

    const dit_value_t a = rb_dit_value(va);
    const dit_value_t b = rb_dit_value(vb);

    /* snapshot the intervals first, so that the block cannot observe - nor
     * corrupt - a tree being modified */
    VALUE removed = Qnil;
    if (rb_block_given_p())
    {
        removed = rb_ary_new();
        rb_dit_traverse(tree, 0, a, b, rb_dit_cb_push, (void *) removed);
    }

    const size_t n = dit_remove(tree, a, b);

    if (!NIL_P(removed))
    {
        RUBY_ASSERT((size_t) RARRAY_LEN(removed) == n);
        for (long i = 0 ; i < RARRAY_LEN(removed) ; ++i)
        {
            VALUE interval = rb_ary_entry(removed, i);
            rb_yield_values(2, rb_ary_entry(interval, 0), rb_ary_entry(interval, 1));
        }
    }

    return ULL2NUM((unsigned long long) n);
}

static VALUE
rb_dit_enum_size(VALUE self, VALUE args, VALUE eobj)
{
    (void) args;
    (void) eobj;
    return rb_dit_size(self);
}

/*
 *  call-seq:
 *      tree.each { |a, b| ... } -> self
 *      tree.each                -> enumerator
 *
 *  Yield every stored interval, in increasing order.
 */
static VALUE
rb_dit_each(VALUE self)
{
    dit_t * tree = rb_dit_get(self);

    RETURN_SIZED_ENUMERATOR(self, 0, 0, rb_dit_enum_size);

    rb_dit_traverse(tree, 1, 0, 0, rb_dit_cb_yield, NULL);

    return self;
}

/*
 *  call-seq:
 *      tree.to_a -> array
 *
 *  Every stored interval, in increasing order, as an array of `[a, b]` pairs.
 */
static VALUE
rb_dit_to_a(VALUE self)
{
    dit_t * tree = rb_dit_get(self);
    VALUE ary = rb_ary_new_capa((long) dit_size(tree));
    rb_dit_traverse(tree, 1, 0, 0, rb_dit_cb_push, (void *) ary);
    return ary;
}

/*
 *  call-seq:
 *      tree.at(x) -> [a, b] or nil
 *
 *  The stored interval containing the point `x`, or nil. O(log n).
 */
static VALUE
rb_dit_at(VALUE self, VALUE vx)
{
    const dit_t * tree = rb_dit_get(self);
    const dit_node_t * node = dit_at(tree, rb_dit_value(vx));
    return node ? rb_dit_interval(node->a, node->b) : Qnil;
}

/*
 *  call-seq:
 *      tree.cover?(x) -> true or false
 *
 *  Whether the point `x` is covered by a stored interval. O(log n).
 */
static VALUE
rb_dit_cover_p(VALUE self, VALUE vx)
{
    const dit_t * tree = rb_dit_get(self);
    return dit_at(tree, rb_dit_value(vx)) ? Qtrue : Qfalse;
}

/*
 *  call-seq:
 *      tree.hull -> [a, b] or nil
 *
 *  The smallest interval including every stored interval, or nil when the tree
 *  is empty.
 *
 *  This is the `includes` augment of the root node, so it is O(1).
 */
static VALUE
rb_dit_hull(VALUE self)
{
    const dit_t * tree = rb_dit_get(self);

    if (tree->root == NULL)
        return Qnil;

    return rb_dit_interval(tree->root->includes.a, tree->root->includes.b);
}

/*
 *  call-seq:
 *      tree.size -> integer
 *
 *  The number of stored intervals.
 */
static VALUE
rb_dit_size(VALUE self)
{
    const dit_t * tree = rb_dit_get(self);
    return ULL2NUM((unsigned long long) dit_size(tree));
}

/*
 *  call-seq:
 *      tree.empty? -> true or false
 */
static VALUE
rb_dit_empty_p(VALUE self)
{
    const dit_t * tree = rb_dit_get(self);
    return dit_empty(tree) ? Qtrue : Qfalse;
}

/*
 *  call-seq:
 *      tree.height -> integer
 *
 *  The height of the underlying AVL tree, mostly useful for testing.
 */
static VALUE
rb_dit_height(VALUE self)
{
    const dit_t * tree = rb_dit_get(self);
    return INT2NUM(dit_height(tree));
}

/*
 *  call-seq:
 *      tree.clear -> self
 *
 *  Remove every stored interval.
 */
static VALUE
rb_dit_clear(VALUE self)
{
    dit_t * tree = rb_dit_get_mutable(self);
    dit_clear(tree);
    return self;
}

/*
 *  call-seq:
 *      tree.check! -> self
 *
 *  Verify every structural invariant of the underlying tree, and raise
 *  CorruptedError if one is broken. Mostly useful for testing, it is O(n).
 */
static VALUE
rb_dit_check(VALUE self)
{
    const dit_t * tree = rb_dit_get(self);

    char err[512];
    if (dit_check(tree, err, sizeof(err)))
        rb_raise(eCorruptedError, "%s", err);

    return self;
}

static VALUE
rb_dit_inspect(VALUE self)
{
    const dit_t * tree = rb_dit_get(self);
    return rb_sprintf("#<%"PRIsVALUE" size=%lu height=%d>",
            rb_obj_class(self), (unsigned long) dit_size(tree), dit_height(tree));
}

/*
 *  call-seq:
 *      tree.initialize_copy(other) -> self
 *
 *  Called by #dup and #clone.
 */
static VALUE
rb_dit_initialize_copy(VALUE self, VALUE other)
{
    dit_t * tree = rb_dit_get_mutable(self);
    dit_t * src  = rb_dit_get(other);

    if (tree == src)
        return self;

    dit_clear(tree);

    /* in-order insertions would degenerate into a rotation at every step, but
     * the AVL rebalancing keeps it O(n.log n) overall */
    VALUE intervals = rb_ary_new_capa((long) dit_size(src));
    rb_dit_traverse(src, 1, 0, 0, rb_dit_cb_push, (void *) intervals);

    for (long i = 0 ; i < RARRAY_LEN(intervals) ; ++i)
    {
        VALUE interval = rb_ary_entry(intervals, i);
        const dit_value_t a = rb_dit_value(rb_ary_entry(interval, 0));
        const dit_value_t b = rb_dit_value(rb_ary_entry(interval, 1));
        if (dit_insert(tree, a, b) != DIT_OK)
            rb_raise(eError, "could not copy interval [%"PRIu64"..%"PRIu64"[", a, b);
    }

    return self;
}

void
Init_disjoint_interval_tree(void)
{
    cTree = rb_define_class("DisjointIntervalTree", rb_cObject);
    rb_include_module(cTree, rb_mEnumerable);

    eError          = rb_define_class_under(cTree, "Error", rb_eStandardError);
    eOverlapError   = rb_define_class_under(cTree, "OverlapError", eError);
    eCorruptedError = rb_define_class_under(cTree, "CorruptedError", eError);

    /* the largest representable bound, exclusive: intervals live in
     * [0 .. DisjointIntervalTree::MAX[ */
    rb_define_const(cTree, "MAX", ULL2NUM((unsigned long long) DIT_VALUE_MAX));

    rb_define_alloc_func(cTree, rb_dit_alloc);

    rb_define_method(cTree, "initialize",      rb_dit_initialize,      -1);
    rb_define_method(cTree, "initialize_copy", rb_dit_initialize_copy,  1);

    rb_define_method(cTree, "insert",      rb_dit_insert,       2);
    rb_define_method(cTree, "insert?",     rb_dit_insert_p,     2);
    rb_define_method(cTree, "intersect",  rb_dit_intersect,   2);
    rb_define_method(cTree, "intersect?", rb_dit_intersect_p, 2);
    rb_define_method(cTree, "remove",      rb_dit_remove,       2);
    rb_define_method(cTree, "each",        rb_dit_each,         0);
    rb_define_method(cTree, "to_a",        rb_dit_to_a,         0);
    rb_define_method(cTree, "at",          rb_dit_at,           1);
    rb_define_method(cTree, "cover?",      rb_dit_cover_p,      1);
    rb_define_method(cTree, "hull",        rb_dit_hull,         0);
    rb_define_method(cTree, "size",        rb_dit_size,         0);
    rb_define_method(cTree, "empty?",      rb_dit_empty_p,      0);
    rb_define_method(cTree, "height",      rb_dit_height,       0);
    rb_define_method(cTree, "clear",       rb_dit_clear,        0);
    rb_define_method(cTree, "check!",      rb_dit_check,        0);
    rb_define_method(cTree, "inspect",     rb_dit_inspect,      0);

    rb_define_alias(cTree, "each_intersecting", "intersect");
    rb_define_alias(cTree, "overlaps?",         "intersect?");
    rb_define_alias(cTree, "length",            "size");
    rb_define_alias(cTree, "entries",           "to_a");
    rb_define_alias(cTree, "to_s",              "inspect");
}
