/*
  mVMC - A numerical solver package for a wide range of quantum lattice models based on many-variable Variational Monte Carlo method
  Copyright (C) 2016 The University of Tokyo, All rights reserved.

This program is developed based on the mVMC-mini program
(https://github.com/fiber-miniapp/mVMC-mini)
which follows "The BSD 3-Clause License".

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details. 

  You should have received a copy of the GNU General Public License 
  along with this program. If not, see http://www.gnu.org/licenses/. 
*/
/*-------------------------------------------------------------
 * Variational Monte Carlo
 * make sample
 *-------------------------------------------------------------
 * by Satoshi Morita
 *-------------------------------------------------------------*/
#include "vmcmake_real.h"
#ifndef _SRC_VMCMAKE_REAL
#define _SRC_VMCMAKE_REAL

#include "global.h"
#include "slater.h"
#include "matrix.h"
#include "pfupdate_real.h"
#include "qp_real.h"
#include "splitloop.h"
#include "vmcmake.h"
#include "extract_wavefunction.h"

#include <stdbool.h>
#include <string.h>
#include <stdlib.h>

#ifdef _pf_block_update
// Block-update extension.
#include "../pfupdates/pf_interface.h"
#endif


int makeInitialSample_spin_left_ones(int *eleIdx, int *eleCfg, int *eleNum, int *eleProjCnt,
                                     const int qpStart, const int qpEnd, MPI_Comm comm) {

  const int nsize = Nsize;
  const int nsite2 = Nsite2;
  int flag = 1, flagRdc, loop = 0;
  int ri, mi, si, msi, rsi;
  int rank, size;
  MPI_Comm_size(comm, &size);
  MPI_Comm_rank(comm, &rank);

  if (Ne * 2 != Nsite) {
    fprintf(stderr, "error: makeInitialSample_spin_left_ones only works if every site contains exactly one electron, so Ne * 2 == Nsite\n");
    fprintf(stderr, "we have: Ne=%d, Nsite=%d\n", Ne, Nsite);
    MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
  }

  for (ri = 0; ri < Nsite; ++ri) {
    if (LocSpn[ri] != 1) {
      fprintf(stderr, "error: makeInitialSample_spin_left_ones only works if every site has local spins, so LocSpn[ri] == 1\n");
      fprintf(stderr, "we have: LocSpn[%d] = %d\n", ri, LocSpn[ri]);
      MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
    }
  }

//#pragma omp parallel for default(shared) private(rsi)
  for (int ei = 0; ei < Ne; ++ei) {
    eleCfg[ei] = ei;
    eleCfg[ei + Ne] = -1;
    eleCfg[ei + Nsite + Ne] = ei;
    eleCfg[ei + Nsite] = -1;

    eleIdx[ei] = ei;
    eleIdx[ei + Ne] = ei + Ne;

    eleNum[ei] = 1;
    eleNum[ei + Ne] = 0;
    eleNum[ei + Nsite + Ne] = 1;
    eleNum[ei + Nsite] = 0;
  }
  MakeProjCnt(eleProjCnt,eleNum);

  return 0;
}

// #define _DEBUG

// BASED ON: https://stackoverflow.com/a/1504565/3025981
int GetPermutationSign(const int permutation[], const int length, MPI_Comm comm) {
    bool elements_seen[64];
    if (length > 64) {
      fprintf(stderr, "GetPermutationSign: too many electrons: %d; "
                      "at most 64 electrons are supported at the moment\n",
                      length);
      MPI_Abort(comm, EXIT_FAILURE);
    }
    memset(elements_seen, false, sizeof(elements_seen) / sizeof(bool));

    int cycles = 0;
    for (int index = 0; index < length; ++index) {
      if (elements_seen[index]) { continue; } 

      ++cycles;
      int current = index;
      while (!elements_seen[current]) {
        elements_seen[current] = true;
        current = permutation[current];
        if (current < 0 || current >= length) {
          fprintf(stderr, "Invalid permutation: [");
          for (int k = 0; k < length; ++k) {
            if (k != 0) { fprintf(stderr, ", "); }
            fprintf(stderr, "%d", permutation[k]);
          }
          fprintf(stderr, "]\n");
          MPI_Abort(comm, EXIT_FAILURE);
        }
      }
    }

    return 1 - 2 * ((length - cycles + 1) % 2);
}
// END BASED


void RecordComputedWaveFunction(int const eleIdx[], int const eleCfg[], int const eleNum[],
                                int const eleProjCnt[], int const qpStart, int const qpEnd,
                                MPI_Comm comm, double const ip) {
  FILE* globalOutputFile = WaveFunctionOutputFile();
  if (globalOutputFile == NULL) { return; }

  // We're going to store the spin configuration in a uint64_t,
  // so check that the 64 bits is enough
  if (Nsite > 64) {
    fprintf(stderr, "RecordComputedWaveFunction: Lattice too big: %d; "
                    "at most 64 sites are supported at the moment\n",
                    Nsite);
    MPI_Abort(comm, EXIT_FAILURE);
  }

  uint64_t spin_conf = 0;

  // # ifdef _DEBUG
  // fprintf(globalOutputFile, "\t[");
  // for (int spin = 0; spin < 2; ++spin) {
  //   for (int position = 0; position < Nsite; ++position) {
  //     if (spin != 0 || position != 0) { fprintf(globalOutputFile, ","); }
  //     fprintf(globalOutputFile, "%d", eleCfg[position + spin * Nsite]);
  //   }
  // }
  // fprintf(globalOutputFile, "]\t[");
  // for (int spin = 0; spin < 2; ++spin) {
  //   for (int index = 0; index < Ne; ++index) {
  //     if (spin != 0 || index != 0) { fprintf(globalOutputFile, ","); }
  //     fprintf(globalOutputFile, "%d", eleIdx[index + spin * Ne]);
  //   }
  // }
  // fprintf(globalOutputFile, "]\t[");
  // for (int spin = 0; spin < 2; ++spin) {
  //   for (int position = 0; position < Nsite; ++position) {
  //     if (spin != 0 || position != 0) { fprintf(globalOutputFile, ","); }
  //     fprintf(globalOutputFile, "%d", eleNum[position + spin * Nsite]);
  //   }
  // }
  // fprintf(globalOutputFile, "]\t[");
  // for (int proj = 0; proj < NProj; ++proj) {
  //   if (proj != 0) { fprintf(globalOutputFile, ","); }
  //   fprintf(globalOutputFile, "%d", eleProjCnt[proj]);
  // }
  // fprintf(globalOutputFile, "]\t");

  // fprintf(globalOutputFile, "%i\t%i\t", qpStart, qpEnd);
  // # endif

  for (int position = 0; position < Nsite; ++position) {
    if (eleCfg[position] != -1 || eleCfg[position + Nsite] != -1) {
      spin_conf |= ((uint64_t)(eleCfg[position] == -1)) << position;
    }
    else {
      fprintf(stderr, "RecordComputedWaveFunction: incorrect spin configuration?\n");
      MPI_Abort(comm, EXIT_FAILURE);
    }
  }

  fprintf(globalOutputFile, "%zu\t%f\n", spin_conf, ip * GetPermutationSign(eleIdx, 2 * Ne, comm));
}

void swap_spins(int ri, int rj, int *eleIdx, int *eleCfg, int *eleNum, int *eleProjCnt,
                int qpStart, int qpEnd, MPI_Comm comm) {
  int s = eleCfg[ri] != -1 ? 0 : 1;
  int t = 1 - s;
  int mi = eleCfg[ri + s * Nsite];
  int mj = eleCfg[rj + t * Nsite];
  double pfMNew_real[NQPFull];
  int projCntNew[NProj];
  
  // # ifdef _DEBUG
  // fprintf(stderr, "swap_spins: ri = %d, rj = %d, s = %d, t = %d, mi = %d, mj = %d\n", ri, rj, s, t, mi, mj);
  // # endif

  updateEleConfig(mi, ri, rj, s, eleIdx, eleCfg, eleNum);
  UpdateProjCnt(ri, rj, s, projCntNew, eleProjCnt, eleNum);
  /* The mj-th electron with spin t hops to ri */
  updateEleConfig(mj, rj, ri, t, eleIdx, eleCfg, eleNum);
  UpdateProjCnt(rj, ri, t, projCntNew, projCntNew, eleNum);

  CalculateNewPfMTwo2_real(mi, s, mj, t, pfMNew_real, eleIdx, qpStart, qpEnd);
  
  CalculateLogIP_real(pfMNew_real, qpStart, qpEnd, comm);
  
  RecordComputedWaveFunction(eleIdx, eleCfg, eleNum, eleProjCnt, qpStart, qpEnd, comm,
                             CalculateIP_real(pfMNew_real, qpStart, qpEnd, comm));

  UpdateMAllTwo_real(mi, s, mj, t, ri, rj, eleIdx, qpStart, qpEnd);
}

void walkRight(int n, int k, int pos, int *eleIdx, int *eleCfg, int *eleNum, int *eleProjCnt, int qpStart, int qpEnd, MPI_Comm comm);
void walkLeft(int n, int k, int pos, int *eleIdx, int *eleCfg, int *eleNum, int *eleProjCnt, int qpStart, int qpEnd, MPI_Comm comm);

// Walking bit algorithm
// BASED ON: https://stackoverflow.com/a/36466454/3025981
void walkRight(int n, int k, int pos, int *eleIdx, int *eleCfg, int *eleNum, int *eleProjCnt, 
               int qpStart, int qpEnd, MPI_Comm comm) {
  // # ifdef _DEBUG
  // fprintf(stderr, "walkRight: n = %d, k = %d, pos = %d\n", n, k, pos);
  // # endif

  if (k == 1) {
    for(int p = pos + 1; p < pos + n; ++p) {
      swap_spins(p, p - 1, eleIdx, eleCfg, eleNum, eleProjCnt, qpStart, qpEnd, comm);
    }
  } else {
    for(int i = 1; i <= n - k; i++) {
      if (i % 2 == 1) {
        walkRight(n - i, k - 1, pos + i, eleIdx, eleCfg, eleNum, eleProjCnt, qpStart, qpEnd, comm);
      } else {
        walkLeft(n - i, k - 1, pos + i, eleIdx, eleCfg, eleNum, eleProjCnt, qpStart, qpEnd, comm);
      }
      swap_spins(pos + i - 1, pos + i + (i % 2 ? 0 : k - 1), eleIdx, eleCfg, eleNum, eleProjCnt, 
                 qpStart, qpEnd, comm);
    }
  }
}

void walkLeft(int n, int k, int pos, int *eleIdx, int *eleCfg, int *eleNum, int *eleProjCnt,
              int qpStart, int qpEnd, MPI_Comm comm) {
  // # ifdef _DEBUG
  // fprintf(stderr, "walkLeft: n = %d, k = %d, pos = %d\n", n, k, pos);
  // # endif

  if (k == 1) {
    for(int p = pos + n - 1; p > pos; --p) {
      swap_spins(p, p - 1, eleIdx, eleCfg, eleNum, eleProjCnt, qpStart, qpEnd, comm);
    }
  } else {
    for(int i = 1; i <= n - k; ++i) {
      if (i % 2 == 0) {
        walkRight(n - i, k - 1, pos, eleIdx, eleCfg, eleNum, eleProjCnt, qpStart, qpEnd, comm);
      } else {
        walkLeft(n - i, k - 1, pos, eleIdx, eleCfg, eleNum, eleProjCnt, qpStart, qpEnd, comm);
      }
      swap_spins(pos + n - i - (i % 2 ? 1 : k), pos + n - i, eleIdx, eleCfg, eleNum, eleProjCnt, 
                 qpStart, qpEnd, comm);
    }
  }
}
// END BASED

void VMCMakeSample_real(MPI_Comm comm) {
  InitWaveFunctionExtraction(comm);

  int outStep, nOutStep;
  int inStep, nInStep;
  UpdateType updateType;
  int mi, mj, ri, rj, s, t, i;
  int nAccept = 0;
  int sample;

  double logIpOld, logIpNew; /* logarithm of inner product <phi|L|x> */ // is this ok ? TBC
  int projCntNew[NProj];
  double pfMNew_real[NQPFull];
  double x, w; // TBC x will be complex number

  int qpStart, qpEnd;
  int rejectFlag;
  int rank, size;
  MPI_Comm_size(comm, &size);
  MPI_Comm_rank(comm, &rank);

  SplitLoop(&qpStart, &qpEnd, NQPFull, rank, size);


  StartTimer(30);
  if (GetShouldWalk()) {
    makeInitialSample_spin_left_ones(TmpEleIdx, TmpEleCfg, TmpEleNum, TmpEleProjCnt,
                                     qpStart, qpEnd, comm);
  } else {
    if (BurnFlag == 0) {
      makeInitialSample(TmpEleIdx, TmpEleCfg, TmpEleNum, TmpEleProjCnt,
                        qpStart, qpEnd, comm);
    } else {
      copyFromBurnSample(TmpEleIdx, TmpEleCfg, TmpEleNum, TmpEleProjCnt);
    }
  }

#ifdef _pf_block_update
  // TODO: Compute from qpStart to qpEnd to support loop splitting.
  void *pfOrbital[NQPFull];
  void *pfUpdator[NQPFull];
  // Read block size from input.
  const char *optBlockSize = getenv("VMC_BLOCK_UPDATE_SIZE");
  if (optBlockSize)
    NBlockUpdateSize = atoi(optBlockSize);
  // Fall back to default if input is invalid.
  if (NBlockUpdateSize < 1 || NBlockUpdateSize > 100)
    if (NExUpdatePath == 0)
      NBlockUpdateSize = 2;
    else
      NBlockUpdateSize = 20;

  // Set one universal EleSpn.
  for (mi=0; mi<Ne;  mi++) EleSpn[mi] = 0;
  for (mi=Ne;mi<Ne*2;mi++) EleSpn[mi] = 1;
  // Initialize.
  updated_tdi_v_init_d(NQPFull, Nsite, Nsite2, Nsize,
                       SlaterElm_real, Nsite2*Nsite2,
                       InvM_real, Nsize*Nsize,
                       TmpEleIdx, EleSpn,
                       NBlockUpdateSize,
                       pfUpdator, pfOrbital);
  updated_tdi_v_get_pfa_d(NQPFull, PfM_real, pfUpdator);
#else
  CalculateMAll_real(TmpEleIdx, qpStart, qpEnd);
#endif
  // printf("DEBUG: maker1: PfM=%lf\n",creal(PfM[0]));
  logIpOld = CalculateLogIP_real(PfM_real, qpStart, qpEnd, comm);
  RecordComputedWaveFunction(TmpEleIdx, TmpEleCfg, TmpEleNum, TmpEleProjCnt, qpStart, qpEnd, comm,
                             CalculateIP_real(PfM_real, qpStart, qpEnd, comm));
  if (GetShouldWalk()) {
    walkRight(Nsite, Ne, 0, TmpEleIdx, TmpEleCfg, TmpEleNum, TmpEleProjCnt,
              qpStart, qpEnd, comm);
    fprintf(stderr, "Walking complete; stopping the program now ...\n");
    MPI_Abort(comm, 0);
  }
  
  if (!isfinite(logIpOld)) {
    if (rank == 0) fprintf(stderr, "waring: VMCMakeSample remakeSample logIpOld=%e\n", creal(logIpOld)); //TBC
    makeInitialSample(TmpEleIdx, TmpEleCfg, TmpEleNum, TmpEleProjCnt,
                      qpStart, qpEnd, comm);
#ifdef _pf_block_update
    // Clear and reinitialize.
    updated_tdi_v_free_d(NQPFull, pfUpdator, pfOrbital);
    updated_tdi_v_init_d(NQPFull, Nsite, Nsite2, Nsize,
                         SlaterElm_real, Nsite2*Nsite2,
                         InvM_real, Nsize*Nsize,
                         TmpEleIdx, EleSpn,
                         NBlockUpdateSize,
                         pfUpdator, pfOrbital);
    updated_tdi_v_get_pfa_d(NQPFull, PfM_real, pfUpdator);
#else
    CalculateMAll_real(TmpEleIdx, qpStart, qpEnd);
#endif
    //printf("DEBUG: maker2: PfM=%lf\n",creal(PfM[0]));
    logIpOld = CalculateLogIP_real(PfM_real, qpStart, qpEnd, comm);
    RecordComputedWaveFunction(TmpEleIdx, TmpEleCfg, TmpEleNum, TmpEleProjCnt, qpStart, qpEnd, comm,
                               CalculateIP_real(PfM_real, qpStart, qpEnd, comm));
    BurnFlag = 0;
  }
  StopTimer(30);

  nOutStep = (BurnFlag == 0) ? NVMCWarmUp + NVMCSample : NVMCSample + 1;
  nInStep = NVMCInterval * Nsite;

  for (i = 0; i < Counter_max; i++) Counter[i] = 0;  /* reset counter */

  for (outStep = 0; outStep < nOutStep; outStep++) {
    for (inStep = 0; inStep < nInStep; inStep++) {

      updateType = getUpdateType(NExUpdatePath);

      if (updateType == HOPPING) { /* hopping */
        Counter[0]++;

        StartTimer(31);
        makeCandidate_hopping(&mi, &ri, &rj, &s, &rejectFlag,
                              TmpEleIdx, TmpEleCfg);
        StopTimer(31);

        if (rejectFlag) continue;

        StartTimer(32);
        StartTimer(60);
        /* The mi-th electron with spin s hops to site rj */
        updateEleConfig(mi, ri, rj, s, TmpEleIdx, TmpEleCfg, TmpEleNum);
        UpdateProjCnt(ri, rj, s, projCntNew, TmpEleProjCnt, TmpEleNum);
        StopTimer(60);

        StartTimer(61);
#ifdef _pf_block_update
        updated_tdi_v_push_d(NQPFull, rj+s*Nsite, mi+s*Ne, 1, pfUpdator);
        updated_tdi_v_get_pfa_d(NQPFull, pfMNew_real, pfUpdator);
#else
        //CalculateNewPfM2(mi,s,pfMNew,TmpEleIdx,qpStart,qpEnd);
        CalculateNewPfM2_real(mi, s, pfMNew_real, TmpEleIdx, qpStart, qpEnd);
#endif
        //printf("DEBUG: out %d in %d pfMNew=%lf \n",outStep,inStep,creal(pfMNew[0]));
        StopTimer(61);

        StartTimer(62);
        /* calculate inner product <phi|L|x> */
        //logIpNew = CalculateLogIP_fcmp(pfMNew,qpStart,qpEnd,comm);
        logIpNew = CalculateLogIP_real(pfMNew_real, qpStart, qpEnd, comm);
        RecordComputedWaveFunction(TmpEleIdx, TmpEleCfg, TmpEleNum, TmpEleProjCnt, qpStart, qpEnd, comm,
                                   CalculateIP_real(pfMNew_real, qpStart, qpEnd, comm));
        StopTimer(62);

        /* Metroplis */
        x = LogProjRatio(projCntNew, TmpEleProjCnt);
        w = exp(2.0 * (x + (logIpNew - logIpOld)));
        if (!isfinite(w)) w = -1.0; /* should be rejected */

        if (w > genrand_real2()) { /* accept */
          StartTimer(63);
#ifdef _pf_block_update
          // Inv already updated. Only need to get PfM again.
          updated_tdi_v_get_pfa_d(NQPFull, PfM_real, pfUpdator);
#else
          // UpdateMAll will change SlaterElm, InvM (including PfM)
          UpdateMAll_real(mi, s, TmpEleIdx, qpStart, qpEnd);
          //            UpdateMAll(mi,s,TmpEleIdx,qpStart,qpEnd);
#endif
          StopTimer(63);

          for (i = 0; i < NProj; i++) TmpEleProjCnt[i] = projCntNew[i];
          logIpOld = logIpNew;
          nAccept++;
          Counter[1]++;
        } else { /* reject */
#ifdef _pf_block_update
          StartTimer(61);
          updated_tdi_v_pop_d(NQPFull, 0, pfUpdator);
          StopTimer(61);
#endif
          revertEleConfig(mi, ri, rj, s, TmpEleIdx, TmpEleCfg, TmpEleNum);
        }
        StopTimer(32);

      } else if (updateType == EXCHANGE) { /* exchange */
        Counter[2]++;

        StartTimer(31);
        makeCandidate_exchange(&mi, &ri, &rj, &s, &rejectFlag,
                               TmpEleIdx, TmpEleCfg, TmpEleNum);
        StopTimer(31);

        if (rejectFlag) continue;

        StartTimer(33);
        StartTimer(65);

        /* The mi-th electron with spin s exchanges with the electron on site rj with spin 1-s */
        t = 1 - s;
        mj = TmpEleCfg[rj + t * Nsite];

        /* The mi-th electron with spin s hops to rj */
        updateEleConfig(mi, ri, rj, s, TmpEleIdx, TmpEleCfg, TmpEleNum);
        UpdateProjCnt(ri, rj, s, projCntNew, TmpEleProjCnt, TmpEleNum);
        /* The mj-th electron with spin t hops to ri */
        updateEleConfig(mj, rj, ri, t, TmpEleIdx, TmpEleCfg, TmpEleNum);
        UpdateProjCnt(rj, ri, t, projCntNew, projCntNew, TmpEleNum);

        StopTimer(65);
        StartTimer(66);

#ifdef _pf_block_update
        updated_tdi_v_push_pair_d(NQPFull,
                                  rj+s*Nsite, mi+s*Ne,
                                  ri+t*Nsite, mj+t*Ne,
                                  1, pfUpdator);
        updated_tdi_v_get_pfa_d(NQPFull, pfMNew_real, pfUpdator);
#else
        CalculateNewPfMTwo2_real(mi, s, mj, t, pfMNew_real, TmpEleIdx, qpStart, qpEnd);
#endif
        StopTimer(66);
        StartTimer(67);

        /* calculate inner product <phi|L|x> */
        logIpNew = CalculateLogIP_real(pfMNew_real, qpStart, qpEnd, comm);
        RecordComputedWaveFunction(TmpEleIdx, TmpEleCfg, TmpEleNum, TmpEleProjCnt, qpStart, qpEnd, comm,
                                   CalculateIP_real(pfMNew_real, qpStart, qpEnd, comm));

        StopTimer(67);

        /* Metroplis */
        x = LogProjRatio(projCntNew, TmpEleProjCnt);
        w = exp(2.0 * (x + (logIpNew - logIpOld))); //TBC
        if (!isfinite(w)) w = -1.0; /* should be rejected */

        if (w > genrand_real2()) { /* accept */
          StartTimer(68);
#ifdef _pf_block_update
          // Inv already updated. Only need to get PfM again.
          updated_tdi_v_get_pfa_d(NQPFull, PfM_real, pfUpdator);
#else
          UpdateMAllTwo_real(mi, s, mj, t, ri, rj, TmpEleIdx, qpStart, qpEnd);
#endif
          StopTimer(68);

          for (i = 0; i < NProj; i++) TmpEleProjCnt[i] = projCntNew[i];
          logIpOld = logIpNew;
          nAccept++;
          Counter[3]++;
        } else { /* reject */
#ifdef _pf_block_update
          StartTimer(66);
          updated_tdi_v_pop_d(NQPFull, 0, pfUpdator);
          updated_tdi_v_pop_d(NQPFull, 0, pfUpdator);
          StopTimer(66);
#endif
          revertEleConfig(mj, rj, ri, t, TmpEleIdx, TmpEleCfg, TmpEleNum);
          revertEleConfig(mi, ri, rj, s, TmpEleIdx, TmpEleCfg, TmpEleNum);
        }
        StopTimer(33);
      }

      if (nAccept > Nsite) {
        // Recalculate PfM and InvM.
        StartTimer(34);
#ifdef _pf_block_update
        // Clear and reinitialize.
        updated_tdi_v_free_d(NQPFull, pfUpdator, pfOrbital);
        updated_tdi_v_init_d(NQPFull, Nsite, Nsite2, Nsize,
                             SlaterElm_real, Nsite2*Nsite2,
                             InvM_real, Nsize*Nsize,
                             TmpEleIdx, EleSpn,
                             NBlockUpdateSize,
                             pfUpdator, pfOrbital);
        updated_tdi_v_get_pfa_d(NQPFull, PfM_real, pfUpdator);
#else
        CalculateMAll_real(TmpEleIdx, qpStart, qpEnd);
#endif
        //printf("DEBUG: maker3: PfM=%lf\n",creal(PfM[0]));
        logIpOld = CalculateLogIP_real(PfM_real, qpStart, qpEnd, comm);
        RecordComputedWaveFunction(TmpEleIdx, TmpEleCfg, TmpEleNum, TmpEleProjCnt, qpStart, qpEnd, comm,
                                   CalculateIP_real(PfM_real, qpStart, qpEnd, comm));
        StopTimer(34);
        nAccept = 0;
      }
    } /* end of instep */

    StartTimer(35);
    /* save Electron Configuration */
    if (outStep >= nOutStep - NVMCSample) {
      sample = outStep - (nOutStep - NVMCSample);
      saveEleConfig(sample, logIpOld, TmpEleIdx, TmpEleCfg, TmpEleNum, TmpEleProjCnt);
    }
    StopTimer(35);

  } /* end of outstep */

  copyToBurnSample(TmpEleIdx, TmpEleCfg, TmpEleNum, TmpEleProjCnt);
  BurnFlag = 1;

#ifdef _pf_block_update
  // Free-up updator space.
  updated_tdi_v_free_d(NQPFull, pfUpdator, pfOrbital);
#endif

  ExitWaveFunctionExtraction(comm);
  return;
}

int makeInitialSample_real(int *eleIdx, int *eleCfg, int *eleNum, int *eleProjCnt,
                           const int qpStart, const int qpEnd, MPI_Comm comm) {
  const int nsize = Nsize;
  const int nsite2 = Nsite2;
  int flag = 1, flagRdc, loop = 0;
  int ri, mi, si, msi, rsi;
  int rank, size;
  MPI_Comm_size(comm, &size);
  MPI_Comm_rank(comm, &rank);

  do {
    /* initialize */
#pragma omp parallel for default(shared) private(msi)
    for (msi = 0; msi < nsize; msi++) eleIdx[msi] = -1;
#pragma omp parallel for default(shared) private(rsi)
    for (rsi = 0; rsi < nsite2; rsi++) eleCfg[rsi] = -1;

    /* local spin */
    for (ri = 0; ri < Nsite; ri++) {
      if (LocSpn[ri] == 1) {
        do {
          mi = gen_rand32() % Ne;
          si = (genrand_real2() < 0.5) ? 0 : 1;
        } while (eleIdx[mi + si * Ne] != -1);
        eleCfg[ri + si * Nsite] = mi;
        eleIdx[mi + si * Ne] = ri;
      }
    }

    /* itinerant electron */
    for (si = 0; si < 2; si++) {
      for (mi = 0; mi < Ne; mi++) {
        if (eleIdx[mi + si * Ne] == -1) {
          do {
            ri = gen_rand32() % Nsite;
          } while (eleCfg[ri + si * Nsite] != -1 || LocSpn[ri] == 1);
          eleCfg[ri + si * Nsite] = mi;
          eleIdx[mi + si * Ne] = ri;
        }
      }
    }

    /* EleNum */
#pragma omp parallel for default(shared) private(rsi)
#pragma loop noalias
    for (rsi = 0; rsi < nsite2; rsi++) {
      eleNum[rsi] = (eleCfg[rsi] < 0) ? 0 : 1;
    }

    MakeProjCnt(eleProjCnt, eleNum);

    flag = CalculateMAll_real(eleIdx, qpStart, qpEnd);
    //printf("DEBUG: maker4: PfM=%lf\n",creal(PfM[0]));
    if (size > 1) {
      MPI_Allreduce(&flag, &flagRdc, 1, MPI_INT, MPI_MAX, comm);
      flag = flagRdc;
    }

    loop++;
    if (loop > 100) {
      if (rank == 0) fprintf(stderr, "error: makeInitialSample: Too many loops\n");
      MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
    }
  } while (flag > 0);

  return 0;
}

void VMC_BF_MakeSample_real(MPI_Comm comm) {
  int outStep, nOutStep;
  int inStep, nInStep;
  UpdateType updateType;
  int mi, mj, ri, rj, s, t, i;
  int nAccept = 0;
  int sample;
  int tmp_i;

  double logIpOld, logIpNew; /* logarithm of inner product <phi|L|x> */ // is this ok ? TBC
  int projCntNew[NProj];
  int projBFCntNew[16 * Nsite * Nrange]; // For BackFlow
  int msaTmp[NQPFull * Nsite], icount[NQPFull]; // For BackFlow
  double pfMNew_real[NQPFull];
  double x, w; // TBC x will be complex number

  int qpStart, qpEnd;
  int rejectFlag;
  int rank, size;
  MPI_Comm_size(comm, &size);
  MPI_Comm_rank(comm, &rank);

  SplitLoop(&qpStart, &qpEnd, NQPFull, rank, size);

  StartTimer(30);
  if (BurnFlag == 0) {
    makeInitialSampleBF(TmpEleIdx, TmpEleCfg, TmpEleNum, TmpEleProjCnt, TmpEleProjBFCnt,
                        qpStart, qpEnd, comm);
    //makeInitialSample(TmpEleIdx,TmpEleCfg,TmpEleNum,TmpEleProjCnt,
    //                  qpStart,qpEnd,comm);
  } else {
    //copyFromBurnSample(TmpEleIdx,TmpEleCfg,TmpEleNum,TmpEleProjCnt);
    copyFromBurnSampleBF(TmpEleIdx);
    MakeSlaterElmBF_fcmp(TmpEleNum, TmpEleProjBFCnt);
#pragma omp parallel for default(shared) private(tmp_i)
    for(tmp_i=0;tmp_i<NQPFull*(2*Nsite)*(2*Nsite);tmp_i++) SlaterElm_real[tmp_i]= creal(SlaterElm[tmp_i]);
  }

  CalculateMAll_BF_real(TmpEleIdx, qpStart, qpEnd);
  // printf("DEBUG: maker1: PfM=%lf\n",creal(PfM[0]));
  logIpOld = CalculateLogIP_real(PfM_real, qpStart, qpEnd, comm);
  RecordComputedWaveFunction(TmpEleIdx, TmpEleCfg, TmpEleNum, TmpEleProjCnt, qpStart, qpEnd, comm,
                             CalculateIP_real(PfM_real, qpStart, qpEnd, comm));
  if (!isfinite(logIpOld)) {
    if (rank == 0) fprintf(stderr, "waring: VMCMakeSample remakeSample logIpOld=%e\n", creal(logIpOld)); //TBC
    //    makeInitialSample(TmpEleIdx,TmpEleCfg,TmpEleNum,TmpEleProjCnt,
    //                    qpStart,qpEnd,comm);
    makeInitialSampleBF(TmpEleIdx, TmpEleCfg, TmpEleNum, TmpEleProjCnt, TmpEleProjBFCnt,
                        qpStart, qpEnd, comm);

    CalculateMAll_BF_real(TmpEleIdx, qpStart, qpEnd);
    //printf("DEBUG: maker2: PfM=%lf\n",creal(PfM[0]));
    logIpOld = CalculateLogIP_real(PfM_real, qpStart, qpEnd, comm);
    RecordComputedWaveFunction(TmpEleIdx, TmpEleCfg, TmpEleNum, TmpEleProjCnt, qpStart, qpEnd, comm,
                               CalculateIP_real(PfM_real, qpStart, qpEnd, comm));
    BurnFlag = 0;
  }
  StopTimer(30);

  nOutStep = (BurnFlag == 0) ? NVMCWarmUp + NVMCSample : NVMCSample + 1;
  nInStep = NVMCInterval * Nsite;

  for (i = 0; i < Counter_max; i++) Counter[i] = 0;  /* reset counter */

  for (outStep = 0; outStep < nOutStep; outStep++) {
    for (inStep = 0; inStep < nInStep; inStep++) {

      updateType = getUpdateType(NExUpdatePath);

      if (updateType == HOPPING) { /* hopping */
        Counter[0]++;

        StartTimer(31);
        makeCandidate_hopping(&mi, &ri, &rj, &s, &rejectFlag,
                              TmpEleIdx, TmpEleCfg);
        StopTimer(31);

        if (rejectFlag) continue;

        StartTimer(32);
        StartTimer(60);
        /* The mi-th electron with spin s hops to site rj */
        updateEleConfig(mi, ri, rj, s, TmpEleIdx, TmpEleCfg, TmpEleNum);
        UpdateProjCnt(ri, rj, s, projCntNew, TmpEleProjCnt, TmpEleNum);
        MakeProjBFCnt(projBFCntNew, TmpEleNum);
        StopTimer(60);
        UpdateSlaterElmBF_fcmp(mi, ri, rj, s, TmpEleCfg, TmpEleNum, projBFCntNew, msaTmp, icount,
                               SlaterElmBF);
#pragma omp parallel for default(shared) private(tmp_i)
        for(tmp_i=0;tmp_i<NQPFull*(2*Nsite)*(2*Nsite);tmp_i++) SlaterElm_real[tmp_i]= creal(SlaterElm[tmp_i]);

        StartTimer(61);
        //CalculateNewPfM2(mi,s,pfMNew,TmpEleIdx,qpStart,qpEnd);
        //CalculateNewPfM2_real(mi,s,pfMNew_real,TmpEleIdx,qpStart,qpEnd);
        CalculateNewPfMBF_real(icount, msaTmp, pfMNew_real, TmpEleIdx, qpStart, qpEnd, SlaterElmBF_real);

        //printf("DEBUG: out %d in %d pfMNew=%lf \n",outStep,inStep,creal(pfMNew[0]));
        StopTimer(61);

        StartTimer(62);
        /* calculate inner product <phi|L|x> */
        //logIpNew = CalculateLogIP_fcmp(pfMNew,qpStart,qpEnd,comm);
        logIpNew = CalculateLogIP_real(pfMNew_real, qpStart, qpEnd, comm);
        RecordComputedWaveFunction(TmpEleIdx, TmpEleCfg, TmpEleNum, TmpEleProjCnt, qpStart, qpEnd, comm,
                                   CalculateIP_real(pfMNew_real, qpStart, qpEnd, comm));
        StopTimer(62);

        /* Metroplis */
        x = LogProjRatio(projCntNew, TmpEleProjCnt);
        w = exp(2.0 * (x + (logIpNew - logIpOld)));
        if (!isfinite(w)) w = -1.0; /* should be rejected */

        if (w > genrand_real2()) { /* accept */
          // UpdateMAll will change SlaterElm, InvM (including PfM)
          StartTimer(63);
          UpdateMAll_BF_real(icount, msaTmp, PfM_real, TmpEleIdx, qpStart, qpEnd);
          //          UpdateMAll_real(mi,s,TmpEleIdx,qpStart,qpEnd);
          //            UpdateMAll(mi,s,TmpEleIdx,qpStart,qpEnd);
          StopTimer(63);

          for (i = 0; i < NProj; i++) TmpEleProjCnt[i] = projCntNew[i];
          for (i = 0; i < 16 * Nsite * Nrange; i++) TmpEleProjBFCnt[i] = projBFCntNew[i];
          logIpOld = logIpNew;
          nAccept++;
          Counter[1]++;
        } else { /* reject */
          revertEleConfig(mi, ri, rj, s, TmpEleIdx, TmpEleCfg, TmpEleNum);
          //TODO: Add Timer
          UpdateSlaterElmBF_fcmp(mi, rj, ri, s, TmpEleCfg, TmpEleNum, TmpEleProjBFCnt, msaTmp, icount,
                                 SlaterElmBF);
#pragma omp parallel for default(shared) private(tmp_i)
          for(tmp_i=0;tmp_i<NQPFull*(2*Nsite)*(2*Nsite);tmp_i++) SlaterElm_real[tmp_i]= creal(SlaterElm[tmp_i]);
        }
        StopTimer(32);

      } else if (updateType == EXCHANGE) { /* exchange */
        Counter[2]++;

        StartTimer(31);
        makeCandidate_exchange(&mi, &ri, &rj, &s, &rejectFlag,
                               TmpEleIdx, TmpEleCfg, TmpEleNum);
        StopTimer(31);

        if (rejectFlag) continue;

        StartTimer(33);
        StartTimer(65);

        /* The mi-th electron with spin s exchanges with the electron on site rj with spin 1-s */
        t = 1 - s;
        mj = TmpEleCfg[rj + t * Nsite];

        /* The mi-th electron with spin s hops to rj */
        updateEleConfig(mi, ri, rj, s, TmpEleIdx, TmpEleCfg, TmpEleNum);
        UpdateProjCnt(ri, rj, s, projCntNew, TmpEleProjCnt, TmpEleNum);
        /* The mj-th electron with spin t hops to ri */
        updateEleConfig(mj, rj, ri, t, TmpEleIdx, TmpEleCfg, TmpEleNum);
        UpdateProjCnt(rj, ri, t, projCntNew, projCntNew, TmpEleNum);

        StopTimer(65);
        StartTimer(66);

        CalculateNewPfMTwo2_real(mi, s, mj, t, pfMNew_real, TmpEleIdx, qpStart, qpEnd);
        StopTimer(66);
        StartTimer(67);

        /* calculate inner product <phi|L|x> */
        logIpNew = CalculateLogIP_real(pfMNew_real, qpStart, qpEnd, comm);
        RecordComputedWaveFunction(TmpEleIdx, TmpEleCfg, TmpEleNum, TmpEleProjCnt, qpStart, qpEnd, comm,
                                   CalculateIP_real(pfMNew_real, qpStart, qpEnd, comm));

        StopTimer(67);

        /* Metroplis */
        x = LogProjRatio(projCntNew, TmpEleProjCnt);
        w = exp(2.0 * (x + (logIpNew - logIpOld))); //TBC
        if (!isfinite(w)) w = -1.0; /* should be rejected */

        if (w > genrand_real2()) { /* accept */
          StartTimer(68);
          UpdateMAllTwo_real(mi, s, mj, t, ri, rj, TmpEleIdx, qpStart, qpEnd);
          StopTimer(68);

          for (i = 0; i < NProj; i++) TmpEleProjCnt[i] = projCntNew[i];
          logIpOld = logIpNew;
          nAccept++;
          Counter[3]++;
        } else { /* reject */
          revertEleConfig(mj, rj, ri, t, TmpEleIdx, TmpEleCfg, TmpEleNum);
          revertEleConfig(mi, ri, rj, s, TmpEleIdx, TmpEleCfg, TmpEleNum);
        }
        StopTimer(33);
      }

      if (nAccept > Nsite) {
        StartTimer(34);
        /* recal PfM and InvM */
        //CalculateMAll_real(TmpEleIdx,qpStart,qpEnd);
        //printf("DEBUG: maker3: PfM=%lf\n",creal(PfM[0]));
        CalculateMAll_BF_real(TmpEleIdx, qpStart, qpEnd);
        logIpOld = CalculateLogIP_real(PfM_real, qpStart, qpEnd, comm);
        RecordComputedWaveFunction(TmpEleIdx, TmpEleCfg, TmpEleNum, TmpEleProjCnt, qpStart, qpEnd, comm,
                                   CalculateIP_real(PfM_real, qpStart, qpEnd, comm));
        StopTimer(34);
        nAccept = 0;
      }
    } /* end of instep */

    StartTimer(35);
    /* save Electron Configuration */
    if (outStep >= nOutStep - NVMCSample) {
      sample = outStep - (nOutStep - NVMCSample);
      saveEleConfigBF(sample, logIpOld, TmpEleIdx, TmpEleCfg, TmpEleNum, TmpEleProjCnt, TmpEleProjBFCnt);
      //      saveEleConfig(sample,logIpOld,TmpEleIdx,TmpEleCfg,TmpEleNum,TmpEleProjCnt);
    }
    StopTimer(35);

  } /* end of outstep */

  //copyToBurnSample(TmpEleIdx,TmpEleCfg,TmpEleNum,TmpEleProjCnt);
  copyToBurnSampleBF(TmpEleIdx);
  BurnFlag = 1;
  return;
}

int makeInitialSampleBF_real(int *eleIdx, int *eleCfg, int *eleNum, int *eleProjCnt, int *eleProjBFCnt,
                             const int qpStart, const int qpEnd, MPI_Comm comm) {
  const int nsize = Nsize;
  const int nsite2 = Nsite2;
  int flag = 1, flagRdc, loop = 0;
  int ri, mi, si, msi, rsi;
  int rank, size;
  int tmp_i;

  MPI_Comm_size(comm, &size);
  MPI_Comm_rank(comm, &rank);

  do {
    /* initialize */
#pragma omp parallel for default(shared) private(msi)
    for (msi = 0; msi < nsize; msi++) eleIdx[msi] = -1;
#pragma omp parallel for default(shared) private(rsi)
    for (rsi = 0; rsi < nsite2; rsi++) eleCfg[rsi] = -1;

    /* local spin */
    for (ri = 0; ri < Nsite; ri++) {
      if (LocSpn[ri] == 1) {
        do {
          mi = gen_rand32() % Ne;
          si = (genrand_real2() < 0.5) ? 0 : 1;
        } while (eleIdx[mi + si * Ne] != -1);
        eleCfg[ri + si * Nsite] = mi;
        eleIdx[mi + si * Ne] = ri;
      }
    }

    /* itinerant electron */
    for (si = 0; si < 2; si++) {
      for (mi = 0; mi < Ne; mi++) {
        if (eleIdx[mi + si * Ne] == -1) {
          do {
            ri = gen_rand32() % Nsite;
          } while (eleCfg[ri + si * Nsite] != -1 || LocSpn[ri] == 1);
          eleCfg[ri + si * Nsite] = mi;
          eleIdx[mi + si * Ne] = ri;
        }
      }
    }

    /* EleNum */
#pragma omp parallel for default(shared) private(rsi)
#pragma loop noalias
    for (rsi = 0; rsi < nsite2; rsi++) {
      eleNum[rsi] = (eleCfg[rsi] < 0) ? 0 : 1;
    }

    MakeProjCnt(eleProjCnt, eleNum);
    MakeProjBFCnt(eleProjBFCnt, eleNum);

    MakeSlaterElmBF_fcmp(eleNum, eleProjBFCnt);
#pragma omp parallel for default(shared) private(tmp_i)
    for(tmp_i=0;tmp_i<NQPFull*(2*Nsite)*(2*Nsite);tmp_i++) SlaterElm_real[tmp_i]= creal(SlaterElm[tmp_i]);

    flag = CalculateMAll_BF_real(eleIdx, qpStart, qpEnd);
    //printf("DEBUG: maker4: PfM=%lf\n",creal(PfM[0]));
    if (size > 1) {
      MPI_Allreduce(&flag, &flagRdc, 1, MPI_INT, MPI_MAX, comm);
      flag = flagRdc;
    }

    loop++;
    if (loop > 100) {
      if (rank == 0) fprintf(stderr, "error: makeInitialSample: Too many loops\n");
      MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
    }
  } while (flag > 0);

  return 0;
}

#endif
