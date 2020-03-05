#ifndef _IFFTW_H
#define _IFFTW_H

#ifdef HAVE_MPI
#  include "/qbigwork2/petschlies/software/FFTW/fftw-2.1.5/build.qbig.openmpi/include/fftw_mpi.h"
#  ifdef HAVE_OPENMP
#    include "/qbigwork2/petschlies/software/FFTW/fftw-2.1.5/build.qbig.openmpi/include/fftw_threads.h"
#  endif
#else
#  ifdef HAVE_OPENMP
#    include "/qbigwork2/petschlies/software/FFTW/fftw-2.1.5/build.qbig.openmpi/include/fftw_threads.h"
#    include "/qbigwork2/petschlies/software/FFTW/fftw-2.1.5/build.qbig.openmpi/include/fftw.h"
#  else
#    include "/qbigwork2/petschlies/software/FFTW/fftw-2.1.5/build.qbig.openmpi/include/fftw.h"
#  endif
#endif

#endif
