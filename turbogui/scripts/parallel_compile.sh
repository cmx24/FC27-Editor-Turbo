# Parallel, incremental compiles for build_win.sh and tests/native/run_native.sh (sourced, not run).
#   pc_add <object> <source> <compiler and flags...>   queue one compile (skipped when the object is up to date)
#   pc_wait                                            wait for every queued compile; fails when one failed
# An object is rebuilt when it or its dependency file is missing, or when the source, the calling script (PC_SCRIPT: the
# flags live there) or any header listed in its dependency file (gcc -MMD) is newer. Jobs: TURBO_JOBS, default nproc.
PC_JOBS="${TURBO_JOBS:-$(nproc 2>/dev/null || echo 4)}"
PC_FAIL_DIR="$(mktemp -d)"
PC_BUILT=0

pc_stale() {
  local o="$1" s="$2" d="${1%.o}.d" f
  [ -f "$o" ] && [ -f "$d" ] || return 0
  [ "$s" -nt "$o" ] && return 0
  [ -n "${PC_SCRIPT:-}" ] && [ "$PC_SCRIPT" -nt "$o" ] && return 0
  # gcc writes "pc_target: dep dep \" lines with spaces in paths escaped as "\ "
  while IFS= read -r f; do
    [ -z "$f" ] && continue
    [ -e "$f" ] || return 0
    [ "$f" -nt "$o" ] && return 0
  done < <(sed -e 's/\\ /\x01/g' -e 's/\\$//' -e '1s/^pc_target:[[:space:]]*//' "$d" | tr ' \t' '\n\n' | tr '\001' ' ')
  return 1
}

pc_add() {
  local o="$1" s="$2"
  shift 2
  pc_stale "$o" "$s" || return 0
  while [ "$(jobs -rp | wc -l)" -ge "$PC_JOBS" ]; do wait -n || true; done
  PC_BUILT=$((PC_BUILT + 1))
  ( "$@" -MMD -MT pc_target -MF "${o%.o}.d" -c "$s" -o "$o" || { rm -f "$o"; : > "$PC_FAIL_DIR/$(basename "$o").fail"; } ) &
}

pc_wait() {
  wait
  local failed
  failed="$(ls "$PC_FAIL_DIR" 2>/dev/null | sed 's/\.fail$//' | tr '\n' ' ')"
  rm -rf "$PC_FAIL_DIR"
  echo "compiled $PC_BUILT file(s) with up to $PC_JOBS jobs"
  if [ -n "$failed" ]; then echo "compile failed: $failed"; return 1; fi
}
