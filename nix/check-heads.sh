#!/usr/bin/env bash
# Checks the solution heads that tests/core/definitions_test.cpp writes to a directory
# (LEETVIM_HEADS_DIR) with each language's own compiler or checker: what leetvim uncomments
# from LeetCode's starter code must be code those languages accept. Each language has its
# directory, beside its support files; a language whose tools are missing fails. C#, Kotlin,
# Scala and Swift are left out: leetvim declares no APIs for them, and their toolchains are
# large.
#
# usage: check-heads.sh DIR
set -uo pipefail

dir=${1:?usage: check-heads.sh DIR}
scratch=$(mktemp -d)
trap 'rm -rf "$scratch"' EXIT
export HOME=$scratch GOCACHE=$scratch/go
failed=0
mkdir -p "$scratch/java"

# Errors in LeetCode's own starter code, which leetvim uncomments as it is.
known=(
  python/690.employee-importance.py # `:type id: int` outside a docstring
)

# check LANGUAGE EXT COMMAND...: runs COMMAND FILE for each of LANGUAGE's .EXT files, in its
# directory.
check() {
  local lang=$1 ext=$2
  shift 2
  local file
  for file in "$dir/$lang"/*."$ext"; do
    [[ -f $file && " ${known[*]} " != *" $lang/${file##*/} "* ]] || continue
    if ! output=$(cd "$dir/$lang" && "$@" "$file" 2>&1); then
      printf '%s: %s\n%s\n\n' "$lang" "${file##*/}" "$output"
      failed=1
    fi
  done
}

check cpp cpp clang++ -fsyntax-only -std=c++23 -include include/leetcode.hpp
check c c clang -fsyntax-only -std=c17 -include include/leetcode.h
check java java javac -d "$scratch/java" -Xlint:none
check golang go go vet
check javascript js node --check
check typescript ts tsc --noEmit --noImplicitAny --target es2022
# LeetCode's TreeNode uses Rc and RefCell, which its snippet imports in the code below the
# head, and an import covers the whole file.
# shellcheck disable=SC2329 # called through check
rust_check() {
  { cat "$1" && echo 'use std::{cell::RefCell, rc::Rc};'; } >"$scratch/solution.rs"
  rustc --edition 2021 --crate-type lib --emit=metadata -o "$scratch/rust.rmeta" -A warnings \
    "$scratch/solution.rs"
}
check rust rs rust_check
check ruby rb ruby -c
check php php php -l
check python py python3 -m py_compile

# Python3 against its support files, as an editor would.
if ! output=$(cd "$dir/python3" && pyright 2>&1 && ruff check . 2>&1); then
  printf 'python3:\n%s\n\n' "$output"
  failed=1
fi

exit $failed
