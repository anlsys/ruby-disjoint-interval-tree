# frozen_string_literal: true

require 'mkmf'

# `--enable-paranoid` runs a full O(n) coherency check of the tree after every
# mutation. Only useful to debug the extension itself.
if enable_config('paranoid', false)
  $defs << '-DDIT_PARANOID=1'
  warn 'disjoint_interval_tree: building with paranoid coherency checks'
end

# `--disable-assertions` compiles out every internal consistency assertion.
$defs << '-DNDEBUG' unless enable_config('assertions', true)

append_cflags(['-std=gnu11', '-Wall', '-Wextra'])

create_makefile('disjoint_interval_tree/disjoint_interval_tree')
