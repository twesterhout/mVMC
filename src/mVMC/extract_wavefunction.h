#pragma once

#include <mpi.h>
#include <stdio.h>
#include <stdbool.h>

void InitWaveFunctionExtraction(MPI_Comm comm);
void ExitWaveFunctionExtraction(MPI_Comm comm);
FILE* WaveFunctionOutputFile(void);
bool GetShouldWalk(void);
