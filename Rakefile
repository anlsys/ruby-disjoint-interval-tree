# frozen_string_literal: true

require 'etc'
require 'fileutils'
require 'rake/clean'
require 'rbconfig'

require_relative 'lib/disjoint_interval_tree/version'

EXT_NAME  = 'disjoint_interval_tree'
EXT_DIR   = File.expand_path("ext/#{EXT_NAME}", __dir__)
LIB_DIR   = File.expand_path("lib/#{EXT_NAME}", __dir__)
BUILD_DIR = File.expand_path("tmp/#{RUBY_PLATFORM}/#{EXT_NAME}/#{RUBY_VERSION}", __dir__)
GEM_DIR   = File.expand_path('tmp/gemtest', __dir__)
DLEXT     = RbConfig::CONFIG['DLEXT']
SO_NAME   = "#{EXT_NAME}.#{DLEXT}"
SO_PATH   = File.join(LIB_DIR, SO_NAME)
RUBY_BIN  = RbConfig.ruby
NPROC     = Etc.respond_to?(:nprocessors) ? Etc.nprocessors : 4
VERSION   = DisjointIntervalTree::VERSION
GEM_FILE  = File.expand_path("pkg/#{EXT_NAME}-#{VERSION}.gem", __dir__)

SOURCES = FileList["#{EXT_DIR}/*.c", "#{EXT_DIR}/*.h", "#{EXT_DIR}/extconf.rb"]

CLEAN.include('tmp')
CLOBBER.include(SO_PATH, 'pkg')

# Number of distinct RNG seeds the randomized tests are replayed with by
# `rake verify`. Both suites take a seed, and those randomized cross-checks
# against a naive model are the strongest correctness signal there is here.
SEEDS = Integer(ENV.fetch('SEEDS', 20))

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

  # This is the only task exercising what users actually get: it runs the suite
  # without `-Ilib`, so `require 'disjoint_interval_tree'` resolves through the
  # installed gem. A file missing from `spec.files`, a broken `extconf.rb` or a
  # wrong `require_paths` fails here and nowhere else.
  desc 'Install the built gem in a sandbox and run the ruby test suite against it'
  task gem: :build do
    FileUtils.rm_rf(GEM_DIR)
    FileUtils.mkdir_p(GEM_DIR)

    # the sandbox is the install target, but the default gem dirs stay on the
    # path so that the test suite can still find minitest
    env = {
      'GEM_HOME' => GEM_DIR,
      'GEM_PATH' => ([GEM_DIR] + Gem.path).uniq.join(File::PATH_SEPARATOR)
    }

    sh(env, 'gem', 'install', '--norc', '--no-document', '--local', GEM_FILE)

    # the gem must come from the sandbox, not from a copy installed elsewhere
    sh(env, RUBY_BIN, '-e', <<~RUBY)
      gem 'disjoint_interval_tree', '= #{VERSION}'
      require 'disjoint_interval_tree'

      path = Gem.loaded_specs['disjoint_interval_tree'].full_gem_path
      raise "loaded \#{path}, expected it under #{GEM_DIR}" unless path.start_with?('#{GEM_DIR}')

      tree = DisjointIntervalTree.new([[0, 10], [20, 30]])
      raise 'unexpected content' unless tree.intersect(5, 25) == [[0, 10], [20, 30]]
      raise 'unexpected removal' unless tree.remove(5, 25) == 2
      tree.check!

      puts "installed gem \#{DisjointIntervalTree::VERSION} loads from \#{path}"
    RUBY

    sh(env, RUBY_BIN, '-Itest', 'test/test_disjoint_interval_tree.rb')
  end

  desc 'Replay the randomized tests over SEEDS distinct seeds (default 20)'
  task seeds: :compile do
    (1..SEEDS).each do |seed|
      sh('make', '-C', 'test/c', 'debug', "SEED=#{seed}")
      sh({ 'SEED' => seed.to_s }, RUBY_BIN, '-Ilib', '-Itest',
         'test/test_disjoint_interval_tree.rb')
    end
  end

  # `spec.files` is a `Dir[]` glob over the working tree, so a file that is
  # gitignored, or simply never added, still ends up in the gem while being
  # absent from the repository. That asymmetry is silent, and it is how
  # `test/c/Makefile` - matched by a bare `Makefile` ignore rule - shipped in
  # the gem but broke every CI job.
  desc 'Check that every file the gem ships is present and tracked by git'
  task :files do
    spec = Gem::Specification.load("#{EXT_NAME}.gemspec")
    problems = []

    spec.files.sort.each do |path|
      problems << "#{path}: listed by the gemspec but missing on disk" unless File.exist?(path)

      ignored = !`git check-ignore -- #{path}`.empty?
      problems << "#{path}: shipped in the gem but matched by .gitignore" if ignored

      tracked = system("git ls-files --error-unmatch -- #{path} > /dev/null 2>&1")
      problems << "#{path}: shipped in the gem but not tracked by git" if !tracked && !ignored
    end

    # the repository needs these too, even though the gem does not ship them
    ['.gitignore', 'Rakefile', '.github/workflows/ci.yml'].each do |path|
      next if `git check-ignore -- #{path}`.empty?

      problems << "#{path}: matched by .gitignore"
    end

    unless problems.empty?
      problems.each { |p| warn "  #{p}" }
      abort "test:files: #{problems.size} problem(s)"
    end

    puts "test:files: the #{spec.files.size} files the gem ships are all present and tracked"
  end
end

desc 'Run both the C and the ruby test suites'
task default: ['test:c', :test]

desc 'Run every test suite: C, sanitizers, valgrind, ruby, paranoid, packaged gem, seed sweep'
task verify: ['test:files', 'test:c', 'test:valgrind', :test, 'test:paranoid', 'test:gem',
              'test:seeds'] do
  puts
  puts "#{EXT_NAME} #{VERSION}: everything passed"
end

# `gem build` stamps the gem with the current date, which makes it a different
# file on every run. Pinning SOURCE_DATE_EPOCH to the last commit makes the
# build byte-reproducible, so the sha256 a downstream packager - spack, nix,
# a distro - records for a release can be recomputed and checked.
def source_date_epoch
  ENV['SOURCE_DATE_EPOCH'] || begin
    epoch = `git log -1 --format=%ct 2> /dev/null`.strip
    epoch.empty? ? Time.now.to_i.to_s : epoch
  end
end

desc 'Build the gem into pkg/'
task :build do
  require 'digest'

  FileUtils.mkdir_p('pkg')
  sh({ 'SOURCE_DATE_EPOCH' => source_date_epoch }, 'gem', 'build', '--norc', "#{EXT_NAME}.gemspec")
  FileUtils.mv(FileList["#{EXT_NAME}-*.gem"].to_a, 'pkg', verbose: true)

  puts
  puts "sha256(#{File.basename(GEM_FILE)}) = #{Digest::SHA256.file(GEM_FILE).hexdigest}"
  puts 'spack recipe line:'
  puts %(  version("#{VERSION}", sha256="#{Digest::SHA256.file(GEM_FILE).hexdigest}", expand=False))
  puts
end

task gem: :build

desc "Verify, tag and publish v#{DisjointIntervalTree::VERSION} to rubygems.org"
task :release do
  abort 'release: the working tree has uncommitted changes' unless `git status --porcelain`.empty?

  branch = `git rev-parse --abbrev-ref HEAD`.strip
  abort "release: on branch #{branch}, expected main" unless branch == 'main'

  tag = "v#{VERSION}"
  abort "release: #{tag} already exists" if system("git rev-parse -q --verify refs/tags/#{tag} > /dev/null")

  Rake::Task[:verify].invoke

  sh('git', 'tag', '-a', tag, '-m', "#{EXT_NAME} #{VERSION}")
  sh('git', 'push', 'origin', 'main', '--follow-tags')
  sh('gem', 'push', GEM_FILE)

  require 'digest'
  puts
  puts "published #{tag}."
  puts
  puts 'attach the gem to the github release with:'
  puts "  gh release create #{tag} #{GEM_FILE} --title #{tag} --generate-notes"
  puts
  puts 'and record this in the spack recipe:'
  puts %(  version("#{VERSION}", sha256="#{Digest::SHA256.file(GEM_FILE).hexdigest}", expand=False))
end
