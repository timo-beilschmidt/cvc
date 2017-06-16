/****************************************************
 * piN2piN_factorized.c
 * 
 * Tue May 30 10:40:59 CEST 2017
 *
 * PURPOSE:
 * TODO:
 * DONE:
 *
 ****************************************************/

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <complex.h>
#include <math.h>
#include <time.h>
#ifdef HAVE_MPI
#  include <mpi.h>
#endif
#ifdef HAVE_OPENMP
#include <omp.h>
#endif
#include <getopt.h>

#ifdef HAVE_LHPC_AFF
#include "lhpc-aff.h"
#endif

#ifdef __cplusplus
extern "C"
{
#endif

#  ifdef HAVE_TMLQCD_LIBWRAPPER
#    include "tmLQCD.h"
#  endif

#ifdef __cplusplus
}
#endif

#define MAIN_PROGRAM

#include "cvc_complex.h"
#include "ilinalg.h"
#include "icontract.h"
#include "global.h"
#include "cvc_geometry.h"
#include "cvc_utils.h"
#include "mpi_init.h"
#include "io.h"
#include "propagator_io.h"
#include "gauge_io.h"
#include "read_input_parser.h"
#include "smearing_techniques.h"
#include "contractions_io.h"
#include "matrix_init.h"
#include "project.h"
#include "prepare_source.h"
#include "prepare_propagator.h"
#include "contract_factorized.h"

using namespace cvc;

/************************************************************************************
 * determine all stochastic source timeslices needed; make a source timeslice list
 ************************************************************************************/
int **stochastic_source_timeslice_lookup_table;
int *stochastic_source_timeslice_list;
int stochastic_source_timeslice_number;

int get_stochastic_source_timeslices (void) {
  int tmp_list[T_global];
  int i_snk;

  for( int t = 0; t<T_global; t++) { tmp_list[t] = -1; }

  i_snk = 0;
  for( int i_src = 0; i_src<g_source_location_number; i_src++) {
    int t_base = g_source_coords_list[i_src][0];
    for( int i_coherent = 0; i_coherent < g_coherent_source_number; i_coherent++) {
      int t_coherent = ( t_base + i_coherent * ( T_global / g_coherent_source_number) ) % T_global;
      for( int t = 0; t<=g_src_snk_time_separation; t++) {
        int t_snk = ( t_coherent + t ) % T_global;
        if( tmp_list[t_snk] == -1 ) {
          tmp_list[t_snk] = i_snk;
          i_snk++;
        }
      }
    }  /* of loop on coherent source timeslices */
  }    /* of loop on base source timeslices */
  if(g_cart_id == 0) { fprintf(stdout, "# [get_stochastic_source_timeslices] number of stochastic timeslices = %2d\n", i_snk); }

  stochastic_source_timeslice_number = i_snk;
  if(stochastic_source_timeslice_number == 0) {
    fprintf(stderr, "# [get_stochastic_source_timeslices] Error, stochastic_source_timeslice_number = 0\n");
    return(4);
  }

  stochastic_source_timeslice_list = (int*)malloc(i_snk*sizeof(int));
  if(stochastic_source_timeslice_list == NULL) {
    fprintf(stderr, "[get_stochastic_source_timeslices] Error from malloc\n");
    return(1);
  }

  stochastic_source_timeslice_lookup_table = (int**)malloc( g_source_location_number * g_coherent_source_number * sizeof(int*));
  if(stochastic_source_timeslice_lookup_table == NULL) {
    fprintf(stderr, "[get_stochastic_source_timeslices] Error from malloc\n");
    return(2);
  }

  stochastic_source_timeslice_lookup_table[0] = (int*)malloc( (g_src_snk_time_separation+1) * g_source_location_number * g_coherent_source_number * sizeof(int));
  if(stochastic_source_timeslice_lookup_table[0] == NULL) {
    fprintf(stderr, "[get_stochastic_source_timeslices] Error from malloc\n");
    return(3);
  }
  for( int i_src=1; i_src<g_source_location_number*g_coherent_source_number; i_src++) {
    stochastic_source_timeslice_lookup_table[i_src] = stochastic_source_timeslice_lookup_table[i_src-1] + (g_src_snk_time_separation+1);
  }

  for( int i_src = 0; i_src<g_source_location_number; i_src++) {
    int t_base = g_source_coords_list[i_src][0];
    for( int i_coherent = 0; i_coherent < g_coherent_source_number; i_coherent++) {
      int i_prop = i_src * g_coherent_source_number + i_coherent;
      int t_coherent = ( t_base + i_coherent * ( T_global / g_coherent_source_number) ) % T_global;
      for( int t = 0; t<=g_src_snk_time_separation; t++) {
        int t_snk = ( t_coherent + t ) % T_global;
        if( tmp_list[t_snk] != -1 ) {
          stochastic_source_timeslice_list[ tmp_list[t_snk] ] = t_snk;
          stochastic_source_timeslice_lookup_table[i_prop][t] = tmp_list[t_snk];
        }
      }
    }  /* of loop on coherent source timeslices */
  }    /* of loop on base source timeslices */

  if(g_cart_id == 0) {
    /* TEST */
    for( int i_src = 0; i_src<g_source_location_number; i_src++) {
      int t_base = g_source_coords_list[i_src][0];
      for( int i_coherent = 0; i_coherent < g_coherent_source_number; i_coherent++) {
        int i_prop = i_src * g_coherent_source_number + i_coherent;
        int t_coherent = ( t_base + i_coherent * ( T_global / g_coherent_source_number) ) % T_global;

        for( int t = 0; t <= g_src_snk_time_separation; t++) {
          fprintf(stdout, "# [get_stochastic_source_timeslices] i_src = %d, i_prop = %d, t_src = %d, dt = %d, t_snk = %d, lookup table = %d\n",
              i_src, i_prop, t_coherent, t,
              stochastic_source_timeslice_list[ stochastic_source_timeslice_lookup_table[i_prop][t] ],
              stochastic_source_timeslice_lookup_table[i_prop][t]);
        }
      }
    }

    /* TEST */
    for( int t=0; t<stochastic_source_timeslice_number; t++) {
      fprintf(stdout, "# [get_stochastic_source_timeslices] stochastic source timeslice no. %d is t = %d\n", t, stochastic_source_timeslice_list[t]);
    }
  }  /* end of if g_cart_id == 0 */
  return(0);
}  /* end of get_stochastic_source_timeslices */


/***********************************************************
 * usage function
 ***********************************************************/
void usage() {
  fprintf(stdout, "Code to perform contractions for piN 2-pt. function\n");
  fprintf(stdout, "Usage:    [options]\n");
  fprintf(stdout, "Options: -f input filename [default cvc.input]\n");
  fprintf(stdout, "         -h? this help\n");
#ifdef HAVE_MPI
  MPI_Abort(MPI_COMM_WORLD, 1);
  MPI_Finalize();
#endif
  exit(0);
}
  
  
/***********************************************************
 * main program
 ***********************************************************/
int main(int argc, char **argv) {
  
  const int n_c=3;
  const int n_s=4;
  const int max_num_diagram = 6;


  int c, k;
  int filename_set = 0;
  int exitstatus;
  int op_id_up= -1, op_id_dn = -1;
  int gsx[4], sx[4];
  int source_proc_id = 0;
  int read_stochastic_source      = 0;
  int read_stochastic_propagator  = 0;
  int write_stochastic_source     = 0;
  int write_stochastic_propagator = 0;
  char filename[200];
  double ratime, retime;
  double plaq_m = 0., plaq_r = 0.;
  double *spinor_work[2];
  unsigned int VOL3;
  size_t sizeof_spinor_field = 0, sizeof_spinor_field_timeslice = 0;
  spinor_propagator_type **conn_X=NULL;
  double ****buffer=NULL;
  int io_proc = -1;
  double **propagator_list_up = NULL, **propagator_list_dn = NULL, **sequential_propagator_list = NULL, **stochastic_propagator_list = NULL,
         **stochastic_source_list = NULL;
  double *gauge_field_smeared = NULL, *tmLQCD_gauge_field = NULL;

/*******************************************************************
 * Gamma components for the piN and Delta:
 *                                                                 */
  /* vertex i2, gamma_5 only */
  const int gamma_i2_number = 1;
  int gamma_i2_list[1]      = {  5 };
  double gamma_i2_sign[1]   = { +1 };

  /* vertex f2, gamma_5 and id,  vector indices and pseudo-vector */
  const int gamma_f2_number = 1;
  int gamma_f2_list[1]      = {  5 };
  double gamma_f2_sign[1]   = { +1 };
  double gamma_f2_adjoint_sign[1]   = { +1 };
  double gamma_f2_g5_adjoint_sign[1]   = { +1 };

  /* vertex c, vector indices and pseudo-vector */
  const int gamma_c_number = 6;
  int gamma_c_list[6]       = {  1,  2,  3,  7,  8,  9 };
  double gamma_c_sign[6]    = { +1, +1, +1, +1, +1, +1 };


  /* vertex f1 for nucleon-type, C g5, C, C g0 g5, C g0 */
  const int gamma_f1_nucleon_number = 4;
  int gamma_f1_nucleon_list[4]      = { 14, 11,  8,  2 };
  double gamma_f1_nucleon_sign[4]   = { +1, +1, -1, -1 };
  double gamma_f1_nucleon_transposed_sign[4]   = { -1, -1, +1, -1 };

  /* vertex f1 for Delta-type operators, C gi, C gi g0 */
  const int gamma_f1_delta_number = 6;
  int gamma_f1_delta_list[6]      = { 9,  0,  7, 13,  4, 15 };
  double gamma_f1_delta_sign[6]   = {+1, +1, -1, -1, +1, +1 };

/*
 *******************************************************************/

#ifdef HAVE_LHPC_AFF
  struct AffWriter_s *affw = NULL;
  char * aff_status_str;
  char aff_tag[200];
#endif

#ifdef HAVE_MPI
  MPI_Init(&argc, &argv);
#endif

  while ((c = getopt(argc, argv, "rRwWh?f:")) != -1) {
    switch (c) {
    case 'f':
      strcpy(filename, optarg);
      filename_set=1;
      break;
    case 'r':
      read_stochastic_source = 1;
      fprintf(stdout, "# [piN2piN_factorized] will read stochastic source\n");
      break;
    case 'R':
      read_stochastic_propagator = 1;
      fprintf(stdout, "# [piN2piN_factorized] will read stochastic propagator\n");
      break;
    case 'w':
      write_stochastic_source = 1;
      fprintf(stdout, "# [piN2piN_factorized] will write stochastic source\n");
      break;
    case 'W':
      write_stochastic_propagator = 1;
      fprintf(stdout, "# [piN2piN_factorized] will write stochastic propagator\n");
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
  fprintf(stdout, "# reading input from file %s\n", filename);
  read_input_parser(filename);

  if(g_fermion_type == -1 ) {
    fprintf(stderr, "# [piN2piN_factorized] fermion_type must be set\n");
    exit(1);
  } else {
    fprintf(stdout, "# [piN2piN_factorized] using fermion type %d\n", g_fermion_type);
  }

#ifdef HAVE_TMLQCD_LIBWRAPPER

  fprintf(stdout, "# [piN2piN_factorized] calling tmLQCD wrapper init functions\n");

  /*********************************
   * initialize MPI parameters for cvc
   *********************************/
  /* exitstatus = tmLQCD_invert_init(argc, argv, 1, 0); */
  exitstatus = tmLQCD_invert_init(argc, argv, 1);
  if(exitstatus != 0) {
    EXIT(14);
  }
  exitstatus = tmLQCD_get_mpi_params(&g_tmLQCD_mpi);
  if(exitstatus != 0) {
    EXIT(15);
  }
  exitstatus = tmLQCD_get_lat_params(&g_tmLQCD_lat);
  if(exitstatus != 0) {
    EXIT(16);
  }
#endif

#ifdef HAVE_OPENMP
  omp_set_num_threads(g_num_threads);
#else
  fprintf(stdout, "[piN2piN_factorized] Warning, resetting global thread number to 1\n");
  g_num_threads = 1;
#endif

  /* initialize MPI parameters */
  mpi_init(argc, argv);

  /******************************************************
   *
   ******************************************************/

  if(init_geometry() != 0) {
    fprintf(stderr, "[piN2piN_factorized] Error from init_geometry\n");
    EXIT(1);
  }
  geometry();

  VOL3 = LX*LY*LZ;
  sizeof_spinor_field           = _GSI(VOLUME) * sizeof(double);
  sizeof_spinor_field_timeslice = _GSI(VOL3)   * sizeof(double);


#ifdef HAVE_MPI
  /***********************************************
   * set io process
   ***********************************************/
  if( g_proc_coords[0] == 0 && g_proc_coords[1] == 0 && g_proc_coords[2] == 0 && g_proc_coords[3] == 0) {
    io_proc = 2;
    fprintf(stdout, "# [piN2piN_factorized] proc%.4d tr%.4d is io process\n", g_cart_id, g_tr_id);
  } else {
    if( g_proc_coords[1] == 0 && g_proc_coords[2] == 0 && g_proc_coords[3] == 0) {
      io_proc = 1;
      fprintf(stdout, "# [piN2piN_factorized] proc%.4d tr%.4d is send process\n", g_cart_id, g_tr_id);
    } else {
      io_proc = 0;
    }
  }
#else
  io_proc = 2;
#endif



  /* read the gauge field */
  alloc_gauge_field(&g_gauge_field, VOLUMEPLUSRAND);
#ifndef HAVE_TMLQCD_LIBWRAPPER
  switch(g_gauge_file_format) {
    case 0:
      sprintf(filename, "%s.%.4d", gaugefilename_prefix, Nconf);
      if(g_cart_id==0) fprintf(stdout, "reading gauge field from file %s\n", filename);
      exitstatus = read_lime_gauge_field_doubleprec(filename);
      break;
    case 1:
      sprintf(filename, "%s.%.5d", gaugefilename_prefix, Nconf);
      if(g_cart_id==0) fprintf(stdout, "\n# [piN2piN_factorized] reading gauge field from file %s\n", filename);
      exitstatus = read_nersc_gauge_field(g_gauge_field, filename, &plaq_r);
      break;
  }
  if(exitstatus != 0) {
    fprintf(stderr, "[piN2piN_factorized] Error, could not read gauge field\n");
    EXIT(21);
  }
#else
  Nconf = g_tmLQCD_lat.nstore;
  if(g_cart_id== 0) fprintf(stdout, "[piN2piN_factorized] Nconf = %d\n", Nconf);

  exitstatus = tmLQCD_read_gauge(Nconf);
  if(exitstatus != 0) {
    EXIT(3);
  }

  exitstatus = tmLQCD_get_gauge_field_pointer(&tmLQCD_gauge_field);
  if(exitstatus != 0) {
    EXIT(4);
  }
  if( tmLQCD_gauge_field == NULL) {
    fprintf(stderr, "[piN2piN_factorized] Error, tmLQCD_gauge_field is NULL\n");
    EXIT(5);
  }
  memcpy( g_gauge_field, tmLQCD_gauge_field, 72*VOLUME*sizeof(double));
#endif

#ifdef HAVE_MPI
  xchange_gauge_field ( g_gauge_field );
#endif
  /* measure the plaquette */
  
  if ( ( exitstatus = plaquetteria  ( g_gauge_field ) ) != 0 ) {
    fprintf(stderr, "[piN2piN_factorized] Error from plaquetteria, status was %d\n", exitstatus);
    EXIT(2);
  }
  free ( g_gauge_field ); g_gauge_field = NULL;

  /***********************************************
   * smeared gauge field
   ***********************************************/
  if( N_Jacobi > 0 ) {

    alloc_gauge_field(&gauge_field_smeared, VOLUMEPLUSRAND);

    memcpy(gauge_field_smeared, tmLQCD_gauge_field, 72*VOLUME*sizeof(double));

    if ( N_ape > 0 ) {
      exitstatus = APE_Smearing(gauge_field_smeared, alpha_ape, N_ape);
      if(exitstatus != 0) {
        fprintf(stderr, "[piN2piN_factorized] Error from APE_Smearing, status was %d\n", exitstatus);
        EXIT(47);
      }
    }  /* end of if N_aoe > 0 */
  }  /* end of if N_Jacobi > 0 */

  /***********************************************************
   * determine the stochastic source timeslices
   ***********************************************************/
  exitstatus = get_stochastic_source_timeslices();
  if(exitstatus != 0) {
    fprintf(stderr, "[piN2piN_factorized] Error from get_stochastic_source_timeslices, status was %d\n", exitstatus);
    EXIT(19);
  }

  /***********************************************************
   * allocate work spaces with halo
   ***********************************************************/
  alloc_spinor_field(&spinor_work[0], VOLUMEPLUSRAND);
  alloc_spinor_field(&spinor_work[1], VOLUMEPLUSRAND);


  /***********************************************************
   * set operator ids depending on fermion type
   ***********************************************************/
  if(g_fermion_type == _TM_FERMION) {
    op_id_up = 0;
    op_id_dn = 1;
  } else if(g_fermion_type == _WILSON_FERMION) {
    op_id_up = 0;
    op_id_dn = 0;
  }

  /******************************************************
   ******************************************************
   **
   ** stochastic inversions
   **  
   **  dn-type inversions
   ******************************************************
   ******************************************************/

  /******************************************************
   * initialize random number generator
   ******************************************************/
  exitstatus = init_rng_stat_file (g_seed, NULL);
  if(exitstatus != 0) {
    fprintf(stderr, "[piN2piN_factorized] Error from init_rng_stat_file status was %d\n", exitstatus);
    EXIT(38);
  }



  /******************************************************
   * allocate memory for stochastic sources
   *   and propagators
   ******************************************************/
  exitstatus = init_2level_buffer ( &stochastic_propagator_list, g_nsample, _GSI(VOLUME) );
  if( exitstatus != 0 ) {
    fprintf(stderr, "[piN2piN_factorized] Error from init_2level_buffer, status was %d\n", exitstatus);
    EXIT(44);
  }

  exitstatus = init_2level_buffer ( &stochastic_source_list, g_nsample, _GSI(VOLUME) );
  if( exitstatus != 0 ) {
    fprintf(stderr, "[piN2piN_factorized] Error from init_2level_buffer, status was %d\n", exitstatus);
    EXIT(44);
  }

  /* loop on stochastic samples */
  for(int isample = 0; isample < g_nsample; isample++) {

    if ( read_stochastic_source ) {
      sprintf(filename, "%s.%.4d.%.5d", filename_prefix, Nconf, isample);
      if ( ( exitstatus = read_lime_spinor( stochastic_source_list[isample], filename, 0) ) != 0 ) {
        fprintf(stderr, "[piN2piN_factorized] Error from read_lime_spinor, status was %d\n", exitstatus);
        EXIT(2);
      }
    } else {

      /* set a stochstic volume source */
      exitstatus = prepare_volume_source(stochastic_source_list[isample], VOLUME);
      if(exitstatus != 0) {
        fprintf(stderr, "[piN2piN_factorized] Error from prepare_volume_source, status was %d\n", exitstatus);
        EXIT(39);
      }
    }  /* end of if read stochastic source */


    /******************************************************
     * dummy inversion to start the deflator
     ******************************************************/
    if ( isample == 0 ) {
      memset(spinor_work[1], 0, sizeof_spinor_field);
      exitstatus = tmLQCD_invert(spinor_work[1], stochastic_source_list[0], op_id_up, 0);
      if(exitstatus != 0) {
        fprintf(stderr, "[piN2piN_factorized] Error from tmLQCD_invert, status was %d\n", exitstatus);
        EXIT(12);
      }
    }

    if ( read_stochastic_propagator ) {
      sprintf(filename, "%s.%.4d.%.5d", filename_prefix2, Nconf, isample);
      if ( ( exitstatus = read_lime_spinor( stochastic_propagator_list[isample], filename, 0) ) != 0 ) {
        fprintf(stderr, "[piN2piN_factorized] Error from read_lime_spinor, status was %d\n", exitstatus);
        EXIT(2);
      }
    } else {

      memset( stochastic_propagator_list[isample], 0, sizeof_spinor_field);
  
      /* project to timeslices, invert */
      for( int i_src = 0; i_src < stochastic_source_timeslice_number; i_src++) {
  
        /******************************************************
         * i_src is just a counter; we take the timeslices from
         * the list stochastic_source_timeslice_list, which are
         * in some order;
         * t_src should be used to address the fields
         ******************************************************/
        int t_src = stochastic_source_timeslice_list[i_src];
        memset(spinor_work[0], 0, sizeof_spinor_field);
  
        int have_source = ( g_proc_coords[0] == t_src / T );
        if( have_source ) {
          fprintf(stdout, "# [piN2piN_factorized] proc %4d = ( %d, %d, %d, %d) has t_src = %3d \n", g_cart_id, 
              g_proc_coords[0], g_proc_coords[1], g_proc_coords[2], g_proc_coords[3], t_src);
          /* this process copies timeslice t_src%T from source */
          unsigned int shift = _GSI(g_ipt[t_src%T][0][0][0]);
          memcpy(spinor_work[0]+shift, stochastic_source_list[isample]+shift, sizeof_spinor_field_timeslice );
        }
  
        /* tm-rotate stochastic source */
        if( g_fermion_type == _TM_FERMION ) {
          spinor_field_tm_rotation ( spinor_work[0], spinor_work[0], -1, g_fermion_type, VOLUME);
        }
  
        memset(spinor_work[1], 0, sizeof_spinor_field);
        exitstatus = tmLQCD_invert(spinor_work[1], spinor_work[0], op_id_dn, 0);
        if(exitstatus != 0) {
          fprintf(stderr, "[piN2piN_factorized] Error from tmLQCD_invert, status was %d\n", exitstatus);
          EXIT(12);
        }
  
        /* tm-rotate stochastic propagator at sink */
        if( g_fermion_type == _TM_FERMION ) {
          spinor_field_tm_rotation(spinor_work[1], spinor_work[1], -1, g_fermion_type, VOLUME);
        }
  
        /* copy only source timeslice from propagator */
        if(have_source) {
          unsigned int shift = _GSI(g_ipt[t_src%T][0][0][0]);
          memcpy( stochastic_propagator_list[isample]+shift, spinor_work[1]+shift, sizeof_spinor_field_timeslice);
        }
  
      }  /* end of loop on stochastic source timeslices */
   
      /* source-smear the stochastic source */
      exitstatus = Jacobi_Smearing(gauge_field_smeared, stochastic_source_list[isample], N_Jacobi, kappa_Jacobi);
  
      /* sink-smear the stochastic propagator */
      exitstatus = Jacobi_Smearing(gauge_field_smeared, stochastic_propagator_list[isample], N_Jacobi, kappa_Jacobi);
  
      if ( write_stochastic_source ) {
        /* write to file */
        sprintf( filename, "%s.%.4d.%.5d", filename_prefix, Nconf, isample); 
        exitstatus = write_propagator( stochastic_source_list[isample], filename, 0, 64 );
        if ( exitstatus != 0 ) {
          fprintf(stderr, "[piN2piN_factorized] Error from write_propagator, status was %d\n", exitstatus);
        }
      }
      if ( write_stochastic_propagator ) {
        sprintf( filename, "%s.%.4d.%.5d", filename_prefix2, Nconf, isample); 
        exitstatus = write_propagator( stochastic_propagator_list[isample], filename, 0, 64 );
        if ( exitstatus != 0 ) {
          fprintf(stderr, "[piN2piN_factorized] Error from write_propagator, status was %d\n", exitstatus);
        }
      }

    }  /* end of if read stochastic propagator else */

  }  /* end of loop on samples */



  /***********************************************************
   * up-type, dn-type and sequential propagator
   ***********************************************************/
  no_fields = g_coherent_source_number * n_s*n_c;
  exitstatus = init_2level_buffer ( &propagator_list_up, no_fields, _GSI(VOLUME) );
  if( exitstatus != 0 ) {
    fprintf(stderr, "[piN2piN_factorized] Error from init_2level_buffer, status was %d\n", exitstatus);
    EXIT(44);
  }
  exitstatus = init_2level_buffer ( &propagator_list_dn, no_fields, _GSI(VOLUME) );
  if( exitstatus != 0 ) {
    fprintf(stderr, "[piN2piN_factorized] Error from init_2level_buffer, status was %d\n", exitstatus);
    EXIT(45);
  }

  no_fields = n_s*n_c;
  exitstatus = init_2level_buffer ( &sequential_propagator_list, no_fields, _GSI(VOLUME) );
  if( exitstatus != 0 ) {
    fprintf(stderr, "[piN2piN_factorized] Error from init_2level_buffer, status was %d\n", exitstatus);
    EXIT(45);
  }



  /* loop on source locations */
  for( int i_src = 0; i_src<g_source_location_number; i_src++) {
    int t_base = g_source_coords_list[i_src][0];
 
    if(io_proc == 2) {
      sprintf(filename, "%s.%.4d.tsrc%.2d.aff", "piN_piN", Nconf, t_base );
      fprintf(stdout, "# [piN2piN_factorized] writing data to file %s\n", filename);
      affw = aff_writer(filename);
      aff_status_str = (char*)aff_writer_errstr(affw);
      if( aff_status_str != NULL ) {
        fprintf(stderr, "[piN2piN_factorized] Error from aff_writer, status was %s\n", aff_status_str);
        EXIT(4);
      }
    }  /* end of if io_proc == 2 */

    for( int i_coherent=0; i_coherent<g_coherent_source_number; i_coherent++) {
      int t_coherent = ( t_base + ( T_global / g_coherent_source_number ) * i_coherent ) % T_global; 
 
      gsx[0] = t_coherent;
      gsx[1] = ( g_source_coords_list[i_src][1] + (LX_global/2) * i_coherent ) % LX_global;
      gsx[2] = ( g_source_coords_list[i_src][2] + (LY_global/2) * i_coherent ) % LY_global;
      gsx[3] = ( g_source_coords_list[i_src][3] + (LZ_global/2) * i_coherent ) % LZ_global;

      ratime = _GET_TIME;
      get_point_source_info (gsx, sx, &source_proc_id);

      /***********************************************************
       * up-type propagator
       ***********************************************************/
      exitstatus = point_source_propagator ( &(propagator_list_up[12*i_coherent]), gsx, op_id_up, 1, 1, gauge_field_smeared );
      if(exitstatus != 0) {
        fprintf(stderr, "[piN2piN_factorized] Error from point_source_propagator, status was %d\n", exitstatus);
        EXIT(12);
      }

      /***********************************************************
       * dn-type propagator
       ***********************************************************/
      exitstatus = point_source_propagator ( &(propagator_list_dn[12*i_coherent]), gsx, op_id_dn, 1, 1, gauge_field_smeared );
      if(exitstatus != 0) {
        fprintf(stderr, "[piN2piN_factorized] Error from point_source_propagator, status was %d\n", exitstatus);
        EXIT(12);
      }

      /***********************************************************
       * contractions with up propagator and stochastic source
       ***********************************************************/
 
      fermion_propagator_type *fp = NULL, *fp2 = NULL;
      double **v1 = NULL, **v2 = NULL, **v3 = NULL, ***vp = NULL;

      fp  = create_fp_field ( VOLUME );
      fp2 = create_fp_field ( VOLUME );

      exitstatus= init_2level_buffer ( &v3, VOLUME, 24 );
      if ( exitstatus != 0 ) {
        fprintf(stderr, "[piN2piN_factorized] Error from init_2level_buffer, status was %d\n", exitstatus);
        EXIT(47);
      }

      exitstatus= init_3level_buffer ( &vp, T, g_sink_momentum_number, 24 );
      if ( exitstatus != 0 ) {
        fprintf(stderr, "[piN2piN_factorized] Error from init_3level_buffer, status was %d\n", exitstatus);
        EXIT(47);
      }

      /* up propagator as propagator type field */
      assign_fermion_propagator_from_spinor_field ( fp,  &(propagator_list_up[i_coherent * n_s*n_c]), VOLUME);

      /* dn propagator as propagator type field */
      assign_fermion_propagator_from_spinor_field ( fp2, &(propagator_list_dn[i_coherent * n_s*n_c]), VOLUME);

      /* loop on gamma structures at vertex f2 */
      for ( int i = 0; i < gamma_f2_number; i++ ) {

        /* loop on samples */
        for ( int i_sample = 0; i_sample < g_nsample; i_sample++ ) {

          /* multiply with Dirac structure at vertex f2 */
          spinor_field_eq_gamma_ti_spinor_field (spinor_work[0], gamma_f2_list[i], stochastic_source_list[i_sample], VOLUME );
          spinor_field_ti_eq_re ( spinor_work[0], gamma_f2_adjoint_sign[i], VOLUME);

          /*****************************************************************
           * xi - gf2 - u
           *****************************************************************/
          sprintf(aff_tag, "/v3/t%.2dx%.2dy%.2dz%.2d/xi-g%.2d-u/sample%.2d", 
              gsx[0], gsx[1], gsx[2], gsx[3],
              gamma_f2_list[i], i_sample);

          exitstatus = contract_v3  ( v3, spinor_work[0], fp, VOLUME );
          if ( exitstatus != 0 ) {
            fprintf(stderr, "[piN2piN_factorized] Error from init_2level_buffer, status was %d\n", exitstatus);
            EXIT(47);
          }

          exitstatus = contract_vn_momentum_projection ( vp, v3, 12, g_sink_momentum_list, g_sink_momentum_number);
          if ( exitstatus != 0 ) {
            fprintf(stderr, "[piN2piN_factorized] Error from contract_vn_momentum_projection, status was %d\n", exitstatus);
            EXIT(48);
          }

          exitstatus = contract_vn_write_aff ( vp, 12, affw, aff_tag, g_sink_momentum_list, g_sink_momentum_number, io_proc );
          if ( exitstatus != 0 ) {
            fprintf(stderr, "[piN2piN_factorized] Error from contract_vn_write_aff, status was %d\n", exitstatus);
            EXIT(49);
          }
#if 0
          /*****************************************************************
           * xi - gf2 - d
           *****************************************************************/
          sprintf(aff_tag, "/v3/t%.2dx%.2dy%.2dz%.2d/xi-g%.2d-d/sample%.2d", 
              gsx[0], gsx[1], gsx[2], gsx[3],
              gamma_f2_list[i], i_sample);

          exitstatus = contract_v3  ( v3, stochastic_source_list[i_sample], fp2, VOLUME );
          if ( exitstatus != 0 ) {
            fprintf(stderr, "[piN2piN_factorized] Error from init_2level_buffer, status was %d\n", exitstatus);
            EXIT(47);
          }

          exitstatus = contract_vn_momentum_projection ( vp, v3, 12, g_sink_momentum_list, g_sink_momentum_number);
          if ( exitstatus != 0 ) {
            fprintf(stderr, "[piN2piN_factorized] Error from contract_vn_momentum_projection, status was %d\n", exitstatus);
            EXIT(48);
          }

          exitstatus = contract_vn_write_aff ( vp, 12, affw, aff_tag, g_sink_momentum_list, g_sink_momentum_number, io_proc );
          if ( exitstatus != 0 ) {
            fprintf(stderr, "[piN2piN_factorized] Error from contract_vn_write_aff, status was %d\n", exitstatus);
            EXIT(49);
          }
#endif  /* of if 0 */

#if 0
          /*****************************************************************
           * phi - gf2 - u
           *****************************************************************/
          /* multiply with Dirac structure at vertex f2 */
          spinor_field_eq_gamma_ti_spinor_field (spinor_work[0], gamma_f2_list[i], stochastic_propagator_list[i_sample], VOLUME );
          spinor_field_ti_eq_re ( spinor_work[0], gamma_f2_adjoint_sign[i], VOLUME);

          sprintf(aff_tag, "/v3/t%.2dx%.2dy%.2dz%.2d/phi-g%.2d-u/sample%.2d", 
              gsx[0], gsx[1], gsx[2], gsx[3],
              gamma_f2_list[i], i_sample);

          exitstatus = contract_v3  ( v3, spinor_work[0], fp, VOLUME );
          if ( exitstatus != 0 ) {
            fprintf(stderr, "[piN2piN_factorized] Error from init_2level_buffer, status was %d\n", exitstatus);
            EXIT(47);
          }

          exitstatus = contract_vn_momentum_projection ( vp, v3, 12, g_sink_momentum_list, g_sink_momentum_number);
          if ( exitstatus != 0 ) {
            fprintf(stderr, "[piN2piN_factorized] Error from contract_vn_momentum_projection, status was %d\n", exitstatus);
            EXIT(48);
          }

          exitstatus = contract_vn_write_aff ( vp, 12, affw, aff_tag, g_sink_momentum_list, g_sink_momentum_number, io_proc );
          if ( exitstatus != 0 ) {
            fprintf(stderr, "[piN2piN_factorized] Error from contract_vn_write_aff, status was %d\n", exitstatus);
            EXIT(49);
          }

#endif  /* of if 0 */

#if 0
          /*****************************************************************
           * phi - gf2 - d
           *****************************************************************/
          sprintf(aff_tag, "/v3/t%.2dx%.2dy%.2dz%.2d/phi-g%.2d-d/sample%.2d", 
              gsx[0], gsx[1], gsx[2], gsx[3],
              gamma_f2_list[i], i_sample);

          exitstatus = contract_v3  ( v3, stochastic_propagator_list[i_sample], fp2, VOLUME );
          if ( exitstatus != 0 ) {
            fprintf(stderr, "[piN2piN_factorized] Error from init_2level_buffer, status was %d\n", exitstatus);
            EXIT(47);
          }

          exitstatus = contract_vn_momentum_projection ( vp, v3, 12, g_sink_momentum_list, g_sink_momentum_number);
          if ( exitstatus != 0 ) {
            fprintf(stderr, "[piN2piN_factorized] Error from contract_vn_momentum_projection, status was %d\n", exitstatus);
            EXIT(48);
          }

          exitstatus = contract_vn_write_aff ( vp, 12, affw, aff_tag, g_sink_momentum_list, g_sink_momentum_number, io_proc );
          if ( exitstatus != 0 ) {
            fprintf(stderr, "[piN2piN_factorized] Error from contract_vn_write_aff, status was %d\n", exitstatus);
            EXIT(49);
          }

#endif  /* of if 0 */

        }   /* end of loop on samples */
      }  /* end of loop on gf2 */

      fini_2level_buffer ( &v3 );
      fini_3level_buffer ( &vp );

      /*****************************************************************/
      /*****************************************************************/

      exitstatus= init_2level_buffer ( &v1, VOLUME, 72 );
      if ( exitstatus != 0 ) {
        fprintf(stderr, "[piN2piN_factorized] Error from init_2level_buffer, status was %d\n", exitstatus);
        EXIT(47);
      }

      exitstatus= init_2level_buffer ( &v2, VOLUME, 384 );
      if ( exitstatus != 0 ) {
        fprintf(stderr, "[piN2piN_factorized] Error from init_2level_buffer, status was %d\n", exitstatus);
        EXIT(47);
      }

      exitstatus= init_3level_buffer ( &vp, T, g_sink_momentum_number, 384 );
      if ( exitstatus != 0 ) {
        fprintf(stderr, "[piN2piN_factorized] Error from init_3level_buffer, status was %d\n", exitstatus);
        EXIT(47);
      }

      for ( int i = 0; i < gamma_f1_nucleon_number; i++ ) {

        for ( int i_sample = 0; i_sample < g_nsample; i_sample++ ) {

#if 0
          /* multiply with Dirac structure at vertex f2 */
          spinor_field_eq_gamma_ti_spinor_field (spinor_work[0], gamma_f1_nucleon_list[i], stochastic_source_list[i_sample], VOLUME );
          spinor_field_ti_eq_re ( spinor_work[0], gamma_f1_nucleon_sign[i], VOLUME);

          /*****************************************************************
           * xi - gf1 - u
           *****************************************************************/
          exitstatus = contract_v1 ( v1, spinor_work[0], fp, VOLUME  );
          if ( exitstatus != 0 ) {
            fprintf(stderr, "[piN2piN_factorized] Error from contract_v1, status was %d\n", exitstatus);
            EXIT(47);
          }
#endif  /* of if 0 */
#if 0
          /*****************************************************************
           * xi - gf1 - u - u
           *****************************************************************/
          sprintf(aff_tag, "/v2/t%.2dx%.2dy%.2dz%.2d/xi-g%.2d-u-u/sample%.2d", gsx[0], gsx[1], gsx[2], gsx[3], gamma_f1_nucleon_list[i], i_sample);

          exitstatus = contract_v2_from_v1 ( v2, v1, fp, VOLUME );
          if ( exitstatus != 0 ) {
            fprintf(stderr, "[piN2piN_factorized] Error from contract_v2_from_v1, status was %d\n", exitstatus);
            EXIT(47);
          }

          exitstatus = contract_vn_momentum_projection ( vp, v2, 192, g_sink_momentum_list, g_sink_momentum_number);
          if ( exitstatus != 0 ) {
            fprintf(stderr, "[piN2piN_factorized] Error from contract_vn_momentum_projection, status was %d\n", exitstatus);
            EXIT(48);
          }

          exitstatus = contract_vn_write_aff ( vp, 192, affw, aff_tag, g_sink_momentum_list, g_sink_momentum_number, io_proc );
          if ( exitstatus != 0 ) {
            fprintf(stderr, "[piN2piN_factorized] Error from contract_vn_write_aff, status was %d\n", exitstatus);
            EXIT(49);
          }

          /*****************************************************************
           * xi - gf1 - u - d
           *****************************************************************/
          sprintf(aff_tag, "/v2/t%.2dx%.2dy%.2dz%.2d/xi-g%.2d-u-d/sample%.2d", gsx[0], gsx[1], gsx[2], gsx[3], gamma_f1_nucleon_list[i], i_sample);

          exitstatus = contract_v2_from_v1 ( v2, v1, fp2, VOLUME );
          if ( exitstatus != 0 ) {
            fprintf(stderr, "[piN2piN_factorized] Error from contract_v2_from_v1, status was %d\n", exitstatus);
            EXIT(47);
          }

          exitstatus = contract_vn_momentum_projection ( vp, v2, 192, g_sink_momentum_list, g_sink_momentum_number);
          if ( exitstatus != 0 ) {
            fprintf(stderr, "[piN2piN_factorized] Error from contract_vn_momentum_projection, status was %d\n", exitstatus);
            EXIT(48);
          }

          exitstatus = contract_vn_write_aff ( vp, 192, affw, aff_tag, g_sink_momentum_list, g_sink_momentum_number, io_proc );
          if ( exitstatus != 0 ) {
            fprintf(stderr, "[piN2piN_factorized] Error from contract_vn_write_aff, status was %d\n", exitstatus);
            EXIT(49);
          }

          /*****************************************************************/
          /*****************************************************************/

          /*****************************************************************
           * xi - gf1 - d
           *****************************************************************/
          exitstatus = contract_v1 ( v1, spinor_work[0], fp2, VOLUME  );
          if ( exitstatus != 0 ) {
            fprintf(stderr, "[piN2piN_factorized] Error from contract_v1, status was %d\n", exitstatus);
            EXIT(47);
          }

          /*****************************************************************
           * xi - gf1 - d - u
           *****************************************************************/
          sprintf(aff_tag, "/v2/t%.2dx%.2dy%.2dz%.2d/xi-g%.2d-d-u/sample%.2d", gsx[0], gsx[1], gsx[2], gsx[3], gamma_f1_nucleon_list[i], i_sample);

          exitstatus = contract_v2_from_v1 ( v2, v1, fp, VOLUME );
          if ( exitstatus != 0 ) {
            fprintf(stderr, "[piN2piN_factorized] Error from contract_v2_from_v1, status was %d\n", exitstatus);
            EXIT(47);
          }

          exitstatus = contract_vn_momentum_projection ( vp, v2, 192, g_sink_momentum_list, g_sink_momentum_number);
          if ( exitstatus != 0 ) {
            fprintf(stderr, "[piN2piN_factorized] Error from contract_vn_momentum_projection, status was %d\n", exitstatus);
            EXIT(48);
          }

          exitstatus = contract_vn_write_aff ( vp, 192, affw, aff_tag, g_sink_momentum_list, g_sink_momentum_number, io_proc );
          if ( exitstatus != 0 ) {
            fprintf(stderr, "[piN2piN_factorized] Error from contract_vn_write_aff, status was %d\n", exitstatus);
            EXIT(49);
          }

          /*****************************************************************
           * xi - gf1 - d - d
           *****************************************************************/
          sprintf(aff_tag, "/v2/t%.2dx%.2dy%.2dz%.2d/xi-g%.2d-d-d/sample%.2d", gsx[0], gsx[1], gsx[2], gsx[3], gamma_f1_nucleon_list[i], i_sample);

          exitstatus = contract_v2_from_v1 ( v2, v1, fp2, VOLUME );
          if ( exitstatus != 0 ) {
            fprintf(stderr, "[piN2piN_factorized] Error from contract_v2_from_v1, status was %d\n", exitstatus);
            EXIT(47);
          }

          exitstatus = contract_vn_momentum_projection ( vp, v2, 192, g_sink_momentum_list, g_sink_momentum_number);
          if ( exitstatus != 0 ) {
            fprintf(stderr, "[piN2piN_factorized] Error from contract_vn_momentum_projection, status was %d\n", exitstatus);
            EXIT(48);
          }

          exitstatus = contract_vn_write_aff ( vp, 192, affw, aff_tag, g_sink_momentum_list, g_sink_momentum_number, io_proc );
          if ( exitstatus != 0 ) {
            fprintf(stderr, "[piN2piN_factorized] Error from contract_vn_write_aff, status was %d\n", exitstatus);
            EXIT(49);
          }
#endif  /* of if 0 */

          /*****************************************************************/
          /*****************************************************************/

          /* multiply with Dirac structure at vertex f1 */
          spinor_field_eq_gamma_ti_spinor_field (spinor_work[0], gamma_f1_nucleon_list[i], stochastic_propagator_list[i_sample], VOLUME );
          spinor_field_ti_eq_re ( spinor_work[0], gamma_f1_nucleon_sign[i], VOLUME);


          /*****************************************************************
           * phi - gf1 - u
           *****************************************************************/
          exitstatus = contract_v1 ( v1, spinor_work[0], fp, VOLUME  );
          if ( exitstatus != 0 ) {
            fprintf(stderr, "[piN2piN_factorized] Error from contract_v1, status was %d\n", exitstatus);
            EXIT(47);
          }

          /*****************************************************************
           * phi - gf1 - u - u
           *****************************************************************/
          sprintf(aff_tag, "/v2/t%.2dx%.2dy%.2dz%.2d/phi-g%.2d-u-u/sample%.2d", gsx[0], gsx[1], gsx[2], gsx[3], gamma_f1_nucleon_list[i], i_sample);

          exitstatus = contract_v2_from_v1 ( v2, v1, fp, VOLUME );
          if ( exitstatus != 0 ) {
            fprintf(stderr, "[piN2piN_factorized] Error from contract_v2_from_v1, status was %d\n", exitstatus);
            EXIT(47);
          }

          exitstatus = contract_vn_momentum_projection ( vp, v2, 192, g_sink_momentum_list, g_sink_momentum_number);
          if ( exitstatus != 0 ) {
            fprintf(stderr, "[piN2piN_factorized] Error from contract_vn_momentum_projection, status was %d\n", exitstatus);
            EXIT(48);
          }

          exitstatus = contract_vn_write_aff ( vp, 192, affw, aff_tag, g_sink_momentum_list, g_sink_momentum_number, io_proc );
          if ( exitstatus != 0 ) {
            fprintf(stderr, "[piN2piN_factorized] Error from contract_vn_write_aff, status was %d\n", exitstatus);
            EXIT(49);
          }
#if 0
          /*****************************************************************
           * phi - gf1 - u - d
           *****************************************************************/
          sprintf(aff_tag, "/v2/t%.2dx%.2dy%.2dz%.2d/phi-g%.2d-u-d/sample%.2d", gsx[0], gsx[1], gsx[2], gsx[3], gamma_f1_nucleon_list[i], i_sample);

          exitstatus = contract_v2_from_v1 ( v2, v1, fp2, VOLUME );
          if ( exitstatus != 0 ) {
            fprintf(stderr, "[piN2piN_factorized] Error from contract_v2_from_v1, status was %d\n", exitstatus);
            EXIT(47);
          }

          exitstatus = contract_vn_momentum_projection ( vp, v2, 192, g_sink_momentum_list, g_sink_momentum_number);
          if ( exitstatus != 0 ) {
            fprintf(stderr, "[piN2piN_factorized] Error from contract_vn_momentum_projection, status was %d\n", exitstatus);
            EXIT(48);
          }

          exitstatus = contract_vn_write_aff ( vp, 192, affw, aff_tag, g_sink_momentum_list, g_sink_momentum_number, io_proc );
          if ( exitstatus != 0 ) {
            fprintf(stderr, "[piN2piN_factorized] Error from contract_vn_write_aff, status was %d\n", exitstatus);
            EXIT(49);
          }
#endif  /* of if 0 */

          /*****************************************************************/
          /*****************************************************************/
#if 0
          /*****************************************************************
           * phi - gf1 - d
           *****************************************************************/
          exitstatus = contract_v1 ( v1, spinor_work[0], fp2, VOLUME  );
          if ( exitstatus != 0 ) {
            fprintf(stderr, "[piN2piN_factorized] Error from contract_v1, status was %d\n", exitstatus);
            EXIT(47);
          }
#endif  /* of if 0 */

#if 0
          /*****************************************************************
           * phi - gf1 - d - u
           *****************************************************************/
          sprintf(aff_tag, "/v2/t%.2dx%.2dy%.2dz%.2d/phi-g%.2d-d-u/sample%.2d", gsx[0], gsx[1], gsx[2], gsx[3], gamma_f1_nucleon_list[i], i_sample);

          exitstatus = contract_v2_from_v1 ( v2, v1, fp, VOLUME );
          if ( exitstatus != 0 ) {
            fprintf(stderr, "[piN2piN_factorized] Error from contract_v2_from_v1, status was %d\n", exitstatus);
            EXIT(47);
          }

          exitstatus = contract_vn_momentum_projection ( vp, v2, 192, g_sink_momentum_list, g_sink_momentum_number);
          if ( exitstatus != 0 ) {
            fprintf(stderr, "[piN2piN_factorized] Error from contract_vn_momentum_projection, status was %d\n", exitstatus);
            EXIT(48);
          }

          exitstatus = contract_vn_write_aff ( vp, 192, affw, aff_tag, g_sink_momentum_list, g_sink_momentum_number, io_proc );
          if ( exitstatus != 0 ) {
            fprintf(stderr, "[piN2piN_factorized] Error from contract_vn_write_aff, status was %d\n", exitstatus);
            EXIT(49);
          }
#endif  /* of if 0 */

#if 0
          /*****************************************************************
           * phi - gf1 - d - d
           *****************************************************************/
          sprintf(aff_tag, "/v2/t%.2dx%.2dy%.2dz%.2d/phi-g%.2d-d-d/sample%.2d", gsx[0], gsx[1], gsx[2], gsx[3], gamma_f1_nucleon_list[i], i_sample);

          exitstatus = contract_v2_from_v1 ( v2, v1, fp2, VOLUME );
          if ( exitstatus != 0 ) {
            fprintf(stderr, "[piN2piN_factorized] Error from contract_v2_from_v1, status was %d\n", exitstatus);
            EXIT(47);
          }

          exitstatus = contract_vn_momentum_projection ( vp, v2, 192, g_sink_momentum_list, g_sink_momentum_number);
          if ( exitstatus != 0 ) {
            fprintf(stderr, "[piN2piN_factorized] Error from contract_vn_momentum_projection, status was %d\n", exitstatus);
            EXIT(48);
          }

          exitstatus = contract_vn_write_aff ( vp, 192, affw, aff_tag, g_sink_momentum_list, g_sink_momentum_number, io_proc );
          if ( exitstatus != 0 ) {
            fprintf(stderr, "[piN2piN_factorized] Error from contract_vn_write_aff, status was %d\n", exitstatus);
            EXIT(49);
          }
#endif  /* of if 0 */

        }  /* end of loop on samples */
      }  /* end of loop on gf1  */

      fini_2level_buffer ( &v1 );
      fini_2level_buffer ( &v2 );
      fini_3level_buffer ( &vp );
      free_fp_field ( &fp  );
      free_fp_field ( &fp2 );

    } /* end of loop on coherent source timeslices */



    /***********************************************************
     * sequential propagator
     ***********************************************************/

    /* loop on sequential source momenta */
    for( int iseq_mom=0; iseq_mom < g_seq_source_momentum_number; iseq_mom++) {

      /***********************************************************
       * sequential propagator U^{-1} g5 exp(ip) D^{-1}: tfii
       ***********************************************************/
      if(g_cart_id == 0) fprintf(stdout, "# [piN2piN_factorized] sequential inversion fpr pi2 = (%d, %d, %d)\n", 
      g_seq_source_momentum_list[iseq_mom][0], g_seq_source_momentum_list[iseq_mom][1], g_seq_source_momentum_list[iseq_mom][2]);

      double **prop_list = (double**)malloc(g_coherent_source_number * sizeof(double*));
      if(prop_list == NULL) {
        fprintf(stderr, "[piN2piN_factorized] Error from malloc\n");
        EXIT(43);
      }

      ratime = _GET_TIME;
      for( int is=0;is<n_s*n_c;is++) {

        /* extract spin-color source-component is from coherent source dn propagators */
        for( int i=0; i<g_coherent_source_number; i++) {
          if(g_cart_id == 0) fprintf(stdout, "# [piN2piN_factorized] using dn prop id %d / %d\n", (i_src * g_coherent_source_number + i), (i_src * g_coherent_source_number + i)*n_s*n_c + is);
          prop_list[i] = propagator_list_dn[i * n_s*n_c + is];
        }

        /* build sequential source */
        exitstatus = init_coherent_sequential_source(spinor_work[0], prop_list, g_source_coords_list[i_src][0], g_coherent_source_number, g_seq_source_momentum_list[iseq_mom], 5);
        if(exitstatus != 0) {
          fprintf(stderr, "[piN2piN_factorized] Error from init_coherent_sequential_source, status was %d\n", exitstatus);
          EXIT(14);
        }

        /* source-smear the coherent source */
        exitstatus = Jacobi_Smearing(gauge_field_smeared, spinor_work[0], N_Jacobi, kappa_Jacobi);

        /* tm-rotate sequential source */
        if( g_fermion_type == _TM_FERMION ) {
          spinor_field_tm_rotation(spinor_work[0], spinor_work[0], +1, g_fermion_type, VOLUME);
        }

        memset(spinor_work[1], 0, sizeof_spinor_field);
        /* invert */
        exitstatus = tmLQCD_invert(spinor_work[1], spinor_work[0], op_id_up, 0);
        if(exitstatus != 0) {
          fprintf(stderr, "[piN2piN_factorized] Error from tmLQCD_invert, status was %d\n", exitstatus);
          EXIT(12);
        }

        /* tm-rotate at sink */
        if( g_fermion_type == _TM_FERMION ) {
          spinor_field_tm_rotation(spinor_work[1], spinor_work[1], +1, g_fermion_type, VOLUME);
        }

        /* sink-smear the coherent-source propagator */
        exitstatus = Jacobi_Smearing(gauge_field_smeared, spinor_work[1], N_Jacobi, kappa_Jacobi);

        memcpy( sequential_propagator_list[is], spinor_work[1], sizeof_spinor_field);

      }  /* end of loop on spin-color component */
      retime = _GET_TIME;
      if(g_cart_id == 0) fprintf(stdout, "# [piN2piN_factorized] time for seq propagator = %e seconds\n", retime-ratime);

      free(prop_list);
 
      /***********************************************/
      /***********************************************/


      /***********************************************
       * contractions involving sequential propagator
       ***********************************************/
      double **v1 = NULL, **v2 = NULL, **v3 = NULL, ***vp = NULL;
      fermion_propagator_type *fp = NULL, *fp2 = NULL, *fp3 = NULL;

      exitstatus= init_2level_buffer ( &v3, VOLUME, 24 );
      if ( exitstatus != 0 ) {
        fprintf(stderr, "[piN2piN_factorized] Error from init_2level_buffer, status was %d\n", exitstatus);
        EXIT(47);
      }

      exitstatus= init_3level_buffer ( &vp, T, g_sink_momentum_number, 24 );
      if ( exitstatus != 0 ) {
        fprintf(stderr, "[piN2piN_factorized] Error from init_3level_buffer, status was %d\n", exitstatus);
        EXIT(47);
      }

      fp = create_fp_field ( VOLUME );
      assign_fermion_propagator_from_spinor_field ( fp, sequential_propagator_list, VOLUME);

      for ( int i = 0; i < gamma_f2_number; i++ ) {

        for ( int i_sample = 0; i_sample < g_nsample; i_sample++ ) {


          /*****************************************************************
           * xi - gf2 - ud
           *****************************************************************/
          /* multiply with Dirac structure at vertex f2 */
          spinor_field_eq_gamma_ti_spinor_field (spinor_work[0], gamma_f2_list[i], stochastic_source_list[i_sample], VOLUME );
          spinor_field_ti_eq_re ( spinor_work[0], gamma_f2_adjoint_sign[i], VOLUME);

          sprintf(aff_tag, "/v3/t%.2dx%.2dy%.2dz%.2d/pi2x%.2dpi2y%.2dpi2z%.2d/xi-g%.2d-ud/sample%.2d", 
              g_source_coords_list[i_src][0],
              g_source_coords_list[i_src][1],
              g_source_coords_list[i_src][2],
              g_source_coords_list[i_src][3],
              g_seq_source_momentum_list[iseq_mom][0], g_seq_source_momentum_list[iseq_mom][1], g_seq_source_momentum_list[iseq_mom][2],
              gamma_f2_list[i], i_sample);

          exitstatus = contract_v3  ( v3, spinor_work[0], fp, VOLUME );
          if ( exitstatus != 0 ) {
            fprintf(stderr, "[piN2piN_factorized] Error from contract_v3, status was %d\n", exitstatus);
            EXIT(47);
          }

          exitstatus = contract_vn_momentum_projection ( vp, v3, 12, g_sink_momentum_list, g_sink_momentum_number);
          if ( exitstatus != 0 ) {
            fprintf(stderr, "[piN2piN_factorized] Error from contract_vn_momentum_projection, status was %d\n", exitstatus);
            EXIT(48);
          }

          exitstatus = contract_vn_write_aff ( vp, 12, affw, aff_tag, g_sink_momentum_list, g_sink_momentum_number, io_proc );
          if ( exitstatus != 0 ) {
            fprintf(stderr, "[piN2piN_factorized] Error from contract_vn_write_aff, status was %d\n", exitstatus);
            EXIT(49);
          }
#if 0
          /*****************************************************************
           * phi - gf2 - ud
           *****************************************************************/
          /* multiply with Dirac structure at vertex f2 */
          spinor_field_eq_gamma_ti_spinor_field (spinor_work[0], gamma_f2_list[i], stochastic_propagator_list[i_sample], VOLUME );
          spinor_field_ti_eq_re ( spinor_work[0], gamma_f2_adjoint_sign[i], VOLUME);

          sprintf(aff_tag, "/v3/t%.2dx%.2dy%.2dz%.2d/pi2x%.2dpi2y%.2dpi2z%.2d/phi-g%.2d-ud/sample%.2d", 
              g_source_coords_list[i_src][0],
              g_source_coords_list[i_src][1],
              g_source_coords_list[i_src][2],
              g_source_coords_list[i_src][3],
              g_seq_source_momentum_list[iseq_mom][0], g_seq_source_momentum_list[iseq_mom][1], g_seq_source_momentum_list[iseq_mom][2],
              gamma_f2_list[i], i_sample);

          exitstatus = contract_v3  ( v3, spinor_work[0], fp, VOLUME );
          if ( exitstatus != 0 ) {
            fprintf(stderr, "[piN2piN_factorized] Error from contract_v3, status was %d\n", exitstatus);
            EXIT(47);
          }

          exitstatus = contract_vn_momentum_projection ( vp, v3, 12, g_sink_momentum_list, g_sink_momentum_number);
          if ( exitstatus != 0 ) {
            fprintf(stderr, "[piN2piN_factorized] Error from contract_vn_momentum_projection, status was %d\n", exitstatus);
            EXIT(48);
          }

          exitstatus = contract_vn_write_aff ( vp, 12, affw, aff_tag, g_sink_momentum_list, g_sink_momentum_number, io_proc );
          if ( exitstatus != 0 ) {
            fprintf(stderr, "[piN2piN_factorized] Error from contract_vn_write_aff, status was %d\n", exitstatus);
            EXIT(49);
          }
#endif

        }  /* end of loop on samples */
      }  /* end of loop on gf2  */

      fini_2level_buffer ( &v3 );
      fini_3level_buffer ( &vp );

      exitstatus= init_2level_buffer ( &v1, VOLUME, 72 );
      if ( exitstatus != 0 ) {
        fprintf(stderr, "[piN2piN_factorized] Error from init_2level_buffer, status was %d\n", exitstatus);
        EXIT(47);
      }

      exitstatus= init_2level_buffer ( &v2, VOLUME, 384 );
      if ( exitstatus != 0 ) {
        fprintf(stderr, "[piN2piN_factorized] Error from init_2level_buffer, status was %d\n", exitstatus);
        EXIT(47);
      }

      exitstatus= init_3level_buffer ( &vp, T, g_sink_momentum_number, 384 );
      if ( exitstatus != 0 ) {
        fprintf(stderr, "[piN2piN_factorized] Error from init_3level_buffer, status was %d\n", exitstatus);
        EXIT(47);
      }

      fp2 = create_fp_field ( VOLUME );
      fp3 = create_fp_field ( VOLUME );

      for( int i_coherent=0; i_coherent<g_coherent_source_number; i_coherent++) {
        int t_coherent = ( t_base + ( T_global / g_coherent_source_number ) * i_coherent ) % T_global;

        gsx[0] = t_coherent;
        gsx[1] = ( g_source_coords_list[i_src][1] + (LX_global/2) * i_coherent ) % LX_global;
        gsx[2] = ( g_source_coords_list[i_src][2] + (LY_global/2) * i_coherent ) % LY_global;
        gsx[3] = ( g_source_coords_list[i_src][3] + (LZ_global/2) * i_coherent ) % LZ_global;

        get_point_source_info (gsx, sx, &source_proc_id);

        assign_fermion_propagator_from_spinor_field ( fp2,  &(propagator_list_up[i_coherent * n_s*n_c]), VOLUME);
        assign_fermion_propagator_from_spinor_field ( fp3,  &(propagator_list_dn[i_coherent * n_s*n_c]), VOLUME);

        for ( int i = 0; i < gamma_f1_nucleon_number; i++ ) {
          for ( int i_sample = 0; i_sample < g_nsample; i_sample++ ) {
  
#if 0
            /* multiply with Dirac structure at vertex f1 */
            spinor_field_eq_gamma_ti_spinor_field (spinor_work[0], gamma_f1_nucleon_list[i], stochastic_source_list[i_sample], VOLUME );
            spinor_field_ti_eq_re ( spinor_work[0], gamma_f1_nucleon_sign[i], VOLUME);


            /*****************************************************************
             * xi - gf1 - ud 
             *****************************************************************/
            exitstatus = contract_v1 ( v1, spinor_work[0], fp, VOLUME  );
            if ( exitstatus != 0 ) {
              fprintf(stderr, "[piN2piN_factorized] Error from contract_v1, status was %d\n", exitstatus);
              EXIT(47);
            }
  
            /*****************************************************************
             * (1) xi - gf1 - ud - u
             *****************************************************************/
            sprintf(aff_tag, "/v2/t%.2dx%.2dy%.2dz%.2d/pi2x%.2dpi2y%.2dpi2z%.2d/xi-g%.2d-ud-u/sample%.2d", gsx[0], gsx[1], gsx[2], gsx[3], 
                g_seq_source_momentum_list[iseq_mom][0], g_seq_source_momentum_list[iseq_mom][1], g_seq_source_momentum_list[iseq_mom][2],
                gamma_f1_nucleon_list[i], i_sample);
  
            exitstatus = contract_v2_from_v1 ( v2, v1, fp2, VOLUME );
            if ( exitstatus != 0 ) {
              fprintf(stderr, "[piN2piN_factorized] Error from contract_v2_from_v1, status was %d\n", exitstatus);
              EXIT(47);
            }
  
            exitstatus = contract_vn_momentum_projection ( vp, v2, 192, g_sink_momentum_list, g_sink_momentum_number);
            if ( exitstatus != 0 ) {
              fprintf(stderr, "[piN2piN_factorized] Error from contract_vn_momentum_projection, status was %d\n", exitstatus);
              EXIT(48);
            }
  
            exitstatus = contract_vn_write_aff ( vp, 192, affw, aff_tag, g_sink_momentum_list, g_sink_momentum_number, io_proc );
            if ( exitstatus != 0 ) {
              fprintf(stderr, "[piN2piN_factorized] Error from contract_vn_write_aff, status was %d\n", exitstatus);
              EXIT(49);
            }

            /*****************************************************************
             * (2) xi - gf1 - ud - d
             *****************************************************************/
            sprintf(aff_tag, "/v2/t%.2dx%.2dy%.2dz%.2d/pi2x%.2dpi2y%.2dpi2z%.2d/xi-g%.2d-ud-d/sample%.2d", gsx[0], gsx[1], gsx[2], gsx[3],
                g_seq_source_momentum_list[iseq_mom][0], g_seq_source_momentum_list[iseq_mom][1], g_seq_source_momentum_list[iseq_mom][2],
                gamma_f1_nucleon_list[i], i_sample);
  
            exitstatus = contract_v2_from_v1 ( v2, v1, fp3, VOLUME );
            if ( exitstatus != 0 ) {
              fprintf(stderr, "[piN2piN_factorized] Error from contract_v2_from_v1, status was %d\n", exitstatus);
              EXIT(47);
            }
  
            exitstatus = contract_vn_momentum_projection ( vp, v2, 192, g_sink_momentum_list, g_sink_momentum_number);
            if ( exitstatus != 0 ) {
              fprintf(stderr, "[piN2piN_factorized] Error from contract_vn_momentum_projection, status was %d\n", exitstatus);
              EXIT(48);
            }
  
            exitstatus = contract_vn_write_aff ( vp, 192, affw, aff_tag, g_sink_momentum_list, g_sink_momentum_number, io_proc );
            if ( exitstatus != 0 ) {
              fprintf(stderr, "[piN2piN_factorized] Error from contract_vn_write_aff, status was %d\n", exitstatus);
              EXIT(49);
            }

            /*****************************************************************/
            /*****************************************************************/

            /*****************************************************************
             * xi - gf1 - u
             *****************************************************************/
            exitstatus = contract_v1 ( v1, spinor_work[0], fp2, VOLUME  );
            if ( exitstatus != 0 ) {
              fprintf(stderr, "[piN2piN_factorized] Error from contract_v1, status was %d\n", exitstatus);
              EXIT(47);
            }
  
            /*****************************************************************
             * (3) xi - gf1 - u - ud
             *****************************************************************/
            sprintf(aff_tag, "/v2/t%.2dx%.2dy%.2dz%.2d/pi2x%.2dpi2y%.2dpi2z%.2d/xi-g%.2d-u-ud/sample%.2d", gsx[0], gsx[1], gsx[2], gsx[3],
                g_seq_source_momentum_list[iseq_mom][0], g_seq_source_momentum_list[iseq_mom][1], g_seq_source_momentum_list[iseq_mom][2],
                gamma_f1_nucleon_list[i], i_sample);
  
            exitstatus = contract_v2_from_v1 ( v2, v1, fp, VOLUME );
            if ( exitstatus != 0 ) {
              fprintf(stderr, "[piN2piN_factorized] Error from contract_v2_from_v1, status was %d\n", exitstatus);
              EXIT(47);
            }
  
            exitstatus = contract_vn_momentum_projection ( vp, v2, 192, g_sink_momentum_list, g_sink_momentum_number);
            if ( exitstatus != 0 ) {
              fprintf(stderr, "[piN2piN_factorized] Error from contract_vn_momentum_projection, status was %d\n", exitstatus);
              EXIT(48);
            }
  
            exitstatus = contract_vn_write_aff ( vp, 192, affw, aff_tag, g_sink_momentum_list, g_sink_momentum_number, io_proc );
            if ( exitstatus != 0 ) {
              fprintf(stderr, "[piN2piN_factorized] Error from contract_vn_write_aff, status was %d\n", exitstatus);
              EXIT(49);
            }

            /*****************************************************************/
            /*****************************************************************/
  
            /*****************************************************************
             * xi - gf1 - d
             *****************************************************************/
            exitstatus = contract_v1 ( v1, spinor_work[0], fp3, VOLUME  );
            if ( exitstatus != 0 ) {
              fprintf(stderr, "[piN2piN_factorized] Error from contract_v1, status was %d\n", exitstatus);
              EXIT(47);
            }
  
            /*****************************************************************
             * (4) xi - gf1 - d - ud
             *****************************************************************/
            sprintf(aff_tag, "/v2/t%.2dx%.2dy%.2dz%.2d/pi2x%.2dpi2y%.2dpi2z%.2d/xi-g%.2d-d-ud/sample%.2d", gsx[0], gsx[1], gsx[2], gsx[3],
                g_seq_source_momentum_list[iseq_mom][0], g_seq_source_momentum_list[iseq_mom][1], g_seq_source_momentum_list[iseq_mom][2],
                gamma_f1_nucleon_list[i], i_sample);
  
            exitstatus = contract_v2_from_v1 ( v2, v1, fp, VOLUME );
            if ( exitstatus != 0 ) {
              fprintf(stderr, "[piN2piN_factorized] Error from contract_v2_from_v1, status was %d\n", exitstatus);
              EXIT(47);
            }
  
            exitstatus = contract_vn_momentum_projection ( vp, v2, 192, g_sink_momentum_list, g_sink_momentum_number);
            if ( exitstatus != 0 ) {
              fprintf(stderr, "[piN2piN_factorized] Error from contract_vn_momentum_projection, status was %d\n", exitstatus);
              EXIT(48);
            }
  
            exitstatus = contract_vn_write_aff ( vp, 192, affw, aff_tag, g_sink_momentum_list, g_sink_momentum_number, io_proc );
            if ( exitstatus != 0 ) {
              fprintf(stderr, "[piN2piN_factorized] Error from contract_vn_write_aff, status was %d\n", exitstatus);
              EXIT(49);
            }
#endif  /* of if 0 */

            /*****************************************************************/
            /*****************************************************************/

            /* multiply with Dirac structure at vertex f1 */
            spinor_field_eq_gamma_ti_spinor_field (spinor_work[0], gamma_f1_nucleon_list[i], stochastic_propagator_list[i_sample], VOLUME );
            spinor_field_ti_eq_re ( spinor_work[0], gamma_f1_nucleon_sign[i], VOLUME);
  
            /*****************************************************************
             * phi - gf1 - ud
             *****************************************************************/
            exitstatus = contract_v1 ( v1, spinor_work[0], fp, VOLUME  );
            if ( exitstatus != 0 ) {
              fprintf(stderr, "[piN2piN_factorized] Error from contract_v1, status was %d\n", exitstatus);
              EXIT(47);
            }
  
            /*****************************************************************
             * (1) phi - gf1 - ud - u
             *****************************************************************/
            sprintf(aff_tag, "/v2/t%.2dx%.2dy%.2dz%.2d/pi2x%.2dpi2y%.2dpi2z%.2d/phi-g%.2d-ud-u/sample%.2d", gsx[0], gsx[1], gsx[2], gsx[3],
                g_seq_source_momentum_list[iseq_mom][0], g_seq_source_momentum_list[iseq_mom][1], g_seq_source_momentum_list[iseq_mom][2],
                gamma_f1_nucleon_list[i], i_sample);
  
            exitstatus = contract_v2_from_v1 ( v2, v1, fp2, VOLUME );
            if ( exitstatus != 0 ) {
              fprintf(stderr, "[piN2piN_factorized] Error from contract_v2_from_v1, status was %d\n", exitstatus);
              EXIT(47);
            }
  
            exitstatus = contract_vn_momentum_projection ( vp, v2, 192, g_sink_momentum_list, g_sink_momentum_number);
            if ( exitstatus != 0 ) {
              fprintf(stderr, "[piN2piN_factorized] Error from contract_vn_momentum_projection, status was %d\n", exitstatus);
              EXIT(48);
            }
  
            exitstatus = contract_vn_write_aff ( vp, 192, affw, aff_tag, g_sink_momentum_list, g_sink_momentum_number, io_proc );
            if ( exitstatus != 0 ) {
              fprintf(stderr, "[piN2piN_factorized] Error from contract_vn_write_aff, status was %d\n", exitstatus);
              EXIT(49);
            }

#if 0
            /*****************************************************************
             * (2 )phi - gf1 - ud - d
             *****************************************************************/
            sprintf(aff_tag, "/v2/t%.2dx%.2dy%.2dz%.2d/pi2x%.2dpi2y%.2dpi2z%.2d/phi-g%.2d-ud-d/sample%.2d", gsx[0], gsx[1], gsx[2], gsx[3],
                g_seq_source_momentum_list[iseq_mom][0], g_seq_source_momentum_list[iseq_mom][1], g_seq_source_momentum_list[iseq_mom][2],
                gamma_f1_nucleon_list[i], i_sample);
  
            exitstatus = contract_v2_from_v1 ( v2, v1, fp3, VOLUME );
            if ( exitstatus != 0 ) {
              fprintf(stderr, "[piN2piN_factorized] Error from contract_v2_from_v1, status was %d\n", exitstatus);
              EXIT(47);
            }
  
            exitstatus = contract_vn_momentum_projection ( vp, v2, 192, g_sink_momentum_list, g_sink_momentum_number);
            if ( exitstatus != 0 ) {
              fprintf(stderr, "[piN2piN_factorized] Error from contract_vn_momentum_projection, status was %d\n", exitstatus);
              EXIT(48);
            }
  
            exitstatus = contract_vn_write_aff ( vp, 192, affw, aff_tag, g_sink_momentum_list, g_sink_momentum_number, io_proc );
            if ( exitstatus != 0 ) {
              fprintf(stderr, "[piN2piN_factorized] Error from contract_vn_write_aff, status was %d\n", exitstatus);
              EXIT(49);
            }
  
#endif  /* end of if 0  */

            /*****************************************************************/
            /*****************************************************************/
  
            /*****************************************************************
             * phi - gf1 - u
             *****************************************************************/
            exitstatus = contract_v1 ( v1, spinor_work[0], fp2, VOLUME  );
            if ( exitstatus != 0 ) {
              fprintf(stderr, "[piN2piN_factorized] Error from contract_v1, status was %d\n", exitstatus);
              EXIT(47);
            }
  
            /*****************************************************************
             * (3) phi - gf1 - u - ud
             *****************************************************************/
            sprintf(aff_tag, "/v2/t%.2dx%.2dy%.2dz%.2d/pi2x%.2dpi2y%.2dpi2z%.2d/phi-g%.2d-u-ud/sample%.2d", gsx[0], gsx[1], gsx[2], gsx[3],
                g_seq_source_momentum_list[iseq_mom][0], g_seq_source_momentum_list[iseq_mom][1], g_seq_source_momentum_list[iseq_mom][2],
                gamma_f1_nucleon_list[i], i_sample);
  
            exitstatus = contract_v2_from_v1 ( v2, v1, fp, VOLUME );
            if ( exitstatus != 0 ) {
              fprintf(stderr, "[piN2piN_factorized] Error from contract_v2_from_v1, status was %d\n", exitstatus);
              EXIT(47);
            }
  
            exitstatus = contract_vn_momentum_projection ( vp, v2, 192, g_sink_momentum_list, g_sink_momentum_number);
            if ( exitstatus != 0 ) {
              fprintf(stderr, "[piN2piN_factorized] Error from contract_vn_momentum_projection, status was %d\n", exitstatus);
              EXIT(48);
            }
  
            exitstatus = contract_vn_write_aff ( vp, 192, affw, aff_tag, g_sink_momentum_list, g_sink_momentum_number, io_proc );
            if ( exitstatus != 0 ) {
              fprintf(stderr, "[piN2piN_factorized] Error from contract_vn_write_aff, status was %d\n", exitstatus);
              EXIT(49);
            }
  
            /*****************************************************************/
            /*****************************************************************/
  
#if 0
            /*****************************************************************
             * phi - gf1 - d
             *****************************************************************/
            exitstatus = contract_v1 ( v1, spinor_work[0], fp3, VOLUME  );
            if ( exitstatus != 0 ) {
              fprintf(stderr, "[piN2piN_factorized] Error from contract_v1, status was %d\n", exitstatus);
              EXIT(47);
            }
  
            /*****************************************************************
             * (4) phi - gf1 - d - ud
             *****************************************************************/
            sprintf(aff_tag, "/v2/t%.2dx%.2dy%.2dz%.2d/pi2x%.2dpi2y%.2dpi2z%.2d/phi-g%.2d-d-ud/sample%.2d", gsx[0], gsx[1], gsx[2], gsx[3],
                g_seq_source_momentum_list[iseq_mom][0], g_seq_source_momentum_list[iseq_mom][1], g_seq_source_momentum_list[iseq_mom][2],
                gamma_f1_nucleon_list[i], i_sample);
  
            exitstatus = contract_v2_from_v1 ( v2, v1, fp, VOLUME );
            if ( exitstatus != 0 ) {
              fprintf(stderr, "[piN2piN_factorized] Error from contract_v2_from_v1, status was %d\n", exitstatus);
              EXIT(47);
            }
  
            exitstatus = contract_vn_momentum_projection ( vp, v2, 192, g_sink_momentum_list, g_sink_momentum_number);
            if ( exitstatus != 0 ) {
              fprintf(stderr, "[piN2piN_factorized] Error from contract_vn_momentum_projection, status was %d\n", exitstatus);
              EXIT(48);
            }
  
            exitstatus = contract_vn_write_aff ( vp, 192, affw, aff_tag, g_sink_momentum_list, g_sink_momentum_number, io_proc );
            if ( exitstatus != 0 ) {
              fprintf(stderr, "[piN2piN_factorized] Error from contract_vn_write_aff, status was %d\n", exitstatus);
              EXIT(49);
            }
#endif  /* end of if 0  */

          }  /* end of loop on samples */
        }  /* end of loop on gf1  */
      }  /* end of loop on coherent source timeslices */

      fini_2level_buffer ( &v3 );
      fini_3level_buffer ( &vp );
      free_fp_field ( &fp  );
      free_fp_field ( &fp2 );
      free_fp_field ( &fp3 );

    }  /* end of loop on sequential momentum list */
#if 0  
#endif  /* of if 0 */

#ifdef HAVE_LHPC_AFF
    if(io_proc == 2) {
      aff_status_str = (char*)aff_writer_close (affw);
      if( aff_status_str != NULL ) {
        fprintf(stderr, "[piN2piN_factorized] Error from aff_writer_close, status was %s\n", aff_status_str);
        EXIT(111);
      }
    }  /* end of if io_proc == 2 */
#endif  /* of ifdef HAVE_LHPC_AFF */
  }  /* end of loop on base source locations */

  fini_2level_buffer ( &sequential_propagator_list );
  fini_2level_buffer ( &stochastic_propagator_list );
  fini_2level_buffer ( &stochastic_source_list );
  fini_2level_buffer ( &propagator_list_up );
  fini_2level_buffer ( &propagator_list_dn );



  /***********************************************
   ***********************************************
   **
   ** stochastic contractions using the 
   **   one-end-trick
   **
   ***********************************************
   ***********************************************/
  exitstatus = init_2level_buffer ( &stochastic_propagator_list, 4, _GSI(VOLUME) );
  if ( exitstatus != 0 ) {
    fprintf(stderr, "[piN2piN_factorized] Error from init_2level_buffer, status was %d\n", exitstatus);
    EXIT(48);
  }

  exitstatus = init_2level_buffer ( &stochastic_source_list, 4, _GSI(VOLUME) );
  if ( exitstatus != 0 ) {
    fprintf(stderr, "[piN2piN_factorized] Error from init_2level_buffer, status was %d\n", exitstatus);
    EXIT(48);
  }

  exitstatus = init_2level_buffer ( &propagator_list_up, 12, _GSI(VOLUME) );
  if ( exitstatus != 0 ) {
    fprintf(stderr, "[piN2piN_factorized] Error from init_2level_buffer, status was %d\n", exitstatus);
    EXIT(48);
  }

  exitstatus = init_2level_buffer ( &propagator_list_dn, 12, _GSI(VOLUME) );
  if ( exitstatus != 0 ) {
    fprintf(stderr, "[piN2piN_factorized] Error from init_2level_buffer, status was %d\n", exitstatus);
    EXIT(48);
  }


  /* loop on base source locations */
  for( int i_src=0; i_src < g_source_location_number; i_src++) {
    int t_base = g_source_coords_list[i_src][0];

#ifdef HAVE_LHPC_AFF
    /***********************************************
     * open aff output file
     ***********************************************/
    if(io_proc == 2) {
      sprintf(filename, "%s.%.4d.tsrc%.2d.aff", "piN_piN_oet", Nconf, t_base );
      fprintf(stdout, "# [piN2piN_factorized] writing data to file %s\n", filename);
      affw = aff_writer(filename);
      aff_status_str = (char*)aff_writer_errstr(affw);
      if( aff_status_str != NULL ) {
        fprintf(stderr, "[piN2piN_factorized] Error from aff_writer, status was %s\n", aff_status_str);
        EXIT(4);
      }
    }  /* end of if io_proc == 2 */
#endif

    /* loop on coherent source locations */
    for(int i_coherent = 0; i_coherent < g_coherent_source_number; i_coherent++) {

      int t_coherent = ( t_base + ( T_global / g_coherent_source_number ) * i_coherent ) % T_global;
      gsx[0] = t_coherent;
      gsx[1] = ( g_source_coords_list[i_src][1] + (LX_global/2) * i_coherent ) % LX_global;
      gsx[2] = ( g_source_coords_list[i_src][2] + (LY_global/2) * i_coherent ) % LY_global;
      gsx[3] = ( g_source_coords_list[i_src][3] + (LZ_global/2) * i_coherent ) % LZ_global;

      exitstatus = point_source_propagator ( propagator_list_up, gsx, op_id_up, 1, 1, gauge_field_smeared );
      if(exitstatus != 0) {
        fprintf(stderr, "[piN2piN_factorized] Error from point_source_propagator, status was %d %s %d\n", exitstatus, __FILE__, __LINE__);
        EXIT(12);
      }

      exitstatus = point_source_propagator ( propagator_list_dn, gsx, op_id_dn, 1, 1, gauge_field_smeared );
      if(exitstatus != 0) {
        fprintf(stderr, "[piN2piN_factorized] Error from point_source_propagator, status was %d %s %d\n", exitstatus, __FILE__, __LINE__);
        EXIT(12);
      }

      fermion_propagator_type *fp = NULL, *fp2 = NULL, *fp3 = NULL;
      fp  = create_fp_field ( VOLUME );
      fp2 = create_fp_field ( VOLUME );
      fp3 = create_fp_field ( VOLUME );
      assign_fermion_propagator_from_spinor_field ( fp,  propagator_list_up, VOLUME);
      assign_fermion_propagator_from_spinor_field ( fp2, propagator_list_dn, VOLUME);


      /* loop on oet samples */
      for( int isample=0; isample < g_nsample_oet; isample++) {

        if ( read_stochastic_source ) {
          for ( int ispin = 0; ispin < 4; ispin++ ) {
            sprintf(filename, "%s-oet.%.4d.t%.2d.%.2d.%.5d", filename_prefix, Nconf, gsx[0], ispin, isample);
            if ( ( exitstatus = read_lime_spinor( stochastic_source_list[ispin], filename, 0) ) != 0 ) {
              fprintf(stderr, "[piN2piN_factorized] Error from read_lime_spinor, status was %d\n", exitstatus);
              EXIT(2);
            }
          }
          /* recover the random field */
          if( (exitstatus = init_timeslice_source_oet(stochastic_source_list, gsx[0], NULL, -1 ) ) != 0 ) {
            fprintf(stderr, "[piN2piN_factorized] Error from init_timeslice_source_oet, status was %d %s %d\n", exitstatus, __FILE__, __LINE__);
            EXIT(64);
          }
        } else {
          /* dummy call to initialize the ran field, we do not use the resulting stochastic_source_list */
          if( (exitstatus = init_timeslice_source_oet(stochastic_source_list, gsx[0], NULL, 1 ) ) != 0 ) {
            fprintf(stderr, "[piN2piN_factorized] Error from init_timeslice_source_oet, status was %d %s %d\n", exitstatus, __FILE__, __LINE__);
            EXIT(64);
          }
          if ( write_stochastic_source ) {
            for ( int ispin = 0; ispin < 4; ispin++ ) {
              sprintf(filename, "%s-oet.%.4d.t%.2d.%.2d.%.5d", filename_prefix, Nconf, gsx[0], ispin, isample);
              if ( ( exitstatus = write_propagator( stochastic_source_list[ispin], filename, 0, 64) ) != 0 ) {
                fprintf(stderr, "[piN2piN_factorized] Error from write_propagator, status was %d\n", exitstatus);
                EXIT(2);
              }
            }
          }
        }  /* end of if read stochastic source - else */


        /* loop on sequential source momenta p_i2 */
        for( int iseq_mom=0; iseq_mom < g_seq_source_momentum_number; iseq_mom++) {

          int seq_source_momentum[3] = { -g_seq_source_momentum_list[iseq_mom][0], -g_seq_source_momentum_list[iseq_mom][1], -g_seq_source_momentum_list[iseq_mom][2] };

          if( (exitstatus = init_timeslice_source_oet(stochastic_source_list, gsx[0], seq_source_momentum, 0 ) ) != 0 ) {
            fprintf(stderr, "[piN2piN_factorized] Error from init_timeslice_source_oet, status was %d %s %d\n", exitstatus, __FILE__, __LINE__);
            EXIT(64);
          }

          /*****************************************************************
           * invert for stochastic timeslice propagator
           *****************************************************************/
          for( int i = 0; i < 4; i++) {
            memcpy(spinor_work[0], stochastic_source_list[i], sizeof_spinor_field);

            /* source-smearing stochastic momentum source */
            exitstatus = Jacobi_Smearing(gauge_field_smeared, spinor_work[0], N_Jacobi, kappa_Jacobi);

            /* tm-rotate stochastic source */
            if( g_fermion_type == _TM_FERMION ) {
              spinor_field_tm_rotation ( spinor_work[0], spinor_work[0], +1, g_fermion_type, VOLUME);
            }

            memset(spinor_work[1], 0, sizeof_spinor_field);
            exitstatus = tmLQCD_invert(spinor_work[1], spinor_work[0], op_id_up, 0);
            if(exitstatus != 0) {
              fprintf(stderr, "[piN2piN_factorized] Error from tmLQCD_invert, status was %d\n", exitstatus);
              EXIT(44);
            }

            /* tm-rotate stochastic propagator at sink */
            if( g_fermion_type == _TM_FERMION ) {
              spinor_field_tm_rotation(spinor_work[1], spinor_work[1], +1, g_fermion_type, VOLUME);
            }

            /* sink smearing stochastic propagator */
            exitstatus = Jacobi_Smearing(gauge_field_smeared, spinor_work[1], N_Jacobi, kappa_Jacobi);

            memcpy( stochastic_propagator_list[i], spinor_work[1], sizeof_spinor_field);
          }

          /*****************************************************************
           * calculate V3
           *
           * phi^+ g5 Gamma_f2 ( pf2 ) U ( z_1xi )
           * phi^+ g5 Gamma_f2 ( pf2 ) D
           *****************************************************************/
#if 0
          if ( g_seq_source_momentum_list[iseq_mom][0] == 0 &&
               g_seq_source_momentum_list[iseq_mom][1] == 0 &&
               g_seq_source_momentum_list[iseq_mom][2] == 0     ) {
#endif  /* of if 0 */

            double **v3 = NULL, ***vp = NULL;

            exitstatus= init_2level_buffer ( &v3, VOLUME, 24 );
            if ( exitstatus != 0 ) {
              fprintf(stderr, "[piN2piN_factorized] Error from init_2level_buffer, status was %d\n", exitstatus);
              EXIT(47);
            }

            exitstatus= init_3level_buffer ( &vp, T, g_sink_momentum_number, 24 );
            if ( exitstatus != 0 ) {
              fprintf(stderr, "[piN2piN_factorized] Error from init_3level_buffer, status was %d\n", exitstatus);
              EXIT(47);
            }

            for ( int if2 = 0; if2 < gamma_f2_number; if2++ ) {

              for( int ispin = 0; ispin < 4; ispin++ ) {

                /*****************************************************************
                 * (1) phi - gf2 - u
                 *****************************************************************/

                /* multiply with Dirac structure at vertex f2 */
                spinor_field_eq_gamma_ti_spinor_field (spinor_work[0], gamma_f2_list[if2], stochastic_propagator_list[ispin], VOLUME );
                g5_phi( spinor_work[0], VOLUME );
                spinor_field_ti_eq_re ( spinor_work[0], gamma_f2_g5_adjoint_sign[if2], VOLUME);

                sprintf(aff_tag, "/v3-oet/t%.2dx%.2dy%.2dz%.2d/pi2x%.2dpi2y%.2dpi2z%.2d/phi-g%.2d-u/sample%.2d/d%d", gsx[0], gsx[1], gsx[2], gsx[3],
                    g_seq_source_momentum_list[iseq_mom][0], g_seq_source_momentum_list[iseq_mom][1], g_seq_source_momentum_list[iseq_mom][2],
                    gamma_f2_list[if2], isample, ispin);

                exitstatus = contract_v3 ( v3, spinor_work[0], fp, VOLUME );
                if ( exitstatus != 0 ) {
                  fprintf(stderr, "[piN2piN_factorized] Error from contract_v3, status was %d\n", exitstatus);
                  EXIT(47);
                }
 
                exitstatus = contract_vn_momentum_projection ( vp, v3, 12, g_sink_momentum_list, g_sink_momentum_number);
                if ( exitstatus != 0 ) {
                  fprintf(stderr, "[piN2piN_factorized] Error from contract_vn_momentum_projection, status was %d\n", exitstatus);
                  EXIT(48);
                }

                exitstatus = contract_vn_write_aff ( vp, 12, affw, aff_tag, g_sink_momentum_list, g_sink_momentum_number, io_proc );
                if ( exitstatus != 0 ) {
                  fprintf(stderr, "[piN2piN_factorized] Error from contract_vn_write_aff, status was %d\n", exitstatus);
                  EXIT(49);
                }

#if 0
                /*****************************************************************
                 * (2) phi - gf2 - d
                 *****************************************************************/
                sprintf(aff_tag, "/v3-oet/t%.2dx%.2dy%.2dz%.2d/pi2x%.2dpi2y%.2dpi2z%.2d/phi-g%.2d-d/sample%.2d/d%d", gsx[0], gsx[1], gsx[2], gsx[3],
                    g_seq_source_momentum_list[iseq_mom][0], g_seq_source_momentum_list[iseq_mom][1], g_seq_source_momentum_list[iseq_mom][2],
                    gamma_f2_list[if2], isample, ispin);

                exitstatus = contract_v3 ( v3, spinor_work[0], fp2, VOLUME );
                if ( exitstatus != 0 ) {
                  fprintf(stderr, "[piN2piN_factorized] Error from contract_v3, status was %d\n", exitstatus);
                  EXIT(47);
                }
 
                exitstatus = contract_vn_momentum_projection ( vp, v3, 12, g_sink_momentum_list, g_sink_momentum_number);
                if ( exitstatus != 0 ) {
                  fprintf(stderr, "[piN2piN_factorized] Error from contract_vn_momentum_projection, status was %d\n", exitstatus);
                  EXIT(48);
                }

                exitstatus = contract_vn_write_aff ( vp, 12, affw, aff_tag, g_sink_momentum_list, g_sink_momentum_number, io_proc );
                if ( exitstatus != 0 ) {
                  fprintf(stderr, "[piN2piN_factorized] Error from contract_vn_write_aff, status was %d\n", exitstatus);
                  EXIT(49);
                }
#endif  /* of if 0 */
              }  /* end of loop on spin components ispin */

            }  /* end of loop on gamma_f2_list */

            fini_2level_buffer ( &v3 );
            fini_3level_buffer ( &vp );
#if 0
          }  /* end of if momentum p_i2 = 0 */
#endif  /* of if 0 */

          /*****************************************************************
           * calculate V2 and V4
           *
           * z_1phi = V4
           * z_3phi = V2
           *****************************************************************/

          if ( g_seq_source_momentum_list[iseq_mom][0] == 0 &&
               g_seq_source_momentum_list[iseq_mom][1] == 0 &&
               g_seq_source_momentum_list[iseq_mom][2] == 0     ) {

            double **v1 = NULL, **v2 = NULL, ***vp = NULL;

            exitstatus= init_2level_buffer ( &v1, VOLUME, 72 );
            if ( exitstatus != 0 ) {
              fprintf(stderr, "[piN2piN_factorized] Error from init_2level_buffer, status was %d\n", exitstatus);
              EXIT(47);
            }

            exitstatus= init_2level_buffer ( &v2, VOLUME, 384 );
            if ( exitstatus != 0 ) {
              fprintf(stderr, "[piN2piN_factorized] Error from init_2level_buffer, status was %d\n", exitstatus);
              EXIT(47);
            }

            exitstatus= init_3level_buffer ( &vp, T, g_sink_momentum_number, 384 );
            if ( exitstatus != 0 ) {
              fprintf(stderr, "[piN2piN_factorized] Error from init_3level_buffer, status was %d\n", exitstatus);
              EXIT(47);
            }


            for( int if1 = 0; if1 < gamma_f1_nucleon_number; if1++ ) {

              for( int ispin = 0; ispin < 4; ispin++ ) {
                /* multiply with Dirac structure at vertex f2 */
                spinor_field_eq_gamma_ti_spinor_field (spinor_work[0], gamma_f1_nucleon_list[if1], stochastic_propagator_list[ispin], VOLUME );
                spinor_field_ti_eq_re ( spinor_work[0], gamma_f1_nucleon_transposed_sign[if1], VOLUME);

                /*****************************************************************/
                /*****************************************************************/

                /*****************************************************************
                 * phi - gf1 - d
                 *****************************************************************/
                exitstatus = contract_v1 ( v1, spinor_work[0], fp2, VOLUME  );
                if ( exitstatus != 0 ) {
                  fprintf(stderr, "[piN2piN_factorized] Error from contract_v1, status was %d\n", exitstatus);
                  EXIT(47);
                }
  
                /*****************************************************************
                 * (3) phi - gf1 - d - u
                 *****************************************************************/
                sprintf(aff_tag, "/v2-oet/t%.2dx%.2dy%.2dz%.2d/pi2x%.2dpi2y%.2dpi2z%.2d/phi-g%.2d-d-u/sample%.2d/d%d", gsx[0], gsx[1], gsx[2], gsx[3],
                    g_seq_source_momentum_list[iseq_mom][0], g_seq_source_momentum_list[iseq_mom][1], g_seq_source_momentum_list[iseq_mom][2],
                    gamma_f1_nucleon_list[if1], isample, ispin);

                exitstatus = contract_v2_from_v1 ( v2, v1, fp, VOLUME );
                if ( exitstatus != 0 ) {
                  fprintf(stderr, "[piN2piN_factorized] Error from contract_v4, status was %d\n", exitstatus);
                  EXIT(47);
                }

                exitstatus = contract_vn_momentum_projection ( vp, v2, 192, g_sink_momentum_list, g_sink_momentum_number);
                if ( exitstatus != 0 ) {
                  fprintf(stderr, "[piN2piN_factorized] Error from contract_vn_momentum_projection, status was %d\n", exitstatus);
                  EXIT(48);
                }

                exitstatus = contract_vn_write_aff ( vp, 192, affw, aff_tag, g_sink_momentum_list, g_sink_momentum_number, io_proc );
                if ( exitstatus != 0 ) {
                  fprintf(stderr, "[piN2piN_factorized] Error from contract_vn_write_aff, status was %d\n", exitstatus);
                  EXIT(49);
                }

              }  /* end of loop on ispin */

              /*****************************************************************/
              /*****************************************************************/

              fermion_propagator_field_eq_gamma_ti_fermion_propagator_field (fp3, gamma_f1_nucleon_list[if1], fp2, VOLUME );
              fermion_propagator_field_eq_fermion_propagator_field_ti_re ( fp3, fp3, gamma_f1_nucleon_sign[if1], VOLUME );


              for( int ispin = 0; ispin < 4; ispin++ ) {
                /*****************************************************************
                 * (4) phi - gf1 - d - u
                 *****************************************************************/
                sprintf(aff_tag, "/v4-oet/t%.2dx%.2dy%.2dz%.2d/pi2x%.2dpi2y%.2dpi2z%.2d/phi-g%.2d-d-u/sample%.2d/d%d", gsx[0], gsx[1], gsx[2], gsx[3],
                    g_seq_source_momentum_list[iseq_mom][0], g_seq_source_momentum_list[iseq_mom][1], g_seq_source_momentum_list[iseq_mom][2],
                    gamma_f1_nucleon_list[if1], isample, ispin);

                exitstatus = contract_v4 ( v2, stochastic_propagator_list[ispin], fp3, fp, VOLUME );
                if ( exitstatus != 0 ) {
                  fprintf(stderr, "[piN2piN_factorized] Error from contract_v4, status was %d\n", exitstatus);
                  EXIT(47);
                }

                exitstatus = contract_vn_momentum_projection ( vp, v2, 192, g_sink_momentum_list, g_sink_momentum_number);
                if ( exitstatus != 0 ) {
                  fprintf(stderr, "[piN2piN_factorized] Error from contract_vn_momentum_projection, status was %d\n", exitstatus);
                  EXIT(48);
                }

                exitstatus = contract_vn_write_aff ( vp, 192, affw, aff_tag, g_sink_momentum_list, g_sink_momentum_number, io_proc );
                if ( exitstatus != 0 ) {
                  fprintf(stderr, "[piN2piN_factorized] Error from contract_vn_write_aff, status was %d\n", exitstatus);
                  EXIT(49);
                }

              }  /* end of loop on ispin */
            }  /* end of loop on gamma at f1 */


            fini_2level_buffer ( &v1 );
            fini_2level_buffer ( &v2 );
            fini_3level_buffer ( &vp );

          }  /* of if pi2 == 0 */

        }  /* end of loop on sequential source momenta pi2 */

      } /* end of loop on oet samples */

      free_fp_field( &fp  );
      free_fp_field( &fp2 );
      free_fp_field( &fp3 );

    }  /* end of loop on coherent sources */

#ifdef HAVE_LHPC_AFF
    if(io_proc == 2) {
      aff_status_str = (char*)aff_writer_close (affw);
      if( aff_status_str != NULL ) {
        fprintf(stderr, "[piN2piN_factorized] Error from aff_writer_close, status was %s\n", aff_status_str);
        EXIT(111);
      }
    }  /* end of if io_proc == 2 */
#endif  /* of ifdef HAVE_LHPC_AFF */
  } /* end of loop on base sources */ 


  fini_2level_buffer ( &stochastic_propagator_list );
  fini_2level_buffer ( &stochastic_source_list );
  fini_2level_buffer ( &propagator_list_up );
  fini_2level_buffer ( &propagator_list_dn );
#if 0
#endif  /* of if 0 */

  /***********************************************
   * free gauge fields and spinor fields
   ***********************************************/
#ifndef HAVE_TMLQCD_LIBWRAPPER
  if(g_gauge_field != NULL) free(g_gauge_field);
#endif

  /***********************************************
   * free the allocated memory, finalize
   ***********************************************/
  free_geometry();

  if( gauge_field_smeared != NULL ) free(gauge_field_smeared);

#ifdef HAVE_TMLQCD_LIBWRAPPER
  tmLQCD_finalise();
#endif

#ifdef HAVE_MPI
  MPI_Finalize();
#endif
  if(g_cart_id == 0) {
    g_the_time = time(NULL);
    fprintf(stdout, "# [piN2piN_factorized] %s# [piN2piN_factorized] end fo run\n", ctime(&g_the_time));
    fflush(stdout);
    fprintf(stderr, "# [piN2piN_factorized] %s# [piN2piN_factorized] end fo run\n", ctime(&g_the_time));
    fflush(stderr);
  }

  return(0);
}
