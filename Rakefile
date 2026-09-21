# frozen_string_literal: true

require 'etc'
require 'fileutils'
require 'rake/clean'
require 'rbconfig'

EXT_NAME  = 'disjoint_interval_tree'
EXT_DIR   = File.expand_path("ext/#{EXT_NAME}", __dir__)
LIB_DIR   = File.expand_path("lib/#{EXT_NAME}", __dir__)
BUILD_DIR = File.expand_path("tmp/#{RUBY_PLATFORM}/#{EXT_NAME}/#{RUBY_VERSION}", __dir__)
DLEXT     = RbConfig::CONFIG['DLEXT']
SO_NAME   = "#{EXT_NAME}.#{DLEXT}"
SO_PATH   = File.join(LIB_DIR, SO_NAME)
RUBY_BIN  = RbConfig.ruby
NPROC     = Etc.respond_to?(:nprocessors) ? Etc.nprocessors : 4

SOURCES = FileList["#{EXT_DIR}/*.c", "#{EXT_DIR}/*.h", "#{EXT_DIR}/extconf.rb"]

CLEAN.include('tmp')
CLOBBER.include(SO_PATH, 'pkg')

# `rake compile EXTOPTS=--enable-paranoid` to build an extension running the
# full coherency check after every mutation
EXTOPTS = (ENV['EXTOPTS'] || '').split

directory BUILD_DIR
directory LIB_DIR

file SO_PATH => SOURCES + [BUILD_DIR, LIB_DIR] do
  Dir.chdir(BUILD_DIR) do
    sh(RUBY_BIN, File.join(EXT_DIR, 'extconf.rb'), *EXTOPTS)
    sh('make', "-j#{NPROC}")
  end
  FileUtils.cp(File.join(BUILD_DIR, SO_NAME), SO_PATH, verbose: true)
end

desc 'Build the C extension'
task compile: SO_PATH

# `clean` alone would not be enough: rake never considers a file task out of
# date because of a directory prerequisite, so the stale .so has to go
desc 'Rebuild the C extension from scratch'
task :recompile do
  Rake::Task[:clean].invoke
  FileUtils.rm_f(SO_PATH)
  [BUILD_DIR, LIB_DIR, SO_PATH].each { |t| Rake::Task[t].reenable }
  Rake::Task[SO_PATH].invoke
end

desc 'Run the ruby test suite'
task test: :compile do
  sh(RUBY_BIN, '-Ilib', '-Itest', 'test/test_disjoint_interval_tree.rb')
end

namespace :test do
  desc 'Run the C test suite (debug, sanitizers and release builds)'
  task :c do
    sh('make', '-C', 'test/c', 'all')
  end

  desc 'Run the C test suite under valgrind'
  task :valgrind do
    sh('make', '-C', 'test/c', 'valgrind')
  end

  # `EXTOPTS` is read when the Rakefile is loaded, hence the sub-invocation.
  # The regular extension is then rebuilt, so that a paranoid - and much
  # slower - build is never left behind in lib/
  desc 'Run the ruby test suite against a paranoid build of the extension'
  task :paranoid do
    sh({ 'EXTOPTS' => '--enable-paranoid' }, RUBY_BIN, $PROGRAM_NAME, 'recompile', 'test')
  ensure
    sh(RUBY_BIN, $PROGRAM_NAME, 'recompile')
  end
end

desc 'Run both the C and the ruby test suites'
task default: ['test:c', :test]

desc 'Build the gem into pkg/'
task :gem do
  FileUtils.mkdir_p('pkg')
  sh('gem', 'build', "#{EXT_NAME}.gemspec")
  FileUtils.mv(FileList["#{EXT_NAME}-*.gem"].to_a, 'pkg', verbose: true)
end
