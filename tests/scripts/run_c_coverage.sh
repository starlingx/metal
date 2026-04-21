#!/bin/bash
# C/C++ Coverage Analysis Script for metal project
# Uses gcov/lcov to measure coverage of C/C++ source files
set -e

PROJECT_ROOT="${1:-.}"
UTCOVERAGE_DIR="${PROJECT_ROOT}/UTCoverage"
mkdir -p "${UTCOVERAGE_DIR}"

# Check for required tools
for tool in gcc g++ gcov lcov genhtml; do
    if ! command -v "$tool" &>/dev/null; then
        echo "WARNING: $tool not found. Skipping C/C++ coverage." | tee "${UTCOVERAGE_DIR}/coverage-c_skipped.txt"
        exit 0
    fi
done

echo "=== C/C++ Coverage Analysis for metal ==="
echo ""

# Collect all C/C++ source files
CPP_FILES=$(find "${PROJECT_ROOT}" -type f \( -name '*.cpp' -o -name '*.c' \) \
    -not -path '*/.tox/*' -not -path '*/UTCoverage/*' -not -path '*/tests/*' | sort)
H_FILES=$(find "${PROJECT_ROOT}" -type f \( -name '*.h' -o -name '*.hpp' \) \
    -not -path '*/.tox/*' -not -path '*/UTCoverage/*' -not -path '*/tests/*' | sort)

CPP_COUNT=$(echo "$CPP_FILES" | grep -c '.' || true)
H_COUNT=$(echo "$H_FILES" | grep -c '.' || true)

echo "Found ${CPP_COUNT} C/C++ source files and ${H_COUNT} header files"
echo ""

# Create a temporary build directory
BUILD_DIR=$(mktemp -d)
trap "rm -rf ${BUILD_DIR}" EXIT

# We compile individual source files with --coverage to get gcov data
# Since this is a complex project with many dependencies, we compile
# what we can and report coverage on compilable units
COMPILED=0
FAILED=0
TOTAL_LINES=0
COVERED_LINES=0

REPORT_FILE="${UTCOVERAGE_DIR}/coverage_c_report.txt"
echo "=== C/C++ Coverage Report for metal ===" > "${REPORT_FILE}"
echo "Generated: $(date)" >> "${REPORT_FILE}"
echo "" >> "${REPORT_FILE}"

# Try to compile standalone C files first (fsync.c, amon.c)
STANDALONE_C_FILES=""
for cfile in "${PROJECT_ROOT}/mtce/src/fsync/fsync.c" \
    "${PROJECT_ROOT}/mtce/src/public/amon.c"; do
    if [ -f "$cfile" ]; then
        STANDALONE_C_FILES="${STANDALONE_C_FILES} ${cfile}"
        BASENAME=$(basename "$cfile" .c)
        OBJ="${BUILD_DIR}/${BASENAME}.o"

        # Compile with coverage flags - allow failure for missing deps
        if gcc -c --coverage -fprofile-arcs -ftest-coverage \
            -I"${PROJECT_ROOT}/mtce/src/public" \
            -I"${PROJECT_ROOT}/mtce-common/src/common" \
            -I"${PROJECT_ROOT}/mtce-common/src/daemon" \
            -o "$OBJ" "$cfile" 2>/dev/null; then
            COMPILED=$((COMPILED + 1))
            # Run gcov
            (cd "${BUILD_DIR}" && gcov "$OBJ" 2>/dev/null) || true
        else
            FAILED=$((FAILED + 1))
        fi
    fi
done

# Try to compile C++ files with coverage flags
INCLUDE_DIRS="-I${PROJECT_ROOT}/mtce-common/src/common \
    -I${PROJECT_ROOT}/mtce-common/src/daemon \
    -I${PROJECT_ROOT}/mtce/src/common \
    -I${PROJECT_ROOT}/mtce/src/maintenance \
    -I${PROJECT_ROOT}/mtce/src/heartbeat \
    -I${PROJECT_ROOT}/mtce/src/hwmon \
    -I${PROJECT_ROOT}/mtce/src/alarm \
    -I${PROJECT_ROOT}/mtce/src/pmon \
    -I${PROJECT_ROOT}/mtce/src/lmon \
    -I${PROJECT_ROOT}/mtce/src/fsmon \
    -I${PROJECT_ROOT}/mtce/src/hostw \
    -I${PROJECT_ROOT}/mtce/src/public \
    -I${PROJECT_ROOT}/mtce/src/mtclog"

for cppfile in $CPP_FILES; do
    BASENAME=$(basename "$cppfile" .cpp)
    OBJ="${BUILD_DIR}/${BASENAME}_$$.o"

    if g++ -c --coverage -fprofile-arcs -ftest-coverage \
        -std=c++11 -DDEBIAN_BULLSEYE \
        ${INCLUDE_DIRS} \
        -o "$OBJ" "$cppfile" 2>/dev/null; then
        COMPILED=$((COMPILED + 1))
    else
        FAILED=$((FAILED + 1))
    fi
done

echo "Compilation Results:" >> "${REPORT_FILE}"
echo "  Successfully compiled: ${COMPILED}" >> "${REPORT_FILE}"
echo "  Failed (missing deps): ${FAILED}" >> "${REPORT_FILE}"
echo "  Total C/C++ files: ${CPP_COUNT}" >> "${REPORT_FILE}"
echo "" >> "${REPORT_FILE}"

# Generate lcov baseline
LCOV_INFO="${BUILD_DIR}/coverage.info"
if lcov --capture --directory "${BUILD_DIR}" --output-file "${LCOV_INFO}" \
    --no-external 2>/dev/null; then

    # Generate initial (zero-coverage) data for all source files
    lcov --initial --directory "${BUILD_DIR}" --capture \
        --output-file "${BUILD_DIR}/base.info" 2>/dev/null || true

    # Combine
    if [ -f "${BUILD_DIR}/base.info" ]; then
        lcov --add-tracefile "${BUILD_DIR}/base.info" \
            --add-tracefile "${LCOV_INFO}" \
            --output-file "${BUILD_DIR}/combined.info" 2>/dev/null || true
        LCOV_INFO="${BUILD_DIR}/combined.info"
    fi

    # Generate HTML report
    genhtml "${LCOV_INFO}" --output-directory "${UTCOVERAGE_DIR}/htmlcov_c" \
        2>/dev/null || true

    # Extract summary
    lcov --summary "${LCOV_INFO}" 2>&1 | tee -a "${REPORT_FILE}" || true
else
    echo "lcov capture produced no data (expected for compile-only analysis)" >> "${REPORT_FILE}"
fi

echo "" >> "${REPORT_FILE}"
echo "=== C/C++ Source File Inventory ===" >> "${REPORT_FILE}"
echo "" >> "${REPORT_FILE}"

# List all source files with line counts
for cppfile in $CPP_FILES; do
    REL_PATH=$(echo "$cppfile" | sed "s|${PROJECT_ROOT}/||")
    LINES=$(wc -l < "$cppfile")
    TOTAL_LINES=$((TOTAL_LINES + LINES))
    echo "  ${REL_PATH}: ${LINES} lines" >> "${REPORT_FILE}"
done

echo "" >> "${REPORT_FILE}"
echo "Total C/C++ source lines: ${TOTAL_LINES}" >> "${REPORT_FILE}"
echo "" >> "${REPORT_FILE}"
echo "NOTE: Full C/C++ coverage requires the complete build environment" >> "${REPORT_FILE}"
echo "with all dependencies (libfmcommon, libevent, libjson-c, etc.)." >> "${REPORT_FILE}"
echo "Coverage measurement here is based on compilation analysis." >> "${REPORT_FILE}"
echo "In a full Zuul CI environment with all deps, gcov/lcov will" >> "${REPORT_FILE}"
echo "provide accurate line-by-line coverage after running test binaries." >> "${REPORT_FILE}"

echo ""
echo "C/C++ coverage report: ${REPORT_FILE}"
cat "${REPORT_FILE}"
