{
  inputs.flake-utils.url = "github:numtide/flake-utils";

  outputs =
    {
      nixpkgs,
      flake-utils,
    }:
    flake-utils.lib.eachDefaultSystem (
      system:
      let
        pkgs = nixpkgs.legacyPackages.${system};
      in
      {
        devShells.default =
          with pkgs;
          mkShell {
            PICO_SDK_PATH = "${pico-sdk.override { withSubmodules = true; }}/lib/pico-sdk";

            buildInputs = [
              cmake
              gcc-arm-embedded
              python3
              picotool
            ];
          };
      }
    );
}
