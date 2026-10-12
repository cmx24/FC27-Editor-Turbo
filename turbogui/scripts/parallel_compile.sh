# Parallel, incremental compiles for build_win.sh and tests/native/run_native.sh (sourced, not run).
#   pc_add <object> <source> <compiler and flags...>   queue one compile (skipped when the object is up to date)
#   pc_wait                                            wait for every queued compile; fails when one failed
# An object is rebuilt when it, its dependency file or its flags file is missing, when the compile command differs from the one
# that built it (<object>.flags: the compiler and every flag, so a -D / -O / include change made in the calling script or through
# CXX, SANITIZE ... in the environment rebuilds it), or when the source, the calling script (PC_SCRIPT: the flags live there), this
# file (the -MMD / -MT options) or any header listed in its dependency file (gcc -MMD) is newer.
# PC_SCRIPT must be a path that still works after the caller's `cd` (set it from the script's absolute folder, never from a relative
# "$0"): a path that does not name a file stops the build instead of silently skipping the rule. Jobs: TURBO_JOBS, default nproc.
PC_JOBS="${TURBO_JOBS:-$(nproc 2>/dev/null || echo 4)}"
PC_SELF="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/$(basename "${BASH_SOURCE[0]}")"
PC_DEPOPTS="-MMD -MT pc_target"
if [ -n "${PC_SCRIPT:-}" ] && [ ! -f "$PC_SCRIPT" ]; then
  echo "parallel_compile.sh: PC_SCRIPT does not name a file from $(pwd): $PC_SCRIPT (give the calling script's absolute path)"
  return 1
fi
PC_FAIL_DIR="$(mktemp -d)"
PC_BUILT=0

pc_stale() {
  local o="$1" s="$2" cmd="$3" d="${1%.o}.d" fl="${1%.o}.flags" f prev=""
  [ -f "$o" ] && [ -f "$d" ] && [ -f "$fl" ] || return 0
  IFS= read -r prev < "$fl" || true   # the file has no newline: read still fills prev (no subprocess per object)
  [ "$prev" = "$cmd" ] || return 0
  [ "$s" -nt "$o" ] && return 0
  [ -n "${PC_SCRIPT:-}" ] && [ "$PC_SCRIPT" -nt "$o" ] && return 0
  [ "$PC_SELF" -nt "$o" ] && return 0
  # gcc writes "pc_target: dep dep \" lines with spaces in paths escaped as "\ "
  while IFS= read -r f; do
    [ -z "$f" ] && continue
    [ -e "$f" ] || return 0
    [ "$f" -nt "$o" ] && return 0
  done < <(sed -e 's/\\ /\x01/g' -e 's/\\$//' -e '1s/^pc_target:[[:space:]]*//' "$d" | tr ' \t' '\n\n' | tr '\001' ' ')
  return 1
}

pc_add() {
  local o="$1" s="$2" cmd
  shift 2
  cmd="$* $PC_DEPOPTS"
  pc_stale "$o" "$s" "$cmd" || return 0
  while [ "$(jobs -rp | wc -l)" -ge "$PC_JOBS" ]; do wait -n || true; done
  PC_BUILT=$((PC_BUILT + 1))
  rm -f "${o%.o}.flags"
  # the flags file is written only after a good compile: a failed or killed compile leaves the object stale
  ( "$@" $PC_DEPOPTS -MF "${o%.o}.d" -c "$s" -o "$o" && printf '%s' "$cmd" > "${o%.o}.flags" ||
      { rm -f "$o" "${o%.o}.flags"; : > "$PC_FAIL_DIR/$(basename "$o").fail"; } ) &
}

pc_wait() {
  wait
  local failed
  failed="$(ls "$PC_FAIL_DIR" 2>/dev/null | sed 's/\.fail$//' | tr '\n' ' ')"
  rm -rf "$PC_FAIL_DIR"
  echo "compiled $PC_BUILT file(s) with up to $PC_JOBS jobs"
  if [ -n "$failed" ]; then echo "compile failed: $failed"; return 1; fi
}
