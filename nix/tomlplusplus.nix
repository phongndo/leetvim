# toml++ headers (leetvim uses it header-only) at an upstream commit: the 3.4.0 release in
# nixpkgs has parser bugs on malformed input, since fixed, that the config fuzzer finds.
{ fetchFromGitHub, runCommand }:

let
  src = fetchFromGitHub {
    owner = "marzer";
    repo = "tomlplusplus";
    rev = "1e8829b793b66ad17011732a146b8077d379b011";
    hash = "sha256-NVt6C2OH4CIYWBZlY3M7QWI+H7NY5NBfWhrXGyQL/dc=";
  };
in
runCommand "tomlplusplus-headers-3.4.0-unstable-2026-07-21" { passthru = { inherit src; }; } ''
  mkdir -p $out/include
  cp -r ${src}/include/toml++ $out/include/
''
