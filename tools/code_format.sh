#!/bin/bash
#
# Auto format script for X-MAP project
# Formats C/C++ files in maptile/ and example/, and CMake files
#

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

# Check if clang-format is installed
if command -v clang-format-14 &>/dev/null; then
    CLANG_FORMAT=clang-format-14
elif command -v clang-format &>/dev/null; then
    CLANG_FORMAT=clang-format
else
    echo "Error: clang-format is not installed"
    echo "Install it with: sudo apt install clang-format-14"
    exit 1
fi

# Check if cmake-format is installed
CMAKE_FORMAT=""
export PATH="$PATH:$HOME/.local/bin"
if command -v cmake-format &>/dev/null; then
    CMAKE_FORMAT=cmake-format
elif command -v gersemi &>/dev/null; then
    CMAKE_FORMAT=gersemi
fi

echo "Using clang-format: $CLANG_FORMAT"
echo "Version: $($CLANG_FORMAT --version)"
if [[ -n "$CMAKE_FORMAT" ]]; then
    echo "Using cmake-format: $CMAKE_FORMAT"
else
    echo "Warning: cmake-format/gersemi not installed, CMake files will be skipped"
    echo "Install with: pip install cmake-format  OR  pip install gersemi"
fi

# Directories to format (C/C++ files)
FORMAT_DIRS=(
    "$PROJECT_ROOT/maptile"
    "$PROJECT_ROOT/example"
    "$PROJECT_ROOT/tests"
)

# File extensions to format
EXTENSIONS=("*.c" "*.cpp" "*.h" "*.hpp")

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

echo "========================================="
echo "X-MAP Code Formatter"
echo "========================================="
echo ""

CHECK_MODE=false
if [[ "$1" == "--check" ]]; then
    CHECK_MODE=true
    echo -e "${YELLOW}Running in check mode (no changes will be made)${NC}"
    echo ""
fi

TOTAL_FILES=0
FORMATTED_FILES=0
FAILED_FILES=0

format_file() {
    local file="$1"
    TOTAL_FILES=$((TOTAL_FILES + 1))

    if $CHECK_MODE; then
        if ! $CLANG_FORMAT --dry-run --Werror "$file" 2>/dev/null; then
            echo -e "  ${RED}[NEEDS FORMAT]${NC} $file"
            FAILED_FILES=$((FAILED_FILES + 1))
        else
            echo -e "  ${GREEN}[OK]${NC} $file"
            FORMATTED_FILES=$((FORMATTED_FILES + 1))
        fi
    else
        if $CLANG_FORMAT -i "$file" 2>/dev/null; then
            echo -e "  ${GREEN}[FORMATTED]${NC} $file"
            FORMATTED_FILES=$((FORMATTED_FILES + 1))
        else
            echo -e "  ${RED}[FAILED]${NC} $file"
            FAILED_FILES=$((FAILED_FILES + 1))
        fi
    fi
}

format_cmake_file() {
    local file="$1"
    TOTAL_FILES=$((TOTAL_FILES + 1))

    if $CHECK_MODE; then
        if [[ "$CMAKE_FORMAT" == "cmake-format" ]]; then
            FORMATTED_CONTENT=$($CMAKE_FORMAT "$file" 2>/dev/null)
            ORIGINAL_CONTENT=$(cat "$file")
            if [[ "$FORMATTED_CONTENT" != "$ORIGINAL_CONTENT" ]]; then
                echo -e "  ${RED}[NEEDS FORMAT]${NC} $file"
                FAILED_FILES=$((FAILED_FILES + 1))
            else
                echo -e "  ${GREEN}[OK]${NC} $file"
                FORMATTED_FILES=$((FORMATTED_FILES + 1))
            fi
        else
            if ! $CMAKE_FORMAT --check "$file" 2>/dev/null; then
                echo -e "  ${RED}[NEEDS FORMAT]${NC} $file"
                FAILED_FILES=$((FAILED_FILES + 1))
            else
                echo -e "  ${GREEN}[OK]${NC} $file"
                FORMATTED_FILES=$((FORMATTED_FILES + 1))
            fi
        fi
    else
        if [[ "$CMAKE_FORMAT" == "cmake-format" ]]; then
            if $CMAKE_FORMAT -i "$file" 2>/dev/null; then
                echo -e "  ${GREEN}[FORMATTED]${NC} $file"
                FORMATTED_FILES=$((FORMATTED_FILES + 1))
            else
                echo -e "  ${RED}[FAILED]${NC} $file"
                FAILED_FILES=$((FAILED_FILES + 1))
            fi
        else
            if $CMAKE_FORMAT --in-place "$file" 2>/dev/null; then
                echo -e "  ${GREEN}[FORMATTED]${NC} $file"
                FORMATTED_FILES=$((FORMATTED_FILES + 1))
            else
                echo -e "  ${RED}[FAILED]${NC} $file"
                FAILED_FILES=$((FAILED_FILES + 1))
            fi
        fi
    fi
}

# Process C/C++ files
for dir in "${FORMAT_DIRS[@]}"; do
    if [[ ! -d "$dir" ]]; then
        echo -e "${YELLOW}Warning: Directory not found: $dir${NC}"
        continue
    fi

    echo "Processing C/C++: $dir"

    for ext in "${EXTENSIONS[@]}"; do
        while IFS= read -r -d '' file; do
            # Skip third-party headers
            case "$file" in
                */doctest.h) continue ;;
            esac
            format_file "$file"
        done < <(find "$dir" -type f -name "$ext" -print0 2>/dev/null)
    done
done

# Process CMake files
if [[ -n "$CMAKE_FORMAT" ]]; then
    echo ""
    echo "Processing CMake files..."

    # Root CMakeLists.txt
    if [[ -f "$PROJECT_ROOT/CMakeLists.txt" ]]; then
        format_cmake_file "$PROJECT_ROOT/CMakeLists.txt"
    fi

    # CMake files in maptile/ and example/
    for dir in "${FORMAT_DIRS[@]}"; do
        if [[ ! -d "$dir" ]]; then
            continue
        fi

        while IFS= read -r -d '' file; do
            format_cmake_file "$file"
        done < <(find "$dir" -type f \( -name "CMakeLists.txt" -o -name "*.cmake" \) -not -path "*/build/*" -print0 2>/dev/null)
    done
fi

echo ""
echo "========================================="
echo "Summary:"
echo "  Total files:     $TOTAL_FILES"
if $CHECK_MODE; then
    echo "  Properly formatted: $FORMATTED_FILES"
    echo "  Need formatting:    $FAILED_FILES"
else
    echo "  Formatted:       $FORMATTED_FILES"
    echo "  Failed:          $FAILED_FILES"
fi
echo "========================================="

if $CHECK_MODE && [[ $FAILED_FILES -gt 0 ]]; then
    exit 1
fi

exit 0
