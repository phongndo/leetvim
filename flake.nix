{
  description = "leetvim: LeetCode in your terminal, solved in your own vim";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixpkgs-unstable";

  outputs =
    { self, nixpkgs }:
    let
      # nixpkgs 26.11 dropped x86_64-darwin.
      systems = [
        "aarch64-darwin"
        "aarch64-linux"
        "x86_64-linux"
      ];
      forAllSystems = f: nixpkgs.lib.genAttrs systems (system: f nixpkgs.legacyPackages.${system});

      # Newest LLVM in nixpkgs: clang for the build, clang-format/clang-tidy/clangd for tooling.
      llvmFor = pkgs: pkgs.llvmPackages_latest;

      formatTools = pkgs: [
        pkgs.treefmt
        (llvmFor pkgs).clang-tools
        pkgs.nixfmt
        pkgs.gersemi
      ];
    in
    {
      packages = forAllSystems (pkgs: rec {
        leetvim = pkgs.callPackage ./nix/package.nix { inherit (llvmFor pkgs) stdenv; };
        default = leetvim;
        # `nix run .#fuzz -- TARGET [SECONDS]`
        fuzz = pkgs.callPackage ./nix/fuzz.nix { inherit leetvim; };
      });

      apps = forAllSystems (pkgs: rec {
        leetvim = {
          type = "app";
          program = "${self.packages.${pkgs.stdenv.hostPlatform.system}.leetvim}/bin/leetvim";
          meta.description = "LeetCode TUI with an embedded vim";
        };
        default = leetvim;
        fuzz = {
          type = "app";
          program = "${self.packages.${pkgs.stdenv.hostPlatform.system}.fuzz}/bin/leetvim-fuzz";
          meta.description = "Run one of leetvim's libFuzzer targets";
        };
      });

      devShells = forAllSystems (
        pkgs:
        let
          llvm = llvmFor pkgs;
        in
        {
          default = (pkgs.mkShell.override { inherit (llvm) stdenv; }) {
            inputsFrom = [ self.packages.${pkgs.stdenv.hostPlatform.system}.leetvim ];
            packages = formatTools pkgs ++ [ pkgs.python3 ]; # python3: run-clang-tidy
            shellHook = ''
              export CMAKE_EXPORT_COMPILE_COMMANDS=ON
            '';
          };
        }
      );

      formatter = forAllSystems (
        pkgs:
        pkgs.writeShellApplication {
          name = "leetvim-fmt";
          runtimeInputs = formatTools pkgs;
          text = ''exec treefmt "$@"'';
        }
      );

      checks = forAllSystems (
        pkgs:
        let
          inherit (pkgs) lib;
          llvm = llvmFor pkgs;
          inherit (self.packages.${pkgs.stdenv.hostPlatform.system}) leetvim fuzz;

          # Warnings are errors, and failing tests print their output.
          strict =
            drv:
            drv.overrideAttrs (old: {
              cmakeFlags = old.cmakeFlags ++ [ (lib.cmakeBool "LEETVIM_WERROR" true) ];
              env = (old.env or { }) // {
                CTEST_OUTPUT_ON_FAILURE = "1";
              };
            });

          # The unit tests under sanitizers; any report fails the check.
          sanitized =
            name: sanitizers:
            (strict leetvim).overrideAttrs (old: {
              pname = "leetvim-${name}";
              cmakeFlags = old.cmakeFlags ++ [ "-DLEETVIM_SANITIZE=${sanitizers}" ];
              cmakeBuildType = "RelWithDebInfo";
              # The wrapper's _FORTIFY_SOURCE checks get in the way of the sanitizers' interceptors.
              hardeningDisable = [
                "fortify"
                "fortify3"
              ];
              dontStrip = true;
              env = old.env // {
                UBSAN_OPTIONS = "print_stacktrace=1";
                # die_after_fork=0: the abort_all test forks a child that starts threads.
                TSAN_OPTIONS = "halt_on_error=1 second_deadlock_stack=1 die_after_fork=0";
              };
              installPhase = "touch $out";
            });
        in
        {
          # The package itself; building it runs the unit tests.
          leetvim = strict leetvim;
          sanitize = sanitized "sanitize" "address,undefined";
          tsan = sanitized "tsan" "thread";

          # Every fuzz target over its seeds plus a fixed-seed burst of mutations, so this
          # is deterministic; open-ended fuzzing runs in CI and with `nix run .#fuzz`.
          fuzz = pkgs.runCommand "leetvim-fuzz-smoke" { nativeBuildInputs = [ fuzz ]; } ''
            export LEETVIM_FUZZ_CORPUS=$TMPDIR/corpus LEETVIM_FUZZ_ARTIFACTS=$TMPDIR/crashes
            for bin in ${fuzz.fuzzers}/bin/fuzz_*; do
              leetvim-fuzz "''${bin##*/fuzz_}" 0 -runs=50000 -seed=1 -print_final_stats=0
            done
            touch $out
          '';

          actionlint =
            pkgs.runCommand "leetvim-actionlint"
              {
                nativeBuildInputs = [
                  pkgs.actionlint
                  pkgs.shellcheck
                ];
              }
              ''
                cd ${self} && actionlint .github/workflows/*.yml
                touch $out
              '';

          format = pkgs.runCommand "leetvim-format-check" { nativeBuildInputs = formatTools pkgs; } ''
            cp -r ${self} src && chmod -R u+w src && cd src
            treefmt --ci --no-cache
            touch $out
          '';

          clang-tidy = leetvim.overrideAttrs (old: {
            pname = "leetvim-clang-tidy";
            nativeBuildInputs = old.nativeBuildInputs ++ [
              llvm.clang-tools
              pkgs.python3
            ];
            # The tests are linted too, so their dependencies must be present.
            buildInputs = old.buildInputs ++ old.checkInputs;
            cmakeFlags = old.cmakeFlags ++ [
              "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON"
              (lib.cmakeBool "LEETVIM_FUZZ" true) # so fuzz/ is linted as well
            ];
            buildPhase = ''
              runHook preBuild
              # Through python3: the script's /usr/bin/env shebang doesn't exist in the Linux sandbox.
              python3 "$(type -P run-clang-tidy)" -quiet -p . -j $NIX_BUILD_CORES "$NIX_BUILD_TOP/source/(src|tests|fuzz)/.*\.cpp$"
              runHook postBuild
            '';
            doCheck = false;
            installPhase = "touch $out";
          });
        }
        // lib.optionalAttrs pkgs.stdenv.hostPlatform.isLinux {
          # A second compiler and standard library catch non-portable code.
          gcc = strict (leetvim.override { stdenv = pkgs.gccStdenv; });
        }
      );
    };
}
