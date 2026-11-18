# Host-side tests for components/core (plain C11, no ESP-IDF).
CC ?= cc
CFLAGS := -std=c11 -Wall -Wextra -Werror -Icomponents/core/include
CORE_SRC := $(wildcard components/core/*.c)
TESTS := $(wildcard tests/test_*.c)
BIN := build-host

.PHONY: test
test:
	@mkdir -p $(BIN)
	@for t in $(TESTS); do \
	  n=$$(basename $$t .c); \
	  $(CC) $(CFLAGS) $$t $(CORE_SRC) -o $(BIN)/$$n || exit 1; \
	  $(BIN)/$$n || exit 1; \
	  echo "PASS $$n"; \
	done
