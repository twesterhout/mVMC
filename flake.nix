{
  description = "A numerical solver package for a wide range of quantum lattice models based on many-variable Variational Monte Carlo method";

  # nixConfig = {
  #   extra-experimental-features = "nix-command flakes";
  #   extra-substituters = "https://halide-haskell.cachix.org";
  #   extra-trusted-public-keys = "halide-haskell.cachix.org-1:cFPqtShCsH4aNjn2q4PHb39Omtd/FWRhrkTBcSrtNKQ=";
  # };

  inputs = {
    nixpkgs.url = "github:nixos/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
    # nix-filter.url = "github:numtide/nix-filter";
    flake-compat = {
      url = "github:edolstra/flake-compat";
      # don't look for a flake.nix file in this repository
      # this tells Nix to retrieve this input as just source code
      flake = false;
    };
  };

  outputs = inputs: inputs.flake-utils.lib.eachDefaultSystem (system:
    let
      inherit (inputs.nixpkgs) lib;
      pkgs = inputs.nixpkgs.legacyPackages.${system};

      mVMC = pkgs.stdenv.mkDerivation {
        pname = "mVMC";
        version = "1.2.0";
        src = ./.;

        nativeBuildInputs = with pkgs; [ cmake gcc gfortran git python3 ];
        buildInputs = with pkgs; [ openmpi lapack blis ];
        cmakeFlags = [ ];

        preConfigure = ''
          cd src/
          rm -rf StdFace
          git clone --depth=1 --branch c9e24dbe8c200cd9f06cc2fd51ace8abd8ac08c0 https://github.com/issp-center-dev/StdFace
        '';

        meta = with lib; {
          homepage = "https://github.com/twesterhout/mVMC";
          description = "A numerical solver package for a wide range of quantum lattice models based on many-variable Variational Monte Carlo method";
          licencse = licenses.GPL3;
        };
      };
    in
    {
      packages.default = mVMC;
      devShells.default = pkgs.mkShell {
        buildInputs = with pkgs; [
          openmpi
          lapack
          blis
        ];
        nativeBuildInputs = with pkgs; [
          cmake
          gcc
          gfortran
          python3
        ];
        shellHook = ''
          export PROMPT_COMMAND=""
          export PS1='$(tput bold)(nix)$(tput sgr0) mVMC \w ⚡ '
        '';
      };
      formatter = pkgs.nixpkgs-fmt;
    });
}

