# libFuzzer builds of fuzz/*.cpp, run through `leetvim-fuzz TARGET [SECONDS]`, which adds
# the target's seeds and dictionary and keeps a growing corpus in ./fuzz-corpus.
{
  lib,
  leetvim,
  writeShellApplication,
}:

let
  fuzzers = leetvim.overrideAttrs (old: {
    pname = "leetvim-fuzzers";
    cmakeFlags = old.cmakeFlags ++ [
      (lib.cmakeBool "LEETVIM_FUZZ" true)
      (lib.cmakeBool "LEETVIM_BUILD_TESTS" false)
      (lib.cmakeBool "LEETVIM_WERROR" true)
    ];
    cmakeBuildType = "RelWithDebInfo";
    # The wrapper's _FORTIFY_SOURCE checks get in the way of the sanitizers' interceptors.
    hardeningDisable = [
      "fortify"
      "fortify3"
    ];
    dontStrip = true;
    doCheck = false;
    postInstall = ''
      share=$out/share/leetvim-fuzz
      mkdir -p $share/seeds/html $share/seeds/responses
      cp -r $src/fuzz/dicts $share/dicts
      cp -r $src/fuzz/seeds/. $share/seeds/
      cp $src/tests/fixtures/*.html $share/seeds/html/
      cp $src/tests/fixtures/*.json $share/seeds/responses/
    '';
  });
in
writeShellApplication {
  name = "leetvim-fuzz";
  passthru = { inherit fuzzers; };
  text = ''
    bin=${fuzzers}/bin
    share=${fuzzers}/share/leetvim-fuzz
    if [ $# -eq 0 ]; then
      echo "usage: leetvim-fuzz TARGET|all [SECONDS] [libFuzzer flags...]" >&2
      targets=()
      for exe in "$bin"/fuzz_*; do targets+=("''${exe##*/fuzz_}"); done
      echo "targets: ''${targets[*]}" >&2
      exit 2
    fi
    target=$1
    seconds=''${2:-60}
    shift $(($# < 2 ? $# : 2))

    if [ "$target" = all ]; then
      # Every target at once, one process each; logs go to ./fuzz-logs.
      logs=''${LEETVIM_FUZZ_LOGS:-fuzz-logs}
      mkdir -p "$logs"
      names=()
      pids=()
      for exe in "$bin"/fuzz_*; do
        name=''${exe##*/fuzz_}
        "$0" "$name" "$seconds" "$@" >"$logs/$name.log" 2>&1 &
        names+=("$name")
        pids+=($!)
      done
      status=0
      for i in "''${!pids[@]}"; do
        name=''${names[$i]}
        if wait "''${pids[$i]}"; then
          runs=$(grep -o 'number_of_executed_units: [0-9]*' "$logs/$name.log" | grep -o '[0-9]*$' || true)
          # Keep only the inputs that add coverage, so the saved corpus stays small.
          corpus=''${LEETVIM_FUZZ_CORPUS:-fuzz-corpus}/$name
          rm -rf "$corpus.min" && mkdir "$corpus.min"
          if "$bin/fuzz_$name" -merge=1 "$corpus.min" "$corpus" >>"$logs/$name.log" 2>&1; then
            rm -rf "$corpus" && mv "$corpus.min" "$corpus"
          fi
          echo "ok    $name  ''${runs:-?} runs, corpus $(find "$corpus" -type f | wc -l | tr -d ' ') inputs"
        else
          status=1
          echo "FAIL  $name"
          grep -A 30 -E 'property failed|ERROR:|runtime error' "$logs/$name.log" | head -n 60 || true
        fi
      done
      exit "$status"
    fi
    if [ ! -x "$bin/fuzz_$target" ]; then
      echo "leetvim-fuzz: no target '$target'" >&2
      exit 2
    fi
    corpus=''${LEETVIM_FUZZ_CORPUS:-fuzz-corpus}/$target
    artifacts=''${LEETVIM_FUZZ_ARTIFACTS:-fuzz-crashes}
    mkdir -p "$corpus" "$artifacts"
    flags=(
      -max_total_time="$seconds"
      -timeout=10
      -rss_limit_mb=2048
      -print_final_stats=1
      -artifact_prefix="$artifacts/$target-"
    )
    if [ -f "$share/dicts/$target.dict" ]; then flags+=(-dict="$share/dicts/$target.dict"); fi
    seeds=()
    if [ -d "$share/seeds/$target" ]; then seeds=("$share/seeds/$target"); fi
    exec "$bin/fuzz_$target" "''${flags[@]}" "$@" "$corpus" "''${seeds[@]}"
  '';
  meta.description = "Run one of leetvim's libFuzzer targets";
}
