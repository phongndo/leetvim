# The release archive for one platform: a leetvim that runs without Nix, the installer that
# `leetvim update` reruns, and the licenses of the libraries linked into it. On Linux the
# executable is static (musl), so it runs on any distribution. On macOS nixpkgs already links
# the system's libc++, so building the other libraries static leaves only macOS's own.
{
  pkgs,
  tomlplusplus,
}:

let
  inherit (pkgs) lib;
  inherit (pkgs.stdenv) hostPlatform;
  llvm = pkgs.llvmPackages_latest;

  target =
    {
      x86_64-linux = "x86_64-unknown-linux-musl";
      aarch64-linux = "aarch64-unknown-linux-musl";
      aarch64-darwin = "aarch64-apple-darwin";
    }
    .${hostPlatform.system};

  # HTTPS over HTTP/2 is all leetvim asks of curl; the IDN libraries would also add LGPL code.
  minimalCurl =
    curl:
    curl.override {
      scpSupport = false;
      gssSupport = false;
    };

  deps =
    if hostPlatform.isLinux then
      {
        inherit (pkgs.pkgsStatic)
          openssl
          nghttp2
          zlib
          sqlite
          libvterm-neovim
          nlohmann_json
          cli11
          ;
        curl = minimalCurl pkgs.pkgsStatic.curlMinimal;
        musl = pkgs.pkgsStatic.stdenv.cc.libc;
      }
    else
      let
        static = pkg: pkg.override { stdenv = pkgs.stdenvAdapters.makeStaticLibraries pkgs.stdenv; };
      in
      rec {
        openssl = pkgs.openssl.override { static = true; };
        zlib = pkgs.zlib.override { shared = false; };
        nghttp2 = static pkgs.nghttp2;
        curl = (minimalCurl (static pkgs.curlMinimal)).override { inherit openssl zlib nghttp2; };
        sqlite = static pkgs.sqlite;
        # libtool's -static builds only the archive.
        libvterm-neovim = pkgs.libvterm-neovim.overrideAttrs (old: {
          makeFlags = old.makeFlags ++ [ "LDFLAGS=-static" ];
        });
        inherit (pkgs) nlohmann_json cli11;
      };

  leetvim =
    if hostPlatform.isLinux then
      (pkgs.pkgsStatic.callPackage ./package.nix {
        inherit tomlplusplus;
        inherit (deps) curl;
      }).overrideAttrs
        (old: {
          cmakeFlags = old.cmakeFlags ++ [
            # musl gives threads 128 KiB of stack unless the executable asks for more; this is
            # glibc's 8 MiB.
            "-DCMAKE_EXE_LINKER_FLAGS=-Wl,-z,stack-size=8388608"
          ];
        })
    else
      (pkgs.callPackage ./package.nix {
        inherit (llvm) stdenv;
        inherit tomlplusplus;
        inherit (deps)
          curl
          openssl
          sqlite
          libvterm-neovim
          ;
      }).overrideAttrs
        (old: {
          cmakeFlags = old.cmakeFlags ++ [
            # Static curl reads the system proxy settings through these, but its CMake package
            # doesn't say so.
            "-DCMAKE_EXE_LINKER_FLAGS=-Wl,-framework,CoreFoundation,-framework,SystemConfiguration"
          ];
        });

  # Sources whose licenses ask for their notice in binary distributions. (SQLite is in the
  # public domain; libstdc++ and libgcc carry the GCC Runtime Library Exception.)
  notices = {
    curl = deps.curl.src;
    openssl = deps.openssl.src;
    nghttp2 = deps.nghttp2.src;
    zlib = deps.zlib.src;
    libvterm = deps.libvterm-neovim.src;
    nlohmann_json = deps.nlohmann_json.src;
    CLI11 = deps.cli11.src;
    "toml++" = tomlplusplus.src;
  }
  // lib.optionalAttrs hostPlatform.isLinux { musl = deps.musl.src; };
in
pkgs.stdenvNoCC.mkDerivation {
  pname = "leetvim-dist";
  inherit (leetvim) version;
  dontUnpack = true;
  nativeBuildInputs = [ llvm.llvm ]; # llvm-readelf, llvm-otool

  buildPhase = ''
    runHook preBuild
    name=leetvim-v$version-${target}
    root=$PWD/$name
    install -Dm755 ${leetvim}/bin/leetvim $root/bin/leetvim
    install -Dm755 ${../scripts/install.sh} $root/share/leetvim/install.sh
    install -Dm644 ${../LICENSE} $root/LICENSE
    install -Dm644 ${../README.md} $root/README.md

    ${lib.concatStrings (
      lib.mapAttrsToList (name: src: ''
        unpacked=$TMPDIR/notices/${lib.escapeShellArg name}
        mkdir -p $unpacked
        if [ -d ${src} ]; then cp -r ${src}/. $unpacked; else tar -xf ${src} -C $unpacked; fi
        top=$(find $unpacked -mindepth 1 -maxdepth 1)
        [ -d "$top" ] && [ "$(printf '%s\n' "$top" | wc -l)" -eq 1 ] || top=$unpacked
        found=
        for file in "$top"/LICEN[CS]E* "$top"/COPYING* "$top"/COPYRIGHT*; do
          [ -f "$file" ] || continue
          install -Dm644 "$file" "$root/share/doc/leetvim/licenses/"${lib.escapeShellArg name}"/''${file##*/}"
          found=1
        done
        [ -n "$found" ] || { echo "no license file in ${src}" >&2; exit 1; }
      '') notices
    )}

    bin=$root/bin/leetvim
    [ "$($bin --version)" = "leetvim $version" ]
    ${
      if hostPlatform.isLinux then
        ''
          if llvm-readelf --program-headers $bin | grep -q INTERP || llvm-readelf --dynamic $bin | grep -q NEEDED; then
            echo "leetvim is not statically linked" >&2
            exit 1
          fi
        ''
      else
        ''
          # After the first line, otool -L lists the libraries the executable loads.
          if llvm-otool -L $bin | tail -n +2 | grep -vE '^[[:space:]]+(/usr/lib/|/System/Library/)'; then
            echo "leetvim links libraries that macOS does not have" >&2
            exit 1
          fi
        ''
    }

    mkdir -p $out
    tar --sort=name --mtime=@$SOURCE_DATE_EPOCH --owner=0 --group=0 --numeric-owner -cf - $name |
      gzip -9n >$out/$name.tar.gz
    (cd $out && sha256sum $name.tar.gz >$name.tar.gz.sha256)
    runHook postBuild
  '';

  dontInstall = true;

  passthru = { inherit leetvim target; };
}
