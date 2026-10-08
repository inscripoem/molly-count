{
  description = "molly-count development shell";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-25.05";

  outputs =
    { self, nixpkgs }:
    let
      system = "x86_64-linux";
      pkgs = import nixpkgs { inherit system; };
    in
    {
      devShells.${system}.default = pkgs.mkShell {
        packages = with pkgs; [
          gcc # host linker: cargo build scripts & host unit tests
          pkg-config
          git
          platformio # firmware/ (Arduino) build + flash
          noto-fonts-cjk-sans # prerender.py: preview font (resolved from /nix/store at runtime)
          (python3.withPackages (ps: [
            ps.pyserial
            ps.pillow
            ps.numpy
            ps.intelhex # PIO's bundled python lacks it; esptool imports it
          ])) # tools/ + console
        ];
        # espup-installed Xtensa GCC (bare-metal linker) + esp-clang
        shellHook = ''
          [ -f "$HOME/export-esp.sh" ] && source "$HOME/export-esp.sh"
          # PlatformIO's bundled python has no pip and misses esptool deps
          # (intelhex); expose the devShell python's site-packages to it.
          export PYTHONPATH="$(python3 -c 'import sysconfig; print(sysconfig.get_paths()["purelib"])')"''${PYTHONPATH:+:$PYTHONPATH}
          # The pinned tool-esptoolpy registry package sometimes installs
          # without a manifest and PIO then fails with
          # MissingPackageManifestError; repair it on the next shell entry.
          m="$HOME/.platformio/packages/tool-esptoolpy/package.json"
          if [ -d "$HOME/.platformio/packages/tool-esptoolpy" ] && [ ! -f "$m" ]; then
            printf '%s\n' '{"name":"tool-esptoolpy","version":"2.40900.250804","system":["*"],"type":"uploader"}' > "$m"
            echo "devShell: wrote missing tool-esptoolpy package.json"
          fi
        '';
      };
    };
}
