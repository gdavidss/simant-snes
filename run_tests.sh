#!/bin/sh
# Build + run the SimAnt decomp behavioral test harness.
#
# Usage:  cd /Users/guilhermedavid/simant-re && sh run_tests.sh
set -e

CC=${CC:-clang}
CFLAGS="-Wall -Wextra -Wno-unused-function -O0 -g"

# Compile only the test glue. All other .o files are already produced by
# `make` (the main build). If they don't exist, run `make` first.
if [ ! -f simulation.o ]; then
    echo "running make to produce object files first"
    make
fi

$CC $CFLAGS -c tests.c -o tests.o
$CC $CFLAGS -c stubs_for_test.c -o stubs_for_test.o

# Link everything except stubs.o (which defines its own main()).
OBJS="tests.o stubs_for_test.o stubs_test_extras.o \
      simulation.o scent.o combat.o \
      simant.o entities_a.o entities_b.o entities_c.o entities_d.o \
      states_gameplay.o states_menu.o vsync.o mouse.o \
      control_panels.o scenarios.o ui_menus.o player_actions.o \
      text_screens.o save_options.o misc_helpers.o \
      gaps.o territory.o text_content.o render_helpers.o \
      assets.o audio_intro.o asset_data_1.o asset_data_2.o asset_data_3.o \
      asset_data_4.o asset_data_5.o asset_data_6.o audio_driver.o \
      gap_fillers.o player_actions_full.o \
      lifted_helpers_1.o lifted_helpers_2.o lifted_helpers_3.o \
      lifted_helpers_4.o lifted_helpers_5.o lifted_helpers_6.o"

$CC -o test_runner $OBJS 2>&1 | grep -v 'reducing alignment' || true

./test_runner
