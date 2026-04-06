CC       ?= gcc
CFLAGS   ?= -Wall -Wextra -Werror -std=c11
CPPFLAGS ?= -D_POSIX_C_SOURCE=200809L -D_GNU_SOURCE

SRCDIR   = buildsysdep
SRCS     = $(SRCDIR)/neo_core.c $(SRCDIR)/neo_arena.c $(SRCDIR)/neo_platform.c \
           $(SRCDIR)/neo_command.c $(SRCDIR)/neo_deps.c $(SRCDIR)/neo_compile.c \
           $(SRCDIR)/neo_graph.c $(SRCDIR)/neo_toolchain.c $(SRCDIR)/neo_detect.c \
           $(SRCDIR)/neo_install.c $(SRCDIR)/neo_test_runner.c $(SRCDIR)/neo_config.c
OBJS     = $(SRCS:.c=.o)

.PHONY: build test clean docs

build: $(OBJS)

%.o: %.c
	$(CC) -c $< -o $@ $(CFLAGS) $(CPPFLAGS)

test: $(OBJS)
	bash tests/run_tests.sh

clean:
	rm -f $(SRCDIR)/*.o
	rm -rf /tmp/neo_test_bins
	find tests -name '*.o' -delete 2>/dev/null || true

docs:
	@echo "Run: npx docsify-cli serve docs"
	@echo " or: cd docs && python3 -m http.server 3000"
