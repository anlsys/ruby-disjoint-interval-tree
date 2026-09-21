# frozen_string_literal: true

require_relative 'lib/disjoint_interval_tree/version'

Gem::Specification.new do |spec|
  spec.name        = 'disjoint_interval_tree'
  spec.version     = DisjointIntervalTree::VERSION
  spec.authors     = ['Romain PEREIRA']
  spec.email       = ['romain.pereira@inria.fr', 'rpereira@anl.gov']

  spec.summary     = 'Disjoint interval tree, in C, with Ruby bindings'
  spec.description = <<~DESC
    A set of pairwise disjoint half-open intervals [a..b[, backed by an
    augmented AVL tree written in C. Insertion, point lookup, intersection
    queries and range removal are all logarithmic. Each node caches the hull of
    the subtree it roots, so intersection queries prune whole subtrees in O(1).
  DESC

  spec.homepage              = 'https://github.com/rpereira-dev/ruby-disjoint-interval-tree'
  spec.license               = 'CECILL-C'
  spec.required_ruby_version = '>= 2.7.0'

  spec.files = Dir[
    'lib/**/*.rb',
    'ext/**/*.{c,h,rb}',
    'test/**/*.{c,rb}',
    'test/c/Makefile',
    'README.md',
    'Rakefile',
    'disjoint_interval_tree.gemspec'
  ]

  spec.require_paths = ['lib']
  spec.extensions    = ['ext/disjoint_interval_tree/extconf.rb']

  spec.metadata = {
    'source_code_uri'       => spec.homepage,
    'rubygems_mfa_required' => 'true'
  }
end
