#!/bin/bash

set -o pipefail

BUILD_DIR_LINUX="./build_linux"
BUILD_DIR_WIN="./build_win"
BUILD_FOR_WINDOWS=0
BUILD_ALL=0
CLEAN_BUILD=0
BUILD_TESTS=0
ENABLE_COVERAGE=0

# Parse arguments
while [[ $# -gt 0 ]]; do
  case "$1" in
    --clean|-c)
      CLEAN_BUILD=1
      ;;
    --windows|-w)
      BUILD_FOR_WINDOWS=1
      ;;
    --all|-a)
      BUILD_ALL=1
      ;;
    --test|-t)
      BUILD_TESTS=1
      ENABLE_COVERAGE=1
      ;;
    --coverage|-cov)
      BUILD_TESTS=1
      ENABLE_COVERAGE=1
      ;;
    --help)
      echo "Usage: build.sh [--clean|-c] [--windows|-w] [--all|-a] [--test|-t] [--help]"
      echo ""
      echo "Options:"
      echo "  --clean, -c       Clean build directories before building"
      echo "  --windows, -w     Build for Windows (cross-compile)"
      echo "  --all, -a         Build for both Windows and Linux in parallel"
      echo "  --test, -t        Build and run unit tests with coverage report"
      exit 0
      ;;
    *)
      echo "Unknown option: $1"
      exit 1
      ;;
  esac
  shift
done

# Ensure log directory exists
LOG_DIR="./logs"
mkdir -p "$LOG_DIR"

# Signal handler for graceful interruption
handle_interrupt() {
  echo ""
  echo "Build interrupted. Logs saved up to this point."
  # Kill any remaining child processes in our process group
  pkill -P $$ 2>/dev/null || true
  exit 130
}
trap handle_interrupt SIGINT SIGTERM

format_code() {
  echo "Formatting source files..."
  find src/ include/models include/panels include/system \
    \( -name "*.cpp" -o -name "*.h" \) \
    | xargs clang-format -i
}

# Handle clean build
if [[ $CLEAN_BUILD -eq 1 ]]; then
  echo "Deleting Build Directories"
  rm -rf "$BUILD_DIR_LINUX" "$BUILD_DIR_WIN"
fi

format_code

if [[ $BUILD_ALL -eq 1 ]]; then
  # Parallel build: both targets run concurrently, each logs to ./logs/
  LOG_DIR="./logs"
  mkdir -p "$LOG_DIR"

  echo "Starting Windows and Linux builds in parallel..."

  # Use stdbuf -oL to force line-buffering so logs write in real-time
  (
    stdbuf -oL cmake -B"$BUILD_DIR_WIN" -H. \
      --log-level=VERBOSE \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_SYSTEM_NAME=Windows \
      -DCMAKE_C_COMPILER="x86_64-w64-mingw32-gcc-posix" \
      -DCMAKE_CXX_COMPILER="x86_64-w64-mingw32-g++-posix"
    stdbuf -oL make -C"$BUILD_DIR_WIN" -j6 VERBOSE=1
  ) > "$LOG_DIR/build_win.log" 2>&1 &
  WIN_PID=$!

  (
    stdbuf -oL cmake -B"$BUILD_DIR_LINUX" -H. --log-level=VERBOSE -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_STANDARD=20
    stdbuf -oL make -C"$BUILD_DIR_LINUX" -j6 VERBOSE=1
  ) > "$LOG_DIR/build_linux.log" 2>&1 &
  LIN_PID=$!

  wait $WIN_PID; WIN_EXIT=$?
  wait $LIN_PID; LIN_EXIT=$?

  [[ $WIN_EXIT -eq 0 ]] && echo "Windows: PASS" || echo "Windows: FAIL (see logs/build_win.log)"
  [[ $LIN_EXIT -eq 0 ]] && echo "Linux:   PASS" || echo "Linux:   FAIL (see logs/build_linux.log)"

  # Quill captures this script's stdout/stderr and passes it to the implementation retry. The
  # parallel jobs write detailed output to files, so surface only the failing logs before exiting.
  if [[ $WIN_EXIT -ne 0 ]]; then
    echo "===== logs/build_win.log ====="
    cat "$LOG_DIR/build_win.log"
    echo "===== end logs/build_win.log ====="
  fi
  if [[ $LIN_EXIT -ne 0 ]]; then
    echo "===== logs/build_linux.log ====="
    cat "$LOG_DIR/build_linux.log"
    echo "===== end logs/build_linux.log ====="
  fi

  # Either target failing fails the script (see the exit at the end).
  [[ $WIN_EXIT -eq 0 && $LIN_EXIT -eq 0 ]] || SCRIPT_EXIT=1
else
  # Single-target builds: synchronous, set -e active
  set -e

  # Build for Windows
  if [[ $BUILD_FOR_WINDOWS -eq 1 ]]; then
    echo "Configuring Windows build..."
    CMAKE_FLAGS="-DCMAKE_BUILD_TYPE=Release -DCMAKE_SYSTEM_NAME=Windows -DCMAKE_C_COMPILER=x86_64-w64-mingw32-gcc-posix -DCMAKE_CXX_COMPILER=x86_64-w64-mingw32-g++-posix"
    
    if [[ $BUILD_TESTS -eq 1 ]]; then
      CMAKE_FLAGS="$CMAKE_FLAGS -DBUILD_TESTS=ON"
    fi
    
    if [[ $ENABLE_COVERAGE -eq 1 ]]; then
      CMAKE_FLAGS="$CMAKE_FLAGS -DENABLE_COVERAGE=ON"
    fi
    
    WIN_LOG="$LOG_DIR/build_win.log"
    { cmake -B"$BUILD_DIR_WIN" -H. --log-level=VERBOSE $CMAKE_FLAGS; } 2>&1 | tee "$WIN_LOG"
    { make -C"$BUILD_DIR_WIN" -j6 VERBOSE=1; } 2>&1 | tee -a "$WIN_LOG"
  else
    # Default: Linux only
    echo "Configuring Linux build..."
    CMAKE_FLAGS="-DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_STANDARD=20"
    
    if [[ $BUILD_TESTS -eq 1 ]]; then
      CMAKE_FLAGS="$CMAKE_FLAGS -DBUILD_TESTS=ON"
    fi
    
    if [[ $ENABLE_COVERAGE -eq 1 ]]; then
      CMAKE_FLAGS="$CMAKE_FLAGS -DENABLE_COVERAGE=ON"
    fi
    
    LINUX_LOG="$LOG_DIR/build_linux.log"
    { cmake -B"$BUILD_DIR_LINUX" -H. --log-level=VERBOSE $CMAKE_FLAGS; } 2>&1 | tee "$LINUX_LOG"
    { make -C"$BUILD_DIR_LINUX" -j6 VERBOSE=1; } 2>&1 | tee -a "$LINUX_LOG"
    
    # Run tests if requested
    if [[ $BUILD_TESTS -eq 1 ]]; then
      LOG_DIR="./logs"
      mkdir -p "$LOG_DIR"
      TEST_LOG="$LOG_DIR/test_output.log"

      # Run tests. Keep going even if they fail so coverage below still runs, but remember the
      # real exit status and fail the script on it at the end -- a swallowed status here reports
      # a red test suite as a green build to every caller (CI, quill's build_test gate).
      # PIPESTATUS[0] is gtest's status, not tee's, which is what the pipeline would otherwise
      # return; the `|| true` keeps `set -e` (line 112) from aborting before coverage.
      ./build_linux/WorkbenchTests --gtest_color=yes 2>&1 | tee "$TEST_LOG" || true
      SCRIPT_EXIT=${PIPESTATUS[0]}
      
      # Generate coverage report using gcov (GCC format)
      if command -v gcov &> /dev/null; then
        echo ""
        echo "========================================"
        echo "         CODE COVERAGE REPORT"
        echo "========================================"
        
        cd ./build_linux

        # Run gcov on all source gcno files, filter to project source files only
        gcov -b $(find . -path "./tests/CMakeFiles/WorkbenchTests.dir/__/src/*.gcno" 2>/dev/null) 2>/dev/null > /tmp/gcov_output.txt 2>&1

        # Parse gcov output, only counting files under our src/ directory
        awk '
          /^File .*\/src\//{active=1; next}
          /^File /{active=0; next}
          active && /^Lines executed:/{
            split($0, a, ":")
            split(a[2], b, "% of ")
            pct = b[1] + 0
            lines = b[2] + 0
            if (lines > 0) {
              total += lines
              exec += lines * pct / 100
            }
            active=0
          }
        END {
          if (total > 0)
            printf "Total lines: %d\nExecuted: %d\nOVERALL COVERAGE: %.1f%%\n", total, exec, exec*100/total
          else
            print "No coverage data"
        }' /tmp/gcov_output.txt

        cd ..
      fi
    fi
  fi
fi

# Single exit for every path: the parallel --all branch sets this on a build failure, the
# single-target branch sets it to the test suite's real status. Unset means nothing failed
# (a build-only run whose failures already aborted under `set -e`), so 0.
exit "${SCRIPT_EXIT:-0}"
