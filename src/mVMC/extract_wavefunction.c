#include "extract_wavefunction.h"

#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

FILE* globalOutputFile = NULL;
bool shouldWalk = false;

void InitWaveFunctionExtraction(MPI_Comm comm) {
  char const* filename = getenv("EXTRACT_WAVEFUNCTION");
  if (filename != NULL) {
    if (globalOutputFile != NULL) {
      fprintf(stderr, "InitWaveFunctionExtraction: globalOutputFile already initialized. Aborting ...\n");
      MPI_Abort(comm, -1);
    }

    int size;
    MPI_Comm_size(comm, &size);
    if (size > 1) {
      fprintf(stderr, "InitWaveFunctionExtraction: wave function extraction for MPI jobs with more than one rank is not supported. Aborting ...\n");
      MPI_Abort(comm, -1);
    }

    fprintf(stderr, "InitWaveFunctionExtraction: opening '%s' ...\n"
                    "InitWaveFunctionExtraction: all wave function evaluations will be written to it\n",
                    filename);
    globalOutputFile = fopen(filename, "a");

    if (getenv("WALK") != NULL) {
      shouldWalk = true;
    }
  }
}

void ExitWaveFunctionExtraction(MPI_Comm comm) {
  if (globalOutputFile != NULL) {
    fprintf(stderr, "ExitWaveFunctionExtraction: closing the file ...\n");
    fflush(globalOutputFile);
    fclose(globalOutputFile);
    globalOutputFile = NULL;
    shouldWalk = false;
  }
}

FILE* WaveFunctionOutputFile(void) {
  return globalOutputFile;
}

bool GetShouldWalk(void) {
  return shouldWalk;
}
