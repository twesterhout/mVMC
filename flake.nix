{
  description = "A numerical solver package for a wide range of quantum lattice models based on many-variable Variational Monte Carlo method";

  nixConfig = {
    # extra-experimental-features = "nix-command flakes";
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
      pkgs = import inputs.nixpkgs { inherit system; };

      mVMC = pkgs.callPackage ./mVMC.nix { };

      singularity = pkgs.singularity-tools.buildImage {
        name = "mVMC";
        contents = [ mVMC ];
        diskSize = 10240;
        memSize = 5120;
      };
    in
    {
      packages = {
        default = mVMC;
        inherit singularity;
      };
      devShells.default = pkgs.mkShell {
        buildInputs = with pkgs; [
          mpich
          openblas
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
          llvmPackages_11.openmp
          nil
          nixpkgs-fmt
        ];
      };
      formatter = pkgs.nixpkgs-fmt;
    });
}
