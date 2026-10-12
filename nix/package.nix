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
  doctest,
}:

stdenv.mkDerivation {
  pname = "leetvim";
  version = "0.1.0";

  src = lib.fileset.toSource {
    root = ../.;
    fileset = lib.fileset.unions [
      ../CMakeLists.txt
      ../src
      ../tests
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
