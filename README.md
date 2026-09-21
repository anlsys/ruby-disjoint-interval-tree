# disjoint\_interval\_tree

A set of **pairwise disjoint half-open intervals `[a..b[`**, implemented in C
and exposed to Ruby.

It is a self-balancing (AVL) binary search tree, augmented - as the `lp-tree`
it is inspired from - with the *hull* of the subtree each node roots, so that
intersection queries prune whole subtrees in `O(1)`.

```ruby
require 'disjoint_interval_tree'

tree = DisjointIntervalTree.new
tree.insert(0x1000, 0x2000)
tree.insert(0x3000, 0x4000)

tree.intersect(0x1800, 0x3800) do |a, b|
  puts format('[0x%x..0x%x[', a, b)
end
# => [0x1000..0x2000[
# => [0x3000..0x4000[

tree.remove(0x1800, 0x3800)   # => 2
tree.to_a                     # => []
```

## Semantics

* Intervals are **half-open**: `[a..b[` contains `a` but not `b`. `[0..10[` and
  `[10..20[` are adjacent, they do **not** intersect.
* Bounds are **unsigned 64 bits integers**, so intervals live in
  `[0 .. DisjointIntervalTree::MAX[`. Negative or non-integer bounds raise.
* Stored intervals are **pairwise disjoint**. It is a usage contract that the
  caller never inserts an interval overlapping an already inserted one.
  Breaking it raises `DisjointIntervalTree::OverlapError` and leaves the tree
  unchanged - intervals are never merged nor split implicitly.

## Complexities

With `n` intervals stored and `k` intervals reported:

| operation                | complexity     |
| ------------------------ | -------------- |
| `insert(a, b)`           | `O(log n)`     |
| `intersect(a, b, &blk)` | `O(k + log n)` |
| `intersect?(a, b)`      | `O(log n)`     |
| `remove(a, b)`           | `O(k.log n)`   |
| `at(x)`, `cover?(x)`     | `O(log n)`     |
| `hull`                   | `O(1)`         |
| `each`, `to_a`           | `O(n)`         |
| `check!`                 | `O(n)`         |

## Ruby API

```ruby
tree = DisjointIntervalTree.new                      # empty
tree = DisjointIntervalTree.new([[0, 10], [20, 30]]) # pre-filled

# --- the three core operations -------------------------------------------

tree.insert(a, b)            # -> self, raises OverlapError if it overlaps
tree.insert?(a, b)           # -> true / false instead of raising

tree.intersect(a, b) { |x, y| ... }   # -> self, yields in increasing order
tree.intersect(a, b)                  # -> [[x, y], ...] when no block given

tree.remove(a, b)                      # -> number of intervals removed
tree.remove(a, b) { |x, y| ... }       # ... and yields each removed interval

# --- queries --------------------------------------------------------------

tree.intersect?(a, b)       # -> true if any stored interval intersect [a..b[
tree.at(x)                   # -> [a, b] containing the point x, or nil
tree.cover?(x)               # -> true if x is covered by a stored interval
tree.hull                    # -> [a, b] spanning every interval, or nil
tree.size                    # -> number of stored intervals
tree.empty?
tree.height                  # -> height of the underlying AVL tree

# --- iteration (DisjointIntervalTree includes Enumerable) -----------------

tree.each { |a, b| ... }
tree.to_a                    # -> [[a, b], ...], in increasing order
tree.map { |a, b| b - a }
tree.coverage                # -> total length covered

# --- misc -----------------------------------------------------------------

tree.clear                   # -> self
tree.dup                     # -> deep copy
tree == other
tree.check!                  # -> self, raises CorruptedError if an invariant
                             #    of the underlying tree is broken
```

Aliases: `each_intersecting` (`intersect`), `overlaps?` (`intersect?`),
`length` (`size`), `entries` (`to_a`).

The tree **must not be modified from within `each` or `intersect`**: doing so
raises `DisjointIntervalTree::Error` rather than corrupting the structure. The
block given to `remove` is called after the removal, on a snapshot, so it may
modify the tree.

## C API

The Ruby extension is a thin binding over a standalone, dependency-free C
library made of `ext/disjoint_interval_tree/dit.{c,h}`. It can be vendored as
is into a C or C++ project.

```c
#include "dit.h"

static int print_cb(dit_value_t a, dit_value_t b, void * user)
{
    (void) user;
    printf("[%lu..%lu[\n", a, b);
    return 0;   /* non-zero stops the traversal */
}

dit_t tree;
dit_init(&tree);

if (dit_insert(&tree, 0, 10) != DIT_OK)
    { /* DIT_EMPTY, DIT_OVERLAP or DIT_NOMEM */ }

dit_intersect(&tree, 5, 25, print_cb, NULL);
dit_remove(&tree, 5, 25);

dit_destroy(&tree);
```

Customization points, to define before including `dit.h`:

| macro           | default                | purpose                                         |
| --------------- | ---------------------- | ----------------------------------------------- |
| `DIT_VALUE_T`   | `uint64_t`             | interval bound type                              |
| `DIT_ASSERT`    | `assert`               | internal consistency assertions                  |
| `DIT_MALLOC`    | `malloc` / `free`      | node allocation                                  |
| `DIT_PARANOID`  | `0`                    | run `dit_check()` after every mutation           |

### Invariants

`dit_check()` validates, without ever aborting, that:

1. every stored interval is non-empty (`a < b`),
2. the binary search tree ordering holds, which - the bounds narrowing at each
   level - also proves the intervals are pairwise disjoint,
3. an in-order traversal yields increasing, non-overlapping intervals,
4. the `height` augment of every node is correct,
5. every node is AVL-balanced,
6. the `size` augment of every node is correct,
7. the `includes` augment of every node is the hull of its subtree,
8. that hull spans exactly from the leftmost to the rightmost descendant,
9. the cached cardinality matches the number of nodes,
10. the tree depth is logarithmic in the number of nodes,
11. no traversal is leaking.

On top of that, the implementation is littered with `DIT_ASSERT`s on the
invariants it relies on locally (rotations, augment refresh, deletion cases,
insertion contract, ...).

## Building and testing

```sh
rake compile                      # build the extension into lib/
rake test                         # run the ruby test suite
rake test:c                       # run the C test suite: debug, asan+ubsan, release
rake test:valgrind                # run the C test suite under valgrind
rake test:paranoid                # run the ruby test suite against a paranoid build
rake                              # everything but valgrind
```

The C test suite can also be driven directly:

```sh
make -C test/c            # debug, sanitized and release builds
make -C test/c debug SEED=42
```

Both suites cross-check the tree against a naive reference implementation on
randomized workloads, and validate every structural invariant after each
operation.

## License

CeCILL-C, see the headers of the source files.
