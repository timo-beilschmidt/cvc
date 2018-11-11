/****************************************************
 * piN2piN_projection.cpp
 * 
 * Fr 27. Jul 16:46:24 CEST 2018
 *
 * PURPOSE:
 *   originally copied from piN2piN_correlators.cpp
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
#include "read_input_parser.h"
#include "matrix_init.h"
#include "table_init_z.h"
#include "contract_diagrams.h"
#include "aff_key_conversion.h"
#include "zm4x4.h"
#include "gamma.h"
#include "twopoint_function_utils.h"
#include "rotations.h"
#include "group_projection.h"
#include "little_group_projector_set.h"


using namespace cvc;

/***********************************************************
 * main program
 ***********************************************************/
int main(int argc, char **argv) {
 
#if defined CUBIC_GROUP_DOUBLE_COVER
  char const little_group_list_filename[] = "little_groups_2Oh.tab";
  int (* const set_rot_mat_table ) ( rot_mat_table_type*, const char*, const char*) = set_rot_mat_table_cubic_group_double_cover;
#elif defined CUBIC_GROUP_SINGLE_COVER
  const char little_group_list_filename[] = "little_groups_Oh.tab";
  int (* const set_rot_mat_table ) ( rot_mat_table_type*, const char*, const char*) = set_rot_mat_table_cubic_group_single_cover;
#endif


  int c;
  int filename_set = 0;
  int exitstatus;
  char filename[200];
  // double ratime, retime;
  FILE *ofs = NULL;

#ifdef HAVE_MPI
  MPI_Init(&argc, &argv);
#endif

  while ((c = getopt(argc, argv, "h?f:")) != -1) {
    switch (c) {
    case 'f':
      strcpy(filename, optarg);
      filename_set=1;
      break;
    case 'h':
    case '?':
    default:
      exit(1);
      break;
    }
  }

  /***********************************************************
   * set the default values
   ***********************************************************/
  if(filename_set==0) strcpy(filename, "cvc.input");
  read_input_parser(filename);

#ifdef HAVE_OPENMP
  omp_set_num_threads(g_num_threads);
#else
  fprintf(stdout, "[piN2piN_projection] Warning, resetting global thread number to 1\n");
  g_num_threads = 1;
#endif

  /* initialize MPI parameters */
  mpi_init(argc, argv);

  /***********************************************************
   * report git version
   ***********************************************************/
  if ( g_cart_id == 0 ) {
    fprintf(stdout, "# [piN2piN_projection] git version = %s\n", g_gitversion);
  }

  /***********************************************************
   * set geometry
   ***********************************************************/
  exitstatus = init_geometry();
  if( exitstatus != 0 ) {
    fprintf(stderr, "[piN2piN_projection] Error from init_geometry, status was %d %s %d\n", exitstatus, __FILE__, __LINE__ );
    EXIT(1);
  }
  geometry();

  /***********************************************************
   * set io process
   ***********************************************************/
  int const io_proc = get_io_proc ();

  /****************************************************
   * set cubic group single/double cover
   * rotation tables
   ****************************************************/
  rot_init_rotation_table();

  /***********************************************************
   * initialize gamma matrix algebra and several
   * gamma basis matrices
   ***********************************************************/
  init_gamma_matrix ();

  /******************************************************
   * set gamma matrices
   *   tmLQCD counting
   ******************************************************/
  gamma_matrix_type gamma[16];
  for ( int i = 0; i < 16; i++ ) {
    gamma_matrix_set ( &(gamma[i]), i, 1. );
  }


  /******************************************************
   * check source coords list
   ******************************************************/
  for ( int i = 0; i < g_source_location_number; i++ ) {
    g_source_coords_list[i][0] = ( g_source_coords_list[i][0] +  T_global ) %  T_global;
    g_source_coords_list[i][1] = ( g_source_coords_list[i][1] + LX_global ) % LX_global;
    g_source_coords_list[i][2] = ( g_source_coords_list[i][2] + LY_global ) % LY_global;
    g_source_coords_list[i][3] = ( g_source_coords_list[i][3] + LZ_global ) % LZ_global;
  }

   
  /******************************************************
   * loop on 2-point functions
   ******************************************************/
  for ( int i2pt = 0; i2pt < g_twopoint_function_number; i2pt++ ) {

    /******************************************************
     * print the 2-point function parameters
     ******************************************************/
    sprintf ( filename, "twopoint_function_%d.show", i2pt );
    if ( ( ofs = fopen ( filename, "w" ) ) == NULL ) {
      fprintf ( stderr, "[piN2piN_projection] Error from fopen %s %d\n", __FILE__, __LINE__ );
      EXIT(12);
    }
    twopoint_function_print ( &(g_twopoint_function_list[i2pt]), "TWPT", ofs );
    fclose ( ofs );

    /****************************************************
     * set number of timeslices
     ****************************************************/
    int const nT = g_twopoint_function_list[i2pt].T;
    if ( io_proc == 2 ) fprintf( stdout, "# [piN2piN_projection] number of timeslices (incl. src and snk) is %d\n", nT);

    /****************************************************
     * read little group parameters
     ****************************************************/
    little_group_type little_group;
    if ( ( exitstatus = little_group_read ( &little_group, g_twopoint_function_list[i2pt].group, little_group_list_filename ) ) != 0 ) {
      fprintf ( stderr, "[piN2piN_projection] Error from little_group_read, status was %d %s %d\n", exitstatus, __FILE__, __LINE__ );
      EXIT(2);
    }
    
    sprintf ( filename, "little_group_%d.show", i2pt );
    if ( ( ofs = fopen ( filename, "w" ) ) == NULL ) {
      fprintf ( stderr, "[piN2piN_projection] Error from fopen %s %d\n", __FILE__, __LINE__ );
      EXIT(12);
    }
    little_group_show ( &little_group, ofs, 1 );
    fclose ( ofs );

    /****************************************************
     * initialize and set projector 
     * for current little group and irrep
     ****************************************************/
    little_group_projector_type projector;
    if ( ( exitstatus = init_little_group_projector ( &projector ) ) != 0 ) {
      fprintf ( stderr, "# [piN2piN_projection] Error from init_little_group_projector, status was %d %s %d\n", exitstatus, __FILE__, __LINE__);
      EXIT(2);
    }

    int ref_row_target    = -1;     // no reference row for target irrep
    int * ref_row_spin    = NULL;   // no reference row for spin matrices
    int refframerot       = -1;     // reference frame rotation FIXME none for now
    int row_target        = -1;     // no target row
    int cartesian_list[1] = { 0 };  // not cartesian
    int parity_list[1]    = { 1 };  // intrinsic parity is +1
    const int ** momentum_list  = NULL;   // no momentum list given
    int bispinor_list[1]  = { 1 };  // bispinor yes
    int J2_list[1]        = { 1 };  // spin 1/2
    int Pref[3];

    int const Ptot[3] = {
      g_twopoint_function_list[i2pt].pf1[0] + g_twopoint_function_list[i2pt].pf2[0],
      g_twopoint_function_list[i2pt].pf1[1] + g_twopoint_function_list[i2pt].pf2[1],
      g_twopoint_function_list[i2pt].pf1[2] + g_twopoint_function_list[i2pt].pf2[2] };

    /* if ( g_verbose > 1 ) fprintf ( stdout, "# [piN2piN_projection] twopoint_function %3d Ptot = %3d %3d %3d\n", i2pt, 
        Ptot[0], Ptot[1], Ptot[2] ); */

    /****************************************************
     * do we need a reference frame rotation ?
     ****************************************************/
    exitstatus = get_reference_rotation ( Pref, &refframerot, Ptot );
    if ( exitstatus != 0 ) {
      fprintf ( stderr, "[piN2piN_projection] Error from get_reference_rotation, status was %d %s %d\n", exitstatus, __FILE__, __LINE__);
      EXIT(4);
    } else if ( g_verbose > 1 ) {
      fprintf ( stdout, "# [piN2piN_projection] twopoint_function %3d Ptot = %3d %3d %3d refframerot %2d\n", i2pt, 
          Ptot[0], Ptot[1], Ptot[2], refframerot );
    }

    fflush ( stdout );


    /****************************************************
     * set the projector with the info we have
     ****************************************************/
    exitstatus = little_group_projector_set ( &projector, &little_group, g_twopoint_function_list[i2pt].irrep , row_target, 1,
        J2_list, momentum_list, bispinor_list, parity_list, cartesian_list, ref_row_target, ref_row_spin, g_twopoint_function_list[i2pt].type, refframerot );

    if ( exitstatus != 0 ) {
      fprintf ( stderr, "[piN2piN_projection] Error from little_group_projector_set, status was %d %s %d\n", exitstatus, __FILE__, __LINE__);
      EXIT(3);
    }

    sprintf ( filename, "little_group_projector_%d.show", i2pt );
    if ( ( ofs = fopen ( filename, "w" ) ) == NULL ) {
      fprintf ( stderr, "[piN2piN_projection] Error from fopen %s %d\n", __FILE__, __LINE__ );
      EXIT(12);
    }
    exitstatus = little_group_projector_show ( &projector, ofs, 1 );
    fclose ( ofs );

    /****************************************************
     * check, that projector has correct d-vector
     ****************************************************/
    if ( ( projector.P[0] != Ptot[0] ) || ( projector.P[1] != Ptot[1] ) || ( projector.P[2] != Ptot[2] ) ) {
      fprintf ( stderr, "[piN2piN_projection] Error, projector P != Ptot\n" );
      EXIT(12);
    } else {
      if ( g_verbose > 2 ) fprintf ( stdout, "# [piN2piN_projection] projector P == Ptot\n" );
    }


    int const nrot      = projector.rtarget->n;
    int const irrep_dim = projector.rtarget->dim;

    /******************************************************
     * loop on source locations
     ******************************************************/
    for( int i_src = 0; i_src<g_source_location_number; i_src++) {
      int t_base = g_source_coords_list[i_src][0];

      /******************************************************
       * loop on coherent source locations
       ******************************************************/
      for( int i_coherent=0; i_coherent<g_coherent_source_number; i_coherent++) {
        int t_coherent = ( t_base + ( T_global / g_coherent_source_number ) * i_coherent ) % T_global;

        int source_proc_id, sx[4], gsx[4] = { t_coherent,
                      ( g_source_coords_list[i_src][1] + (LX_global/2) * i_coherent ) % LX_global,
                      ( g_source_coords_list[i_src][2] + (LY_global/2) * i_coherent ) % LY_global,
                      ( g_source_coords_list[i_src][3] + (LZ_global/2) * i_coherent ) % LZ_global };


        get_point_source_info (gsx, sx, &source_proc_id);

        /******************************************************
         * set current source coords in 2pt function
         ******************************************************/
        g_twopoint_function_list[i2pt].source_coords[0] = gsx[0];
        g_twopoint_function_list[i2pt].source_coords[1] = gsx[1];
        g_twopoint_function_list[i2pt].source_coords[2] = gsx[2];
        g_twopoint_function_list[i2pt].source_coords[3] = gsx[3];

        twopoint_function_type tp, * tp_project = NULL;

        twopoint_function_init ( &tp );

        twopoint_function_copy ( &tp, &( g_twopoint_function_list[i2pt] ) );

        if ( twopoint_function_allocate ( &tp ) == NULL ) {
          fprintf ( stderr, "[piN2piN_projection] Error from twopoint_function_allocate %s %d\n", __FILE__, __LINE__ );
          EXIT(123);

        }

        /******************************************************
         * tp_project = list of projected 2-pt functions
         *   how many ? well,...
         *   n_tp_project =      ref row     row sink    row source
         *
         *   i.e. for each reference row used in the projector
         *   we have nrow x nrow ( source, sink ) operators
         *   
         ******************************************************/
        int const n_tp_project = irrep_dim * irrep_dim * irrep_dim;
        tp_project = (twopoint_function_type *) malloc ( n_tp_project * sizeof (twopoint_function_type ) );
        if (  tp_project == NULL ) {
          fprintf ( stderr, "[piN2piN_projection] Error from malloc %s %d\n", __FILE__, __LINE__ );
          EXIT(124);
        }
 
        /******************************************************
         * loop on elements of tp_project
         * - initialize
         * - copy content of current reference element of
         *   g_twopoint_function_list
         ******************************************************/
        for ( int i = 0; i < n_tp_project; i++ ) {
          twopoint_function_init ( &(tp_project[i]) );
          twopoint_function_copy ( &(tp_project[i]), &( g_twopoint_function_list[i2pt] ) );

          /* number of data sets in tp_project is always 1
           *   we save the sum of all diagrams in here */
          tp_project[i].n = 1;
          sprintf ( tp_project[i].norm, "NA" );
          /* abuse the diagrams name string to label the row-coordinates */
          sprintf ( tp_project[i].diagrams, "rref_%d_rsnk%d_rsrc%d", i/(irrep_dim*irrep_dim), (i%(irrep_dim*irrep_dim))/irrep_dim, i%irrep_dim );


          /* allocate memory */
          if ( twopoint_function_allocate ( &(tp_project[i]) ) == NULL ) {
            fprintf ( stderr, "[piN2piN_projection] Error from twopoint_function_allocate %s %d\n", __FILE__, __LINE__ );
            EXIT(125);
          }
        }

        gamma_matrix_type gl, gr, gi11, gi12, gi2, gf11, gf12, gf2;
        gamma_matrix_init ( &gl );
        gamma_matrix_init ( &gr );

        /******************************************************
         * loop on little group elements
         *  - rotations and rotation-reflections
         *  - at sink, left-applied element
         ******************************************************/
        for ( int irotl = 0; irotl < 2*nrot; irotl ++ ) {


          double _Complex ** Rpl = ( irotl < nrot ) ? projector.rp->R[irotl] : projector.rp->IR[irotl-nrot];

          rot_point ( tp.pf1, g_twopoint_function_list[i2pt].pf1, Rpl );
          rot_point ( tp.pf2, g_twopoint_function_list[i2pt].pf2, Rpl );

          double _Complex ** Rsl = ( irotl < nrot ) ? projector.rspin[0].R[irotl] : projector.rspin[0].IR[irotl-nrot];
          memcpy ( gl.v, Rsl[0], 16*sizeof(double _Complex) );

          /* if ( g_verbose > 4 ) gamma_matrix_printf ( &gl, "gl", stdout ); */

          gamma_matrix_set ( &gf11, g_twopoint_function_list[i2pt].gf1[0], 1. );
          gamma_eq_gamma_op_ti_gamma_matrix_ti_gamma_op ( &gf11, &gl, 'C', &gf11, &gl, 'H' );

          gamma_matrix_set ( &gf12, g_twopoint_function_list[i2pt].gf1[1], 1. );
          gamma_eq_gamma_op_ti_gamma_matrix_ti_gamma_op ( &gf12, &gl, 'N', &gf12, &gl, 'H' );

          gamma_matrix_set ( &gf2, g_twopoint_function_list[i2pt].gf2, 1. );
          gamma_eq_gamma_op_ti_gamma_matrix_ti_gamma_op ( &gf2, &gl, 'N', &gf2, &gl, 'H' );

          tp.gf1[0] = gf11.id;
          tp.gf1[1] = gf12.id;
          tp.gf2    = gf2.id;

        /******************************************************
         * loop on little group elements
         *  - rotations and rotation-reflections
         *  - at source, right-applied element
         ******************************************************/
        for ( int irotr = 0; irotr < 2*nrot; irotr ++ ) {

          double _Complex ** Rpr = ( irotr < nrot ) ? projector.rp->R[irotr] : projector.rp->IR[irotr-nrot];

          rot_point ( tp.pi1, g_twopoint_function_list[i2pt].pi1, Rpr );
          rot_point ( tp.pi2, g_twopoint_function_list[i2pt].pi2, Rpr );

          double _Complex ** Rsr = ( irotr < nrot ) ? projector.rspin[0].R[irotr] : projector.rspin[0].IR[irotr-nrot];
          memcpy ( gr.v, Rsr[0], 16*sizeof(double _Complex) );

          gamma_matrix_set ( &gi11, g_twopoint_function_list[i2pt].gi1[0], 1. );
          gamma_eq_gamma_op_ti_gamma_matrix_ti_gamma_op ( &gi11, &gr, 'N', &gi11, &gr, 'T' );

          gamma_matrix_set ( &gi12, g_twopoint_function_list[i2pt].gi1[1], 1. );
          gamma_eq_gamma_op_ti_gamma_matrix_ti_gamma_op ( &gi12, &gr, 'N', &gi12, &gr, 'H' );

          gamma_matrix_set ( &gi2, g_twopoint_function_list[i2pt].gi2, 1. );
          gamma_eq_gamma_op_ti_gamma_matrix_ti_gamma_op ( &gi2, &gr, 'N', &gi2, &gr, 'H' );

          tp.gi1[0] = gi11.id;
          tp.gi1[1] = gi12.id;
          tp.gi2    = gi2.id;

          // TEST
          // fprintf ( stdout, "# [piN2piN_projection]  rot %2d %2d     gf11 %2d %6.2f   gf12 %2d %6.2f   gf2 %2d %6.2f   gi11 %2d %6.2f   gi12 %2d %6.2f   gi2 %2d %6.2f\n", 
          //    irotl, irotr, gf11.id, gf11.s, gf12.id, gf12.s, gf2.id, gf2.s, gi11.id, gi11.s, gi12.id, gi12.s, gi2.id, gi2.s);

          //char name[100];
          //sprintf (  name, "R%.2d_TWPT_R%.2d", irotl, irotr );
          //twopoint_function_print ( &tp, name, stdout );


          /******************************************************
           * fill the diagram with data
           ******************************************************/
 
          if ( ( exitstatus = twopoint_function_fill_data ( &tp, filename_prefix ) ) != 0 ) {
            fprintf ( stderr, "[piN2piN_projection] Error from twopoint_function_fill_data, status was %d %s %d\n", exitstatus, __FILE__, __LINE__ );
            EXIT(212);
          }


          /******************************************************
           * sum up data sets in tp
           * - add data sets 1,...,tp.n-1 to data set 0
           ******************************************************/
          for ( int i = 1; i < tp.n; i++ ) {
            contract_diagram_zm4x4_field_pl_eq_zm4x4_field ( tp.c[0], tp.c[i], tp.T );
          }

          /******************************************************
           * projection variants
           ******************************************************/

          double _Complex ** Tirrepl = ( irotl < nrot ) ? projector.rtarget->R[irotl] : projector.rtarget->IR[irotl-nrot];
          double _Complex ** Tirrepr = ( irotr < nrot ) ? projector.rtarget->R[irotr] : projector.rtarget->IR[irotr-nrot];

          for ( int rref = 0; rref < irrep_dim; rref++ ) {

            for ( int rsnk = 0; rsnk < irrep_dim; rsnk++ ) {
            for ( int rsrc = 0; rsrc < irrep_dim; rsrc++ ) {

              int const irrr = ( rref * irrep_dim + rsnk ) * irrep_dim + rsrc;

              double _Complex const zcoeff = 
                gf11.s * gf12.s * gf2.s *        Tirrepr[rsnk][rref] * 
                gi11.s * gi12.s * gi2.s * conj ( Tirrepl[rsrc][rref] );
                
              contract_diagram_zm4x4_field_eq_zm4x4_field_pl_zm4x4_field_ti_co ( tp_project[irrr].c[0], tp_project[irrr].c[0], tp.c[0], zcoeff, tp.T );

            }  // end of loop on rsrc
            }  // end of loop on rsnk
          }  // end of loop on rref
#if 0
#endif  /* end of if 0 */

        }  // end of loop on source rotations
        }  // end of loop on sink rotations



        /******************************************************
         * output of tp_project
         ******************************************************/

        for ( int itp = 0; itp < n_tp_project; itp++ ) {
 
          exitstatus = twopoint_function_write_data ( &( tp_project[itp] ) );
          if ( exitstatus != 0 ) {
            fprintf ( stderr, "[piN2piN_projection] Error from twopoint_function_write_data, status was %d %s %d\n", exitstatus, __FILE__, __LINE__);
            EXIT(12);
          }

        }  /* end of loop on 2-point functions */
#if 0
#endif  /* end of if 0 */

        /******************************************************
         * deallocate twopoint_function vars tp and tp_project
         ******************************************************/
        twopoint_function_fini ( &tp );
        for ( int i = 0; i < n_tp_project; i++ ) {
          twopoint_function_fini ( &(tp_project[i]) );
        }
        free ( tp_project );
        tp_project = NULL;

      }  // end of loop on coherent source locations

    }  // end of loop on base source locations
#if 0
#endif  /* of if 0 */

    /******************************************************
     * deallocate space inside little_group
     ******************************************************/
    little_group_fini ( &little_group );

    /******************************************************
     * deallocate space inside projector
     ******************************************************/
    fini_little_group_projector ( &projector );


  }  // end of loop on 2-point functions

  /******************************************************/
  /******************************************************/

  /******************************************************
   * finalize
   *
   * free the allocated memory, finalize
   ******************************************************/
  free_geometry();

#ifdef HAVE_TMLQCD_LIBWRAPPER
  tmLQCD_finalise();
#endif

#ifdef HAVE_MPI
  MPI_Finalize();
#endif
  if(g_cart_id == 0) {
    g_the_time = time(NULL);
    fprintf(stdout, "# [piN2piN_projection] %s# [piN2piN_projection] end fo run\n", ctime(&g_the_time));
    fflush(stdout);
    fprintf(stderr, "# [piN2piN_projection] %s# [piN2piN_projection] end fo run\n", ctime(&g_the_time));
    fflush(stderr);
  }

  return(0);
}
