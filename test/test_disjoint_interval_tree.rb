# frozen_string_literal: true

#
# Copyright 2025 INRIA
#
# Contributors :
# Romain PEREIRA, romain.pereira@inria.fr + rpereira@anl.gov
#
# This software is governed by the CeCILL-C license under French law and
# abiding by the rules of distribution of free software.  You can  use,
# modify and/ or redistribute the software under the terms of the CeCILL-C
# license as circulated by CEA, CNRS and INRIA at the following URL
# "http://www.cecill.info".
#

require 'minitest/autorun'
require 'disjoint_interval_tree'

# A naive reference implementation, used to cross-check the tree.
class IntervalModel
  attr_reader :intervals

  def initialize
    @intervals = []
  end

  def intersect(a, b)
    return [] if a >= b

    @intervals.select { |x, y| a < y && x < b }
  end

  def insert(a, b)
    return false if a >= b || !intersect(a, b).empty?

    @intervals << [a, b]
    @intervals.sort!
    true
  end

  def remove(a, b)
    hit = intersect(a, b)
    @intervals -= hit
    hit.size
  end

  def size
    @intervals.size
  end
end

class TestDisjointIntervalTree < Minitest::Test
  def setup
    @tree = DisjointIntervalTree.new
  end

  def teardown
    @tree.check!
  end

  # a tree holding [0..10[ [20..30[ [40..50[ [60..70[
  def spaced_tree
    tree = DisjointIntervalTree.new
    4.times { |i| tree.insert(20 * i, 20 * i + 10) }
    tree
  end

  ##########
  # BASICS #
  ##########

  def test_empty_tree
    assert_empty @tree
    assert_equal 0, @tree.size
    assert_equal 0, @tree.length
    assert_equal 0, @tree.height
    assert_equal [], @tree.to_a
    assert_nil @tree.at(0)
    assert_nil @tree.hull
    refute @tree.cover?(0)
    refute @tree.intersect?(0, 100)
    assert_equal [], @tree.intersect(0, 100)
    assert_equal 0, @tree.remove(0, 100)
  end

  def test_insert_returns_self
    assert_same @tree, @tree.insert(0, 10)
  end

  def test_insert_single
    @tree.insert(10, 20)

    refute_empty @tree
    assert_equal 1, @tree.size
    assert_equal 1, @tree.height
    assert_equal [[10, 20]], @tree.to_a
    assert_equal [10, 20], @tree.hull
  end

  def test_intervals_are_half_open
    @tree.insert(10, 20)

    assert_nil @tree.at(9)
    assert_equal [10, 20], @tree.at(10)
    assert_equal [10, 20], @tree.at(19)
    assert_nil @tree.at(20)

    refute @tree.cover?(9)
    assert @tree.cover?(10)
    assert @tree.cover?(19)
    refute @tree.cover?(20)
  end

  def test_adjacent_intervals_do_not_overlap
    @tree.insert(0, 10)
    @tree.insert(10, 20)
    @tree.insert(20, 30)

    assert_equal 3, @tree.size
    assert_equal [[0, 10], [10, 20], [20, 30]], @tree.to_a
    assert_equal [0, 30], @tree.hull
  end

  def test_intervals_are_ordered
    [50, 10, 90, 30, 70, 0, 20].each { |a| @tree.insert(a, a + 5) }

    assert_equal [[0, 5], [10, 15], [20, 25], [30, 35], [50, 55], [70, 75], [90, 95]],
                 @tree.to_a
    assert_equal @tree.to_a, @tree.to_a.sort
  end

  def test_coverage
    @tree.insert(0, 10)
    @tree.insert(100, 130)

    assert_equal 40, @tree.coverage
  end

  def test_inspect
    @tree.insert(0, 10)

    assert_equal '#<DisjointIntervalTree size=1 height=1>', @tree.inspect
    assert_equal @tree.inspect, @tree.to_s
  end

  ############
  # CONTRACT #
  ############

  def test_insert_overlapping_raises
    @tree.insert(10, 20)
    @tree.insert(30, 40)

    overlapping = [
      [10, 20],   # identical
      [12, 18],   # strictly included
      [5, 25],    # strictly including
      [5, 11],    # overlapping on the left
      [19, 25],   # overlapping on the right
      [0, 35]     # spanning both
    ]

    overlapping.each do |a, b|
      assert_raises(DisjointIntervalTree::OverlapError) { @tree.insert(a, b) }
    end

    # the tree was left untouched
    assert_equal [[10, 20], [30, 40]], @tree.to_a
  end

  def test_overlap_error_is_a_standard_error
    @tree.insert(10, 20)

    error = assert_raises(DisjointIntervalTree::OverlapError) { @tree.insert(15, 25) }

    assert_kind_of DisjointIntervalTree::Error, error
    assert_kind_of StandardError, error
    assert_match(/\[15\.\.25\[ overlaps \[10\.\.20\[/, error.message)
  end

  def test_insert_predicate_does_not_raise
    assert @tree.insert?(10, 20)
    refute @tree.insert?(15, 25)
    assert @tree.insert?(20, 25)
    assert_equal [[10, 20], [20, 25]], @tree.to_a
  end

  def test_insert_empty_interval_raises
    assert_raises(ArgumentError) { @tree.insert(10, 10) }
    assert_raises(ArgumentError) { @tree.insert(20, 10) }
    assert_raises(ArgumentError) { @tree.insert?(10, 10) }
    assert_empty @tree
  end

  def test_insert_invalid_bounds_raise
    # negative bounds must not silently wrap around
    assert_raises(RangeError) { @tree.insert(-1, 10) }
    assert_raises(RangeError) { @tree.insert(-(2**63), 10) }
    assert_raises(RangeError) { @tree.insert(-(2**70), 10) }
    assert_raises(RangeError) { @tree.insert(0, -1) }

    # nor must out of range ones
    assert_raises(RangeError) { @tree.insert(0, DisjointIntervalTree::MAX + 1) }
    assert_raises(RangeError) { @tree.insert(2**64, 2**65) }

    # only integers are accepted, floats are not silently truncated
    assert_raises(TypeError) { @tree.insert(1.5, 10.5) }
    assert_raises(TypeError) { @tree.insert('0', 10) }
    assert_raises(TypeError) { @tree.insert(nil, 10) }

    assert_empty @tree
  end

  def test_invalid_bounds_raise_everywhere
    assert_raises(RangeError) { @tree.remove(-1, 10) }
    assert_raises(RangeError) { @tree.intersect(-1, 10) }
    assert_raises(RangeError) { @tree.intersect?(-1, 10) }
    assert_raises(RangeError) { @tree.at(-1) }
    assert_raises(RangeError) { @tree.cover?(-1) }
    assert_raises(TypeError)  { @tree.at('nope') }
  end

  def test_large_bounds_are_supported
    @tree.insert(2**63, 2**63 + 10)

    assert_equal [[2**63, 2**63 + 10]], @tree.to_a
    assert_equal [2**63, 2**63 + 10], @tree.at(2**63 + 5)
  end

  ##############
  # INTERSECTS #
  ##############

  def test_intersect_yields_matching_intervals
    tree = spaced_tree
    seen = []
    result = tree.intersect(5, 45) { |a, b| seen << [a, b] }

    assert_same tree, result
    assert_equal [[0, 10], [20, 30], [40, 50]], seen
  end

  def test_intersect_without_block_returns_an_array
    assert_equal [[0, 10], [20, 30], [40, 50]], spaced_tree.intersect(5, 45)
  end

  def test_intersect_reports_nothing_when_querying_a_hole
    assert_equal [], spaced_tree.intersect(12, 18)
  end

  def test_intersect_ignores_adjacency
    tree = spaced_tree

    assert_equal [], tree.intersect(10, 20)
    assert_equal [[0, 10]], tree.intersect(9, 20)
    assert_equal [[20, 30]], tree.intersect(10, 21)
  end

  def test_intersect_on_an_empty_query
    assert_equal [], spaced_tree.intersect(5, 5)
    assert_equal [], spaced_tree.intersect(45, 5)
  end

  def test_intersect_out_of_range
    assert_equal [], spaced_tree.intersect(1000, 2000)
  end

  def test_intersect_everything
    assert_equal 4, spaced_tree.intersect(0, 1000).size
  end

  def test_intersect_predicate
    tree = spaced_tree

    refute tree.intersect?(12, 18)
    refute tree.intersect?(10, 20)
    assert tree.intersect?(9, 20)
    assert tree.intersect?(0, 1000)
    assert tree.overlaps?(0, 1000)
  end

  def test_each_intersecting_is_an_alias
    assert_equal spaced_tree.intersect(5, 45), spaced_tree.each_intersecting(5, 45)
  end

  def test_intersect_can_be_broken_out_of
    tree = spaced_tree
    seen = []

    result = tree.intersect(0, 1000) do |a, b|
      seen << [a, b]
      break :stopped if seen.size == 2
    end

    assert_equal :stopped, result
    assert_equal [[0, 10], [20, 30]], seen

    # the traversal state was properly restored
    tree.insert(100, 110)
    assert_equal 5, tree.size
    tree.check!
  end

  def test_an_exception_raised_in_a_block_leaves_the_tree_usable
    tree = spaced_tree

    assert_raises(RuntimeError) do
      tree.intersect(0, 1000) { raise 'boom' }
    end

    tree.insert(100, 110)
    assert_equal 5, tree.size
    tree.check!
  end

  ##############
  # REENTRANCY #
  ##############

  def test_mutating_while_traversing_raises
    tree = spaced_tree

    assert_raises(DisjointIntervalTree::Error) do
      tree.each { tree.insert(1000, 1010) }
    end

    assert_raises(DisjointIntervalTree::Error) do
      tree.intersect(0, 1000) { tree.remove(0, 10) }
    end

    assert_raises(DisjointIntervalTree::Error) do
      tree.each { tree.clear }
    end

    # and the tree is unharmed
    assert_equal 4, tree.size
    tree.check!
  end

  def test_mutating_a_frozen_tree_raises
    tree = spaced_tree.freeze

    assert_raises(FrozenError) { tree.insert(100, 110) }
    assert_raises(FrozenError) { tree.insert?(100, 110) }
    assert_raises(FrozenError) { tree.remove(0, 10) }
    assert_raises(FrozenError) { tree.clear }

    # reading is still fine
    assert_equal 4, tree.size
    assert_equal [[0, 10]], tree.intersect(0, 10)
    tree.check!
  end

  def test_reading_while_traversing_is_allowed
    tree = spaced_tree
    sizes = []

    tree.each { sizes << tree.size + tree.intersect(0, 1000).size }

    assert_equal [8, 8, 8, 8], sizes
  end

  ##########
  # REMOVE #
  ##########

  def test_remove_returns_the_number_of_removed_intervals
    tree = spaced_tree

    assert_equal 0, tree.remove(10, 20)
    assert_equal 4, tree.size

    assert_equal 3, tree.remove(5, 45)
    assert_equal [[60, 70]], tree.to_a
  end

  def test_remove_does_not_split_intervals
    tree = spaced_tree

    assert_equal 1, tree.remove(25, 26)
    assert_nil tree.at(20)
    assert_nil tree.at(29)
    assert_equal [[0, 10], [40, 50], [60, 70]], tree.to_a
  end

  def test_remove_ignores_adjacency
    tree = spaced_tree

    assert_equal 0, tree.remove(10, 20)
    assert_equal 0, tree.remove(30, 40)
    assert_equal 4, tree.size
  end

  def test_remove_on_an_empty_query
    tree = spaced_tree

    assert_equal 0, tree.remove(5, 5)
    assert_equal 0, tree.remove(45, 5)
    assert_equal 4, tree.size
  end

  def test_remove_yields_removed_intervals
    tree = spaced_tree
    seen = []

    assert_equal 3, tree.remove(5, 45) { |a, b| seen << [a, b] }
    assert_equal [[0, 10], [20, 30], [40, 50]], seen
    assert_equal [[60, 70]], tree.to_a
  end

  def test_remove_then_reinsert
    tree = spaced_tree

    tree.remove(20, 30)
    assert_raises(DisjointIntervalTree::OverlapError) { tree.insert(0, 10) }
    tree.insert(20, 30)
    assert_equal 4, tree.size
  end

  def test_remove_everything
    tree = spaced_tree

    assert_equal 4, tree.remove(0, DisjointIntervalTree::MAX)
    assert_empty tree
    assert_equal 0, tree.remove(0, DisjointIntervalTree::MAX)
  end

  def test_clear
    tree = spaced_tree

    assert_same tree, tree.clear
    assert_empty tree
    assert_equal [], tree.to_a

    tree.insert(0, 1000)
    assert_equal 1, tree.size
  end

  ##############
  # ENUMERABLE #
  ##############

  def test_each_returns_an_enumerator_without_a_block
    tree = spaced_tree
    enumerator = tree.each

    assert_kind_of Enumerator, enumerator
    assert_equal 4, enumerator.size
    assert_equal [[0, 10], [20, 30], [40, 50], [60, 70]], enumerator.to_a
  end

  def test_enumerable_methods
    tree = spaced_tree

    assert_equal [10, 10, 10, 10], tree.map { |a, b| b - a }
    assert_equal [[0, 10]], tree.select { |a, _| a.zero? }
    assert_equal [0, 10], tree.first
    assert_equal [[0, 10], [20, 30]], tree.first(2)
    assert tree.include?([20, 30])
    refute tree.include?([20, 31])
    assert_equal 4, tree.count
    assert_equal 40, tree.sum { |a, b| b - a }
  end

  def test_entries_is_an_alias_of_to_a
    assert_equal spaced_tree.to_a, spaced_tree.entries
  end

  ###############
  # CONSTRUCTOR #
  ###############

  def test_new_with_intervals
    tree = DisjointIntervalTree.new([[20, 30], [0, 10]])

    assert_equal [[0, 10], [20, 30]], tree.to_a
  end

  def test_new_with_overlapping_intervals_raises
    assert_raises(DisjointIntervalTree::OverlapError) do
      DisjointIntervalTree.new([[0, 10], [5, 15]])
    end
  end

  def test_new_with_garbage_raises
    assert_raises(TypeError)     { DisjointIntervalTree.new(42) }
    assert_raises(ArgumentError) { DisjointIntervalTree.new([[0, 10, 20]]) }
    assert_raises(ArgumentError) { DisjointIntervalTree.new([0]) }
  end

  def test_dup_is_a_deep_copy
    tree = spaced_tree
    copy = tree.dup

    assert_equal tree, copy
    assert_equal tree.to_a, copy.to_a

    copy.insert(100, 110)
    refute_equal tree, copy
    assert_equal 4, tree.size
    assert_equal 5, copy.size

    copy.check!
    tree.check!
  end

  def test_equality
    assert_equal DisjointIntervalTree.new, DisjointIntervalTree.new
    assert_equal spaced_tree, spaced_tree
    refute_equal spaced_tree, DisjointIntervalTree.new
    refute_equal spaced_tree, [[0, 10], [20, 30], [40, 50], [60, 70]]
  end

  ##################
  # EXTREME VALUES #
  ##################

  def test_extreme_values
    max = DisjointIntervalTree::MAX

    @tree.insert(0, 1)
    @tree.insert(max - 1, max)
    @tree.insert(1, max - 1)

    assert_equal [0, max], @tree.hull
    assert_equal [max - 1, max], @tree.at(max - 1)

    # no half-open interval can ever contain MAX
    assert_nil @tree.at(max)
    refute @tree.cover?(max)

    assert_equal 3, @tree.intersect(0, max).size
    assert_equal 3, @tree.remove(0, max)
  end

  ###########
  # BALANCE #
  ###########

  def test_sorted_insertions_stay_balanced
    n = 4095
    n.times { |i| @tree.insert(2 * i, 2 * i + 1) }

    assert_equal n, @tree.size

    # 4095 nodes fit in a perfectly balanced tree of height 12, an AVL tree is
    # at most ~1.44*log2(n) deep
    assert_operator @tree.height, :<=, 18
  end

  def test_reverse_sorted_insertions_stay_balanced
    n = 4095
    n.downto(1) { |i| @tree.insert(2 * i, 2 * i + 1) }

    assert_equal n, @tree.size
    assert_operator @tree.height, :<=, 18
  end

  def test_sequential_removals_stay_balanced
    n = 1023
    n.times { |i| @tree.insert(2 * i, 2 * i + 1) }

    n.times do |i|
      assert_equal 1, @tree.remove(2 * i, 2 * i + 1)
      assert_equal n - i - 1, @tree.size
      assert_operator @tree.height, :<=, 16
    end

    assert_empty @tree
  end

  ##############
  # RANDOMIZED #
  ##############

  def test_random_against_a_reference_model
    srand(Integer(ENV.fetch('SEED', 1)))

    universe = 512
    model = IntervalModel.new

    2000.times do |iteration|
      a = rand(universe)
      b = a + 1 + rand(8)

      case rand(3)
      when 0
        if model.intersect(a, b).empty?
          @tree.insert(a, b)
          model.insert(a, b)
        else
          assert_raises(DisjointIntervalTree::OverlapError) { @tree.insert(a, b) }
        end
      when 1
        assert_equal model.remove(a, b), @tree.remove(a, b),
                     "iteration #{iteration}: remove(#{a}, #{b})"
      else
        assert_equal model.intersect(a, b), @tree.intersect(a, b),
                     "iteration #{iteration}: intersect(#{a}, #{b})"
      end

      assert_equal model.size, @tree.size, "iteration #{iteration}"
      assert_equal model.intervals, @tree.to_a, "iteration #{iteration}"
      @tree.check!
    end

    refute_empty @tree
  end

  def test_random_point_queries
    srand(Integer(ENV.fetch('SEED', 1)))

    model = IntervalModel.new
    200.times do
      a = rand(4096)
      b = a + 1 + rand(16)
      model.insert(a, b) && @tree.insert(a, b)
    end

    @tree.check!

    2000.times do
      x = rand(4096 + 32)
      expected = model.intervals.find { |a, b| a <= x && x < b }

      if expected
        assert_equal expected, @tree.at(x), "at(#{x})"
        assert @tree.cover?(x), "cover?(#{x})"
      else
        assert_nil @tree.at(x), "at(#{x})"
        refute @tree.cover?(x), "cover?(#{x})"
      end
    end
  end

  def test_garbage_collection
    100.times do
      tree = DisjointIntervalTree.new
      100.times { |i| tree.insert(10 * i, 10 * i + 5) }
      tree.check!
    end

    GC.start
    GC.compact if GC.respond_to?(:compact)

    @tree.insert(0, 10)
    assert_equal [[0, 10]], @tree.to_a
  end
end
