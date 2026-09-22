#!/usr/bin/env bash
# add-kicad-lib.sh — Add a KiCad library (symbol/footprint) as a git submodule
# and wire it into the project's sym-lib-table / fp-lib-table.
#
# Usage:
#   ./add-kicad-lib.sh <git-url> [lib-name]
#
#   git-url   HTTPS or SSH URL of the KiCad library git repo
#   lib-name  Optional friendly name (defaults to repo name without .git)
#
# Examples:
#   ./add-kicad-lib.sh https://github.com/Zektopic/esp32-h2-supermini-kicad.git
#   ./add-kicad-lib.sh https://github.com/someuser/some-kicad-lib.git MyLib
#
# Run from the weathernerd-hub project dir (or any dir with sym-lib-table/fp-lib-table).
set -euo pipefail

# ── setup ──────────────────────────────────────────────────────────────
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="${PROJECT_DIR:-$SCRIPT_DIR}"
REPO_ROOT="$(cd "$PROJECT_DIR/../.." && pwd)"  # weathernerd repo root

LIBS_DIR="$PROJECT_DIR/libs"
SYM_TABLE="$PROJECT_DIR/sym-lib-table"
FP_TABLE="$PROJECT_DIR/fp-lib-table"

# ── args ───────────────────────────────────────────────────────────────
if [[ $# -lt 1 ]]; then
  echo "Usage: $0 <git-url> [lib-name]" >&2
  exit 1
fi

GIT_URL="$1"
LIB_NAME="${2:-}"

# Derive lib name from URL if not provided
if [[ -z "$LIB_NAME" ]]; then
  LIB_NAME="$(basename "$GIT_URL" .git)"
fi

SUBMODULE_PATH="$LIBS_DIR/$LIB_NAME"

# ── checks ─────────────────────────────────────────────────────────────
if [[ -d "$SUBMODULE_PATH" ]]; then
  echo "ERROR: $SUBMODULE_PATH already exists" >&2
  exit 1
fi

# ── add submodule ──────────────────────────────────────────────────────
echo "==> Adding submodule $LIB_NAME ..."
cd "$REPO_ROOT"
git submodule add "$GIT_URL" "$(realpath --relative-to="$REPO_ROOT" "$SUBMODULE_PATH")"

# ── scan for libraries ─────────────────────────────────────────────────
echo "==> Scanning for KiCad library files ..."

SYM_FILES=()
FP_DIRS=()
FP_FILES=()

while IFS= read -r f; do
  SYM_FILES+=("$f")
done < <(find "$SUBMODULE_PATH" -name "*.kicad_sym" -type f 2>/dev/null)

while IFS= read -r d; do
  FP_DIRS+=("$d")
done < <(find "$SUBMODULE_PATH" -name "*.pretty" -type d 2>/dev/null)

while IFS= read -r f; do
  FP_FILES+=("$f")
done < <(find "$SUBMODULE_PATH" -name "*.kicad_mod" -type f 2>/dev/null)

# ── handle loose .kicad_mod files (wrap in a .pretty dir) ──────────────
if [[ ${#FP_DIRS[@]} -eq 0 && ${#FP_FILES[@]} -gt 0 ]]; then
  PRETTY_DIR="$LIBS_DIR/${LIB_NAME}.pretty"
  echo "==> No .pretty dir found; creating wrapper: $PRETTY_DIR"
  mkdir -p "$PRETTY_DIR"
  for mod in "${FP_FILES[@]}"; do
    ln -sf "$(realpath --relative-to="$PRETTY_DIR" "$mod")" "$PRETTY_DIR/$(basename "$mod")"
  done
  FP_DIRS+=("$PRETTY_DIR")
fi

# ── update sym-lib-table ───────────────────────────────────────────────
add_to_table() {
  local table="$1" name="$2" uri="$3" descr="$4"
  local entry="  (lib (name \"${name}\")(type \"KiCad\")(uri \"${uri}\")(options \"\")(descr \"${descr}\"))"

  # Create file if missing
  if [[ ! -f "$table" ]]; then
    local kind
    kind="$(basename "$table" | sed 's/-lib-table//; s/-/_/g')"
    echo "(${kind}_lib_table" > "$table"
    echo "  (version 7)" >> "$table"
    echo ")" >> "$table"
  fi

  # Skip if already present
  if grep -q "\"${name}\"" "$table" 2>/dev/null; then
    echo "   → '$name' already in $(basename "$table"), skipping"
    return
  fi

  # Insert before closing paren
  sed -i "/^)/i\\${entry}" "$table"
  echo "   → Added '$name' to $(basename "$table")"
}

echo "==> Updating lib-tables ..."

for sym in "${SYM_FILES[@]}"; do
  sym_name="$(basename "$sym" .kicad_sym)"
  rel_uri="\${KIPRJMOD}/$(realpath --relative-to="$PROJECT_DIR" "$sym")"
  add_to_table "$SYM_TABLE" "$sym_name" "$rel_uri" "$LIB_NAME symbol library"
done

for fpd in "${FP_DIRS[@]}"; do
  fp_name="$(basename "$fpd" .pretty)"
  rel_uri="\${KIPRJMOD}/$(realpath --relative-to="$PROJECT_DIR" "$fpd")"
  add_to_table "$FP_TABLE" "$fp_name" "$rel_uri" "$LIB_NAME footprint library"
done

# ── summary ─────────────────────────────────────────────────────────────
echo ""
echo "Done! Summary:"
echo "  Submodule:  $SUBMODULE_PATH"
echo "  Symbols:    ${#SYM_FILES[@]} library file(s)"
echo "  Footprints: ${#FP_DIRS[@]} library dir(s)"
echo ""
echo "Restart KiCad (or reopen the project) to pick up the new libraries."
