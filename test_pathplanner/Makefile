CXX ?= g++
CXXFLAGS = -std=c++17 -O2 -Wall -Wextra -I../lib/pathplanner -I../lib/Clipper2/CPP/Clipper2Lib/include

# Every binary lands in here. One .gitignore rule covers the directory, so
# adding a test below never needs another ignore entry. Not "out/" — the tests
# write their debug SVGs there, relative to the working directory.
BIN_DIR = build

CLIPPER_SRC = \
	../lib/Clipper2/CPP/Clipper2Lib/src/clipper.engine.cpp \
	../lib/Clipper2/CPP/Clipper2Lib/src/clipper.offset.cpp \
	../lib/Clipper2/CPP/Clipper2Lib/src/clipper.rectclip.cpp

PLANNER_SRC = \
	../lib/pathplanner/pathplanner.cpp \
	../lib/pathplanner/route_report.cpp

# Every test is a self-contained main() linked against the planner library.
# Add a new test by dropping its .cpp here.
TESTS = \
	test_exclusion \
	test_settings_variants \
	test_user_map \
	test_user_map2 \
	test_route_report

BINS = $(addprefix $(BIN_DIR)/,$(TESTS))

all: $(BINS)

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

$(BIN_DIR)/%: %.cpp $(PLANNER_SRC) $(CLIPPER_SRC) | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) -o $@ $< $(PLANNER_SRC) $(CLIPPER_SRC)

# Runs every test and stops at the first failure. Started from this directory
# so the tests keep writing their SVGs to out/.
run: $(BINS)
	@for t in $(TESTS); do echo "== $$t"; $(BIN_DIR)/$$t || exit 1; done

clean:
	rm -rf $(BIN_DIR)

.PHONY: all run clean
