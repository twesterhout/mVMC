{ stdenv
, gcc
, gfortran
, fetchFromGitHub
, mpich
, openblas
  # , blas
  # , lapack
, cmake
, ninja
, python3
, doCheck ? false
}:

stdenv.mkDerivation rec {
  pname = "mVMC";
  version = "1.2.0";
  src = ./.;

  src-StdFace = fetchFromGitHub {
    owner = "issp-center-dev";
    repo = "StdFace";
    rev = "v0.5";
    hash = "sha256-Nx2/6Hnel6fVqWeLfI01V0U8MaIDVWicbHdBIEiuvDo=";
  };

  src-pfapack = fetchFromGitHub {
    owner = "xrq-phys";
    repo = "pfapack";
    rev = "b655435f2351c1f18bad0bfb7107822f505332ec";
    hash = "sha256-+PcyCx4rHD6s+HDgNz4TgwkoGWOSFeaxA3nrws+XznI=";
  };

  src-pfaffine = fetchFromGitHub {
    owner = "xrq-phys";
    repo = "pfaffine";
    rev = "ebbb37ee4ac9b08b0907e81c5b593df5c975514a";
    hash = "sha256-UVsmRmaKw3F6QP4n/aJQJw49mQRtkhx+skLeBf9X2sk=";
  };

  postPatch = ''
    mkdir -p src/StdFace src/pfapack src/pfaffine
    cp -R --no-preserve=mode,ownership ${src-StdFace}/* ./src/StdFace/
    cp -R --no-preserve=mode,ownership ${src-pfapack}/* ./src/pfapack/
    cp -R --no-preserve=mode,ownership ${src-pfaffine}/* ./src/pfaffine/
  '';

  inherit doCheck;
  checkPhase = "cmake --build . --target test";

  buildInputs = [
    mpich
    openblas
    # blas
    # lapack
  ];

  cmakeArgs = [ "-GNinja" "-DPFAFFIAN_BLOCKED=ON" "-DGIT_SUBMODULE_UPDATE=OFF" "-DDocument=OFF" ];

  nativeBuildInputs = [
    cmake
    ninja
    (python3.withPackages (ps: with ps; [ numpy ]))
    gcc
    gfortran
  ];

  meta = {
    description = "A numerical solver package for a wide range of quantum lattice models based on many-variable Variational Monte Carlo method";
    homepage = "http://www.pasums.issp.u-tokyo.ac.jp/mvmc/en/";
  };
}
