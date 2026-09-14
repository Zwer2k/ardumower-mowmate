CXX ?= g++
CXXFLAGS = -std=c++17 -O2 -Wall -Wextra -I../lib/pathplanner -I../lib/Clipper2/CPP/Clipper2Lib/include

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

all: $(TESTS)

$(TESTS): %: %.cpp $(PLANNER_SRC) $(CLIPPER_SRC)
	$(CXX) $(CXXFLAGS) -o $@ $< $(PLANNER_SRC) $(CLIPPER_SRC)

# Runs every test and stops at the first failure.
run: $(TESTS)
	@for t in $(TESTS); do echo "== $$t"; ./$$t || exit 1; done

clean:
	rm -f $(TESTS)

.PHONY: all run clean
