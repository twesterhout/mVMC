{ stdenv
, gcc
, gfortran
, mpich
, blis
, lapack
, cmake
, ninja
, python3
, doCheck ? false
}:

stdenv.mkDerivation {
  pname = "mVMC";
  version = "1.2.0";
  src = ./.;

  doCheck = doCheck;
  checkPhase = ''
    cmake --build . --target test
  '';

  buildInputs = [
    mpich
    lapack
    blis
  ];

  cmakeArgs = [ "-GNinja" "-DPFAFFIAN_BLOCKED:BOOL=OFF" "-DDocument:BOOL=OFF" ];

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
