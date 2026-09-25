#!/usr/bin/env bash
export LC_ALL=C
set -e -o pipefail

# Inlined from contrib/shell/realpath.bash (Bitcoin) — Blocknet does not vendor contrib/shell
bash_realpath() { canonicalize_path "$(resolve_symlinks "$1")"; }
resolve_symlinks() { _resolve_symlinks "$1"; }
_resolve_symlinks() {
    _assert_no_path_cycles "$@" || return
    local dir_context path
    if path=$(readlink -- "$1"); then
        dir_context=$(dirname -- "$1")
        _resolve_symlinks "$(_prepend_dir_context_if_necessary "$dir_context" "$path")" "$@"
    else
        printf '%s\n' "$1"
    fi
}
_prepend_dir_context_if_necessary() {
    if [ "$1" = . ]; then printf '%s\n' "$2"; else _prepend_path_if_relative "$1" "$2"; fi
}
_prepend_path_if_relative() {
    case "$2" in /*) printf '%s\n' "$2" ;; *) printf '%s\n' "$1/$2" ;; esac
}
_assert_no_path_cycles() {
    local target path; target=$1; shift
    for path in "$@"; do [ "$path" = "$target" ] && return 1; done
}
canonicalize_path() {
    if [ -d "$1" ]; then _canonicalize_dir_path "$1"; else _canonicalize_file_path "$1"; fi
}
_canonicalize_dir_path() { (cd "$1" 2>/dev/null && pwd -P); }
_canonicalize_file_path() {
    local dir file; dir=$(dirname -- "$1"); file=$(basename -- "$1")
    (cd "$dir" 2>/dev/null && printf '%s/%s\n' "$(pwd -P)" "$file")
}
git_root() { git rev-parse --show-toplevel 2>/dev/null; }
git_head_version() {
    local recent_tag
    if recent_tag="$(git describe --exact-match HEAD 2>/dev/null)"; then echo "${recent_tag#v}"
    else git rev-parse --short=12 HEAD; fi
}

################
# Required non-builtin commands should be invocable
################

check_tools() {
    for cmd in "$@"; do
        if ! command -v "$cmd" > /dev/null 2>&1; then
            echo "ERR: This script requires that '$cmd' is installed and available in your \$PATH"
            exit 1
        fi
    done
}

check_tools cat env readlink dirname basename git

################
# We should be at the top directory of the repository
################

same_dir() {
    local resolved1 resolved2
    resolved1="$(bash_realpath "${1}")"
    resolved2="$(bash_realpath "${2}")"
    [ "$resolved1" = "$resolved2" ]
}

if ! same_dir "${PWD}" "$(git_root)"; then
cat << EOF
ERR: This script must be invoked from the top level of the git repository

Hint: This may look something like:
    env FOO=BAR ./contrib/guix/guix-<blah>

EOF
exit 1
fi

################
# Execute "$@" in a pinned, possibly older version of Guix, for reproducibility
# across time.
time-machine() {
    # shellcheck disable=SC2086
    guix time-machine --url=https://codeberg.org/guix/guix.git \
                      --commit=c5eee3336cc1d10a3cc1c97fde2809c3451624d3 \
                      --cores="$JOBS" \
                      --keep-failed \
                      --fallback \
                      ${SUBSTITUTE_URLS:+--substitute-urls="$SUBSTITUTE_URLS"} \
                      ${ADDITIONAL_GUIX_COMMON_FLAGS} ${ADDITIONAL_GUIX_TIMEMACHINE_FLAGS} \
                      -- "$@"
}


################
# Set common variables
################

# Substitute servers, bordeaux first: ci.guix.gnu.org repeatedly stalls
# ("server is somewhat slow"), and those stalls can trip the upstream
# display-download-progress crash (guix #38493/#55337) on any large
# download. Both servers are in Guix's default authorized keys, so this
# only changes preference order. Overridable via environment.
SUBSTITUTE_URLS="${SUBSTITUTE_URLS:-https://bordeaux.guix.gnu.org https://ci.guix.gnu.org}"

VERSION="${FORCE_VERSION:-$(git_head_version)}"
DISTNAME="${DISTNAME:-blocknet-${VERSION}}"

version_base_prefix="${PWD}/guix-build-"
VERSION_BASE="${version_base_prefix}${VERSION}"  # TOP

DISTSRC_BASE="${DISTSRC_BASE:-${VERSION_BASE}}"

OUTDIR_BASE="${OUTDIR_BASE:-${VERSION_BASE}/output}"

var_base_basename="var"
VAR_BASE="${VAR_BASE:-${VERSION_BASE}/${var_base_basename}}"

profiles_base_basename="profiles"
PROFILES_BASE="${PROFILES_BASE:-${VAR_BASE}/${profiles_base_basename}}"
