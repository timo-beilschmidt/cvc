/****************************************************
 * loops_em.cpp
 *
 * Thu Sep 28 15:27:11 CEST 2017
 *
 * - originally copied from loops_caa_lma.cpp
 *
 * PURPOSE:
 * DONE:
 * TODO:
 ****************************************************/

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <time.h>
#ifdef HAVE_MPI
#  include <mpi.h>
#endif
#ifdef HAVE_OPENMP
#  include <omp.h>
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
#include "cvc_linalg.h"
#include "global.h"
#include "cvc_geometry.h"
#include "cvc_utils.h"
#include "mpi_init.h"
#include "set_default.h"
#include "io.h"
#include "propagator_io.h"
#include "read_input_parser.h"
#include "contractions_io.h"
#include "Q_clover_phi.h"
#include "contract_cvc_tensor.h"
#include "prepare_source.h"
#include "prepare_propagator.h"
#include "project.h"
#include "matrix_init.h"
#include "clover.h"
#include "scalar_products.h"
#include "fft.h"
#include "Q_phi.h"

#define _OP_ID_UP 0
#define _OP_ID_DN 1

#define _SQR(_a) ((_a)*(_a))

using namespace cvc;

void usage() {
  fprintf(stdout, "Code to perform loop contractions\n");
  fprintf(stdout, "Usage:    [options]\n");
  fprintf(stdout, "Options:  -f input <filename> : input filename for cvc    [default cvc.input]\n");
  fprintf(stdout, "          -w                  : check position space WI   [default false]\n");
  fprintf(stdout, "          -c                  : check propagator residual [default false]\n");
  EXIT(0);
}

/******************************************************/
/******************************************************/

int dummy_eo_solver (double * const propagator, double * const source, const int op_id) {
  memcpy(propagator, source, _GSI(VOLUME)/2*sizeof(double) );
  return(0);
}

/******************************************************/
/******************************************************/

int main(int argc, char **argv) {
  
  int c;
  int filename_set = 0;
  int check_position_space_WI=0;
  int check_momentum_space_WI=0;
  int exitstatus;
  int io_proc = -1;
  int evecs_num = 0;
  int check_propagator_residual = 1;
  int transverse_projection     = 0;
  int check_eigenvectors        = 1;
  int evecs_projection          = 1;
  unsigned int Vhalf;
  size_t sizeof_eo_spinor_field;
  double **eo_stochastic_source = NULL, **eo_stochastic_propagator = NULL;
  double **eo_spinor_field=NULL, **eo_spinor_work=NULL, *eo_evecs_block=NULL;
  double **eo_evecs_field=NULL;
  double *evecs_eval = NULL, *evecs_lambdainv=NULL, *evecs_4kappasqr_lambdainv = NULL;
  char filename[100], contype[600];
  /* double ratime, retime; */
  double **mzz[2], **mzzinv[2];
  double *gauge_field_with_phase = NULL;

  double _Complex ztmp;

#ifdef HAVE_MPI
  MPI_Status mstatus;
#endif


#ifdef HAVE_LHPC_AFF
  struct AffWriter_s *affw = NULL;
  char * aff_status_str;
  char aff_tag[400];
  struct AffNode_s *affn = NULL, *affdir=NULL;
#endif

#ifdef HAVE_MPI
  MPI_Init(&argc, &argv);
#endif

  while ((c = getopt(argc, argv, "ecwWh?f:p:v:")) != -1) {
    switch (c) {
    case 'f':
      strcpy(filename, optarg);
      filename_set=1;
      break;
    case 'w':
      check_position_space_WI = 1;
      break;
    case 'W':
      check_momentum_space_WI = 1;
      break;
    case 'c':
      check_propagator_residual = 1;
      break;
    case 'e':
      check_eigenvectors = 1;
      break;
    case 'p':
      transverse_projection = atoi( optarg );
      break;
    case 'v':
      evecs_projection = atoi( optarg );
      break;
    case 'h':
    case '?':
    default:
      usage();
      break;
    }
  }

  g_the_time = time(NULL);

  /* set the default values */
  if(filename_set==0) strcpy(filename, "cvc.input");
  /* fprintf(stdout, "# [loops_em] Reading input from file %s\n", filename); */
  read_input_parser(filename);

#ifdef HAVE_TMLQCD_LIBWRAPPER

  fprintf(stdout, "# [loops_em] calling tmLQCD wrapper init functions\n");

  /*********************************
   * initialize MPI parameters for cvc
   *********************************/
  exitstatus = tmLQCD_invert_init(argc, argv, 1);
  if(exitstatus != 0) {
    EXIT(1);
  }
  exitstatus = tmLQCD_get_mpi_params(&g_tmLQCD_mpi);
  if(exitstatus != 0) {
    EXIT(2);
  }
  exitstatus = tmLQCD_get_lat_params(&g_tmLQCD_lat);
  if(exitstatus != 0) {
    EXIT(3);
  }
#endif

  /*********************************
   * initialize MPI parameters for cvc
   *********************************/
  mpi_init(argc, argv);
  mpi_init_xchange_contraction(2);


  /******************************************************
   * report git version
   ******************************************************/
  if ( g_cart_id == 0 ) {
    fprintf(stdout, "# [loops_em] git version = %s\n", g_gitversion);
  }

  /*********************************
   * set number of openmp threads
   *********************************/
#ifdef HAVE_OPENMP
  if(g_cart_id == 0) fprintf(stdout, "# [loops_em] setting omp number of threads to %d\n", g_num_threads);
  omp_set_num_threads(g_num_threads);
#pragma omp parallel
{
  fprintf(stdout, "# [loops_em] proc%.4d thread%.4d using %d threads\n", g_cart_id, omp_get_thread_num(), omp_get_num_threads());
}
#else
  if(g_cart_id == 0) fprintf(stdout, "[loops_em] Warning, resetting global thread number to 1\n");
  g_num_threads = 1;
#endif

  if(init_geometry() != 0) {
    fprintf(stderr, "[loops_em] Error from init_geometry %s %d\n", __FILE__, __LINE__);
    EXIT(4);
  }

  geometry();

  mpi_init_xchange_eo_spinor();
  mpi_init_xchange_eo_propagator();

  Vhalf = VOLUME / 2;
  sizeof_eo_spinor_field = _GSI(Vhalf) * sizeof(double);

#ifndef HAVE_TMLQCD_LIBWRAPPER
  alloc_gauge_field(&g_gauge_field, VOLUMEPLUSRAND);
  if(!(strcmp(gaugefilename_prefix,"identity")==0)) {
    /* read the gauge field */
    sprintf(filename, "%s.%.4d", gaugefilename_prefix, Nconf);
    if(g_cart_id==0) fprintf(stdout, "# [loops_em] reading gauge field from file %s\n", filename);
    read_lime_gauge_field_doubleprec(filename);
  } else {
    /* initialize unit matrices */
    if(g_cart_id==0) fprintf(stdout, "\n# [loops_em] initializing unit matrices\n");
    for(unsigned int ix=0;ix<VOLUME;ix++) {
      _cm_eq_id( g_gauge_field + _GGI(ix, 0) );
      _cm_eq_id( g_gauge_field + _GGI(ix, 1) );
      _cm_eq_id( g_gauge_field + _GGI(ix, 2) );
      _cm_eq_id( g_gauge_field + _GGI(ix, 3) );
    }
  }
#else
  Nconf = g_tmLQCD_lat.nstore;
  if(g_cart_id== 0) fprintf(stdout, "[loops_em] Nconf = %d\n", Nconf);

  exitstatus = tmLQCD_read_gauge(Nconf);
  if(exitstatus != 0) {
    EXIT(5);
  }

  exitstatus = tmLQCD_get_gauge_field_pointer( &g_gauge_field );
  if(exitstatus != 0) {
    EXIT(6);
  }
  if( g_gauge_field == NULL) {
    fprintf(stderr, "[loops_em] Error, g_gauge_field is NULL %s %d\n", __FILE__, __LINE__);
    EXIT(7);
  }
#endif

#ifdef HAVE_TMLQCD_LIBWRAPPER
  /***********************************************
   * retrieve deflator paramters from tmLQCD
   ***********************************************/

  exitstatus = tmLQCD_init_deflator(_OP_ID_UP);
  if( exitstatus > 0) {
    fprintf(stderr, "[loops_em] Error from tmLQCD_init_deflator, status was %d %s %d\n", exitstatus, __FILE__, __LINE__);
    EXIT(8);
  }

  exitstatus = tmLQCD_get_deflator_params(&g_tmLQCD_defl, _OP_ID_UP);
  if(exitstatus != 0) {
    fprintf(stderr, "[loops_em] Error from tmLQCD_get_deflator_params, status was %d %s %d\n", exitstatus, __FILE__, __LINE__);
    EXIT(9);
  }

  if(g_cart_id == 1) {
    fprintf(stdout, "# [loops_em] deflator type name = %s\n", g_tmLQCD_defl.type_name);
    fprintf(stdout, "# [loops_em] deflator eo prec   = %d\n", g_tmLQCD_defl.eoprec);
    fprintf(stdout, "# [loops_em] deflator precision = %d\n", g_tmLQCD_defl.prec);
    fprintf(stdout, "# [loops_em] deflator nev       = %d\n", g_tmLQCD_defl.nev);
  }

  eo_evecs_block = (double*)(g_tmLQCD_defl.evecs);
  if(eo_evecs_block == NULL) {
    fprintf(stderr, "[loops_em] Error, eo_evecs_block is NULL %s %d\n", __FILE__, __LINE__);
    EXIT(10);
  }

  evecs_num = g_tmLQCD_defl.nev;
  if(evecs_num == 0) {
    fprintf(stderr, "[loops_em] Error, dimension of eigenspace is zero %s %d\n", __FILE__, __LINE__);
    EXIT(11);
  }

  exitstatus = tmLQCD_set_deflator_fields(_OP_ID_DN, _OP_ID_UP);
  if( exitstatus > 0) {
    fprintf(stderr, "[loops_em] Error from tmLQCD_init_deflator, status was %d %s %d\n", exitstatus, __FILE__, __LINE__);
    EXIT(8);
  }

  evecs_eval                = (double*)malloc(evecs_num*sizeof(double));
  evecs_lambdainv           = (double*)malloc(evecs_num*sizeof(double));
  evecs_4kappasqr_lambdainv = (double*)malloc(evecs_num*sizeof(double));
  if(    evecs_eval                == NULL 
      || evecs_lambdainv           == NULL 
      || evecs_4kappasqr_lambdainv == NULL 
    ) {
    fprintf(stderr, "[loops_em] Error from malloc %s %d\n", __FILE__, __LINE__);
    EXIT(39);
  }
  for( int i = 0; i < evecs_num; i++) {
    evecs_eval[i]                = ((double*)(g_tmLQCD_defl.evals))[2*i];
    evecs_lambdainv[i]           = 2.* g_kappa / evecs_eval[i];
    evecs_4kappasqr_lambdainv[i] = 4.* g_kappa * g_kappa / evecs_eval[i];
    if( g_cart_id == 0 ) fprintf(stdout, "# [loops_em] eval %4d %16.7e\n", i, evecs_eval[i] );
  }

#endif  /* of ifdef HAVE_TMLQCD_LIBWRAPPER */

  /*************************************************
   * allocate memory for the eigenvector fields
   *************************************************/
  eo_evecs_field = (double**)calloc(evecs_num, sizeof(double*));
  eo_evecs_field[0] = eo_evecs_block;
  for( int i = 1; i < evecs_num; i++) eo_evecs_field[i] = eo_evecs_field[i-1] + _GSI(Vhalf);

  /*************************************************
   * allocate memory for eo spinor fields 
   * WITH HALO
   *************************************************/
  exitstatus = init_2level_buffer ( &eo_spinor_work, 6, _GSI((VOLUME+RAND)/2) );
  if ( exitstatus != 0) {
    fprintf(stderr, "[loops_em] Error from init_2level_buffer, status was %d\n", exitstatus);
    EXIT(1);
  }

  /***********************************************************
   * multiply the phase to the gauge field
   ***********************************************************/
  exitstatus = gauge_field_eq_gauge_field_ti_phase ( &gauge_field_with_phase, g_gauge_field, co_phase_up );
  if(exitstatus != 0) {
    fprintf(stderr, "[loops_em] Error from gauge_field_eq_gauge_field_ti_phase, status was %d %s %d\n", exitstatus, __FILE__, __LINE__);
    EXIT(38);
  }

  /***********************************************
   * initialize clover, mzz and mzz_inv
   ***********************************************/
  exitstatus = init_clover ( &mzz, &mzzinv, gauge_field_with_phase );
  if ( exitstatus != 0 ) {
    fprintf(stderr, "[loops_em] Error from init_clover, status was %d\n", exitstatus);
    EXIT(1);
  }

#ifdef HAVE_MPI
  /***********************************************
   * set io process
   ***********************************************/
  if( g_proc_coords[0] == 0 && g_proc_coords[1] == 0 && g_proc_coords[2] == 0 && g_proc_coords[3] == 0) {
    io_proc = 2;
    fprintf(stdout, "# [loops_em] proc%.4d is io process\n", g_cart_id);
  } else {
    if( g_proc_coords[1] == 0 && g_proc_coords[2] == 0 && g_proc_coords[3] == 0) {
      io_proc = 1;
      fprintf(stdout, "# [loops_em] proc%.4d is send process\n", g_cart_id);
    } else {
      io_proc = 0;
    }
  }
#else
  io_proc = 2;
#endif

#if (defined PARALLELTX) || (defined PARALLELTXY) || (defined PARALLELTXYZ) 
  if(io_proc == 2) {
    if(g_tr_id != 0) {
      fprintf(stderr, "[loops_em] Error, io proc must be id 0 in g_tr_comm %s %d\n", __FILE__, __LINE__);
      EXIT(14);
    }
  }
#endif

  /***********************************************************
   * check eigenvectors
   ***********************************************************/
  if ( check_eigenvectors )  {
    exitstatus = init_2level_buffer ( &eo_spinor_field, 2, _GSI(Vhalf));
    if( exitstatus != 0 ) {
      fprintf(stderr, "[loops_em] Error from init_2level_buffer, status was %d %s %d\n", exitstatus, __FILE__, __LINE__);
      EXIT(123);
    }

    for( int i = 0; i < evecs_num; i++)
    {
      double norm;
      complex w;

      /*
      w.re =  eo_evecs_field[i][0] / ( eo_evecs_field[i][0] * eo_evecs_field[i][0] + eo_evecs_field[i][1] * eo_evecs_field[i][1]);
      w.im = -eo_evecs_field[i][1] / ( eo_evecs_field[i][0] * eo_evecs_field[i][0] + eo_evecs_field[i][1] * eo_evecs_field[i][1]);
#ifdef HAVE_MPI
      MPI_Bcast(&w, 2, MPI_DOUBLE, 0, g_cart_grid);
#endif
      spinor_field_eq_spinor_field_ti_co ( eo_evecs_field[i], eo_evecs_field[i], w, Vhalf);
      */

      C_clover_oo (eo_spinor_field[0], eo_evecs_field[i],  gauge_field_with_phase, eo_spinor_work[2], g_mzz_dn[1], g_mzzinv_dn[0]);
      C_clover_oo (eo_spinor_field[1], eo_spinor_field[0], gauge_field_with_phase, eo_spinor_work[2], g_mzz_up[1], g_mzzinv_up[0]);

      spinor_scalar_product_re(&norm, eo_evecs_field[i], eo_evecs_field[i], Vhalf);
      spinor_scalar_product_co(&w, eo_spinor_field[1], eo_evecs_field[i], Vhalf);

      w.re *= 4.*g_kappa*g_kappa;
      w.im *= 4.*g_kappa*g_kappa;

      if(g_cart_id == 0) {
        fprintf(stdout, "# [loops_em] evec %.4d norm = %25.16e w = %25.16e +I %25.16e diff = %25.16e\n", i, norm, w.re, w.im, fabs( w.re-evecs_eval[i]));
      }

    }

    fini_2level_buffer ( &eo_spinor_field );
  }  /* end of if check eigenvectors */

  /***********************************************************/
  /***********************************************************/

  /***********************************************************
   * initialize random number generator
   ***********************************************************/
  if ( ( exitstatus = init_rng_stat_file (g_seed, NULL) ) != 0 ) {
    fprintf(stderr, "[loops_em] Error from init_rng_stat_file status was %d %s %d\n", exitstatus, __FILE__, __LINE__);
    EXIT(38);
  }

  /***********************************************************/
  /***********************************************************/

  /***********************************************************
   * open AFF file
   ***********************************************************/
#ifdef HAVE_LHPC_AFF
  if(io_proc == 2) {
    sprintf(filename, "%s.%.4d.aff", "loops_em", Nconf );
    fprintf(stdout, "# [loops_em] writing data to file %s\n", filename);
    affw = aff_writer(filename);
    aff_status_str = (char*)aff_writer_errstr(affw);
    if( aff_status_str != NULL ) {
      fprintf(stderr, "[loops_em] Error from aff_writer, status was %s %s %d\n", aff_status_str, __FILE__, __LINE__);
      EXIT(15);
    }
    if( (affn = aff_writer_root(affw)) == NULL ) {
      fprintf(stderr, "[cvc_tensor_tp_write_to_aff_file] Error, aff writer is not initialized %s %d\n", __FILE__, __LINE__);
      EXIT(1);
    }
  }  /* end of if io_proc == 2 */
#endif


  /***********************************************************/
  /***********************************************************/

  /***********************************************************
   ***********************************************************
   **
   ** local currents lma
   **
   ***********************************************************
   ***********************************************************/

  /***********************************************
   * allocate
   ***********************************************/
  double ***local_loop_x = NULL, ***local_loop_p = NULL;

  /*
   *                                                    eo  gamma Vhalf                                
   */
  if( ( exitstatus = init_3level_buffer ( &local_loop_x, 2, 16, 2*Vhalf ) ) != 0 ) {
    fprintf(stderr, "[loops_em] Error from init_3level_buffer, status was %d %s %d\n", exitstatus, __FILE__, __LINE__);
    EXIT(1);
  }

  /***********************************************************/
  /***********************************************************/

  /***********************************************
   * contraction in position space
   ***********************************************/
  contract_local_loop_eo_lma ( local_loop_x, eo_evecs_field, evecs_4kappasqr_lambdainv, evecs_num, gauge_field_with_phase, mzz, mzzinv );

  /***********************************************************/
  /***********************************************************/

  /***********************************************
   * momentum projection
   ***********************************************/
  if ( ( exitstatus = cvc_loop_eo_momentum_projection ( &local_loop_p, local_loop_x, 16, g_sink_momentum_list, g_sink_momentum_number) ) != 0 ) {
    fprintf ( stderr, "[loop_em] Error from cvc_loop_eo_momentum_projection, status was %d %s %d\n", exitstatus, __FILE__, __LINE__ );
    EXIT(1);
  }

  /***********************************************************/
  /***********************************************************/

  /***********************************************
   * write to AFF file
   ***********************************************/
  sprintf(aff_tag, "/loop/local/nev%.4d", evecs_num );
  if( ( exitstatus = cvc_loop_tp_write_to_aff_file ( local_loop_p, 16, affw, aff_tag, g_sink_momentum_list, g_sink_momentum_number, io_proc ) ) != 0 ) {
    fprintf(stderr, "[loops_em] Error from cvc_loop_tp_write_to_aff_file, status was %d %s %d\n", exitstatus, __FILE__, __LINE__);
    EXIT(2);
  }

  /***********************************************/
  /***********************************************/

  fini_3level_buffer ( &local_loop_x );
  fini_3level_buffer ( &local_loop_p );


  /***********************************************************
   ***********************************************************
   **
   ** conserved vector current lma
   **
   ***********************************************************
   ***********************************************************/


  /***********************************************
   * cvc lma loops
   ***********************************************/

  double ****cvc_loop_lma_x = NULL;
  double ***cvc_loop_lma_p = NULL;
  /*
   *                                                   fbwd x gamma x eo x Vhalf
   */
  if( ( exitstatus = init_4level_buffer ( &cvc_loop_lma_x, 2, 4, 2, 2*Vhalf ) ) != 0 ) {
    fprintf(stderr, "[loops_em] Error from init_4level_buffer, status was %d %s %d\n", exitstatus, __FILE__, __LINE__);
    EXIT(1);
  }

  if( ( exitstatus = init_3level_buffer ( &cvc_loop_lma_p, 2, 4, 2*VOLUME ) ) != 0 ) {
    fprintf(stderr, "[loops_em] Error from init_3level_buffer, status was %d %s %d\n", exitstatus, __FILE__, __LINE__);
    EXIT(1);
  }

  /***********************************************
   * cvc fwd and bwd contraction in position space
   ***********************************************/
  contract_cvc_loop_eo_lma ( cvc_loop_lma_x, eo_evecs_field, evecs_4kappasqr_lambdainv, evecs_num, gauge_field_with_phase, mzz, mzzinv );

  /***********************************************/
  /***********************************************/

  /***********************************************
   * cvc momentum projection fwd
   ***********************************************/

  double ***cvc_loop_lma_aux = NULL, ****cvc_loop_tp = NULL;
  /*
   *                                                     eo x gamma x Vhalf
   */
  if( ( exitstatus = init_3level_buffer ( &cvc_loop_lma_aux, 2, 4, 2*Vhalf ) ) != 0 ) {
    fprintf(stderr, "[loops_em] Error from init_3level_buffer, status was %d %s %d\n", exitstatus, __FILE__, __LINE__);
    EXIT(1);
  }

  /*
   *                                                    fbwd x p x gamma x T
   */
  if( ( exitstatus = init_4level_buffer ( &cvc_loop_tp, 2, g_sink_momentum_number, 4, 2*T) ) != 0 ) {
    fprintf(stderr, "[loops_em] Error from init_4level_buffer, status was %d %s %d\n", exitstatus, __FILE__, __LINE__);
    EXIT(1);
  }

  for ( int mu = 0; mu < 4; mu++ ) {
    memcpy ( cvc_loop_lma_aux[0][mu], cvc_loop_lma_x[0][mu][0], 2*Vhalf*sizeof(double) );
    memcpy ( cvc_loop_lma_aux[1][mu], cvc_loop_lma_x[0][mu][1], 2*Vhalf*sizeof(double) );
  }

  /***********************************************/
  /***********************************************/

  /***********************************************
   * momentum projection
   ***********************************************/
  if( ( exitstatus = cvc_loop_eo_momentum_projection ( &(cvc_loop_tp[0]), cvc_loop_lma_aux, 4, g_sink_momentum_list, g_sink_momentum_number) ) != 0 ) {
    fprintf(stderr, "[loops_em] Error from cvc_loop_eo_momentum_projection, status was %d %s %d\n", exitstatus, __FILE__, __LINE__);
    EXIT(2);
  }

  /***********************************************/
  /***********************************************/

  /***********************************************
   * add half-link momentum factor
   ***********************************************/
  if ( ( exitstatus = cvc_loop_eo_momentum_shift ( cvc_loop_tp[0], g_sink_momentum_list, g_sink_momentum_number) ) != 0 ) {
    fprintf(stderr, "[loops_em] Error from cvc_loop_eo_momentum_shift, status was %d %s %d\n", exitstatus, __FILE__, __LINE__);
    EXIT(2);
  }

  /***********************************************/
  /***********************************************/

  /***********************************************
   * write to AFF file
   ***********************************************/
  sprintf(aff_tag, "/loop/cvc/fwd/nev%.4d", evecs_num );
  if( ( exitstatus = cvc_loop_tp_write_to_aff_file ( cvc_loop_tp[0], 4, affw, aff_tag, g_sink_momentum_list, g_sink_momentum_number, io_proc ) ) != 0 ) {
    fprintf(stderr, "[loops_em] Error from cvc_loop_tp_write_to_aff_file, status was %d %s %d\n", exitstatus, __FILE__, __LINE__);
    EXIT(2);
  }

  /***********************************************/
  /***********************************************/

  /***********************************************
   * cvc momentum projection bwd
   ***********************************************/

  for ( int mu = 0; mu < 4; mu++ ) {
    memcpy ( cvc_loop_lma_aux[0][mu], cvc_loop_lma_x[1][mu][0], 2*Vhalf*sizeof(double) );
    memcpy ( cvc_loop_lma_aux[1][mu], cvc_loop_lma_x[1][mu][1], 2*Vhalf*sizeof(double) );
  }

  /***********************************************/
  /***********************************************/

  /***********************************************
   * momentum projection
   ***********************************************/
  if( ( exitstatus = cvc_loop_eo_momentum_projection ( &(cvc_loop_tp[1]), cvc_loop_lma_aux, 4, g_sink_momentum_list, g_sink_momentum_number) ) != 0 ) {
    fprintf(stderr, "[loops_em] Error from cvc_loop_eo_momentum_projection, status was %d %s %d\n", exitstatus, __FILE__, __LINE__);
    EXIT(2);
  }

  /***********************************************/
  /***********************************************/

  /***********************************************
   * add half-link phase factor
   ***********************************************/
  if ( ( exitstatus = cvc_loop_eo_momentum_shift ( cvc_loop_tp[1], g_sink_momentum_list, g_sink_momentum_number) ) != 0 ) {
    fprintf(stderr, "[loops_em] Error from cvc_loop_eo_momentum_shift, status was %d %s %d\n", exitstatus, __FILE__, __LINE__);
    EXIT(2);
  }

  /***********************************************/
  /***********************************************/

  /***********************************************
   * write to AFF file
   ***********************************************/
  sprintf(aff_tag, "/loop/cvc/bwd/nev%.4d", evecs_num );
  if( ( exitstatus = cvc_loop_tp_write_to_aff_file ( cvc_loop_tp[1], 4, affw, aff_tag, g_sink_momentum_list, g_sink_momentum_number, io_proc ) ) != 0 ) {
    fprintf(stderr, "[loops_em] Error from cvc_loop_tp_write_to_aff_file, status was %d %s %d\n", exitstatus, __FILE__, __LINE__);
    EXIT(2);
  }

  /***********************************************************/
  /***********************************************************/

  fini_3level_buffer ( &cvc_loop_lma_aux );


  /***********************************************
   * add bwd to fwd
   ***********************************************/
  complex_field_pl_eq_complex_field   ( cvc_loop_lma_x[0][0][0], cvc_loop_lma_x[1][0][0], 8*Vhalf );


  /***********************************************************/
  /***********************************************************/

  /***********************************************************
   * check Ward identity in position space
   ***********************************************************/
  if ( check_position_space_WI ) {
    double ***cvc_loop_aux = NULL, **cvc_wi = NULL;

    if( ( exitstatus = init_3level_buffer ( &cvc_loop_aux, 2, 4, 2*Vhalf ) ) != 0 ) {
      fprintf(stderr, "[loops_em] Error from init_3level_buffer, status was %d %s %d\n", exitstatus, __FILE__, __LINE__);
      EXIT(1);
    }
    for ( int mu = 0; mu < 4; mu++ ) {
      memcpy ( cvc_loop_aux[0][mu] , cvc_loop_lma_x[0][mu][0], 2*Vhalf*sizeof(double) );
      memcpy ( cvc_loop_aux[1][mu] , cvc_loop_lma_x[0][mu][1], 2*Vhalf*sizeof(double) );
    }

    /********************************************
     * check in position space
     ********************************************/

    exitstatus = cvc_loop_eo_check_wi_position_space_lma ( &cvc_wi, cvc_loop_aux, eo_evecs_field, evecs_4kappasqr_lambdainv, evecs_num,
        gauge_field_with_phase, mzz, mzzinv  );

    if ( exitstatus != 0 ) {
      fprintf(stderr, "[loops_em] Error from cvc_loop_eo_check_wi_position_space_lma, status was %d %s %d\n", exitstatus, __FILE__, __LINE__);
      EXIT(2);
    }

    fini_3level_buffer ( &cvc_loop_aux );

    /***********************************************************/
    /***********************************************************/

    /********************************************
     * add cvc loop bwd to fwd in momentum space
     ********************************************/
    complex_field_pl_eq_complex_field   ( cvc_loop_tp[0][0][0], cvc_loop_tp[1][0][0], g_sink_momentum_number * 4 * T );

    /********************************************
     * check on momentum space
     ********************************************/
    if ( ( exitstatus = cvc_loop_eo_check_wi_momentum_space_lma ( cvc_wi, cvc_loop_tp[0], g_sink_momentum_list, g_sink_momentum_number ) ) != 0 ) {
      fprintf(stderr, "[loops_em] Error from cvc_loop_eo_check_wi_momentum_space_lma, status was %d %s %d\n", exitstatus, __FILE__, __LINE__);
      EXIT(2);
    }

    fini_2level_buffer ( &cvc_wi );

  }  /* end of if check_position_space_WI */

  /***********************************************************/
  /***********************************************************/

  fini_4level_buffer ( &cvc_loop_tp );

  /***********************************************************/
  /***********************************************************/

  /***********************************************
   * multiply with imaginary unit
   ***********************************************/
  complex_field_eq_complex_field_ti_i ( cvc_loop_lma_x[0][0][0], cvc_loop_lma_x[0][0][0], 8*Vhalf );

  /***********************************************************/
  /***********************************************************/

  /***********************************************
   * 4-dim FT,
   * write to file
   ***********************************************/
  sprintf( filename, "loop_cvc_lma_x.%.4d.nev%.4d.lime", Nconf, evecs_num );

  /***********************************************
   * loop on vector components
   ***********************************************/
  for ( int mu = 0; mu < 4; mu++ ) {

    /***********************************************
     * combine e and o part into single field
     ***********************************************/
    complex_field_eo2lexic ( cvc_loop_lma_p[0][mu], cvc_loop_lma_x[0][mu][0], cvc_loop_lma_x[0][mu][1] );

    /***********************************************
     * Fourier transform
     ***********************************************/
    if ( ( exitstatus = ft_4dim ( cvc_loop_lma_p[1][mu], cvc_loop_lma_p[0][mu], -1, (int)( mu==0 ) ) ) != 0 ) {
      fprintf(stderr, "[loops_em] Error from ft_4dim, status was %d %s %d\n", exitstatus, __FILE__, __LINE__);
      EXIT(2);
    }

    /***********************************************
     * add half-link phase factor
     ***********************************************/
    if ( ( exitstatus = half_link_momentum_phase_4dim ( cvc_loop_lma_p[1][mu], cvc_loop_lma_p[1][mu], -1, mu, (int)(mu==0) ) ) != 0 ) {
      fprintf(stderr, "[loops_em] Error from half_link_momentum_phase_4dim, status was %d %s %d\n", exitstatus, __FILE__, __LINE__);
      EXIT(2);
    }

    /***********************************************
     * write lime file
     ***********************************************/
    sprintf(contype, "\n<description> cvc loop lma contraction</description>\n"\
        "<current-index>%d</current-index>\n"\
        "<flavor>%s</flavor\n"\
        "<precision>%d</precision>\n"\
        "<nev>%d</nev>\n"\
        "<space>%s</space>\n",\
        mu, "up", 64, evecs_num, "x" );


    if ( ( exitstatus = write_lime_contraction ( cvc_loop_lma_p[0][mu], filename, 64, 1, contype, Nconf, (int)(mu > 0) ) ) != 0 ) {
      fprintf(stderr, "[loops_em] Error from write_lime_contraction, status was %d %s %d\n", exitstatus, __FILE__, __LINE__);
      EXIT(2);
    }
  }  /* end of loop on mu */

  /***********************************************************/
  /***********************************************************/

  /***********************************************
   * convolution
   *
   * init    = 1
   * project = transverse_projection
   ***********************************************/
  if ( ( exitstatus = current_field_eq_photon_propagator_ti_current_field ( cvc_loop_lma_p[1], cvc_loop_lma_p[1], 1, transverse_projection ) ) != 0 ) {
    fprintf(stderr, "[loops_em] Error from current_field_eq_photon_propagator_ti_current_field, status was %d %s %d\n", exitstatus, __FILE__, __LINE__);
    EXIT(2);
  }

  /***********************************************
   * 4-dim FT, backward
   * write to file
   ***********************************************/

  /***********************************************
   * loop on vector components
   ***********************************************/
  for ( int mu = 0; mu < 4; mu++ ) {

    /***********************************************
     * complex conjugate and multiply with -1
     ***********************************************/
    complex_field_eq_mi_complex_field_conj (cvc_loop_lma_p[1][mu], cvc_loop_lma_p[1][mu], VOLUME );

    /***********************************************
     * add half-link phase factor
     ***********************************************/
    if ( ( exitstatus = half_link_momentum_phase_4dim ( cvc_loop_lma_p[1][mu], cvc_loop_lma_p[1][mu], -1, mu, 0 ) ) != 0 ) {
      fprintf(stderr, "[loops_em] Error from half_link_momentum_phase_4dim, status was %d %s %d\n", exitstatus, __FILE__, __LINE__);
      EXIT(2);
    }

    /***********************************************
     * Fourier transform
     ***********************************************/
    if ( ( exitstatus = ft_4dim ( cvc_loop_lma_p[1][mu], cvc_loop_lma_p[1][mu], -1,              0 ) ) != 0 ) {
      fprintf(stderr, "[loops_em] Error from ft_4dim, status was %d %s %d\n", exitstatus, __FILE__, __LINE__);
      EXIT(2);
    }

    /***********************************************
     * complex conjugate and multiply with -1
     ***********************************************/
    complex_field_eq_mi_complex_field_conj ( cvc_loop_lma_p[1][mu], cvc_loop_lma_p[1][mu], VOLUME );

    /***********************************************
     * normalize with 1 / VOLUME
     ***********************************************/
    complex_field_ti_eq_re ( cvc_loop_lma_p[1][mu], 1./(double)(VOLUME*g_nproc), VOLUME);

    /***********************************************
     * write to lime file
     ***********************************************/
    sprintf(contype, "\n<description> cvc loop lma convoluted</description>\n"\
        "<current-index>%d</current-index>\n"\
        "<flavor>%s</flavor\n"\
        "<precision>%d</precision>\n"\
        "<nev>%d</nev>\n"\
        "<ft_sign>%s</ft_sign>\n"\
        "<space>%s</space>\n",\
        mu, "up", 64, evecs_num, "-1", "x" );

    if ( ( exitstatus = write_lime_contraction ( cvc_loop_lma_p[1][mu], filename, 64, 1, contype, Nconf, 1 ) ) != 0 ) {
      fprintf(stderr, "[loops_em] Error from write_lime_contraction, status was %d %s %d\n", exitstatus, __FILE__, __LINE__);
      EXIT(2);
    }

  }  /* end of loop on mu */
 
  /***********************************************************/
  /***********************************************************/

  /***********************************************
   * check Ward identity for up quark
   * in 4-dim position space
   *   -1 sign in Fourier phase
   ***********************************************/

  if ( check_position_space_WI ) {
 
    double **cvc_wi = NULL;  

    if( ( exitstatus = init_2level_buffer ( &cvc_wi, 2, 2*Vhalf ) ) != 0 ) {
      fprintf(stderr, "[loops_em] Error from init_2level_buffer, status was %d %s %d\n", exitstatus, __FILE__, __LINE__);
      EXIT(1);
    }

    if( ( exitstatus = init_1level_buffer ( &cvc_loop_lexic, 2*VOLUME ) ) != 0 ) {
      fprintf(stderr, "[loops_em] Error from init_1level_buffer, status was %d %s %d\n", exitstatus, __FILE__, __LINE__);
      EXIT(1);
    }

    
    /***********************************************
     * contract the counter term in eo-precon
     ***********************************************/
    contract_cvc_loop_eo_lma_wi ( cvc_wi, eo_evecs_field, evecs_4kappasqr_lambdainv, evecs_num, gauge_field_with_phase, mzz, mzzinv );

    /***********************************************
     * contract the counter term in eo-precon
     ***********************************************/



    /***********************************************
     * combine even and odd part into single field
     ***********************************************/
    complex_field_eo2lexic ( cvc_loop_lexic, cvc_wi[0], cvc_wi[1] );
    if ( ( exitstatus = ft_4dim ( cvc_wi[0], cvc_loop_lexic, 1, 3 ) ) != 0 ) {
      fprintf(stderr, "[loops_em] Error from ft_4dim, status was %d %s %d\n", exitstatus, __FILE__, __LINE__);
      EXIT(2);
    }

  for ( int x0 = 0; x0 <  T; x0++ ) {
    p[0] = M_PI * (x0 + g_proc_coords[0] *  T) / (double)T_global;
  for ( int x1 = 0; x1 < LX; x1++ ) {
    p[1] = M_PI * (x1 + g_proc_coords[1] * LX) / (double)LX_global;
  for ( int x2 = 0; x2 < LY; x2++ ) {
    p[2] = M_PI * (x2 + g_proc_coords[2] * LY) / (double)LY_global;
  for ( int x3 = 0; x3 < LZ; x3++ ) {
    p[3] = M_PI * (x3 + g_proc_coords[3] * LZ) / (double)LZ_global;
    unsigned int ix = g_ipt[x0][x1][x2][x3];

    double dtmp[2] = {0., 0.};
    double rtmp[2];
    for ( int mu = 0; mu < 4; mu++ ) {
      double ephase[2] = { sin( p[mu] ), -cos( p[mu] ) };
      double sinp = 2. * sin ( p[mu] );

      rtmp[0] = cvc_loop_lma_p[0][mu][2*ix  ];
      rtmp[1] = cvc_loop_lma_p[0][mu][2*ix+1];

      dtmp[0] += sinp * ( rtmp[0] * ephase[0] - rtmp[1] * ephase[1] );
      dtmp[1] += sinp * ( rtmp[0] * ephase[1] + rtmp[1] * ephase[0] );
    }


    fprintf ( stdout, "%3d %3d %3d %3d \t %25.16e %25.16e \t %25.16e %25.16e\n",
        x0 + g_proc_coords[0] * T,
        x1 + g_proc_coords[1] * LX,
        x2 + g_proc_coords[2] * LY,
        x3 + g_proc_coords[3] * LZ,
        dtmp[0], dtmp[1], cvc_wi[0][2*ix], cvc_wi[0][2*ix+1] );

    dtmp[0] -= cvc_wi[0][2*ix  ];
    dtmp[1] -= cvc_wi[0][2*ix+1];
    norm_wi += dtmp[0] * dtmp[0] + dtmp[1] * dtmp[1];
  }}}}
#ifdef HAVE_MPI
  double norm_wi_tmp = norm_wi;
  if ( ( exitstatus = MPI_Allreduce (&norm_wi_tmp, &norm_wi, 1, MPI_DOUBLE, MPI_SUM, g_cart_grid) ) != 0 ) {
    fprintf(stderr, "[loops_em] Error from MPI_Allreduce, status was %d %s %d\n", exitstatus, __FILE__, __LINE__);
    return(2);
  }
#endif
  if ( io_proc == 2 ) {
    fprintf ( stdout, "# [loops_em] momentum space wi + norm %25.16e\n", sqrt( norm_wi ) );
  }
  
  fini_2level_buffer ( &cvc_wi );
  /* END OF TEST */
#endif  /* if 0 */

  /***********************************************************/
  /***********************************************************/
#if 0

#if 0
  /***********************************************
   * TEST
   *
   * Ward identity for up quark
   * in 4-momentum space; - sign in Fourier phase
   ***********************************************/
  /* double **cvc_wi = NULL; */
  /* double p[4], norm_wi = 0.; */
  if( ( exitstatus = init_2level_buffer ( &cvc_wi, 2, 2*Vhalf ) ) != 0 ) {
    fprintf(stderr, "[loops_em] Error from init_2level_buffer, status was %d %s %d\n", exitstatus, __FILE__, __LINE__);
    EXIT(1);
  }

  contract_cvc_loop_eo_lma_wi ( cvc_wi, eo_evecs_field, evecs_4kappasqr_lambdainv, evecs_num, gauge_field_with_phase, mzz, mzzinv );

  complex_field_eo2lexic ( cvc_loop_lexic, cvc_wi[0], cvc_wi[1] );
  if ( ( exitstatus = ft_4dim ( cvc_wi[0], cvc_loop_lexic, -1, 3 ) ) != 0 ) {
    fprintf(stderr, "[loops_em] Error from ft_4dim, status was %d %s %d\n", exitstatus, __FILE__, __LINE__);
    EXIT(2);
  }

  for ( int x0 = 0; x0 <  T; x0++ ) {
    p[0] = -M_PI * (x0 + g_proc_coords[0] *  T) / (double)T_global;
  for ( int x1 = 0; x1 < LX; x1++ ) {
    p[1] = -M_PI * (x1 + g_proc_coords[1] * LX) / (double)LX_global;
  for ( int x2 = 0; x2 < LY; x2++ ) {
    p[2] = -M_PI * (x2 + g_proc_coords[2] * LY) / (double)LY_global;
  for ( int x3 = 0; x3 < LZ; x3++ ) {
    p[3] = -M_PI * (x3 + g_proc_coords[3] * LZ) / (double)LZ_global;
    unsigned int ix = g_ipt[x0][x1][x2][x3];

    double dtmp[2] = {0., 0.};
    double rtmp[2];
    for ( int mu = 0; mu < 4; mu++ ) {
      double ephase[2] = { sin( p[mu] ), -cos( p[mu] ) };
      double sinp = 2. * sin ( p[mu] );

      rtmp[0] = cvc_loop_lma_p[1][mu][2*ix  ];
      rtmp[1] = cvc_loop_lma_p[1][mu][2*ix+1];

      dtmp[0] += sinp * ( rtmp[0] * ephase[0] - rtmp[1] * ephase[1] );
      dtmp[1] += sinp * ( rtmp[0] * ephase[1] + rtmp[1] * ephase[0] );
    }


    fprintf ( stdout, "%3d %3d %3d %3d \t %25.16e %25.16e \t %25.16e %25.16e\n",
        x0 + g_proc_coords[0] * T,
        x1 + g_proc_coords[1] * LX,
        x2 + g_proc_coords[2] * LY,
        x3 + g_proc_coords[3] * LZ,
        dtmp[0], dtmp[1], cvc_wi[0][2*ix], cvc_wi[0][2*ix+1] );

    dtmp[0] -= cvc_wi[0][2*ix  ];
    dtmp[1] -= cvc_wi[0][2*ix+1];
    norm_wi += dtmp[0] * dtmp[0] + dtmp[1] * dtmp[1];
  }}}}
#ifdef HAVE_MPI
  norm_wi_tmp = norm_wi;
  if ( ( exitstatus = MPI_Allreduce (&norm_wi_tmp, &norm_wi, 1, MPI_DOUBLE, MPI_SUM, g_cart_grid) ) != 0 ) {
    fprintf(stderr, "[loops_em] Error from MPI_Allreduce, status was %d %s %d\n", exitstatus, __FILE__, __LINE__);
    return(2);
  }
#endif
  if ( io_proc == 2 ) {
    fprintf ( stdout, "# [loops_em] momentum space wi - norm %25.16e\n", sqrt( norm_wi ) );
  }
  
  fini_2level_buffer ( &cvc_wi );

  fini_1level_buffer ( &cvc_loop_lexic );
  /* END OF TEST */
#endif  /* of if 0 */

  /***********************************************/
  /***********************************************/

  /***********************************************
   * write J J
   *
   * up - up - lma - lma
   ***********************************************/
  co_eq_sum_complex_field_ti_complex_field ( (double*)&ztmp, cvc_loop_lma_p[0][0], cvc_loop_lma_p[1][0], 4*VOLUME );

  if ( io_proc == 2 ) {
    sprintf(aff_tag, "/loop/cvc-conv-cvc/lma-lma/nev%.4d", evecs_num);
    affdir = aff_writer_mkpath(affw, affn, aff_tag);
    if ( ( exitstatus = aff_node_put_complex (affw, affdir, &ztmp, (uint32_t)1 ) ) != 0 ) {
      fprintf(stderr, "[loops_em] Error from aff_node_put_double, status was %d %s %d\n", exitstatus, __FILE__, __LINE__);
      EXIT(5);
    }
  }

  fini_4level_buffer ( &cvc_loop_lma_x );

  /***********************************************************/
  /***********************************************************/

  /***********************************************************
   ***********************************************************
   **
   ** stochastic correction
   **
   ***********************************************************
   ***********************************************************/

  double ***cvc_loop_stoch_p_accum = NULL;

  if ( ( exitstatus = init_2level_buffer ( &eo_stochastic_source,     2, _GSI( Vhalf ) ) ) !=  0 ) {
    fprintf(stderr, "[loops_em] Error from init_2level_buffer, status was %d %s %d\n", exitstatus, __FILE__, __LINE__);
    EXIT(1);
  }

  if ( ( exitstatus = init_2level_buffer ( &eo_stochastic_propagator, 2, _GSI( Vhalf ) ) ) != 0 ) {
    fprintf(stderr, "[loops_em] Error from init_2level_buffer, status was %d %s %d\n", exitstatus, __FILE__, __LINE__);
    EXIT(1);
  }

  if( ( exitstatus = init_3level_buffer ( &cvc_loop_stoch_p_accum, 2, 4, 2*VOLUME ) ) != 0 ) {
    fprintf(stderr, "[loops_em] Error from init_3level_buffer, status was %d %s %d\n", exitstatus, __FILE__, __LINE__);
    EXIT(1);
  }

  /***********************************************
   * loop on stochastic samples
   ***********************************************/
  for ( int isample = 0; isample < g_nsample; isample++ ) {

#if 0
    double **full_spinor_work = NULL;
    if ( ( exitstatus = init_2level_buffer ( &full_spinor_work, 3, _GSI( VOLUME+RAND ) ) ) != 0 ) {
      fprintf(stderr, "[loops_em] Error from init_2level_buffer, status was %d %s %d\n", exitstatus, __FILE__, __LINE__);
      EXIT(1);
    }

    if ( g_read_source ) {
      sprintf ( filename, "%s.%.4d.%.5d", filename_prefix, Nconf, isample );
      if ( ( exitstatus = read_lime_spinor( full_spinor_work[0], filename, 0) ) != 0 ) {
        fprintf(stderr, "[loops_em] Error from read_lime_spinor, status was %d %s %d\n", exitstatus, __FILE__, __LINE__);
        EXIT(123);
      }
    } else {
      memset ( eo_spinor_work[0], 0, sizeof_eo_spinor_field );

      exitstatus = prepare_volume_source ( full_spinor_work[0], VOLUME );
      // exitstatus = prepare_volume_source ( eo_spinor_work[1], Vhalf );
      if(exitstatus != 0) {
        fprintf(stderr, "[loops_em] Error from prepare_volume_source, status was %d %s %d\n", exitstatus, __FILE__, __LINE__);
        EXIT(33);
      }
      // spinor_field_eo2lexic ( full_spinor_work[0], eo_spinor_work[0], eo_spinor_work[1] );

    }

    if ( g_write_source ) {
      sprintf ( filename, "%s.%.4d.%.5d", filename_prefix, Nconf, isample );
      if ( ( exitstatus = write_propagator( full_spinor_work[0], filename, 0, g_propagator_precision ) ) != 0 ) {
        fprintf(stderr, "[loops_em] Error from write_propagator, status was %d %s %d\n", exitstatus, __FILE__, __LINE__);
        EXIT(123);
      }
    }
    
    if ( g_read_propagator ) {
      sprintf ( filename, "%s.%.4d.%.5d.inverted", filename_prefix, Nconf, isample );
      if ( ( exitstatus = read_lime_spinor ( full_spinor_work[1], filename, 0) ) != 0 ) {
        fprintf(stderr, "[loops_em] Error from read_lime_spinor, status was %d %s %d\n", exitstatus, __FILE__, __LINE__);
        EXIT(123);
      }
    } else {

      exitstatus = prepare_volume_source ( full_spinor_work[1], VOLUME );
      if(exitstatus != 0) {
        fprintf(stderr, "[loops_em] Error from prepare_volume_source, status was %d %s %d\n", exitstatus, __FILE__, __LINE__);
        EXIT(33);
      }
#if 0
      spinor_field_lexic2eo ( full_spinor_work[0], eo_spinor_work[0], eo_spinor_work[1] );

      /* source -> g5 x source  */
      // g5_phi ( eo_spinor_work[0], Vhalf );
      // g5_phi ( eo_spinor_work[1], Vhalf );

      /* source -> A^-1 source = A^-1 g5 source , in-place*/
      // Q_clover_eo_SchurDecomp_Ainv ( eo_spinor_work[0], eo_spinor_work[1], eo_spinor_work[0], eo_spinor_work[1], gauge_field_with_phase, mzzinv[0][0], eo_spinor_work[2] );

      /* invert */
      memset ( eo_spinor_work[2], 0, sizeof_eo_spinor_field );
      exitstatus = tmLQCD_invert_eo ( eo_spinor_work[2], eo_spinor_work[1], _OP_ID_UP );
      if(exitstatus != 0) {
        fprintf(stderr, "[loops_em] Error from tmLQCD_invert_eo, status was %d %s %d\n", exitstatus, __FILE__, __LINE__);
        EXIT(19);
      }

      /* B^-1 , in-place */
      // Q_clover_eo_SchurDecomp_Binv ( eo_spinor_work[0], eo_spinor_work[1], eo_spinor_work[0], eo_spinor_work[2], gauge_field_with_phase, mzzinv[0][0], eo_spinor_work[3]);


      memcpy( eo_spinor_work[1], eo_spinor_work[2], sizeof_eo_spinor_field );

      spinor_field_ti_eq_re ( eo_spinor_work[1], 2.*g_kappa, Vhalf );

      X_clover_eo ( eo_spinor_work[0], eo_spinor_work[1], gauge_field_with_phase, mzzinv[0][0]);


      spinor_field_eo2lexic ( full_spinor_work[1], eo_spinor_work[0], eo_spinor_work[1] );
#endif  /* of if 0 */
    }


    if ( g_write_propagator ) {
      sprintf ( filename, "%s.%.4d.%.5d.inverted", filename_prefix, Nconf, isample );
      if ( ( exitstatus = write_propagator( full_spinor_work[1], filename, 0, g_propagator_precision ) ) != 0 ) {
        fprintf(stderr, "[loops_em] Error from write_propagator, status was %d %s %d\n", exitstatus, __FILE__, __LINE__);
        EXIT(123);
      }
    }

#if 0
    /* check propagator with full Dirac operator */
    Q_phi ( full_spinor_work[2], full_spinor_work[1], gauge_field_with_phase, g_mu );
    g5_phi ( full_spinor_work[2], VOLUME );
    double norm, norm2;
    spinor_field_norm_diff ( &norm, full_spinor_work[2], full_spinor_work[0], VOLUME );
    if (g_cart_id == 0 ) fprintf(stdout, "# [loops_em] norm diff %4d %25.16e\n", isample, norm);

    spinor_scalar_product_re ( &norm2, full_spinor_work[0], full_spinor_work[0], VOLUME );
    if (g_cart_id == 0 ) fprintf(stdout, "# [loops_em] norm      %4d %25.16e\n", isample, norm2);

#endif  /* of if 0 */

#if 0
    /* TEST */
    for ( int x0 = 0; x0 < T; x0++ ) {
    for ( int x1 = 0; x1 < LX; x1++ ) {
    for ( int x2 = 0; x2 < LY; x2++ ) {
    for ( int x3 = 0; x3 < LZ; x3++ ) {
      unsigned int ix = g_ipt[x0][x1][x2][x3];
      fprintf(stdout, "# [loops_em] x %3d %3d %3d %3d\n", x0, x1, x2, x3);
      for ( int mu = 0; mu < 4; mu++ ) {
        double U[18], V[18];
        complex w;
        _cm_eq_cm_mi_cm( U, g_gauge_field+_GGI(ix,mu), gauge_field_with_phase+_GGI(ix,mu));
        _cm_eq_cm_dag_ti_cm ( V, U, U );
        _co_eq_tr_cm( &w, V );

        fprintf(stdout, "  %3d %25.16e %25.16e\n", mu, w.re, w.im);
      }
    }}}}

    /* END OF TEST */
#endif  /* of if 0 */

    double norm2;
    spinor_scalar_product_re ( &norm2, full_spinor_work[0], full_spinor_work[0], VOLUME );
    fprintf(stdout, "# [loops_em] norm square of source     = %e\n", norm2);
    spinor_scalar_product_re ( &norm2, full_spinor_work[1], full_spinor_work[1], VOLUME );
    fprintf(stdout, "# [loops_em] norm square of propagator = %e\n", norm2);

    double ***w_field = NULL;
    if ( ( exitstatus = init_3level_buffer ( &w_field, 2, 4, 2*VOLUME ) ) != 0 ) {
      fprintf(stderr, "[loops_em] Error from init_3level_buffer, status was %d %s %d\n", exitstatus, __FILE__, __LINE__);
      EXIT(1);
    }

    for ( int x0 = 0; x0 < T; x0++ ) {
    for ( int x1 = 0; x1 < LX; x1++ ) {
    for ( int x2 = 0; x2 < LY; x2++ ) {
    for ( int x3 = 0; x3 < LZ; x3++ ) {
      unsigned int ix = g_ipt[x0][x1][x2][x3];

      /* fprintf(stdout, "# [loops_em] x %3d %3d %3d %3d\n", x0, x1, x2, x3); */
      for ( int mu = 0; mu< 4; mu++ ) {
        unsigned int ixpmu = g_iup[ix][mu];
        double spinor1[24], spinor2[24];
        complex w;

        _fv_eq_cm_ti_fv ( spinor1, gauge_field_with_phase+_GGI(ix,mu), full_spinor_work[1]+_GSI(ixpmu) );
        _fv_eq_gamma_ti_fv(spinor2, mu, spinor1 );
        _fv_mi_eq_fv ( spinor2, spinor1 );
        _co_eq_fv_dag_ti_fv ( &w, full_spinor_work[0]+_GSI(ix), spinor2 );

        _fv_eq_cm_dag_ti_fv ( spinor1, gauge_field_with_phase+_GGI(ix,mu), full_spinor_work[1]+_GSI(ix) );
        _fv_eq_gamma_ti_fv(spinor2, mu, spinor1 );
        _fv_pl_eq_fv ( spinor2, spinor1 );
        _co_pl_eq_fv_dag_ti_fv ( &w, full_spinor_work[0]+_GSI(ixpmu), spinor2 );

        w_field[0][mu][2*ix  ] = -0.5 * w.im / (double)VOLUME;
        w_field[0][mu][2*ix+1] =  0.5 * w.re / (double)VOLUME;

        /* fprintf(stdout, "%3d %25.16e %25.16e\n", mu, w_field[0][mu][2*ix], w_field[0][mu][2*ix+1]); */

      }

    }}}}


    for ( int mu = 0; mu< 4; mu++ ) {
      if ( ( exitstatus = ft_4dim ( w_field[1][mu], w_field[0][mu], -1, mu==0 ) ) != 0 ) {
        fprintf(stderr, "[loops_em] Error from ft_4dim, status was %d %s %d\n", exitstatus, __FILE__, __LINE__);
        EXIT(2);
      }

      if ( ( exitstatus = half_link_momentum_phase_4dim ( w_field[1][mu], w_field[1][mu], -1, mu, 1 ) ) != 0 ) {
        fprintf(stderr, "[loops_em] Error from half_link_momentum_phase_4dim, status was %d %s %d\n", exitstatus, __FILE__, __LINE__);
        EXIT(2);
      }
    }

    /***********************************************
     * convolution
     *
     * init    = 0
     * project = transverse_projection
     ***********************************************/
    if ( ( exitstatus = current_field_eq_photon_propagator_ti_current_field ( w_field[1], w_field[1], 0, transverse_projection ) ) != 0 ) {
      fprintf(stderr, "[loops_em] Error from current_field_eq_photon_propagator_ti_current_field, status was %d %s %d\n", exitstatus, __FILE__, __LINE__);
      EXIT(2);
    }

    for ( int mu = 0; mu < 4; mu++ ) {
      complex_field_eq_mi_complex_field_conj ( w_field[1][mu], w_field[1][mu], VOLUME );

      if ( ( exitstatus = half_link_momentum_phase_4dim ( w_field[1][mu], w_field[1][mu], -1, mu, 0 ) ) != 0 ) {
        fprintf(stderr, "[loops_em] Error from half_link_momentum_phase_4dim, status was %d %s %d\n", exitstatus, __FILE__, __LINE__);
        EXIT(2);
      }
      if ( ( exitstatus = ft_4dim ( w_field[1][mu], w_field[1][mu], -1, 0 ) ) != 0 ) {
        fprintf(stderr, "[loops_em] Error from ft_4dim, status was %d %s %d\n", exitstatus, __FILE__, __LINE__);
        EXIT(2);
      }

      complex_field_eq_mi_complex_field_conj ( w_field[1][mu], w_field[1][mu], VOLUME );
    }  /* end of loop on mu */



    for ( int mu = 0; mu< 4; mu++ ) {
      /* sprintf(filename, "cvc_%d.txt", mu); */
      /* sprintf(filename, "cvc_photon_%d.txt", mu); */
      sprintf(filename, "cvc_photon_x_%d.txt", mu);
      FILE *ofs = fopen(filename, "w");
      for ( int x0 = 0; x0 < T; x0++ ) {
      for ( int x1 = 0; x1 < LX; x1++ ) {
      for ( int x2 = 0; x2 < LY; x2++ ) {
      for ( int x3 = 0; x3 < LZ; x3++ ) {
        unsigned int ix = g_ipt[x0][x1][x2][x3];
          fprintf( ofs, "%3d %3d %3d %3d   %3d   %25.16e %25.16e\n", x0, x1, x2, x3, mu, w_field[1][mu][2*ix], w_field[1][mu][2*ix+1]); 
      }}}}
      fclose( ofs );
    }

    double dtmp[2] = {0., 0.};
#if 0
    for ( unsigned int ix = 0; ix < VOLUME; ix++ ) {
      for ( int mu = 0; mu < 4; mu++ ) {
        dtmp[0] += w_field[0][mu][2*ix  ] * w_field[1][mu][2*ix  ] - w_field[0][mu][2*ix+1] * w_field[1][mu][2*ix+1];
        dtmp[1] += w_field[0][mu][2*ix  ] * w_field[1][mu][2*ix+1] + w_field[0][mu][2*ix+1] * w_field[1][mu][2*ix  ];
      }
    }
    dtmp[0] *= VOLUME;
    dtmp[1] *= VOLUME;
    fprintf( stdout, "# [loops_em] JGJ %25.16e %25.16e\n", dtmp[0], dtmp[1] );
#endif  /* of if 0 */
    co_eq_sum_complex_field_ti_complex_field ( dtmp, w_field[0][0], w_field[1][0], 4*VOLUME );
    dtmp[0] *= VOLUME;
    dtmp[1] *= VOLUME;
    fprintf( stdout, "# [loops_em] JGJ %25.16e %25.16e\n", dtmp[0], dtmp[1] );

    fini_2level_buffer ( &full_spinor_work );
    fini_3level_buffer ( &w_field );
#endif  /* of if 0 */

    /***********************************************/
    /***********************************************/

    /***********************************************
     * volume source
     ***********************************************/
    if ( g_read_source ) {
      sprintf ( filename, "%s.%.4d.%.5d", filename_prefix, Nconf, isample );

      double *sf_aux = NULL;
      if ( ( exitstatus = alloc_spinor_field( &sf_aux, VOLUME ) ) != 0 ) {
        fprintf(stderr, "[loops_em] Error from alloc_spinor_field, status was %d %s %d\n", exitstatus, __FILE__, __LINE__);
        EXIT(33);
      }

      /***********************************************
       * read full-volume spinor field from lime file
       ***********************************************/
      if ( ( exitstatus = read_lime_spinor( sf_aux, filename, 0 ) ) != 0 ) {
        fprintf(stderr, "[loops_em] Error from read_lime_spinor, status was %d %s %d\n", exitstatus, __FILE__, __LINE__);
        EXIT(33);
      }

      /***********************************************
       * decompose sf_aux into even and odd part
       *   we only need the odd part in eo_stochastic_source[0]
       ***********************************************/
      spinor_field_lexic2eo ( sf_aux, eo_stochastic_source[1] , eo_stochastic_source[0] );


      free ( sf_aux );

    } else {
      if ( ( exitstatus = prepare_volume_source ( eo_stochastic_source[0], Vhalf) ) != 0 ) {
        fprintf(stderr, "[loops_em] Error from prepare_volume_source, status was %d %s %d\n", exitstatus, __FILE__, __LINE__);
        EXIT(33);
      }

      if ( g_write_source ) {

        sprintf ( filename, "%s.%.4d.%.5d", filename_prefix, Nconf, isample );

        double *sf_aux = NULL;
        if ( ( exitstatus = alloc_spinor_field( &sf_aux, VOLUME ) ) != 0 ) {
          fprintf(stderr, "[loops_em] Error from alloc_spinor_field, status was %d %s %d\n", exitstatus, __FILE__, __LINE__);
          EXIT(33);
        }

        /***********************************************
         * decompose sf_aux into even and odd part
         *   we only need the odd part in eo_stochastic_source[0]
         ***********************************************/
        memset ( eo_stochastic_source[1], 0, _GSI(Vhalf)*sizeof(double) );
        spinor_field_lexic2eo ( sf_aux, eo_stochastic_source[1] , eo_stochastic_source[0] );

        /***********************************************
         * read full-volume spinor field from lime file
         ***********************************************/
        if ( ( exitstatus = write_propagator( sf_aux, filename, 0, g_propagator_precision ) ) != 0 ) {
          fprintf(stderr, "[loops_em] Error from write_propagator, status was %d %s %d\n", exitstatus, __FILE__, __LINE__);
          EXIT(33);
        }

        free ( sf_aux );
      }

    }  /* end of if g_read_soure else */

    /***********************************************/
    /***********************************************/

    /***********************************************
     * orthogonal projection
     ***********************************************/
    if ( evecs_projection ) {
      exitstatus = project_propagator_field( eo_stochastic_source[0], eo_stochastic_source[0], 0, eo_evecs_field[0], 1, evecs_num, Vhalf);
      if (exitstatus != 0) {
        fprintf(stderr, "[loops_em] Error from project_propagator_field, status was %d %s %d\n", exitstatus, __FILE__, __LINE__);
        EXIT(35);
      }
    }

    /***********************************************/
    /***********************************************/

    /***********************************************
     * invert
     ***********************************************/
    memset( eo_spinor_work[0], 0, sizeof_eo_spinor_field );
    memcpy( eo_spinor_work[1], eo_stochastic_source[0], sizeof_eo_spinor_field );
    exitstatus = tmLQCD_invert_eo ( eo_spinor_work[0], eo_spinor_work[1], _OP_ID_UP );
    if(exitstatus != 0) {
      fprintf(stderr, "[loops_em] Error from tmLQCD_invert_eo, status was %d %s %d\n", exitstatus, __FILE__, __LINE__);
      EXIT(19);
    }
    memcpy( eo_stochastic_propagator[0], eo_spinor_work[0], sizeof_eo_spinor_field );

    /***********************************************
     * check residual
     ***********************************************/
    if( check_propagator_residual ) {
      exitstatus = check_oo_propagator_clover_eo( eo_stochastic_propagator, eo_stochastic_source, &(eo_spinor_work[0]), 
          gauge_field_with_phase, g_mzz_up, g_mzzinv_up, 1 );
      if(exitstatus != 0) {
        fprintf(stderr, "[loops_em] Error from check_oo_propagator_clover_eo, status was %d %s %d\n", exitstatus, __FILE__, __LINE__);
        EXIT(19);
      }
    }

    /***********************************************/
    /***********************************************/

    /***********************************************
     * multiply with 2 kappa
     ***********************************************/
    spinor_field_ti_eq_re ( eo_stochastic_propagator[0], 2.*g_kappa, Vhalf );

    /***********************************************/
    /***********************************************/

    /***********************************************
     * write to stochastic source and propagator
     * to lime file
     ***********************************************/
    if ( g_write_propagator ) {
      
      double **full_spinor_work = NULL;
      if ( ( exitstatus = init_2level_buffer ( &full_spinor_work, 2, _GSI( VOLUME+RAND ) ) ) != 0 ) {
        fprintf(stderr, "[loops_em] Error from init_2level_buffer, status was %d %s %d\n", exitstatus, __FILE__, __LINE__);
        EXIT(1);
      }

      /***********************************************
       * xxi <- Xbar xi
       ***********************************************/
      memcpy ( eo_spinor_work[2], eo_stochastic_source[0], sizeof_eo_spinor_field );
      X_clover_eo (eo_spinor_work[0] , eo_spinor_work[2], gauge_field_with_phase, mzzinv[1][0]);
      g5_phi ( eo_spinor_work[0], Vhalf );
      memcpy ( eo_spinor_work[1], eo_stochastic_source[0], sizeof_eo_spinor_field );
      g5_phi ( eo_spinor_work[1], Vhalf );
      /*                      lexic field        eo field, even     eo field, odd */
      spinor_field_eo2lexic ( full_spinor_work[0], eo_spinor_work[0], eo_spinor_work[1] );
      sprintf ( filename, "%s.%.4d.%.5d", filename_prefix2, Nconf, isample );
      if ( ( exitstatus = write_propagator( full_spinor_work[0], filename, 0, g_propagator_precision ) ) != 0 ) {
        fprintf(stderr, "[loops_em] Error from write_propagator, status was %d %s %d\n", exitstatus, __FILE__, __LINE__);
        EXIT(123);
      }

      /***********************************************
       * xphi <- X phi
       ***********************************************/
      memcpy ( eo_spinor_work[2], eo_stochastic_propagator[0], sizeof_eo_spinor_field );
      X_clover_eo ( eo_spinor_work[0], eo_spinor_work[2], gauge_field_with_phase, mzzinv[0][0]);
      /*                      lexic field        eo field, even     eo field, odd */
      spinor_field_eo2lexic ( full_spinor_work[1], eo_spinor_work[0], eo_stochastic_propagator[0] );

      sprintf ( filename, "%s.%.4d.%.5d.inverted", filename_prefix2, Nconf, isample );
      if ( ( exitstatus = write_propagator( full_spinor_work[1], filename, 0, g_propagator_precision ) ) != 0 ) {
        fprintf(stderr, "[loops_em] Error from write_propagator, status was %d %s %d\n", exitstatus, __FILE__, __LINE__);
        EXIT(123);
      }

      fini_2level_buffer ( &full_spinor_work );
    }  /* end of if write propagator */

    /***********************************************/
    /***********************************************/

    /***********************************************
     * local loops
     ***********************************************/
    double ***local_loop_x = NULL, ***local_loop_p = NULL;

    if( ( exitstatus = init_3level_buffer ( &local_loop_x, 2, 16, 2*Vhalf ) ) != 0 ) {
      fprintf(stderr, "[loops_em] Error from init_3level_buffer, status was %d %s %d\n", exitstatus, __FILE__, __LINE__);
      EXIT(1);
    }

    /***********************************************
     * contraction in position space
     ***********************************************/
    contract_local_loop_eo_stoch ( local_loop_x, eo_stochastic_propagator, eo_stochastic_source, 1, gauge_field_with_phase, mzz, mzzinv );

    /***********************************************
     * momentum projection
     ***********************************************/
    if ( ( exitstatus = cvc_loop_eo_momentum_projection ( &local_loop_p, local_loop_x, 16, g_sink_momentum_list, g_sink_momentum_number) ) != 0 ) {
      fprintf ( stderr, "[loop_em] Error from cvc_loop_eo_momentum_projection, status was %d %s %d\n", exitstatus, __FILE__, __LINE__ );
      EXIT(1);
    }

    /***********************************************
     * write to file
     ***********************************************/
    sprintf(aff_tag, "/loop/local/sample%.4d", isample );
    if( ( exitstatus = cvc_loop_tp_write_to_aff_file ( local_loop_p, 16, affw, aff_tag, g_sink_momentum_list, g_sink_momentum_number, io_proc ) ) != 0 ) {
      fprintf(stderr, "[loops_em] Error from cvc_loop_tp_write_to_aff_file, status was %d %s %d\n", exitstatus, __FILE__, __LINE__);
      EXIT(2);
    }

    /***********************************************/
    /***********************************************/

    fini_3level_buffer ( &local_loop_x );
    fini_3level_buffer ( &local_loop_p );

    /***********************************************/
    /***********************************************/

    /***********************************************
     * cvc fwd and bwd loop
     ***********************************************/
    double ****cvc_loop_stoch_x = NULL, ***cvc_loop_stoch_p = NULL;

    /*
     *                                                         fbwd x gamma x eo x Vhalf
     */
    if ( ( exitstatus = init_4level_buffer ( &cvc_loop_stoch_x, 2, 4, 2, 2*Vhalf ) ) != 0 ) {
      fprintf(stderr, "[loops_em] Error from init_4level_buffer, status was %d %s %d\n", exitstatus, __FILE__, __LINE__);
      EXIT(1);
    }

    /*
     *                                                         eo x gamma x Vhalf
     */
    if( ( exitstatus = init_3level_buffer ( &cvc_loop_stoch_p, 2, 4, 2*VOLUME ) ) != 0 ) {
      fprintf(stderr, "[loops_em] Error from init_3level_buffer, status was %d %s %d\n", exitstatus, __FILE__, __LINE__);
      EXIT(1);
    }

    /***********************************************
     * contraction in position space
     ***********************************************/
    contract_cvc_loop_eo_stoch ( cvc_loop_stoch_x, eo_stochastic_propagator, eo_stochastic_source, 1, gauge_field_with_phase, mzz, mzzinv );

    /***********************************************/
    /***********************************************/

    /***********************************************
     * momentum projection, fwd
     ***********************************************/

    double ***cvc_loop_stoch_aux = NULL;
    if( ( exitstatus = init_3level_buffer ( &cvc_loop_stoch_aux, 2, 4, 2*Vhalf ) ) != 0 ) {
      fprintf(stderr, "[loops_em] Error from init_3level_buffer, status was %d %s %d\n", exitstatus, __FILE__, __LINE__);
      EXIT(1);
    }

    for ( int mu = 0; mu < 4; mu++ ) {
      memcpy ( cvc_loop_stoch_aux[0][mu], cvc_loop_stoch_x[0][mu][0], 2*Vhalf*sizeof(double) );
      memcpy ( cvc_loop_stoch_aux[1][mu], cvc_loop_stoch_x[0][mu][1], 2*Vhalf*sizeof(double) );
    }

    if ( ( exitstatus = cvc_loop_eo_momentum_projection ( &cvc_loop_tp, cvc_loop_stoch_aux, 4, g_sink_momentum_list, g_sink_momentum_number) ) != 0 ) {
      fprintf(stderr, "[loops_em] Error from cvc_loop_eo_momentum_projection, status was %d %s %d\n", exitstatus, __FILE__, __LINE__);
      EXIT(2);
    }

    /***********************************************/
    /***********************************************/

    /***********************************************
     * add half-link momentum shift
     ***********************************************/
    if ( ( exitstatus = cvc_loop_eo_momentum_shift ( cvc_loop_tp, g_sink_momentum_list, g_sink_momentum_number) ) != 0 ) {
      fprintf(stderr, "[loops_em] Error from cvc_loop_eo_momentum_shift, status was %d %s %d\n", exitstatus, __FILE__, __LINE__);
      EXIT(2);
    }

    /***********************************************/
    /***********************************************/

    /***********************************************
     * write to AFF file
     ***********************************************/
    sprintf(aff_tag, "/loop/cvc/fwd/sample%.4d", isample);
    exitstatus = cvc_loop_tp_write_to_aff_file ( cvc_loop_tp, 4, affw, aff_tag, g_sink_momentum_list, g_sink_momentum_number, io_proc );
    if(exitstatus != 0 ) {
      fprintf(stderr, "[loops_em] Error from cvc_loop_tp_write_to_aff_file, status was %d %s %d\n", exitstatus, __FILE__, __LINE__);
      EXIT(2);
    }

    /***********************************************/
    /***********************************************/

    /***********************************************
     * momentum projection, bwd
     ***********************************************/

    for ( int mu = 0; mu < 4; mu++ ) {
      memcpy ( cvc_loop_stoch_aux[0][mu], cvc_loop_stoch_x[1][mu][0], 2*Vhalf*sizeof(double) );
      memcpy ( cvc_loop_stoch_aux[1][mu], cvc_loop_stoch_x[1][mu][1], 2*Vhalf*sizeof(double) );
    }

    exitstatus = cvc_loop_eo_momentum_projection ( &cvc_loop_tp, cvc_loop_stoch_aux, 4, g_sink_momentum_list, g_sink_momentum_number);
    if(exitstatus != 0 ) {
      fprintf(stderr, "[loops_em] Error from cvc_loop_eo_momentum_projection, status was %d %s %d\n", exitstatus, __FILE__, __LINE__);
      EXIT(2);
    }

    /***********************************************/
    /***********************************************/

    /***********************************************
     * add half-link momentum shift
     ***********************************************/
    if ( ( exitstatus = cvc_loop_eo_momentum_shift ( cvc_loop_tp, g_sink_momentum_list, g_sink_momentum_number) ) != 0 ) {
      fprintf(stderr, "[loops_em] Error from cvc_loop_eo_momentum_shift, status was %d %s %d\n", exitstatus, __FILE__, __LINE__);
      EXIT(2);
    }

    /***********************************************/
    /***********************************************/

    /***********************************************
     * write to AFF file
     ***********************************************/
    sprintf(aff_tag, "/loop/cvc/bwd/sample%.4d", isample);
    exitstatus = cvc_loop_tp_write_to_aff_file ( cvc_loop_tp, 4, affw, aff_tag, g_sink_momentum_list, g_sink_momentum_number, io_proc );
    if(exitstatus != 0 ) {
      fprintf(stderr, "[loops_em] Error from cvc_loop_tp_write_to_aff_file, status was %d %s %d\n", exitstatus, __FILE__, __LINE__);
      EXIT(2);
    }

    /***********************************************/
    /***********************************************/

    /***********************************************
     * add fwd and bwd, multiply by imaginary unit
     ***********************************************/
    complex_field_pl_eq_complex_field   ( cvc_loop_stoch_x[0][0][0], cvc_loop_stoch_x[1][0][0], 8*Vhalf );
    complex_field_eq_complex_field_ti_i ( cvc_loop_stoch_x[0][0][0], cvc_loop_stoch_x[0][0][0], 8*Vhalf );

    /***********************************************/
    /***********************************************/

    /***********************************************
     * 4-dim FT, forward
     * half-link phase shift
     *
     * sign = -1, init = 0
     ***********************************************/

    /***********************************************
     * loop on vector components
     ***********************************************/
    for ( int mu = 0; mu < 4; mu++ ) {

      /***********************************************
       * combine e and o part into single field
       ***********************************************/
      complex_field_eo2lexic ( cvc_loop_stoch_p[0][mu], cvc_loop_stoch_x[0][mu][0], cvc_loop_stoch_x[0][mu][1] );

      /***********************************************
       * Fourier transform
       ***********************************************/
      if ( ( exitstatus = ft_4dim ( cvc_loop_stoch_p[1][mu], cvc_loop_stoch_p[0][mu], -1, 0 ) ) != 0 ) {
        fprintf(stderr, "[loops_em] Error from ft_4dim, status was %d %s %d\n", exitstatus, __FILE__, __LINE__);
        EXIT(2);
      }

      /***********************************************
       * add half-link phase factor
       ***********************************************/
      if ( ( exitstatus = half_link_momentum_phase_4dim ( cvc_loop_stoch_p[1][mu], cvc_loop_stoch_p[1][mu], -1, mu, 0 ) ) != 0 ) {
        fprintf(stderr, "[loops_em] Error from half_link_momentum_phase_4dim, status was %d %s %d\n", exitstatus, __FILE__, __LINE__);
        EXIT(2);
      }

    }  /* end of loop on mu */

    /***********************************************/
    /***********************************************/

    /***********************************************
     * convolution
     *
     * init    = 0
     * project = transverse_projection
     ***********************************************/
    if ( ( exitstatus = current_field_eq_photon_propagator_ti_current_field ( cvc_loop_stoch_p[1], cvc_loop_stoch_p[1], 0, transverse_projection ) ) != 0 ) {
      fprintf(stderr, "[loops_em] Error from current_field_eq_photon_propagator_ti_current_field, status was %d %s %d\n", exitstatus, __FILE__, __LINE__);
      EXIT(2);
    }

    /***********************************************/
    /***********************************************/

    /***********************************************
     * 4-dim FT, backward
     * half-link phase shift
     *
     * sign = 1, init = 0
     *
     * we use complex conjugation twice and Fourier
     * transform with sign = -1
     ***********************************************/

    /***********************************************
     * loop on vector components
     ***********************************************/
    for ( int mu = 0; mu < 4; mu++ ) {


      /***********************************************
       * complex conjugate and multiply with -1
       ***********************************************/
      complex_field_eq_mi_complex_field_conj ( cvc_loop_stoch_p[1][mu], cvc_loop_stoch_p[1][mu], VOLUME );

      if ( ( exitstatus = half_link_momentum_phase_4dim ( cvc_loop_stoch_p[1][mu], cvc_loop_stoch_p[1][mu], -1, mu, 0 ) ) != 0 ) {
        fprintf(stderr, "[loops_em] Error from half_link_momentum_phase_4dim, status was %d %s %d\n", exitstatus, __FILE__, __LINE__);
        EXIT(2);
      }

      /***********************************************
       * Fourier transform
       ***********************************************/
      if ( ( exitstatus = ft_4dim ( cvc_loop_stoch_p[1][mu], cvc_loop_stoch_p[1][mu], -1, 0 ) ) != 0 ) {
        fprintf(stderr, "[loops_em] Error from ft_4dim, status was %d %s %d\n", exitstatus, __FILE__, __LINE__);
        EXIT(2);
      }

      /***********************************************
       * complex conjugate and multiply with -1
       ***********************************************/
      complex_field_eq_mi_complex_field_conj ( cvc_loop_stoch_p[1][mu], cvc_loop_stoch_p[1][mu], VOLUME );

      /***********************************************
       * normalize with 1 / VOLUME
       ***********************************************/
      complex_field_ti_eq_re ( cvc_loop_stoch_p[1][mu], 1./(double)(VOLUME*g_nproc), VOLUME);
    }  /* end of loop on mu */

    /***********************************************/
    /***********************************************/

    /***********************************************
     * accumulate sum of loops in momentum space
     ***********************************************/
    complex_field_pl_eq_complex_field ( cvc_loop_stoch_p_accum[0][0], cvc_loop_stoch_p[0][0], 8*VOLUME );

    /***********************************************/
    /***********************************************/

    /***********************************************
     * convolutions scalar for current sample
     ***********************************************/
    co_eq_sum_complex_field_ti_complex_field ( (double*)&ztmp, cvc_loop_stoch_p[0][0], cvc_loop_stoch_p[1][0], 4*VOLUME );

    /***********************************************
     * write to AFF file
     ***********************************************/
    if ( io_proc == 2 ) {
      sprintf(aff_tag, "/loop/cvc-conv-cvc/stoch-stoch/sample%.4d", isample);
      affdir = aff_writer_mkpath(affw, affn, aff_tag);
      if ( ( exitstatus = aff_node_put_complex (affw, affdir, &ztmp, (uint32_t)1 ) ) != 0 ) {
        fprintf(stderr, "[loops_em] Error from aff_node_put_double, status was %d %s %d\n", exitstatus, __FILE__, __LINE__);
        EXIT(5);
      }
    }

    /***********************************************/
    /***********************************************/

    fini_4level_buffer ( &cvc_loop_stoch_x );
    fini_3level_buffer ( &cvc_loop_tp );
    fini_3level_buffer ( &cvc_loop_stoch_aux );
    fini_3level_buffer ( &cvc_loop_stoch_p );

    /***********************************************/
    /***********************************************/

    /***********************************************
     * save results for (isample + 1) multiple of Nsave
     ***********************************************/
    if ( (isample + 1 )% Nsave == 0 ) {

      /***********************************************
       * up - up - stoch - stoch
       ***********************************************/
      co_eq_sum_complex_field_ti_complex_field ( (double*)&ztmp, cvc_loop_stoch_p_accum[0][0], cvc_loop_stoch_p_accum[1][0], 4*VOLUME );

      if ( io_proc == 2 ) {
        sprintf(aff_tag, "/loop/cvc-conv-cvc/stoch-stoch/block%.4d", isample);
        affdir = aff_writer_mkpath(affw, affn, aff_tag);
        if ( ( exitstatus = aff_node_put_complex (affw, affdir, &ztmp, (uint32_t)1 ) ) != 0 ) {
          fprintf(stderr, "[loops_em] Error from aff_node_put_double, status was %d %s %d\n", exitstatus, __FILE__, __LINE__);
          EXIT(5);
        }
      }

      /***********************************************
       * up - up - stoch - lma
       ***********************************************/
      co_eq_sum_complex_field_ti_complex_field ( (double*)&ztmp, cvc_loop_stoch_p_accum[0][0], cvc_loop_lma_p[1][0], 4*VOLUME );

      if ( io_proc == 2 ) {
        sprintf(aff_tag, "/loop/cvc-conv-cvc/stoch-lma/block%.4d", isample);
        affdir = aff_writer_mkpath(affw, affn, aff_tag);
        if ( ( exitstatus = aff_node_put_complex (affw, affdir, &ztmp, (uint32_t)1 ) ) != 0 ) {
          fprintf(stderr, "[loops_em] Error from aff_node_put_double, status was %d %s %d\n", exitstatus, __FILE__, __LINE__);
          EXIT(5);
        }
      }

      /***********************************************
       * up - up - lma - stoch
       ***********************************************/
      co_eq_sum_complex_field_ti_complex_field ( (double*)&ztmp, cvc_loop_lma_p[0][0], cvc_loop_stoch_p_accum[1][0], 4*VOLUME );

      if ( io_proc == 2 ) {
        sprintf(aff_tag, "/loop/cvc-conv-cvc/lma-stoch/block%.4d", isample);
        affdir = aff_writer_mkpath(affw, affn, aff_tag);
        if ( ( exitstatus = aff_node_put_complex (affw, affdir, &ztmp, (uint32_t)1 ) ) != 0 ) {
          fprintf(stderr, "[loops_em] Error from aff_node_put_double, status was %d %s %d\n", exitstatus, __FILE__, __LINE__);
          EXIT(5);
        }
      }

      /***********************************************/
      /***********************************************/


      /***********************************************
       * write to lime file
       ***********************************************/
      sprintf( filename, "loop_cvc_stoch_x.%.4d.block%.4d.lime", Nconf, isample+1 );

      /***********************************************
       * loop on vector components
       ***********************************************/
      for ( int mu = 0; mu < 4; mu++ ) {

        sprintf(contype, "\n<description> cvc loop contraction</description>\n"\
            "<current-index>%d</current-index>\n"\
            "<flavor>%s</flavor\n"\
            "<precision>%d</precision>\n"\
            "<nev>%d</nev>\n"\
            "<nsample>%d</nsample>\n"\
            "<space>%s</space>\n",\
            mu, "up", 64, evecs_num, isample, "x" );

        if ( ( exitstatus = write_lime_contraction ( cvc_loop_stoch_p_accum[0][mu], filename, 64, 1, contype, Nconf, (int)(mu > 0) ) ) != 0 ) {
          fprintf(stderr, "[loops_em] Error from write_lime_contraction, status was %d %s %d\n", exitstatus, __FILE__, __LINE__);
          EXIT(2);
        }

        sprintf(contype, "\n<description> cvc loop convoluted</description>\n"\
            "<current-index>%d</current-index>\n"\
            "<flavor>%s</flavor\n"\
            "<precision>%d</precision>\n"\
            "<ft_sign>%s</ft_sign>\n"\
            "<nev>%d</nev>\n"\
            "<nsample>%d</nsample>\n"\
            "<space>%s</space>\n",\
            mu, "up", 64, "-1", evecs_num, isample, "x" );

        if ( ( exitstatus = write_lime_contraction ( cvc_loop_stoch_p_accum[1][mu], filename, 64, 1, contype, Nconf, 1 ) ) != 0 ) {
          fprintf(stderr, "[loops_em] Error from write_lime_contraction, status was %d %s %d\n", exitstatus, __FILE__, __LINE__);
          EXIT(2);
        }

      }  /* end of loop on mu */

    }  /* end of if isample mod Nsave == 0 */

  }  /* end of loop on stochastic samples */

  /***********************************************/
  /***********************************************/

  fini_2level_buffer ( &eo_stochastic_source );
  fini_2level_buffer ( &eo_stochastic_propagator );
  fini_3level_buffer ( &cvc_loop_stoch_p_accum );

#endif  /* of if 0 */

  /***********************************************/
  /***********************************************/

  fini_3level_buffer ( &cvc_loop_lma_p );

  /***********************************************/
  /***********************************************/

  /***********************************************
   * close AFF writer
   ***********************************************/
#ifdef HAVE_LHPC_AFF
  if(io_proc == 2) {
    aff_status_str = (char*)aff_writer_close (affw);
    if( aff_status_str != NULL ) {
      fprintf(stderr, "[loops_em] Error from aff_writer_close, status was %s %s %d\n", aff_status_str, __FILE__, __LINE__);
      EXIT(32);
    }
  }  /* end of if io_proc == 2 */
#endif  /* of ifdef HAVE_LHPC_AFF */


  /***********************************************/
  /***********************************************/

  /***********************************************
   * free the allocated memory, finalize
   ***********************************************/

#ifndef HAVE_TMLQCD_LIBWRAPPER
  free(g_gauge_field);
#endif
  free( gauge_field_with_phase );

  fini_2level_buffer ( &eo_spinor_field );
  fini_2level_buffer ( &eo_spinor_work );

#ifndef HAVE_TMLQCD_LIBWRAPPER
  free(eo_evecs_block);
#else
  exitstatus = tmLQCD_fini_deflator(_OP_ID_UP);
#endif
  free(eo_evecs_field);

  free ( evecs_eval );
  free ( evecs_lambdainv );
  free ( evecs_4kappasqr_lambdainv );

  /* free clover matrix terms */
  fini_clover ();

  free_geometry();

#ifdef HAVE_TMLQCD_LIBWRAPPER
  tmLQCD_finalise();
#endif


#ifdef HAVE_MPI
  mpi_fini_xchange_contraction();
  mpi_fini_xchange_eo_spinor();
  mpi_fini_datatypes();
  MPI_Finalize();
#endif

  if(g_cart_id==0) {
    g_the_time = time(NULL);
    fprintf(stdout, "# [loops_em] %s# [loops_em] end of run\n", ctime(&g_the_time));
    fprintf(stderr, "# [loops_em] %s# [loops_em] end of run\n", ctime(&g_the_time));
  }

  return(0);

}
