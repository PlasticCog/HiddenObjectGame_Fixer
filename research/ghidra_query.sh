#!/bin/sh
# Run a Ghidra script against an already-analyzed project (no re-analysis).
# usage: ghidra_query.sh <ProjectName> <Script.java> [script args...]
# e.g.   ghidra_query.sh Kronville DecompileAt.java /tmp/out.c 0x4dee80 callers:0x503f00
G="${GHIDRA_DIR:-C:/Users/Plastik2024/Tools/ghidra_12.1.4_PUBLIC}"
W="${HOGFIX_GHIDRA_WORK:-$(cygpath -w "$TEMP")\hogfixer_ghidra}"
HERE="$(cd "$(dirname "$0")" && pwd -W)"
proj="$1"; script="$2"; shift 2
exec "$G/support/analyzeHeadless.bat" "$W\proj_$proj" "$proj" -process -noanalysis -readOnly \
  -scriptPath "$HERE/ghidra_scripts" -postScript "$script" "$@" 2>&1 | grep -E "ERROR|SCRIPT|Exception" | grep -v "Invalid PNG"
