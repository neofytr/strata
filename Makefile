CC       ?= gcc
CFLAGS   ?= -Wall -Wextra -Werror -std=c11
CPPFLAGS ?= -D_POSIX_C_SOURCE=200809L -D_GNU_SOURCE

SRC      = buildsysdep/neobuild.c
OBJ      = buildsysdep/neobuild.o

.PHONY: build test clean docs

build: $(OBJ)

$(OBJ): $(SRC)
	$(CC) -c $(SRC) -o $(OBJ) $(CFLAGS) $(CPPFLAGS)

test: $(OBJ)
	bash tests/run_tests.sh

clean:
	rm -f buildsysdep/neobuild.o
	rm -rf /tmp/neo_test_bins
	find tests -name '*.o' -delete 2>/dev/null || true

docs:
	@echo "To serve docs locally:"
	@echo "  cd docs && python3 -m http.server 8000"
	@echo "Then open http://localhost:8000 in your browser."
