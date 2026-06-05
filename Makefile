# SimAnt SNES decomp — build all lifted modules into one binary.
#
# This is meant to PROVE all the lifted code links together. The binary
# isn't a runnable SimAnt port — it's a structural artifact showing the
# decomp pieces are coherent. Stubs.c provides empty bodies for every
# subroutine not yet lifted.
#
# Usage:
#   make            # build everything
#   make clean      # rm objects + binary
#   make check      # individual compile-check each .c file
#   make count      # line counts per module
#   make stubs      # regenerate stubs.c from the current unresolved-symbols list

CC      ?= clang
CFLAGS  ?= -Wall -Wextra -Wno-unused-function -O0 -g
LDFLAGS ?=

SRCS = simant.c \
       entities_a.c entities_b.c entities_c.c entities_d.c \
       states_menu.c states_gameplay.c \
       vsync.c mouse.c \
       stubs.c

# Round-2 / round-3 / round-4 modules — included if present so partial builds work.
OPTIONAL_SRCS = scent.c control_panels.c simulation.c scenarios.c \
                ui_menus.c player_actions.c text_screens.c save_options.c \
                misc_helpers.c \
                gaps.c territory.c combat.c text_content.c \
                render_helpers.c assets.c audio_intro.c \
                asset_data_1.c asset_data_2.c asset_data_3.c \
                asset_data_4.c asset_data_5.c asset_data_6.c \
                audio_driver.c gap_fillers.c player_actions_full.c \
                lifted_helpers_1.c lifted_helpers_2.c lifted_helpers_3.c \
                lifted_helpers_4.c lifted_helpers_5.c lifted_helpers_6.c
SRCS += $(wildcard $(OPTIONAL_SRCS))

OBJS = $(SRCS:.c=.o)
BIN  = simant_decomp

all: $(BIN)

$(BIN): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $(OBJS) 2>&1 | grep -v 'reducing alignment' || true

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

check:
	@for f in $(SRCS); do \
	  printf "%-22s " $$f; \
	  if $(CC) $(CFLAGS) -c $$f -o /tmp/check.o 2>/tmp/check.err; then \
	    echo "OK"; \
	  else \
	    echo "FAIL"; cat /tmp/check.err | head -3; \
	  fi; \
	done

count:
	@wc -l $(SRCS)
	@printf "\nTotal:\n"
	@cat $(SRCS) | wc -l

stubs:
	@clang -c $(filter-out stubs.c,$(SRCS)) 2>/dev/null || true
	@clang -o /tmp/link_probe $(filter-out stubs.c,$(SRCS:.c=.o)) 2>&1 | \
	     grep '"_' | sort -u | sed 's/.*"_/  /' | sed 's/",.*//' > /tmp/missing_syms.txt
	@python3 gen_stubs.py

clean:
	rm -f $(OBJS) $(BIN)

.PHONY: all check count stubs clean
