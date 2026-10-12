{
  description = "leetvim: LeetCode in your terminal, solved in your own vim";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixpkgs-unstable";

  outputs =
    { self, nixpkgs }:
    let
      systems = [
        "aarch64-darwin"
        "x86_64-darwin"
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
      });

      apps = forAllSystems (pkgs: rec {
        leetvim = {
          type = "app";
          program = "${self.packages.${pkgs.stdenv.hostPlatform.system}.leetvim}/bin/leetvim";
          meta.description = "LeetCode TUI with an embedded vim";
        };
        default = leetvim;
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
          llvm = llvmFor pkgs;
          leetvim = self.packages.${pkgs.stdenv.hostPlatform.system}.leetvim;
        in
        {
          # Building the package also runs the unit tests (doCheck).
          inherit leetvim;

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
            cmakeFlags = (old.cmakeFlags or [ ]) ++ [ "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON" ];
            buildPhase = ''
              runHook preBuild
              run-clang-tidy -quiet -p . -j $NIX_BUILD_CORES "$NIX_BUILD_TOP/source/(src|tests)/.*\.cpp$"
              runHook postBuild
            '';
            doCheck = false;
            installPhase = "touch $out";
          });
        }
      );
    };
}
