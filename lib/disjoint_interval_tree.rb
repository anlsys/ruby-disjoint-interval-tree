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

require 'disjoint_interval_tree/disjoint_interval_tree'
require 'disjoint_interval_tree/version'

# A set of pairwise disjoint half-open intervals `[a..b[`, backed by an
# augmented AVL tree written in C.
#
# Bounds are unsigned 64 bits integers, so intervals live in
# `[0 .. DisjointIntervalTree::MAX[`.
#
#     tree = DisjointIntervalTree.new
#     tree.insert(0, 10)
#     tree.insert(20, 30)
#
#     tree.intersect(5, 25) { |a, b| puts "[#{a}..#{b}[" }
#     # => [0..10[
#     # => [20..30[
#
#     tree.remove(5, 25)  # => 2
#     tree.to_a           # => []
#
# Inserting an interval overlapping an already inserted one is a usage
# contract violation and raises DisjointIntervalTree::OverlapError.
class DisjointIntervalTree
  # Two trees are equal when they hold the same intervals.
  def ==(other)
    other.is_a?(DisjointIntervalTree) && to_a == other.to_a
  end

  alias eql? ==

  # The total length covered by the stored intervals.
  def coverage
    sum = 0
    each { |a, b| sum += b - a }
    sum
  end
end
