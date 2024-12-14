{
  description = " A tool for Go players: SGF editor, analysis tool, game database and pattern search tool, IGS client";

  inputs.flake-compat.url = "git+https://git.lix.systems/lix-project/flake-compat.git";
  inputs.gitignore-nix.inputs.nixpkgs.follows = "nixpkgs";
  inputs.gitignore-nix.url = "github:hercules-ci/gitignore.nix";
  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs = inputs: let
    inherit (inputs.gitignore-nix.lib) gitignoreFilterWith;
    inherit (inputs.nixpkgs) lib;
    inherit (lib.attrsets) genAttrs mapAttrs;
    inherit (lib.sources) cleanSourceWith;
    inherit (lib.strings) escapeNixIdentifier;
    allSystems = genAttrs systems (system: perSystem allSystemArgs.${system});
    allSystemArgs = genAttrs systems perSystemArgs;
    gitignoreSourceWith = args@{ path, ... }: cleanSourceWith {
      name = "source";
      filter = gitignoreFilterWith (removeAttrs args [ "basePath" "path" ] // {
        basePath = path.origSrc or path;
      });
      src = path;
    };
    perSystem = { currentSystem, pkgs, ... }: {
      devShells.default = pkgs.callPackage (
        {
          cntr,
          mkShell,
          ...
        }:
        mkShell {
          inputsFrom = [
            currentSystem.packages.q5Go
          ];
          nativeBuildInputs = [
            cntr
          ];
        }
      ) { };

      formatter = pkgs.writeShellScriptBin "formatter" ''
        ${pkgs.alejandra}/bin/alejandra flake.nix
      '';

      packages.default = currentSystem.packages.q5Go;
      packages.q5Go = pkgs.callPackage (
        {
          lib,
          libsForQt5,
          stdenv,
          ...
        }:
        let
          inherit (lib) licenses platforms;
        in
        stdenv.mkDerivation (finalAttrs: {
          pname = "q5Go";
          version = "unstable-2023-05-01+2.1.3";

          src = gitignoreSourceWith {
            extraRules = ''
              *.nix
              flake.lock
            '';
            path = ./.;
          };

          nativeBuildInputs = [
            libsForQt5.qmake
            libsForQt5.wrapQtAppsHook
          ];
          buildInputs = [
            libsForQt5.qtbase
            libsForQt5.qtmultimedia
            libsForQt5.qtwayland
          ];

          preConfigure = ''
            appendToVar qmakeFlags ./src/q5go.pro
          '';

          meta = {
            description = "A tool for Go players: SGF editor, analysis tool, game database and pattern search tool, IGS client";
            homepage = "https://github.com/bernds/q5Go";
            # not explicitly GPL-2.0-or-later.
            license = licenses.gpl2Only;
            maintainers = [ ];
            platforms = platforms.unix;
            mainProgram = "q5go";
          };
        })
      ) { };
    };
    perSystemArgs = system: {
      inherit system;
      currentSystem = allSystems.${system};
      pkgs = import inputs.nixpkgs {
        inherit system;
        overlays = [
        ];
        config = {};
      };
    };
    systems = lib.systems.flakeExposed;
    transposition = mapAttrs (attrName: attrConfig: mapAttrs (system: currentSystem: currentSystem.${attrName} or (abort ''
      Could not find `perSystem` attribute `${escapeNixIdentifier attrName}` for system `${escapeNixIdentifier system}`. It is required to declare such an attribute whenever `transposition.${escapeNixIdentifier attrName}` is defined.
    '')) allSystems) transpositionConfig;
    transpositionConfig.devShells = { };
    transpositionConfig.formatter = { };
    transpositionConfig.packages = { };
  in transposition // {
  };
}
