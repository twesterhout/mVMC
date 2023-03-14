{
  description = "A numerical solver package for a wide range of quantum lattice models based on many-variable Variational Monte Carlo method";

  nixConfig = {
    extra-experimental-features = "nix-command flakes";
    # extra-substituters = "https://halide-haskell.cachix.org";
    # extra-trusted-public-keys = "halide-haskell.cachix.org-1:cFPqtShCsH4aNjn2q4PHb39Omtd/FWRhrkTBcSrtNKQ=";
  };

  inputs = {
    nixpkgs.url = "github:nixos/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
    flake-compat = {
      url = "github:edolstra/flake-compat";
      flake = false;
    };
  };

  outputs = inputs: inputs.flake-utils.lib.eachDefaultSystem (system:
    let
      inherit (inputs.nixpkgs) lib;
      pkgs = import inputs.nixpkgs { inherit system; };

      mVMC = pkgs.callPackage ./mVMC.nix { };
    in
    {
      packages.default = mVMC;
      devShells.default = pkgs.mkShell {
        buildInputs = with pkgs; [
          mpich
          lapack
          blis
        ];
        nativeBuildInputs = with pkgs; [
          # Build tools
          cmake
          ninja
          (python3.withPackages (ps: with ps; [ numpy ]))
          # gcc
          gfortran
          # Development tools
          clang-tools
          clang
          llvmPackages.openmp
          nil
          nixpkgs-fmt
        ];
      };
      formatter = pkgs.nixpkgs-fmt;
    });
}
