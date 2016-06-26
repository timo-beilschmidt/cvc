/****************************************************
 * test_overlap.c
 *
 * Mi 16. Mär 15:24:46 CET 2016
 *
 * PURPOSE:
 * TODO:
 * DONE:
 * CHANGES:
 ****************************************************/

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <getopt.h>
#ifdef HAVE_MPI
#  include <mpi.h>
#endif
#ifdef HAVE_OPENMP
#include <omp.h>
#endif

#define MAIN_PROGRAM

#include "types.h"
#include "cvc_complex.h"
#include "ilinalg.h"
#include "global.h"
#include "cvc_geometry.h"
#include "cvc_utils.h"
#include "set_default.h"
#include "mpi_init.h"
#include "io.h"
#include "io_utils.h"
#include "propagator_io.h"
#include "gauge_io.h"
#include "read_input_parser.h"
#include "laplace_linalg.h"
#include "hyp_smear.h"
#include "laphs_io.h"
#include "laphs_utils.h"
#include "laphs.h"
#include "Q_phi.h"
#include "invert_Qtm.h"

using namespace cvc;


void usage(void) {
  fprintf(stdout, "usage:\n");
  exit(0);
}

int main(int argc, char **argv) {
  
  const double preset_eigenvalue = 7.864396614243382E-06;

  int c, mu, nu, status, sid;
  int it_src = 1;
  int is_src = 2;
  int iv_src = 3;
  int i, j, ncon=-1, is, idx;
  int filename_set = 0;
  int x0, x1, x2, x3, ix, iix;
  int y0, y1, y2, y3;
  int threadid, nthreads;
  int no_eo_fields;
  int gsx0, gsx1, gsx2, gsx3;
  int lsx0, lsx1, lsx2, lsx3;
  int k1, k2, k3;

  double dtmp[2], dtmp2[2], norm;

  double plaq=0.;
  double *gauge_field_smeared = NULL;
  int verbose = 0;
  char filename[200];
  FILE *ofs=NULL;
  size_t items, bytes;
  complex w, w2;
  double **perambulator = NULL;
  double **eo_spinor_field=NULL;
  double ratime, retime;
  eigensystem_type es;
  randomvector_type rv, prv;
  perambulator_type peram;
  unsigned int Vhalf, VOL3, ioffset;
  int source_proc_coords[4], source_proc_id=0;
  int l_source_location;
  double spinor1[24];
  int momentum_vector[3], momentum_number=1;
  double *momentum_phase = NULL;
  double phase;
  double ***tripleV = NULL;
#ifdef HAVE_MPI
  MPI_Init(&argc, &argv);
#endif

  while ((c = getopt(argc, argv, "h?vf:")) != -1) {
    switch (c) {
    case 'v':
      verbose = 1;
      break;
    case 'f':
      strcpy(filename, optarg);
      filename_set=1;
      break;
    case 'h':
    case '?':
    default:
      usage();
      break;
    }
  }

  /* set the default values */
  if(filename_set==0) strcpy(filename, "cvc.input");
  if(g_cart_id==0) fprintf(stdout, "# [test_overlap] Reading input from file %s\n", filename);
  read_input_parser(filename);

#ifdef HAVE_TMLQCD_LIBWRAPPER

  fprintf(stdout, "# [p2gg_xspace] calling tmLQCD wrapper init functions\n");

  /*********************************
   * initialize MPI parameters for cvc
   *********************************/
  status = tmLQCD_invert_init(argc, argv, 1);
  if(status != 0) {
    EXIT(14);
  }
  status = tmLQCD_get_mpi_params(&g_tmLQCD_mpi);
  if(status != 0) {
    EXIT(15);
  }
  status = tmLQCD_get_lat_params(&g_tmLQCD_lat);
  if(status != 0) {
    EXIT(16);
  }
#endif

  /* initialize MPI parameters */
  mpi_init(argc, argv);

  /* initialize T etc. */
  fprintf(stdout, "# [%2d] parameters:\n"\
                  "# [%2d] T_global     = %3d\n"\
                  "# [%2d] T            = %3d\n"\
		  "# [%2d] Tstart       = %3d\n"\
                  "# [%2d] LX_global    = %3d\n"\
                  "# [%2d] LX           = %3d\n"\
		  "# [%2d] LXstart      = %3d\n"\
                  "# [%2d] LY_global    = %3d\n"\
                  "# [%2d] LY           = %3d\n"\
		  "# [%2d] LYstart      = %3d\n",\
		  g_cart_id, g_cart_id, T_global, g_cart_id, T, g_cart_id, Tstart,
		             g_cart_id, LX_global, g_cart_id, LX, g_cart_id, LXstart,
		             g_cart_id, LY_global, g_cart_id, LY, g_cart_id, LYstart);

  if(init_geometry() != 0) {
    fprintf(stderr, "[test_overlap] ERROR from init_geometry\n");
    EXIT(101);
  }

  geometry();

  mpi_init_xchange_eo_spinor();


  Vhalf = VOLUME / 2;
  VOL3 = (unsigned int)LX * LY * LZ;

  /* read the gauge field */
  if (strcmp(gaugefilename_prefix, "noread")  == 0) {
    if(g_cart_id == 0) fprintf(stdout, "# [test_overlap] NOT reading setting gauge field\n");
    g_gauge_field = NULL;
  } else {

    alloc_gauge_field(&g_gauge_field, VOLUMEPLUSRAND);
    sprintf(filename, "%s.%.4d", gaugefilename_prefix, Nconf);
    if(g_cart_id==0) fprintf(stdout, "# [test_overlap] reading gauge field from file %s\n", filename);
  
    if(strcmp(gaugefilename_prefix,"identity")==0) {
      status = unit_gauge_field(g_gauge_field, VOLUME);
    } else {
      // status = read_nersc_gauge_field_3x3(g_gauge_field, filename, &plaq);
      // status = read_ildg_nersc_gauge_field(g_gauge_field, filename);
      status = read_lime_gauge_field_doubleprec(filename);
      // status = read_nersc_gauge_field(g_gauge_field, filename, &plaq);
    }
    if(status != 0) {
      fprintf(stderr, "[test_overlap] Error, could not read gauge field\n");
      EXIT(11);
    }
    xchange_gauge();
  
    /* measure the plaquette */
    if(g_cart_id==0) fprintf(stdout, "# [test_overlap] read plaquette value 1st field: %25.16e\n", plaq);
    plaquette(&plaq);
    if(g_cart_id==0) fprintf(stdout, "# [test_overlap] measured plaquette value 1st field: %25.16e\n", plaq);
  
    if (N_hyp > 0) {
      /* smear the gauge field */
      status = hyp_smear_3d (g_gauge_field, N_hyp, alpha_hyp, 0, 0);
      if(status != 0) {
        fprintf(stderr, "[test_overlap] Error from hyp_smear_3d, status was %d\n", status);
        EXIT(7);
      }
  
      plaquette(&plaq);
      if(g_cart_id==0) fprintf(stdout, "# [test_overlap] measured plaquette value ofter hyp smearing = %25.16e\n", plaq);
   
      sprintf(filename, "%s_hyp.%.4d", gaugefilename_prefix, Nconf);
      if(g_cart_id==0) fprintf(stdout, "# [test_overlap] writing hyp-smeared gauge field to file %s\n", filename);
   
      status = write_lime_gauge_field(filename, plaq, Nconf, 64);
      if(status != 0) {
        fprintf(stderr, "[apply_lapace] Error friom write_lime_gauge_field, status was %d\n", status);
        EXIT(7);
      }
    }  /* of if N_hyp > 0 */

  }  /* end of if gaugefilename_prefix == noread */

#if 0
  /* init and allocate spinor fields */
  no_fields = 3;
  g_spinor_field = (double**)calloc(no_fields, sizeof(double*));
  for(i=0; i<no_fields; i++) alloc_spinor_field(&g_spinor_field[i], VOLUME+RAND);

  no_eo_fields = 5;
  eo_spinor_field = (double**)calloc(no_eo_fields, sizeof(double*));
  for(i=0; i<no_eo_fields; i++) alloc_spinor_field(&eo_spinor_field[i], (VOLUME+RAND)/2);
#endif
  /* init_eigensystem(&es); */

  status = alloc_eigensystem (&es, T, laphs_eigenvector_number);
  if(status != 0) {
    fprintf(stderr, "[test_overlap] Error from alloc_eigensystem, status was %d\n", status);
    EXIT(7);
  }

  ratime = _GET_TIME;
  status = read_eigensystem(&es);
  if (status != 0) {
    fprintf(stderr, "# [test_overlap] Error from read_eigensystem, status was %d\n", status);
  }
  retime = _GET_TIME;
  if(g_cart_id == 0) fprintf(stdout, "# [test_overlap] time to read eigensystem %e\n", retime-ratime);

/*
  ratime = _GET_TIME;
  status = test_eigensystem(&es, g_gauge_field);
  retime = _GET_TIME;
  if(g_cart_id == 0) fprintf(stdout, "# [test_overlap] time to test eigensystem %e\n", retime-ratime);
*/

  momentum_phase = (double*)malloc(2*VOL3 * sizeof(double));
  if(momentum_phase == NULL) {
    fprintf(stderr, "[] Error, could not allocate momentum_phase\n");
    EXIT(7);
  }
  momentum_vector[0] = 0;
  momentum_vector[1] = 0;
  momentum_vector[2] = 0;
  for(x1=0; x1<LX; x1++) {
    y1 = x1 + g_proc_coords[1] * LX;
  for(x2=0; x2<LY; x2++) {
    y2 = x2 + g_proc_coords[2] * LY;
  for(x3=0; x3<LZ; x3++) {
    y3 = x3 + g_proc_coords[3] * LZ;

    ix = g_ipt[0][x1][x2][x3];

    phase = 2 * M_PI * ( 
        momentum_vector[0] * y1 / LX_global +
        momentum_vector[1] * y2 / LY_global +
        momentum_vector[2] * y3 / LZ_global );

    momentum_phase[2*ix  ] = cos( phase );
    momentum_phase[2*ix+1] = sin( phase );
  }}}

  /* TEST */
  for(ix=0; ix<VOL3; ix++) {
    fprintf(stdout, "\tmomentum_phase[%6d] = %26.16e + I %25.16e\n", ix, momentum_phase[2*ix], momentum_phase[2*ix+1]);
  }

  tripleV = (double***)malloc(T*sizeof(double**));
  if(tripleV == NULL) {
    fprintf(stderr, "[] Error, could not allocate tripleV\n");
    EXIT(80);
  }
  tripleV[0] = (double**)malloc(T*momentum_number*sizeof(double*));
  if(tripleV[0] == NULL) {
    fprintf(stderr, "[] Error, could not allocate tripleV\n");
    EXIT(81);
  }
  for(i=1; i<T; i++) {
    tripleV[i] = tripleV[i-1] + momentum_number;
  }
  /* items = laphs_eigenvector_number * (laphs_eigenvector_number-1) * (laphs_eigenvector_number-2) / 6; */
  items = laphs_eigenvector_number * laphs_eigenvector_number * laphs_eigenvector_number;

  tripleV[0][0] = (double*)malloc(T*momentum_number*items*2*sizeof(double));
  if(tripleV[0][0] == NULL) {
    fprintf(stderr, "[] Error, could not allocate tripleV[0][0]\n");
    EXIT(82);
  }
  k1 = 0;
  for(i=0; i<T; i++) {
    for(j=0; j<momentum_number; j++) {
      if (k1 == 0) {
        k1++;
        continue;
      }
      tripleV[i][j] = tripleV[0][0] + 2 * items * k1;
      k1++;
    }
  }


  /* triple product and projection to momentum */
  for(x0 = 0; x0<T; x0++) {
    is = 0;
  /*
    for(k1 = 0; k1<laphs_eigenvector_number-2; k1++) {
      for(k2 = k1+1; k2<laphs_eigenvector_number-1; k2++) {
        for(k3 = k2+1; k3<laphs_eigenvector_number; k3++) {
   */    
    for(k1 = 0; k1<laphs_eigenvector_number; k1++) {
      for(k2 = 0; k2<laphs_eigenvector_number; k2++) {
        for(k3 = 0; k3<laphs_eigenvector_number; k3++) {
          dtmp2[0] = 0.;
          dtmp2[1] = 0.;
          for(ix=0; ix<VOL3; ix++) {
            ioffset = _GVI(ix);
            _co_eq_cv_dot_cv_cross_cv (dtmp, &(es.v[x0][k1][ioffset]), &(es.v[x0][k2][ioffset]), &(es.v[x0][k3][ioffset]) );

            dtmp2[0] += momentum_phase[2*ix  ] * dtmp[0] - momentum_phase[2*ix+1] * dtmp[1];
            dtmp2[1] += momentum_phase[2*ix  ] * dtmp[1] + momentum_phase[2*ix+1] * dtmp[0];
          }

          tripleV[x0][0][2*is  ] = dtmp2[0];
          tripleV[x0][0][2*is+1] = dtmp2[1];

          is++;

        }  /* end of loop on k3 */
      }    /* end of loop on k2 */
    }      /* end of loop on k1 */

  }  /* end of loop on x0 */

  /* TEST */
  /* write tripleV to file */
  sprintf(filename, "tripleV.%.4d.px%.2dpy%.2dpz%.2d.%.2d", Nconf, momentum_vector[0], momentum_vector[1], momentum_vector[2], g_cart_id);
  ofs = fopen(filename, "w");
  fprintf(ofs, "# [] number of eigenvectors = %d\n# [] number of timeslices = %d\n# [] momentum vector = %3d %3d %3d\n", laphs_eigenvector_number, T, momentum_vector[0], momentum_vector[1], momentum_vector[2]);
  for(x0 = 0; x0<T; x0++) {
    is = 0;
/*
    for(k1 = 0; k1<laphs_eigenvector_number-2; k1++) {
      for(k2 = k1+1; k2<laphs_eigenvector_number-1; k2++) {
        for(k3 = k2+1; k3<laphs_eigenvector_number; k3++) {
*/

    for(k1 = 0; k1<laphs_eigenvector_number; k1++) {
      for(k2 = 0; k2<laphs_eigenvector_number; k2++) {
        for(k3 = 0; k3<laphs_eigenvector_number; k3++) {

          fprintf(ofs, "%3d\t%3d%3d%3d\t%25.16e%25.16e\n", x0, k1, k2, k3, tripleV[x0][0][2*is], tripleV[x0][0][2*is+1]);
          is++; 
        }
      }
    }
  }
  fclose(ofs);


  /* free the allocated fields */
  if(momentum_phase != NULL) free(momentum_phase);
  if(tripleV        != NULL) {
    if(tripleV[0] != NULL ) {
      if(tripleV[0][0] != NULL) {
        free(tripleV[0][0]);
      }
      free(tripleV[0]);
    }
    free(tripleV);
  }


  /***********************************************
   * read eo eigenvector
   ***********************************************/
#if 0
  strcpy(filename, filename_prefix);
  if(g_cart_id == 0) fprintf(stdout, "# [test_overlap] reading C_oo_sym eigenvector from file %s\n", filename);

  status = read_lime_spinor(g_spinor_field[0], filename, 0);
  if( status != 0) {
    fprintf(stderr, "[test_overlap] Error from read_lime_spinor, status was %d\n");
    EXIT(1);
  }

  /* apply C^+ C^- */

  xchange_eo_field( eo_spinor_field[1], 1);

  C_oo(eo_spinor_field[2], eo_spinor_field[1], g_gauge_field, -g_mu, eo_spinor_field[4]);

  xchange_eo_field( eo_spinor_field[2], 1);

  C_oo(eo_spinor_field[3], eo_spinor_field[2], g_gauge_field,  g_mu, eo_spinor_field[4]);

  norm = 4 * g_kappa * g_kappa;
  for(ix=0; ix<Vhalf; ix++ ) {
    _fv_ti_eq_re(eo_spinor_field[3]+_GSI(ix), norm);
  }

  spinor_scalar_product_re(&norm,  eo_spinor_field[1], eo_spinor_field[1], Vhalf);
  spinor_scalar_product_co(&w,  eo_spinor_field[1], eo_spinor_field[3], Vhalf);
  
  if(g_cart_id == 0) {
    fprintf(stdout, "# [] eigenvalue = %16.7e %16.7e; norm = %16.7e\n", w.re / norm, w.im / norm, sqrt(norm));
  }
#endif

  /***********************************************
   * free the allocated memory, finalize 
   ***********************************************/

  fini_eigensystem (&es);


  free(g_gauge_field);
  for(i=0; i<no_fields; i++) free(g_spinor_field[i]);
  free(g_spinor_field);

  for(i=0; i<no_eo_fields; i++) free(eo_spinor_field[i]);
  free(eo_spinor_field);

  free_geometry();

#ifdef HAVE_MPI
  mpi_fini_xchange_eo_spinor();
  mpi_fini_datatypes();
#endif

  if (g_cart_id == 0) {
    g_the_time = time(NULL);
    fprintf(stdout, "# [test_overlap] %s# [test_overlap] end fo run\n", ctime(&g_the_time));
    fflush(stdout);
    fprintf(stderr, "# [test_overlap] %s# [test_overlap] end fo run\n", ctime(&g_the_time));
    fflush(stderr);
  }


#ifdef HAVE_MPI
  MPI_Finalize();
#endif

  return(0);
}
