{
  lib,
  stdenv,
  cmake,
  ninja,
  pkg-config,
  curl,
  nlohmann_json,
  openssl,
  sqlite,
  libvterm-neovim,
  cli11,
  tomlplusplus,
  doctest,
}:

stdenv.mkDerivation {
  pname = "leetvim";
  # Set once, in CMakeLists.txt's project().
  version = builtins.head (
    builtins.match ".*project\\(leetvim VERSION ([0-9.]+).*" (builtins.readFile ../CMakeLists.txt)
  );

  src = lib.fileset.toSource {
    root = ../.;
    fileset = lib.fileset.unions [
      ../CMakeLists.txt
      ../src
      ../tests
      ../fuzz
      ../.clang-tidy
    ];
  };

  nativeBuildInputs = [
    cmake
    ninja
    pkg-config
  ];

  buildInputs = [
    curl
    nlohmann_json
    openssl # Chromium cookie decryption
    sqlite # browser cookie stores
    libvterm-neovim
    cli11
    tomlplusplus # config.toml
  ];

  checkInputs = [ doctest ];

  cmakeFlags = [ (lib.cmakeBool "LEETVIM_BUILD_TESTS" true) ];

  doCheck = true;

  meta = {
    description = "LeetCode in the terminal: browse, read, and solve problems in your own vim";
    mainProgram = "leetvim";
    license = lib.licenses.mit;
    platforms = lib.platforms.unix;
  };
}
