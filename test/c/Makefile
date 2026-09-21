# Unit tests for the C disjoint interval tree.
#
#   make            build and run every configuration
#   make debug      build and run with assertions + paranoid coherency checks
#   make sanitize   build and run under address/undefined sanitizers
#   make release    build and run with -DNDEBUG -O3
#   make valgrind   run the debug build under valgrind
#   make clean

CC      ?= cc
SRCDIR   = ../../ext/disjoint_interval_tree
WARNINGS = -Wall -Wextra -Wpedantic -Wshadow -Wcast-qual -Wwrite-strings \
           -Wstrict-prototypes -Wmissing-prototypes -Wconversion -Wsign-conversion
CFLAGS  += -std=gnu11 $(WARNINGS) -I$(SRCDIR)
LDFLAGS +=

SRC      = $(SRCDIR)/dit.c test_dit.c
HEADERS  = $(SRCDIR)/dit.h

SEED    ?= 1

.PHONY: all debug sanitize release valgrind clean

all: debug sanitize release

# assertions on, plus a full coherency check after each mutation
debug: test_dit_debug
	@echo "=== debug ==="
	./test_dit_debug $(SEED)

test_dit_debug: $(SRC) $(HEADERS)
	$(CC) $(CFLAGS) -O0 -g3 -DDIT_PARANOID=1 -o $@ $(SRC) $(LDFLAGS)

# assertions on, sanitizers on
sanitize: test_dit_asan
	@echo "=== sanitize ==="
	ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
		./test_dit_asan $(SEED)

test_dit_asan: $(SRC) $(HEADERS)
	$(CC) $(CFLAGS) -O1 -g3 -DDIT_PARANOID=1 \
		-fsanitize=address,undefined -fno-omit-frame-pointer \
		-o $@ $(SRC) $(LDFLAGS)

# what a user of the library actually compiles
release: test_dit_release
	@echo "=== release ==="
	./test_dit_release $(SEED)

test_dit_release: $(SRC) $(HEADERS)
	$(CC) $(CFLAGS) -O3 -DNDEBUG -o $@ $(SRC) $(LDFLAGS)

valgrind: test_dit_debug
	valgrind --error-exitcode=1 --leak-check=full --track-origins=yes \
		./test_dit_debug $(SEED)

clean:
	$(RM) test_dit_debug test_dit_asan test_dit_release
