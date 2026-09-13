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
        ];
        # espup-installed Xtensa GCC (bare-metal linker) + esp-clang
        shellHook = ''
          [ -f "$HOME/export-esp.sh" ] && source "$HOME/export-esp.sh"
        '';
      };
    };
}
