/***************************************************
 * gsp.h                                     *
 ***************************************************/
#ifndef _GSP_H
#define _GSP_H
namespace cvc {

int gsp_init (double ******gsp_out, int Np, int Ng, int Nt, int Nv);

int gsp_fini(double******gsp);

int gsp_reset (double ******gsp_out, int Np, int Ng, int Nt, int Nv);

void gsp_make_eo_phase_field (double*phase_e, double*phase_o, int *momentum);

int gsp_calculate_v_dag_gamma_p_w(double *****gsp, double**V, double**W, int num, int momentum_number, int (*momentum_list)[3], int gamma_id_number, int*gamma_id_list, char*tag, int symmetric);

int gsp_read_node (double ***gsp, int num, int momentum[3], int gamma_id, char*tag);

int gsp_write_eval(double *eval, int num, char*tag);
int gsp_read_eval(double **eval, int num, char*tag);

void co_eq_tr_gsp_ti_gsp (complex *w, double**gsp1, double**gsp2, double*lambda, int num);

int gsp_printf (double ***gsp, int num, char*name, FILE*ofs);

}  /* end of namespace cvc */
#endif