# disjoint\_interval\_tree

A set of **pairwise disjoint half-open intervals `[a..b[`**, implemented in C
and exposed to Ruby.

It is a self-balancing (AVL) binary search tree, augmented - as the LP-Tree it
is inspired from, see [References](#references) - with the *hull* of the
subtree each node roots, so that intersection queries prune whole subtrees in
`O(1)`.

## Installation

```sh
gem install disjoint_interval_tree
```

or, in a `Gemfile`:

```ruby
gem 'disjoint_interval_tree'
```

Ruby >= 2.7 and a C compiler are required: the extension is built at install
time.

## Synopsis

```ruby
require 'disjoint_interval_tree'

tree = DisjointIntervalTree.new
tree.insert(10, 20)
tree.insert(30, 40)

tree.intersect(15, 35) do |a, b|
  puts "[#{a}..#{b}["
end
# => [10..20[
# => [30..40[

tree.remove(15, 35)   # => 2
tree.to_a             # => []
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

## Operations

With `n` intervals stored, `k` intervals reported by the call, and `m`
intervals handed to the constructor:

| operation                                    | description                                                                                                              | complexity     |
| -------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------ | -------------- |
| `DisjointIntervalTree.new(intervals = nil)`  | Build an empty tree, or one filled with the given `[a, b]` pairs.                                                         | `O(m.log m)`   |
| `insert(a, b)`                               | Add `[a..b[`, raising `OverlapError` if it intersects an already stored interval.                                         | `O(log n)`     |
| `insert?(a, b)`                              | Same as `insert`, but returns `false` instead of raising when it would overlap.                                           | `O(log n)`     |
| `intersect(a, b, &blk)`                      | Yield every stored interval intersecting `[a..b[` in increasing order, or return them as an array when given no block.    | `O(k + log n)` |
| `intersect?(a, b)`                           | Whether at least one stored interval intersects `[a..b[`.                                                                 | `O(log n)`     |
| `remove(a, b, &blk)`                         | Remove every stored interval intersecting `[a..b[` - whole, never split - and return how many went.                       | `O(k.log n)`   |
| `at(x)`                                      | The stored interval containing the point `x`, or `nil`.                                                                   | `O(log n)`     |
| `cover?(x)`                                  | Whether the point `x` falls inside a stored interval.                                                                     | `O(log n)`     |
| `hull`                                       | The smallest interval enclosing every stored one, read straight off the root augment.                                     | `O(1)`         |
| `size`, `length`                             | How many intervals are stored.                                                                                            | `O(1)`         |
| `empty?`                                     | Whether the tree holds no interval at all.                                                                                | `O(1)`         |
| `height`                                     | Height of the underlying AVL tree, for tests and diagnostics.                                                             | `O(1)`         |
| `coverage`                                   | Total length covered, that is the sum of the lengths of the stored intervals.                                             | `O(n)`         |
| `each(&blk)`                                 | Yield every stored interval in increasing order, or return an `Enumerator` when given no block.                           | `O(n)`         |
| `to_a`, `entries`                            | Every stored interval as an array of `[a, b]` pairs, in increasing order.                                                 | `O(n)`         |
| `map`, `select`, ...                         | Anything `Enumerable` provides, built on `each`.                                                                          | `O(n)`         |
| `clear`                                      | Drop every interval, leaving the tree usable.                                                                             | `O(n)`         |
| `dup`                                        | A deep copy, sharing no node with the original.                                                                           | `O(n.log n)`   |
| `==`                                         | Whether two trees hold exactly the same intervals.                                                                        | `O(n)`         |
| `check!`                                     | Re-derive every structural invariant, raising `CorruptedError` if one is broken.                                          | `O(n)`         |
| `inspect`, `to_s`                            | A short `#<DisjointIntervalTree size=... height=...>` summary.                                                            | `O(1)`         |

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

tree.intersect?(a, b)        # -> true if any stored interval intersect [a..b[
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

### Node augments

Each node stores its own interval, its children, and - in a single `augment`
field - everything it caches about the subtree it roots:

```c
typedef struct dit_augment_s
{
    /* the englobing interval of the subtree, i.e. the smallest interval
     * including every interval stored in that subtree */
    struct { dit_value_t a, b; } hull;

    /* height of the subtree, a leaf has 1 */
    int32_t height;

    /* number of nodes in the subtree */
    uint32_t size;
}   dit_augment_t;
```

Augments are derived from the subtree alone and recomputed bottom-up after
every structural change, so a rotation only has to refresh the two nodes it
moves. `hull` is what lets `dit_intersect()` discard a whole subtree with a
single comparison; the root one is readable in O(1) through `dit_hull()`.

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
7. the `hull` augment of every node englobes its subtree exactly,
8. that hull spans from the leftmost to the rightmost descendant,
9. the cached cardinality matches the number of nodes,
10. the tree depth is logarithmic in the number of nodes,
11. no traversal is leaking.

On top of that, the implementation is littered with `DIT_ASSERT`s on the
invariants it relies on locally (rotations, augment refresh, deletion cases,
insertion contract, ...).

## Building and testing

Building the extension needs the ruby development headers (`ruby-dev` /
`ruby-devel`, or any ruby built from source).

```sh
rake compile                      # build the extension into lib/
rake recompile                    # force a full rebuild, use this when in doubt
rake test                         # ruby test suite
rake test:c                       # C test suite: debug, asan+ubsan, release
rake test:valgrind                # C test suite under valgrind
rake test:paranoid                # ruby test suite against a DIT_PARANOID build
rake test:gem                     # build, install into a sandbox, test the installed gem
rake test:files                   # every file the gem ships is present and tracked by git
rake                              # default: test:c + test
rake verify                       # everything above, plus a randomized seed sweep
```

What each configuration actually proves:

| configuration                          | proves                                                                                        |
| -------------------------------------- | --------------------------------------------------------------------------------------------- |
| `test:c` debug (`-O0 -DDIT_PARANOID=1`) | all the invariants below are re-validated after *every* mutation, plus the inline `DIT_ASSERT`s |
| `test:c` sanitize                       | no undefined behaviour, no out of bounds access, no leak, still paranoid                        |
| `test:c` release (`-DNDEBUG`)           | the behaviour is identical with every assertion compiled out                                    |
| `test:valgrind`                         | no invalid access and no leak, independently of the sanitizers                                  |
| `test` / `test:paranoid`                | the ruby bindings, against the regular and the paranoid extension                               |
| `test:gem`                              | the *packaged* gem installs and works, which `test` cannot tell                                 |
| `test:files`                            | the gem and the repository ship the same files, which neither of the above can tell             |

The C test suite can also be driven directly:

```sh
make -C test/c            # debug, sanitized and release builds
make -C test/c debug SEED=42
```

Both suites cross-check the tree against a naive reference implementation on
randomized workloads, and validate every structural invariant after each
operation. `rake verify SEEDS=100` widens the randomized sweep.

## Releasing

Releases are cut from `main`, with CI green.

**1. Bump the version.**

```ruby
# lib/disjoint_interval_tree/version.rb
VERSION = '0.2.0'
```

**2. Commit, push, and wait for CI.** `rake release` re-runs everything
locally, but a release should not be cut from a commit CI has not seen.

**3. Publish.**

```sh
rake release
```

It refuses to do anything unless the working tree is clean, the branch is
`main`, and the tag is still free. It then runs `rake verify`, tags `vX.Y.Z`,
pushes the tag, and `gem push`es the gem it just built.

**4. Attach the gem to a GitHub release.** `rake release` prints this command
with the version filled in:

```sh
gh release create vX.Y.Z pkg/disjoint_interval_tree-X.Y.Z.gem \
    --title vX.Y.Z --generate-notes
```

**5. Update the downstream packagers.** `rake release` prints the sha256 of
what it published, as a ready to paste spack `version(...)` line. For
[THAPI-spack](https://github.com/argonne-lcf/THAPI-spack), that is
`packages/ruby-disjoint-interval-tree/package.py`. The published artifact can
also be checksummed directly, which is the authoritative source:

```sh
spack checksum ruby-disjoint-interval-tree X.Y.Z
```

## References

`dit` is a C rewrite of two structures published by the author:

* the **SPMT**, a red-black tree of disjoint memory intervals that merges
  adjacent ranges, used to track memory accesses in Taskgrind:

  > R. Pereira, G. Stelle and P. Carribault, *"Taskgrind: Heavyweight Dynamic
  > Binary Instrumentation for Parallel Programs Analysis"*, SC24-W: Workshops
  > of the International Conference for High Performance Computing, Networking,
  > Storage and Analysis, Atlanta, GA, USA, 2024, pp. 214-221,
  > doi: [10.1109/SCW63240.2024.00033](https://doi.org/10.1109/SCW63240.2024.00033).

* the **LP-Tree**, a k-dimensional interval tree where each node caches the
  hyperrectangle hull of its subtree, used to track matrix tile coherence
  across GPUs:

  > R. Pereira, P.-E. Polet, T. Gautier and S. Perarnau, *"Multi-GPU Memory
  > Coherence for BLAS Matrices"*, IPDPS-W HIPS: 31st International Workshop on
  > High-level Parallel Programming Models and Supportive Environments, 2026.

What this library takes from each, and where it departs from them:

* from the **LP-Tree**, the `includes` augment - the hull of the subtree cached
  in every node, here `augment.hull` - and the case analysis of the insertion
  descent. `dit` is the `K = 1` case, so the hyperrectangle collapses to an
  interval, and `includes.hyperrect[0]` to `augment.hull`;
* from the **SPMT**, the flat C style, the callback-based traversals and the
  coherency-check approach (`dit_check()`);
* unlike both, `dit` never merges nor splits intervals. Keeping them disjoint
  is a *caller contract*, checked at no extra cost during the insertion
  descent, which is what lets a stored interval keep its identity;
* unlike the SPMT, the tree is an AVL rather than a red-black tree: rebalancing
  is bottom-up and recursive, which makes the augment refresh and the deletion
  paths markedly harder to get wrong, at the cost of slightly more rotations.

## License

CeCILL-C, see the headers of the source files.
