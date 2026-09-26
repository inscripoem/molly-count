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
          (python3.withPackages (ps: [
            ps.pyserial
            ps.pillow
            ps.numpy
            ps.intelhex # PIO's bundled python lacks it; esptool imports it
          ])) # tools/ + console
        ];
        # prerender.py pins a noto-fonts-cjk-sans store path; enter with:
        #   nix-shell -p noto-fonts-cjk-sans python3Packages.numpy python3Packages.pillow
        # or rely on the path below still existing.
        # espup-installed Xtensa GCC (bare-metal linker) + esp-clang
        shellHook = ''
          [ -f "$HOME/export-esp.sh" ] && source "$HOME/export-esp.sh"
          # PlatformIO's bundled python has no pip and misses esptool deps
          # (intelhex); expose the devShell python's site-packages to it.
          export PYTHONPATH="$(python3 -c 'import sysconfig; print(sysconfig.get_paths()["purelib"])')"''${PYTHONPATH:+:$PYTHONPATH}
        '';
      };
    };
}
