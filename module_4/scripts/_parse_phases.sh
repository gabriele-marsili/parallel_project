#!/usr/bin/env bash
# Helper: convert the breakdown block printed on stderr by the M4 binaries
# into a comma-separated record of seconds (one column per phase, in the
# order documented at the top of every CSV in this folder).

set -euo pipefail

awk '
    /Histogram_local/ { hl=$3/1000 }
    /Scatter_local/   { sl=$3/1000 }
    /Comm_sizes/      { cs=$3/1000 }
    /Comm_payload/    { cp=$3/1000 }
    /Histogram_post/  { hp=$3/1000 }
    /Scatter_post/    { sp=$3/1000 }
    /Join_local/      { jl=$3/1000 }
    /Reduce_final/    { rf=$3/1000 }
    END { printf "%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f",
                  hl+0, sl+0, cs+0, cp+0, hp+0, sp+0, jl+0, rf+0 }
' "$1"
