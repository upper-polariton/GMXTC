/*
 *
 *                This source code is part of
 * 
 *                 G   R   O   M   A   C   S
 * 
 *          GROningen MAchine for Chemical Simulations
 * 
 *                        VERSION 3.2.0
 * Written by David van der Spoel, Erik Lindahl, Berk Hess, and others.
 * Copyright (c) 1991-2000, University of Groningen, The Netherlands.
 * Copyright (c) 2001-2004, The GROMACS development team,
 * check out http://www.gromacs.org for more information.
 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * 
 * If you want to redistribute modifications, please consider that
 * scientific software is very special. Version control is crucial -
 * bugs must be traceable. We will be happy to consider code for
 * inclusion in the official distribution, but derived work must not
 * be called official GROMACS. Details are found in the README & COPYING
 * files - if they are missing, get the official version at www.gromacs.org.
 * 
 * To help us fund GROMACS development, we humbly ask that you cite
 * the papers on the package - you can find them in the top README file.
 * 
 * For more info, check our website at http://www.gromacs.org
 * 
 * And Hey:
 * GROwing Monsters And Cloning Shrimps
 */
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifdef GMX_QMMM_GAUSSIAN

#include <math.h>
#include "sysstuff.h"
#include "typedefs.h"
#include "macros.h"
#include "smalloc.h"
#include "assert.h"
#include "physics.h"
#include "macros.h"
#include "vec.h"
#include "force.h"
#include "invblock.h"
#include "confio.h"
#include "names.h"
#include "network.h"
#include "pbc.h"
#include "ns.h"
#include "nrnb.h"
#include "bondf.h"
#include "mshift.h"
#include "txtdump.h"
#include "copyrite.h"
#include "qmmm.h"
#include <stdio.h>
#include <string.h>
#include "gmx_fatal.h"
#include "typedefs.h"
#include "../tools/eigensolver.h"
#include <stdlib.h>

#include "do_fit.h"
/* eigensolver stuff */
#include "sparsematrix.h"

#ifndef F77_FUNC
#define F77_FUNC(name,NAME) name ## _
#endif

//#include "gmx_lapack.h"
//#include "gmx_arpack.h"


#include <complex.h>
#ifdef I
#undef I
#endif
#define IMAG _Complex_I

#ifdef MKL
#include <mkl_types.h>
#include <mkl_lapack.h>
#else
#include <lapacke.h>
#endif

#define AU2PS (2.418884326505e-5) /* atomic unit of time in ps */
#define VEL2AU (AU2PS/BOHR2NM)
#define MP2AU (1837.36)           /* proton mass in atomic units */

#include <time.h>

typedef double complex dplx;

static double dottrrr(int n, real *f, rvec *A, rvec *B);

/* scalar gromacs vector product */
static void multsv(int n, dvec *R, real f, dvec *A);

/* matrix vector product */
static void multmv(int n, dplx *R, dplx *M, dplx *V);

/* multiply vector by exponential of diagonal matrix given by 
 *  * its diagonal as f*w */
static void multexpdiag(int n, dplx *r, dplx f, double *w, dplx *v);

/* hermitian adjoint of matrix */
static void dagger(int n, dplx *Mt, dplx *M);

/* diagonalize hermitian matrix using lapack/MKL routine zheev */
static void diag(int n, double *w, dplx *V, dplx *M);

/* integrate wavefunction for one MD timestep */
//static void propagate(t_QMrec *qm, t_MMrec *mm, double *QMener);

static double calc_coupling(int J, int K, double dt, int dim, double *vec, double *vecold);

/* used for the fssh algo */

/* \sum_i A_i B_i */
static real dot(int n, real *A, real *B){
  int 
    i;
  real 
    res = 0.0;
  for (i=0; i<n; i++){
    res += A[i]*B[i];
  }
  return res;
}
/* dot product of gromacs vectors with addional factor
 *  * \sum_i f_i rvec A[i] * rvec B[i]
 *   */

static dplx dot_complex(int n, dplx *A, dplx *B){
  int 
    i;
  dplx 
    res = 0.0+IMAG*0.0;
  for (i=0; i<n; i++){
//    res += A[i]*B[i];
    res += conj(A[i])*B[i];
  }
  return res;
}


static double dottrrr(int n, real *f, rvec *A, rvec *B){
  int 
    i;
  double 
    res = 0.0;
  for (i=0; i<n; ++i){
    res += f[i]*A[i][XX]*B[i][XX];
    res += f[i]*A[i][YY]*B[i][YY];
    res += f[i]*A[i][ZZ]*B[i][ZZ];
  }
  return res;
}

/* scalar gromacs vector product */
static void multsv(int n, dvec *R, real f, dvec *A){
  int 
    i;
  for (i=0; i<n; i++){
    R[i][XX] = f*A[i][XX];
    R[i][YY] = f*A[i][YY];
    R[i][ZZ] = f*A[i][ZZ];
  }
}

/* matrix vector product R = M*V, double complex */
static void multmv(int n, dplx *R, dplx *M, dplx *V){
  int 
    i, j;
  for (i=0; i<n; i++)
  {
    R[i] = 0.0;
    for (j=0; j<n; j++)
    {
      R[i] += M[i + j*n]*V[j];
    }
  }
}



int Upper_Triangular_Inverse(dplx *U, int n)
{
  int i, j, k;
  dplx *p_i, *p_j, *p_k;
  dplx sum;

//         Invert the diagonal elements of the upper triangular matrix U.
//
  for (k = 0, p_k = U; k < n; p_k += (n + 1), k++) {
    if (*p_k == 0.0) return -1;
    else *p_k = 1.0 / *p_k;
  }

//         Invert the remaining upper triangular matrix U.

  for (i = n - 2, p_i = U + n * (n - 2); i >=0; p_i -= n, i-- ) {
    for (j = n - 1; j > i; j--) {
      sum = 0.0;
      for (k = i + 1, p_k = p_i + n; k <= j; p_k += n, k++ ) {
        sum += *(p_i + k) * *(p_k + j);
      }
      *(p_i + j) = - *(p_i + i) * sum;
    }
  }
  return 0;
}



static void multexpdiag(int n, dplx *r, dplx f, double *w, dplx *v){
  int 
    i;
  for (i=0; i<n; i++){
    r[i] = cexp(f*w[i])*v[i];
  } 
}

/* hermitian adjoint of matrix */
static void dagger(int n, dplx *Mt, dplx *M){
  int 
    i, j;
  for (i=0; i<n; i++){
    for (j=0; j<n; j++){
      Mt[i + j*n] = conj(M[j + i*n]);
    }
  }
}

static void printM_complex  ( int ndim, dplx *A){
  int
    i,j;
  fprintf(stderr,"{{");
  for(i=0;i<ndim;i++){
    for (j=0;j<ndim;j++){
      fprintf(stderr,"%lf+%lfI, ",creal(A[i*ndim+j]),cimag(A[i*ndim+j]));
    }
    fprintf(stderr,"},\n{");
  }
  fprintf(stderr,"}\n");
}

static void M_complextimesM_complex (int ndim, dplx *A,dplx *B,dplx *C){
  int
    i,j,k;

  for ( i = 0 ; i< ndim ; i++ ){
    for ( j = 0 ; j < ndim ; j++){
      C[i*ndim+j]=0.0;
      for (k=0;k<ndim;k++){
        C[i*ndim+j]+=A[i*ndim+k]*B[k*ndim+j];
      }
    }
  }
}

static void transposeM_complex (int ndim, dplx *A, dplx *At){
/* need to change the sign of the imaginary componennt!!! */
  int
    i,j;
  for (i=0;i<ndim;i++){
    for (j=0;j<ndim;j++){
      At[i*ndim+j]=conj(A[j*ndim+i]);
    }
  }
}


/* diagonalize hermitian matrix using lapack/MKL routine zgeev */

/* diagonalize hermitian matrix using lapack/MKL routine zgeev */
static void diag_complex(int n, dplx *w, dplx *V, dplx *M){
  char
    jobz, uplo,jobvl,jobvr;
  int
    i, j, lwork, info;
  double
    *rwork;
#ifdef MKL
  /* If compilation with MKL linear algebra libraries  */
    MKL_Complex16
      *M_mkl,*w_mkl, *work,*vr,*vl;
#else
  /* else compilation with lapack linear algebra libraries  */
    lapack_complex_double
      *M_mkl,*w_mkl, *work,*vr,*vl;
#endif

  lwork = 2*n;
  jobvl = 'N';
  jobvr = 'V';
  snew(vr,n*n);
  snew(vl,n*n);

  snew(M_mkl, n*n);
  snew(w_mkl,n);
  snew(work, lwork);
  snew(rwork, 3*n);

  for (i=0; i<n; i++){
#ifdef MKL
    w_mkl[i].real = creal(w[i]);
    w_mkl[i].imag = cimag(w[i]);
#else
    w_mkl[i] = w[i];
#endif
    for (j=0; j<n; j++){
#ifdef MKL
      M_mkl[j + i*n].real = creal(M[j + i*n]);
      M_mkl[j + i*n].imag = cimag(M[j + i*n]);
#else
      M_mkl[j + i*n] = M[j + i*n];
#endif
    }
  }
#ifdef MKL
  zgeev(&jobvl,&jobvr,&n, M_mkl, &n, w_mkl, vl,&n,vr,&n,work, &lwork, rwork, &info);
#else
  F77_FUNC(zgeev,ZGEEV)(&jobvl,&jobvr,&n, M_mkl, &n, w_mkl, vl,&n,vr,&n,work, &lwork, rwork, &info);
  if (info != 0){
    gmx_fatal(FARGS, "Lapack returned error code: %d in zheev", info);
  }
#endif
  for (i=0; i<n; i++){
#ifdef MKL
    w[i] = w_mkl[i].real+IMAG*w_mkl[i].imag;
#else
    w[i] = creal(w_mkl[i])+IMAG*cimag(w_mkl[i]);
#endif
    for (j=0; j<n; j++){
#ifdef MKL
      V[j + i*n] = vr[j + i*n].real + IMAG*vr[j + i*n].imag;
#else
      V[j + i*n] = creal(vr[j + i*n]) + IMAG*cimag(vr[j + i*n]);
#endif
    }
  }
  sfree(vl);
  sfree(vr);
  sfree(rwork);
  sfree(M_mkl);
  sfree(w_mkl);
  sfree(work);
}

/* diagonalize hermitian matrix using MKL routine zheev */
static void diag(int n, double *w, dplx *V, dplx *M)
{
  char 
    jobz, uplo;
  int 
    i, j, lwork, info;
  double 
    *rwork;
#ifdef MKL 
  MKL_Complex16
    *M_mkl, *work;
#else 
  lapack_complex_double
    *M_mkl, *work;
#endif
 
  jobz = 'V';
  uplo = 'U';
  lwork = 2*n;

  snew(M_mkl, n*n);
  snew(work, lwork);
  snew(rwork, 3*n);
  /* GG @ 11.1.2023: Zheev expects elements of M to be stored successively within 
   * the array. 
   * "Each column of the matrix is stored successively in the array." 
   * https://www.ibm.com/docs/en/essl/6.2?topic=matrices-matrix-storage-representation
   * Because we store the rows successively, i.e.: A_{ij}=i*ndim+j, I infer that we thus
   * need to transpose matrix M prior to calling ZHEEV
   */
  for (i=0; i<n; i++){
    for (j=0; j<n; j++){
#ifdef MKL       
      M_mkl[j + i*n].real = creal(M[j*n + i]);
      M_mkl[j + i*n].imag = cimag(M[j*n + i]);
#else 
      M_mkl[j + i*n] = M[j*n + i];
#endif 
    }
  }
#ifdef MKL 
  zheev(&jobz, &uplo, &n, M_mkl, &n, w, work, &lwork, rwork, &info);
#else 
  F77_FUNC(zheev,ZHEEV)(&jobz, &uplo, &n, M_mkl, &n, w, work, &lwork, rwork, &info);
  if (info != 0){
    gmx_fatal(FARGS, "Lapack returned error code: %d in zheev", info);
  }
#endif 
  for (i=0; i<n; i++){
    for (j=0; j<n; j++){
#ifdef MKL 
      V[j + i*n] = M_mkl[j + i*n].real + IMAG*M_mkl[j + i*n].imag;
#else 
      V[j + i*n] = creal(M_mkl[j + i*n]) + IMAG*cimag(M_mkl[j + i*n]);
#endif 
    }
  }

  sfree(rwork);
  sfree(M_mkl);
  sfree(work);
}

static double calc_coupling(int J, int K, double dt, int dim, double *vec, double *vecold){
  double 
    coupling=0;
  coupling = 1./(2.*dt) * (dot(dim,&vecold[J*dim],&vec[K*dim]) - dot(dim,&vec[J*dim],&vecold[K*dim]));
//  fprintf(stderr,"coupling between %d and %d: %lf\n", J,K,coupling);
  return (coupling);
//  return 1./(2.*dt) * (dot(dim,&vecold[J*dim],&vec[K*dim]) - dot(dim,&vec[J*dim],&vecold[K*dim]));
}

static dplx calc_coupling_complex(int J, int K, double dt, int dim, dplx *vec, dplx *vecold){
  double 
    coupling=0;
  coupling = 1./(2.*dt) * (dot_complex(dim,&vecold[J*dim],&vec[K*dim]) - dot_complex(dim,&vec[J*dim],&vecold[K*dim]));
  return (coupling);
}

static void transposeM (int ndim, double *A, double *At){
  int
    i,j;
  for (i=0;i<ndim;i++){
    for (j=0;j<ndim;j++){
      At[i*ndim+j]=A[j*ndim+i];
    }
  }
}

static void MtimesV(int ndim, double *A, double *b, double *c){
 /* c = A.b */
  int
    i,k;

  for (i = 0 ; i < ndim ; i++ ){
    c[i] = 0;
    for ( k = 0 ; k < ndim ; k++ ){
      c[i] += A[i*ndim+k]*b[k];
    }
  }
}

static void MtimesV_complex(int ndim, dplx *A, dplx *b, dplx *c){
 /* c = A.b */
  int
    i,k;

  for (i = 0 ; i < ndim ; i++ ){
    c[i] = 0;
    for ( k = 0 ; k < ndim ; k++ ){
      c[i] += A[i*ndim+k]*b[k];
    }
  }
}

static void MtimesM (int ndim,double *A,double *B, double *C){
  int
    i,j,k;
     
  for ( i = 0 ; i< ndim ; i++ ){
    for ( j = 0 ; j < ndim ; j++){
      C[i*ndim+j]=0.0;
      for (k=0;k<ndim;k++){
        C[i*ndim+j]+=A[i*ndim+k]*B[k*ndim+j];
      }
    }
  }
}


static void MtimesM_complex (int ndim, double *A,dplx *B,dplx *C){
  int
    i,j,k;

  for ( i = 0 ; i< ndim ; i++ ){
    for ( j = 0 ; j < ndim ; j++){
      C[i*ndim+j]=0.0;
      for (k=0;k<ndim;k++){
        C[i*ndim+j]+=A[i*ndim+k]*B[k*ndim+j];
      }
    }
  }
}

static void invsqrtM (int ndim, double *A, double *B){
/* B is A^-1/2 */
  int
    i;
  dplx
    *wmatrix,*V,*Vt,*temp,*newA,tmp;
  double
    *w;
  snew(newA,ndim*ndim);
  snew(w,ndim);
  snew(wmatrix,ndim*ndim);
  snew(V,ndim*ndim);
  snew(Vt,ndim*ndim);
  snew(temp,ndim*ndim);
  /* diagonalize */

  for(i=0;i<ndim*ndim;i++){
    newA[i]=A[i];
  };

  diag(ndim,w,V,newA);
//  eigensolver(A,ndim,0,ndim,w,V);

  
  /* take the inverse square root of the diagonalo elements */
  for ( i = 0 ; i < ndim ; i++){
    wmatrix[i*ndim+i] = 1.0/csqrt(w[i]);
  }
  /* Apparently diag (zheev) returns the eigenvectors as rows, not
   * columns. So tranposing is required and conj(V) is v^\dagger = v^{-1} 
   * 
   */
  /* take the conjugate of V */
  for(i=0;i<ndim*ndim;i++){
    tmp=V[i];
    V[i]=conj(tmp);
  }
  
  dagger(ndim, Vt, V);

  /* multiple from the left by Vt */
  M_complextimesM_complex(ndim,Vt,wmatrix,temp);

  /* and from the right by V */
  M_complextimesM_complex(ndim,temp,V,wmatrix);
  for (i = 0 ; i< ndim*ndim ; i++ ){
    B[i] = creal(wmatrix[i]);
  }
  free(Vt);
  free(V);
  free(w);
  free(wmatrix);
  free(temp);
  free(newA);
}

static void invsqrtM_complex(int ndim, dplx *A, dplx *B){
/* B is A^-1/2 */
  int
    i;
  dplx
    *wmatrix,*V,*Vt,*temp,*newA,tmp;
  double
    *w;
  snew(newA,ndim*ndim);
  snew(w,ndim);
  snew(wmatrix,ndim*ndim);
  snew(V,ndim*ndim);
  snew(Vt,ndim*ndim);
  snew(temp,ndim*ndim);

  /* diagonalize */
  for(i=0;i<ndim*ndim;i++){
    newA[i]=A[i];
  };
  diag(ndim,w,V,newA);

  /* take the inverse square root of the diagonalo elements */
  for ( i = 0 ; i < ndim ; i++){
    wmatrix[i*ndim+i] = 1.0/csqrt(w[i]);
  }
  /* Apparently diag (zheev) returns the eigenvectors as rows, not
   * columns. So tranposing is required and conj(V) is v^\dagger = v^{-1} 
   * 
   */
  /* take the conjugate of V */
  for(i=0;i<ndim*ndim;i++){
    tmp=V[i];
    V[i]=conj(tmp);
  }
  dagger(ndim, Vt, V);
  
  /* multiple from the left by Vt */
  M_complextimesM_complex(ndim,Vt,wmatrix,temp);
  
  /* and from the right by V */
  M_complextimesM_complex(ndim,temp,V,wmatrix);
  //  fprintf(stderr,"in invsqrtM_complex\n");
  //  printM_complex(ndim,wmatrix);
  
  for (i = 0 ; i< ndim*ndim ; i++ ){
    B[i] = wmatrix[i];
  }
  free(Vt);
  free(V);
  free(w);
  free(wmatrix);
  free(temp);
  free(newA);
}

static void expM_complex(int ndim, dplx *A, dplx *expA){
  /* expA = exp(A)*/
  int
    i,j;
  dplx
    *wmatrix,*V,*Vt,*temp;
  dplx
    *w;
  snew(w,ndim);
  snew(wmatrix,ndim*ndim);
  snew(V,ndim*ndim);
  snew(Vt,ndim*ndim);
  snew(temp,ndim*ndim);



  /* diagonalize */
  /* H shoudl be hermitian, so I hope it actually is... */
// diag(ndim,w,V,A);
  diag_complex(ndim,w,V,A);

  for ( i = 0 ; i < ndim ; i++){
    wmatrix[i*ndim+i] = cexp((w[i]));
  }
//fprintf(stderr,"expM_complex\n");
//  printM_complex(ndim,V);

//  fprintf(stderr,"expM_complex\n");
//  printM_complex(ndim,wmatrix);
  dagger(ndim,Vt,V);
  M_complextimesM_complex(ndim,wmatrix,V,temp);
  M_complextimesM_complex(ndim,Vt,temp,expA);

//fprintf(stderr,"M_complextimesM_complex\n");
//  printM_complex(ndim,expA);


  free(Vt);
  free(V);
  free(w);
  free(wmatrix);
  free(temp);
}

static void expM_complex2(int ndim, dplx *A, dplx *expA,double dt){
  /* exp(-0.5*I*dt*A), Hatree, equation B11 in JCP 114 10808 (2001) */
  /* NOTE this divides by two and thus assumes that there hamiltonian
   * is sum of Hamiltonians at t and t+dt
   */
  int
    i,j;
  dplx
    *wmatrix,*V,*Vt,*temp,tmp;
  double
    *w;
  snew(w,ndim);
  snew(wmatrix,ndim*ndim);
  snew(V,ndim*ndim);
  snew(Vt,ndim*ndim);
  snew(temp,ndim*ndim);



  /* diagonalize */
  /* H shoudl be hermitian, so I hope it actually is... */
  diag(ndim,w,V,A);
//   diag_complex(ndim,w,V,A);

  for ( i = 0 ; i < ndim ; i++){
    wmatrix[i*ndim+i] = cexp((-0.5*IMAG*dt/AU2PS*w[i]));
  }
  //  fprintf(stderr,"expM_complex v:\n");
  //  printM_complex(ndim,V);
//
//fprintf(stderr,"expM_complex: wmatrix\n");
//printM_complex(ndim,wmatrix);

  /* I don't quite understand why, but this order i.e. taking complex conjugate
   * of V, compute vt and then Vt.wmatrix.V gives
   * results that are in agreement with Mathematica MatrixExp[]
   * Apparently diag (zheev) returns the eigenvectors as rows, not
   * columns. So tranposing is required and conj(V) is v^\dagger = v^{-1} 
   * 
   */
  /* take the conjugate of V */
  for(i=0;i<ndim*ndim;i++){
    tmp=V[i];
    V[i]=conj(tmp);
  }

  dagger(ndim,Vt,V);
  M_complextimesM_complex(ndim,wmatrix,V,temp);
  M_complextimesM_complex(ndim,Vt,temp,expA);
//
//fprintf(stderr,"M_complextimesM_complex, expA: \n");
//printM_complex(ndim,expA);
//
//
  free(Vt);
  free(V);
  free(w);
  free(wmatrix);
  free(temp);
} /*expM_complex2 */

static void inverseM_complex(int n, dplx *M, dplx *Minv){
  /* Vinv is the inverse of complex, non-unitary matrix V. We use zgetrf() & zgetri()
   */
    char
    jobz, uplo,jobvl,jobvr;
  int
    i, j, lwork, info,*ipiv;
  double
    *rwork;
#ifdef MKL
  /* If compilation with MKL linear algebra libraries  */
    MKL_Complex16
      *M_mkl,*w_mkl, *work,*vr,*vl;
#else
  /* else compilation with lapack linear algebra libraries  */
    lapack_complex_double
      *M_mkl, *w_mkl,*work,*vr,*vl;
#endif
    
  lwork = 2*n;
  jobvl = 'N';
  jobvr = 'V';
  snew(vr,n*n);
  snew(vl,n*n);

  snew(M_mkl, n*n);
  snew(w_mkl,n);
  snew(work, lwork);
  snew(rwork, 3*n);
  snew(ipiv,n);

  for (i=0; i<n; i++){
    for (j=0; j<n; j++){
#ifdef MKL
      M_mkl[j + i*n].real = creal(M[j + i*n]);
      M_mkl[j + i*n].imag = cimag(M[j + i*n]);
#else
      M_mkl[j + i*n] = M[j + i*n];
#endif
    }
  }
#ifdef MKL
  zgetrf(&n, &n,M_mkl, &n, ipiv, &info);
  if (info != 0){
    gmx_fatal(FARGS, "Lapack returned error code: %d in zgetrf", info);
  }
  zgetri(&n, M_mkl, &n, ipiv ,work, &lwork, &info);
  if (info != 0){
    gmx_fatal(FARGS, "Lapack returned error code: %d in zgetri", info);
  }
#else
  F77_FUNC(zgetrf,ZGETRF)(&n, &n,M_mkl, &n, ipiv, &info);
  if (info != 0){
    gmx_fatal(FARGS, "Lapack returned error code: %d in zgetrf", info);
  }
  F77_FUNC(zgetri,ZGETRI)(&n, M_mkl, &n, ipiv ,work, &lwork, &info);
  if (info != 0){
    gmx_fatal(FARGS, "Lapack returned error code: %d in zgetri", info);
  }
#endif
  for (i=0; i<n; i++){
    for (j=0; j<n; j++){
#ifdef MKL
      Minv[j + i*n] = M_mkl[j + i*n].real + IMAG*M_mkl[j + i*n].imag;
#else
      Minv[j + i*n] = creal(M_mkl[j + i*n]) + IMAG*cimag(M_mkl[j + i*n]);
#endif
    }
  }
  sfree(vl);
  sfree(vr);
  sfree(rwork);
  sfree(M_mkl);
  sfree(w_mkl);
  sfree(work);
  sfree(ipiv);
} /*inverse_M */


static void expM_non_herm(int ndim, dplx *A, dplx *B,double dt){
  /* exp(-0.5*I*dt*A), where A is non-hermitian */
  /* NOTE this divides by two and thus assumes that there hamiltonian
   * is sum of Hamiltonians at t and t+dt. and that the losses are doubled.
   *
   * exp[A]  = U exp[w] U^-1 where w are the eigenvalues and U the matrix of (right) eigenvectors
   *
   */
  int
    i,j;
  dplx
    *wmatrix,*V,*Vt,*Vinv,*temp,*w;
  
  snew(w,ndim);
  snew(wmatrix,ndim*ndim);
  snew(V,ndim*ndim);
  snew(Vt,ndim*ndim);
  snew(Vinv,ndim*ndim);
  snew(temp,ndim*ndim);

  /* diagonalize A with zgeev  */
   diag_complex(ndim,w,V,A);
   
  
  

  for ( i = 0 ; i < ndim ; i++){
    wmatrix[i*ndim+i] = cexp((-0.5*IMAG*dt/AU2PS*w[i]));
  }
           //fprintf(stderr,"expM_complex\n");
           //  printM_complex(ndim,V);
//
//  fprintf(stderr,"expM_inverse, A matrix is:\n");
//  printM_complex(ndim,A);
  dagger(ndim,Vt,V);

  /* now, we need the inverse of the matrix V
   */
  //fprintf(stderr,"expM_inverse, V matrix is:\n");
  //  printM_complex(ndim,A);
  /* checking the results with mathematica suggests that this the "correct"
   * way. I suspect that also here the matrix is fed as its trapose, and 
   * like for the exp functions, we can use V^-1 Exp[d] V
   */
  inverseM_complex(ndim,V,Vinv);
  
  M_complextimesM_complex(ndim,wmatrix,V,temp);
  M_complextimesM_complex(ndim,Vinv,temp,B);
  //  fprintf(stderr,"M_complextimesM_complex\n");
  // printM_complex(ndim,B);
//
//
  free(Vt);
  free(V);
  free(w);
  free(wmatrix);
  free(temp);
} /*expM_non_herm */

static void printM  ( int ndim, double *A){
  int 
    i,j;
  for(i=0;i<ndim;i++){
    for (j=0;j<ndim;j++){
      fprintf(stderr,"%lf ",A[i*ndim+j]);
    }
    fprintf(stderr,"\n\n");
  }
}


/* Hack to read in the previous eigenvectors for simulations in the adiabatic basis, 
 * if these are provided in the same format as for the analyse_transport.c
 */

static void check_prev_eigvec(t_QMrec *qm,int ndim){

  int
    j,k,m;
  char
    *buf=NULL,*token;
  double
    *all=NULL;
  FILE
    *EVin;

  EVin= fopen("ev.dat","r");
  if(EVin){
    fprintf(stderr,"Reading in the eigenvectors of step -1 from ev.dat, setting qm->restart=TRUE\n");
    qm->restart = 1;
    snew(buf,(ndim*15+100)*2);
    snew(all,2*(1+ndim));
    for(j=0;j<ndim;j++){
      if(!fgets (buf,2*((ndim+1)*15+100),EVin)){
        fprintf(stderr,"error reading %s\n","ev.dat");
        break;
      }
      token = strtok(buf," ");
      m=0;
      while (token != NULL){
        sscanf(token,"%lf",&all[m++]);
        token=strtok(NULL," ");
      }
      for(k=0;k<ndim;k++){
        qm->eigvec[ndim*j+k]=all[2*k+2]+IMAG*all[2*k+3];
      }
    }
    free(all);
    free(buf);
    fclose(EVin);
  }
  else{
    qm->restart = 0;
  }
} /* check_prev_eigvec */

/* integrate wavefunction for one MD timestep */
/* as before use the time-evolution operator.
 * relies on the the Intel MKL Lapack implementation.
 * this could be changed by adding some of the complex
 * Lapack routines to the Gromacs Lapack.
 */
static void  propagate_local_dia(int ndim,double dt, dplx *C, dplx *vec,
				 dplx *vecold, double *QMener, 
				 double *QMenerold,dplx *U){
  int
    i,j,k;
  double
    *E;
  dplx
    *S,*SS, *transposeS;
  dplx
    *invsqrtSS,*T,*transposeT,*ham,*transposeham;
  dplx
    *H,*expH,*ctemp;

  snew(S,ndim*ndim);
  snew(SS,ndim*ndim);
  snew(invsqrtSS,ndim*ndim);
  snew(transposeS,ndim*ndim);
  snew(T,ndim*ndim);
  snew(transposeT,ndim*ndim);
  snew(E,ndim*ndim);
  snew(ham,ndim*ndim);
  snew(transposeham,ndim*ndim);
  snew(H, ndim*ndim);
  snew(expH, ndim*ndim);
  snew(ctemp,ndim);

  for (i = 0 ; i < ndim ; i++){
    E[i*ndim+i]    = QMener[i];
    for ( j = 0 ; j < ndim ; j++ ){
      for ( k = 0 ; k < ndim ; k++ ){
	S[i*ndim+j] += conj(vecold[i*ndim+k])*vec[j*ndim+k];
      }
    }
  }

//
//  fprintf(stderr,"\n\nFrom propagate_local_dia, E matrix elements:\n");
//  printM(ndim,E);
//  fprintf(stderr,"\n\nFrom propagate_local_dia, S matrix elements:\n");
//  printM_complex(ndim,S);
//

  /* build S^dagger S */

  transposeM_complex(ndim,S,transposeS);

//
//  fprintf(stderr,"\n\nFrom propagate_local_dia, transposeS matrix elements:\n");
//  printM_complex(ndim,transposeS);
//

  M_complextimesM_complex(ndim,transposeS,S,SS);

//
//   fprintf(stderr,"\n\nFrom propagate_local_dia, SS matrix elements:\n");
//   printM_complex(ndim,SS);
//

  invsqrtM_complex(ndim,SS,invsqrtSS);


  //  fprintf(stderr,"\n\nFrom propagate_local_dia, invsqrtSS matrix elements:\n");
  // printM_complex(ndim,invsqrtSS);
//

  M_complextimesM_complex(ndim,S,invsqrtSS,T);

//
//  fprintf(stderr,"\n\nFrom propagate_local_dia, T matrix elements:\n");
//printM_complex(ndim,T);
//

  transposeM_complex(ndim,T,transposeT);

//
//  fprintf(stderr,"\n\nFrom propagate_local_dia, transposeT matrix elements:\n");
//  printM_complex(ndim,transposeT);
//
//  fprintf(stderr,"testing if T is unitary\n");
//  M_complextimesM_complex(ndim,transposeT,T,ham);
//  printM_complex(ndim,ham); 
  /* reuse invsqrtS */

  MtimesM_complex(ndim,E,transposeT,invsqrtSS);

//
//  fprintf(stderr,"\n\nFrom propagate_local_dia, invsqrtSS (E times transposeT)  matrix elements:\n");
//  printM_complex(ndim,invsqrtSS);
//
 
   M_complextimesM_complex(ndim,T,invsqrtSS,ham);

//

//
 
  for ( i = 0 ; i< ndim; i++){
    ham[i+ndim*i] = creal(ham[i+ndim*i]) + QMenerold[i] + IMAG*cimag(ham[i+ndim*i]);
  }

  //  fprintf(stderr,"\n\nFrom propagate_local_dia, ham matrix elements:\n");
  //  printM_complex(ndim,ham);
  
  expM_complex2(ndim,ham,expH, dt);

  //  fprintf(stderr,"\n\nFrom propagate_local_dia, H matrix elements:\n");
  //printM_complex(ndim,expH);
//  MtimesM_complex(ndim,transposeT,expH,U);
  M_complextimesM_complex(ndim,transposeT,expH,U);

//
//fprintf(stderr,"\n\nFrom propagate_local_dia, U matrix elements:\n");
//printM_complex(ndim,U);
//
 
  /* we use U to propagate C, and keep it to compute the hopping
   * probabilities */
 // printM_complex(ndim,U);
  MtimesV_complex(ndim,U,C,ctemp);
  for( i=0;i<ndim;i++){
      C[i]=ctemp[i];
  }

//
//  fprintf(stderr,"From propagate_local_dia, C matrix elements:\n");
//  for(i=0;i<ndim;i++){
//    fprintf(stderr,"C[%d]=%lf+%lfI ",i,creal(C[i]),cimag(C[i]));
//  }
//  fprintf(stderr,"\n");

  free(H);
  free(ham);
  free(transposeham);
  free(T);
  free(transposeT);
  free(S);
   free (SS);
  free(invsqrtSS);
  free(transposeS);
  free(E);
  free(expH);
  free(ctemp);
} 

static void propagate(int dim, double dt, dplx *C, dplx *vec, dplx *vecold, double *QMener, double *QMenerold)
{
  int 
    i, j;
  int 
    n = dim;
  double 
    sum;
  double 
    *w;
  dplx 
    *H, *V, *Vt, *t;

/* build A matrix */
  snew(H, n*n);
  snew(w, n);
  snew(V, n*n);
  snew(Vt, n*n);
  snew(t, n);
  for (i=0; i<n; i++)
  {
    for (j=0; j<n; j++)
    {
      H[j + i*n] = -IMAG*calc_coupling_complex(j, i, dt/AU2PS, n, vec, vecold);
    }
    H[i + i*n] += (QMener[i] + QMenerold[i]) / 2.;
  }
// fprintf(stderr,"\n\nFrom propagate, EFFECTIVE HAMILTONIAN \n");
// printM_complex(n,H);
//fflush(stderr);

  diag(n, w, V, H);
// fprintf(stderr,"\n\n From propagate, EIGENVALUES \n");
// for (i=0; i<qm->nstates; i++){
//   fprintf(stderr,"%.5f\t ",w[i]);
// }       
// fprintf(stderr,"\n");               
// fflush(stderr);

// fprintf(stderr,"EIGENVECTORS \n");
// for (i=0; i<qm->nstates; i++){
//   for (j=0; j<qm->nstates; j++){
//     fprintf(stderr,"%.5f %.5f ",creal(V[j + i*n]),cimag(V[j + i*n]));
//   }
//   fprintf(stderr,"\n");
// }                      
// fflush(stderr);*/

  dagger(n, Vt, V);
  multmv(n, t, Vt, C);
  multexpdiag(n, C, -IMAG*dt/AU2PS, w, t);
  multmv(n, t, V, C);

  for (i=0; i<n; i++){
    C[i] = t[i];
  }

  sfree(H);
  sfree(w);
  sfree(V);
  sfree(Vt);
  sfree(t);
}

void eigensolver(real *a, int n, int index_lower, int index_upper, real *eigenvalues, real *eigenvectors){
  int 
    lwork,liwork,il,iu,m,iw0,info;
  int
    *isuppz,*iwork;
  real   
    w0,abstol,vl,vu;
  real
    *work;
  const char 
    *jobz;
//eigensolver(matrix,ndim,0,ndim,eigval,eigvec);    
  if(index_lower<0)
    index_lower = 0;
  
  if(index_upper>=n)
    index_upper = n-1;
    
  /* Make jobz point to the character "V" if eigenvectors
   * should be calculated, otherwise "N" (only eigenvalues).
   */   
  jobz = (eigenvectors != NULL) ? "V" : "N";

  /* allocate lapack stuff */
  snew(isuppz,2*n);
  vl = vu = 0;
    
  /* First time we ask the routine how much workspace it needs */
  lwork  = -1;
  liwork = -1;
  abstol = 0;
    
  /* Convert indices to fortran standard */
  index_lower++;
  index_upper++;
    
  /* Call LAPACK routine using fortran interface. Note that we use upper storage,
   * but this corresponds to lower storage ("L") in Fortran.
   */    
#ifdef GMX_DOUBLE
  F77_FUNC(dsyevr,DSYEVR)(jobz,"I","L",&n,a,&n,&vl,&vu,&index_lower,&index_upper,
                          &abstol,&m,eigenvalues,eigenvectors,&n,
                          isuppz,&w0,&lwork,&iw0,&liwork,&info);
#else
  F77_FUNC(ssyevr,SSYEVR)(jobz,"I","L",&n,a,&n,&vl,&vu,&index_lower,&index_upper,
                          &abstol,&m,eigenvalues,eigenvectors,&n,
                          isuppz,&w0,&lwork,&iw0,&liwork,&info);
#endif

  if(info != 0){
    sfree(isuppz);
    gmx_fatal(FARGS,"Internal errror in LAPACK diagonalization.");        
  }
    
  lwork = w0;
  liwork = iw0;
    
  snew(work,lwork);
  snew(iwork,liwork);
   
  abstol = 0;
    
#ifdef GMX_DOUBLE
  F77_FUNC(dsyevr,DSYEVR)(jobz,"I","L",&n,a,&n,&vl,&vu,&index_lower,&index_upper,
                          &abstol,&m,eigenvalues,eigenvectors,&n,
                          isuppz,work,&lwork,iwork,&liwork,&info);
#else
  F77_FUNC(ssyevr,SSYEVR)(jobz,"I","L",&n,a,&n,&vl,&vu,&index_lower,&index_upper,
                          &abstol,&m,eigenvalues,eigenvectors,&n,
                          isuppz,work,&lwork,iwork,&liwork,&info);
#endif
    
  sfree(isuppz);
  sfree(work);
  sfree(iwork);
    
  if(info != 0){
    gmx_fatal(FARGS,"Internal errror in LAPACK diagonalization.");
  }
}


#ifdef GMX_MPI_NOT
void sparse_parallel_eigensolver(gmx_sparsematrix_t *A, int neig, real *eigenvalues, real *eigenvectors, int maxiter){
  int
    iwork[80],iparam[11],ipntr[11];
  real 
    *resid,*workd,*workl,*v;
  int      
    n,ido,info,lworkl,i,ncv,dovec,iter,nnodes,rank;
  real
    abstol;
  int *
    select;

  MPI_Comm_size( MPI_COMM_WORLD, &nnodes );
  MPI_Comm_rank( MPI_COMM_WORLD, &rank );
	
  if(eigenvectors != NULL)
    dovec = 1;
  else
    dovec = 0;
    
  n   = A->nrow;
  ncv = 2*neig;
    
  if(ncv>n)
    ncv=n;
    
  for(i=0;i<11;i++)
        iparam[i]=ipntr[i]=0;
	
	iparam[0] = 1;       /* Don't use explicit shifts */
	iparam[2] = maxiter; /* Max number of iterations */
	iparam[6] = 1;       /* Standard symmetric eigenproblem */
    
	lworkl = ncv*(8+ncv);
    snew(resid,n);
    snew(workd,(3*n+4));
    snew(workl,lworkl);
    snew(select,ncv);
    snew(v,n*ncv);
	
    /* Use machine tolerance - roughly 1e-16 in double precision */
    abstol = 0;
    
 	ido = info = 0;
    fprintf(stderr,"Calculation Ritz values and Lanczos vectors, max %d iterations...\n",maxiter);
    
    iter = 1;
	do {
#ifdef GMX_DOUBLE
		F77_FUNC(pdsaupd,PDSAUPD)(&ido, "I", &n, "SA", &neig, &abstol, 
								  resid, &ncv, v, &n, iparam, ipntr, 
								  workd, iwork, workl, &lworkl, &info);
#else
		F77_FUNC(pssaupd,PSSAUPD)(&ido, "I", &n, "SA", &neig, &abstol, 
								  resid, &ncv, v, &n, iparam, ipntr, 
								  workd, iwork, workl, &lworkl, &info);
#endif
        if(ido==-1 || ido==1)
            gmx_sparsematrix_vector_multiply(A,workd+ipntr[0]-1, workd+ipntr[1]-1);
        
        fprintf(stderr,"\rIteration %4d: %3d out of %3d Ritz values converged.",iter++,iparam[4],neig);
	} while(info==0 && (ido==-1 || ido==1));
	
    fprintf(stderr,"\n");
	if(info==1){
	    gmx_fatal(FARGS,
                  "Maximum number of iterations (%d) reached in Arnoldi\n"
                  "diagonalization, but only %d of %d eigenvectors converged.\n",
                  maxiter,iparam[4],neig);
	}
	else if(info!=0){
        gmx_fatal(FARGS,"Unspecified error from Arnoldi diagonalization:%d\n",info);
	}
	
	info = 0;
	/* Extract eigenvalues and vectors from data */
    fprintf(stderr,"Calculating eigenvalues and eigenvectors...\n");
    
#ifdef GMX_DOUBLE
    F77_FUNC(pdseupd,PDSEUPD)(&dovec, "A", select, eigenvalues, eigenvectors, 
							  &n, NULL, "I", &n, "SA", &neig, &abstol, 
							  resid, &ncv, v, &n, iparam, ipntr, 
							  workd, workl, &lworkl, &info);
#else
    F77_FUNC(psseupd,PSSEUPD)(&dovec, "A", select, eigenvalues, eigenvectors, 
							  &n, NULL, "I", &n, "SA", &neig, &abstol, 
							  resid, &ncv, v, &n, iparam, ipntr, 
							  workd, workl, &lworkl, &info);
#endif
	
    sfree(v);
    sfree(resid);
    sfree(workd);
    sfree(workl);  
    sfree(select);    
}
#endif


void 
sparse_eigensolver(gmx_sparsematrix_t *    A,
                   int                     neig,
                   real *                  eigenvalues,
                   real *                  eigenvectors,
                   int                     maxiter)
{
    int      iwork[80];
    int      iparam[11];
    int      ipntr[11];
    real *   resid;
    real *   workd;
    real *   workl;
    real *   v;
    int      n;
    int      ido,info,lworkl,i,ncv,dovec;
    real     abstol;
    int *    select;
    int      iter;
    
#ifdef GMX_MPI_NOT
	MPI_Comm_size( MPI_COMM_WORLD, &n );
	if(n > 1)
	{
		sparse_parallel_eigensolver(A,neig,eigenvalues,eigenvectors,maxiter);
		return;
	}
#endif
	
    if(eigenvectors != NULL)
        dovec = 1;
    else
        dovec = 0;
    
    n   = A->nrow;
    ncv = 2*neig;
    
    if(ncv>n)
        ncv=n;
    
    for(i=0;i<11;i++)
        iparam[i]=ipntr[i]=0;
	
	iparam[0] = 1;       /* Don't use explicit shifts */
	iparam[2] = maxiter; /* Max number of iterations */
	iparam[6] = 1;       /* Standard symmetric eigenproblem */
    
	lworkl = ncv*(8+ncv);
    snew(resid,n);
    snew(workd,(3*n+4));
    snew(workl,lworkl);
    snew(select,ncv);
    snew(v,n*ncv);

    /* Use machine tolerance - roughly 1e-16 in double precision */
    abstol = 0;
    
 	ido = info = 0;
    fprintf(stderr,"Calculation Ritz values and Lanczos vectors, max %d iterations...\n",maxiter);
    
    iter = 1;
	do {
#ifdef GMX_DOUBLE
            F77_FUNC(dsaupd,DSAUPD)(&ido, "I", &n, "SA", &neig, &abstol, 
                                    resid, &ncv, v, &n, iparam, ipntr, 
                                    workd, iwork, workl, &lworkl, &info);
#else
            F77_FUNC(ssaupd,SSAUPD)(&ido, "I", &n, "SA", &neig, &abstol, 
                                    resid, &ncv, v, &n, iparam, ipntr, 
                                    workd, iwork, workl, &lworkl, &info);
#endif
        if(ido==-1 || ido==1)
            gmx_sparsematrix_vector_multiply(A,workd+ipntr[0]-1, workd+ipntr[1]-1);
        
        fprintf(stderr,"\rIteration %4d: %3d out of %3d Ritz values converged.",iter++,iparam[4],neig);
	} while(info==0 && (ido==-1 || ido==1));
	
    fprintf(stderr,"\n");
	if(info==1)
    {
	    gmx_fatal(FARGS,
                  "Maximum number of iterations (%d) reached in Arnoldi\n"
                  "diagonalization, but only %d of %d eigenvectors converged.\n",
                  maxiter,iparam[4],neig);
    }
	else if(info!=0)
    {
        gmx_fatal(FARGS,"Unspecified error from Arnoldi diagonalization:%d\n",info);
    }
	
	info = 0;
	/* Extract eigenvalues and vectors from data */
    fprintf(stderr,"Calculating eigenvalues and eigenvectors...\n");
    
#ifdef GMX_DOUBLE
    F77_FUNC(dseupd,DSEUPD)(&dovec, "A", select, eigenvalues, eigenvectors, 
			    &n, NULL, "I", &n, "SA", &neig, &abstol, 
			    resid, &ncv, v, &n, iparam, ipntr, 
			    workd, workl, &lworkl, &info);
#else
    F77_FUNC(sseupd,SSEUPD)(&dovec, "A", select, eigenvalues, eigenvectors, 
			    &n, NULL, "I", &n, "SA", &neig, &abstol, 
			    resid, &ncv, v, &n, iparam, ipntr, 
			    workd, workl, &lworkl, &info);
#endif
	
    sfree(v);
    sfree(resid);
    sfree(workd);
    sfree(workl);  
    sfree(select);    
}
/* end of eigensolver code */


/* TODO: this should be made thread-safe */

/* Gaussian interface routines */
void init_gaussian(t_commrec *cr, t_QMrec *qm, t_MMrec *mm){
  FILE    
    *rffile=NULL,*out=NULL,*Cin=NULL,*Din=NULL;
  ivec
    basissets[eQMbasisNR]={{0,3,0},
			   {0,3,0},/*added for double sto-3g entry in names.c*/
			   {5,0,0},
			   {5,0,1},
			   {5,0,11},
			   {5,6,0},
			   {1,6,0},
			   {1,6,1},
			   {1,6,11},
			   {4,6,0}};
  char
    *buf;
  int
    i,ndim=1,seed;
  
  /* using the ivec above to convert the basis read form the mdp file
   * in a human readable format into some numbers for the gaussian
   * route. This is necessary as we are using non standard routes to
   * do SH.
   */

  /* per layer we make a new subdir for integral file, checkpoint
   * files and such. These dirs are stored in the QMrec for
   * convenience 
   */

  
  if(!qm->nQMcpus){ /* this we do only once per layer 
		     * as we call g01 externally 
		     */

    for(i=0;i<DIM;i++)
      qm->SHbasis[i]=basissets[qm->QMbasis][i];

  /* init gradually switching on of the SA */
      qm->SAstep = 0;
  /* we read the number of cpus and environment from the environment
   * if set.  
   */
      snew(buf,20);
      buf = getenv("NCPUS");
      if (buf)
        sscanf(buf,"%d",&qm->nQMcpus);
      else
        qm->nQMcpus=1;
      fprintf(stderr,"number of CPUs for gaussian = %d\n",qm->nQMcpus);
      snew(buf,50);
      buf = getenv("MEM");
      if (buf)
        sscanf(buf,"%d",&qm->QMmem);
      else
        qm->QMmem=50000000;
      fprintf(stderr,"memory for gaussian = %d\n",qm->QMmem);
      snew(buf,30);
      buf = getenv("ACC");
    if (buf)
      sscanf(buf,"%d",&qm->accuracy);
    else
      qm->accuracy=8;  
    fprintf(stderr,"accuracy in l510 = %d\n",qm->accuracy); 
    snew(buf,30);
    buf = getenv("CPMCSCF");
    if (buf)
	{
		sscanf(buf,"%d",&i);
		qm->cpmcscf = (i!=0);
	}
	else
      qm->cpmcscf=FALSE;
    if (qm->cpmcscf)
      fprintf(stderr,"using cp-mcscf in l1003\n");
    else
      fprintf(stderr,"NOT using cp-mcscf in l1003\n"); 
    snew(buf,50);
    buf = getenv("SASTEP");
    if (buf)
      sscanf(buf,"%d",&qm->SAstep);
    else
      /* init gradually switching on of the SA */
      qm->SAstep = 0;
    /* we read the number of cpus and environment from the environment
     * if set.  
     */
    fprintf(stderr,"Level of SA at start = %d\n",qm->SAstep);
        

    /* punch the LJ C6 and C12 coefficients to be picked up by
     * gaussian and usd to compute the LJ interaction between the
     * MM and QM atoms.
     */
    if(qm->bTS||qm->bOPT){
      out = fopen("LJ.dat","w");
      for(i=0;i<qm->nrQMatoms;i++){

#ifdef GMX_DOUBLE
	fprintf(out,"%3d  %10.7lf  %10.7lf\n",
		qm->atomicnumberQM[i],qm->c6[i],qm->c12[i]);
#else
	fprintf(out,"%3d  %10.7f  %10.7f\n",
		qm->atomicnumberQM[i],qm->c6[i],qm->c12[i]);
#endif
      }
      fclose(out);
    }
    /* gaussian settings on the system */
    snew(buf,200);
    buf = getenv("GAUSS_DIR");

    if (buf){
      snew(qm->gauss_dir,200);
      sscanf(buf,"%s",qm->gauss_dir);
    }
    else
      gmx_fatal(FARGS,"no $GAUSS_DIR, check gaussian manual\n");
    
    snew(buf,200);    
    buf = getenv("GAUSS_EXE");
    if (buf){
      snew(qm->gauss_exe,200);
      sscanf(buf,"%s",qm->gauss_exe);
    }
    else
      gmx_fatal(FARGS,"no $GAUSS_EXE, check gaussian manual\n");
    
    snew(buf,200);
    buf = getenv("DEVEL_DIR");
    if (buf){
      snew(qm->devel_dir,200);
      sscanf(buf,"%s",qm->devel_dir);
    }
    else
      gmx_fatal(FARGS,"no $DEVEL_DIR, this is were the modified links reside.\n");


    if(qm->bQED){
      fprintf(stderr,"\nDoing QED");
      /* prepare for a cavity QED MD run. Obviously only works with QM/MM */
      if(MULTISIM(cr)){
        fprintf(stderr,"doing parallel; ms->nsim, ms_>sim = %d,%d\n", 
                cr->ms->nsim,cr->ms->sim);
        snew(buf,3000);
        buf = getenv("TMP_DIR");
        if (buf){
          snew(qm->subdir,3000);
          /* store the nodeid as the subdir */
          sprintf(qm->subdir,"%s%s%d",buf,"/molecule",cr->ms->sim);
          /* and create the directoru on the FS */
          sprintf(buf,"%s %s","mkdir",qm->subdir);
          system(buf);
          ndim=cr->ms->nsim;
        }
        else
          gmx_fatal(FARGS,"no $TMP_DIR, this is were the temporary in/output is written.\n");
      }
      else{
        snew(buf,3000);
        buf = getenv("TMP_DIR");
        if (buf){
          snew(qm->subdir,3000);
          sprintf(qm->subdir,"%s%s%d",buf,"/molecule",0);
          sprintf(buf,"%s %s","mkdir",qm->subdir);
          system(buf);
        }
        else
          gmx_fatal(FARGS,"no $TMP_DIR, this is were the temporary in/output is written.\n");
      }
      /* now deterimin the actual size of ndim */
      ndim+=qm->n_max-qm->n_min+1;
      snew(qm->creal,ndim);
      snew(qm->cimag,ndim);
      snew(qm->dreal,ndim);
      snew(qm->dimag,ndim);
      snew(qm->matrix,ndim*ndim);
      snew(qm->eigvec,ndim*ndim);
      snew(qm->eigval,ndim);

      /* hack to read in previous eigenvector. To use that there should be an ev.dat, created by
       * sed 's/\I//g' eigenvectors.dat |sed 's/\+//g' | awk '{$1=$2=$3=$4=$5=$6=$7=$10=""; print $0}'
       */
      check_prev_eigvec(qm,ndim);
      
      Cin=fopen("C.dat","r");
      Din=fopen("D.dat","r");
      /* C.dat is read first, if there is no C.dat,check if there is D.dat. 
       * Otherwise set all to zero
       */
      if (Cin){
        fprintf(stderr,"reading coefficients from C.dat\n");
        if(NULL == fgets(buf,3000,Cin)){
          gmx_fatal(FARGS,"Error reading C.dat");
        }
	sscanf(buf,"%d\n",&seed);
        fprintf(stderr,"setting randon seed to %d\n",seed);
        for(i=0;i<ndim;i++){
          if(NULL == fgets(buf,3000,Cin)){
            gmx_fatal(FARGS,"Error reading C.dat, no expansion coeficient");
          }
	  sscanf(buf,"%lf %lf",&qm->creal[i],&qm->cimag[i]);
        }
        /* rho 0, only population 
	 */
        if(NULL == fgets(buf,3000,Cin)){
          gmx_fatal(FARGS,"Error reading C.dat: no rho0");
        }
        sscanf(buf,"%lf",&qm->groundstate);
        /* print for security 
	 */  
        int print = 1;
        if(MULTISIM(cr)){
          if (cr->ms->sim!=0){
            print = 0;
          }
        } 
        if(print){ 
          fprintf (stderr,"coefficients\nC*C: ");
          for(i=0;i<ndim;i++){
            fprintf(stderr,"%lf ",conj(qm->creal[i]+IMAG*qm->cimag[i])*(qm->creal[i]+IMAG*qm->cimag[i]));
          }
          fprintf(stderr,"%lf ",qm->groundstate);
        }
	fclose(Cin);
      }
      /* check if there is also D.dat 
       */
      else if (Din){
        fprintf(stderr,"reading coefficients from D.dat\n");
        if(NULL == fgets(buf,3000,Din)){
          gmx_fatal(FARGS,"Error reading D.dat");
        }
        sscanf(buf,"%d\n",&seed);
        fprintf(stderr,"setting randon seed to %d\n",seed);
        for(i=0;i<ndim;i++){
          if(NULL == fgets(buf,3000,Din)){
            gmx_fatal(FARGS,"Error reading D.dat, no expansion coeficient");
          }
	  sscanf(buf,"%lf %lf",&qm->dreal[i],&qm->dimag[i]);
	  qm->creal[i]=0;
	  qm->cimag[i]=0;
        }
        /* rho 0, only population 
	 */
        if(NULL == fgets(buf,3000,Din)){
          gmx_fatal(FARGS,"Error reading D.dat: no rho0");
        }
        sscanf(buf,"%lf",&qm->groundstate);
        /* print for security 
	 */  
        int print = 1;
        if(MULTISIM(cr)){
          if (cr->ms->sim!=0){
            print = 0;
          }
        } 
        if(print){ 
          fprintf (stderr,"coefficients\nD*D^2: ");
          for(i=0;i<ndim;i++){
            fprintf(stderr,"%lf ",conj(qm->dreal[i]+IMAG*qm->dimag[i])*(qm->dreal[i]+IMAG*qm->dimag[i]));
          }
          fprintf(stderr,"%lf ",qm->groundstate);
        }
	fclose(Din);
      }
      /* no C.dat and no D.dat
       */
      else { 
        snew(buf,200);
        buf = getenv("SEED");
        if (buf){
          sscanf(buf,"%d",&seed);
        }
        fprintf(stderr,"no coefficients in C.dat, C[polariton]=1.0+0.0I\n");
        qm->creal[qm->polariton]=1;
        qm->groundstate=0.0;
        fprintf(stderr,"setting randon seed to %d\n",seed);
      }
      snew(qm->rnr,qm->nsteps);
      srand(seed);
      for (i=0;i< qm->nsteps;i++){
        qm->rnr[i]=(double) rand()/(RAND_MAX*1.0);
      }
      //      snew(qm->eigvec,ndim*ndim);
      //      snew(qm->eigval,ndim);
      snew(buf,3000);
      buf = getenv("WORK_DIR");
      if (buf){
        snew(qm->work_dir,3000);
        sscanf(buf,"%s",qm->work_dir);
      }
      else
        gmx_fatal(FARGS,"no $WORK_DIR, this is were the QED-specific output is written.\n");
    }
  }
  fprintf(stderr,"gaussian initialised...\n");
}  


void write_gaussian_SH_input(int step, gmx_bool swap, t_forcerec *fr, t_QMrec *qm, t_MMrec *mm){
  int
    i;
  gmx_bool
    bSA;
  FILE
    *out;
  t_QMMMrec
    *QMMMrec;

  QMMMrec = fr->qr;
  bSA = (qm->SAstep>0);

  out = fopen("input.com","w");
  /* write the route */
  fprintf(out,"%s","%scr=input\n");
  fprintf(out,"%s","%rwf=input\n");
  fprintf(out,"%s","%int=input\n");
  fprintf(out,"%s","%d2e=input\n");
/*  if(step)
 *   fprintf(out,"%s","%nosave\n");
 */
  fprintf(out,"%s","%chk=input\n");
  fprintf(out,"%s%d\n","%mem=",qm->QMmem);
  fprintf(out,"%s%3d\n","%nprocshare=",qm->nQMcpus);

  /* use the versions of
   * l301 that computes the interaction between MM and QM atoms.
   * l510 that can punch the CI coefficients
   * l701 that can do gradients on MM atoms 
   */

  /* local version */
  fprintf(out,"%s%s%s",
	  "%subst l510 ",
	  qm->devel_dir,
	  "/l510\n");
  fprintf(out,"%s%s%s",
	  "%subst l301 ",
	  qm->devel_dir,
	  "/l301\n");
  fprintf(out,"%s%s%s",
	  "%subst l701 ",
	  qm->devel_dir,
	  "/l701\n");
  
  fprintf(out,"%s%s%s",
	  "%subst l1003 ",
	  qm->devel_dir,
	  "/l1003\n");
  fprintf(out,"%s%s%s",
	  "%subst l9999 ",
	  qm->devel_dir,
	  "/l9999\n");
  /* print the nonstandard route 
   */
  fprintf(out,"%s",
	  "#P nonstd\n 1/18=10,20=1,38=1/1;\n");
  fprintf(out,"%s",
	  " 2/9=110,15=1,17=6,18=5,40=1/2;\n");
  if(mm->nrMMatoms)
    fprintf(out,
	    " 3/5=%d,6=%d,7=%d,25=1,32=1,43=1,94=-2/1,2,3;\n",
	    qm->SHbasis[0],
	    qm->SHbasis[1],
	    qm->SHbasis[2]); /*basisset stuff */
  else
    fprintf(out,
	    " 3/5=%d,6=%d,7=%d,25=1,32=1,43=0,94=-2/1,2,3;\n",
	    qm->SHbasis[0],
	    qm->SHbasis[1],
	    qm->SHbasis[2]); /*basisset stuff */
  /* development */
  if (step+1) /* fetch initial guess from check point file */
    /* hack, to alyays read from chk file!!!!! */
    fprintf(out,"%s%d,%s%d%s"," 4/5=1,7=6,17=",
	    qm->CASelectrons,
	    "18=",qm->CASorbitals,"/1,5;\n");
  else /* generate the first checkpoint file */
    fprintf(out,"%s%d,%s%d%s"," 4/5=0,7=6,17=",
	    qm->CASelectrons,
	    "18=",qm->CASorbitals,"/1,5;\n");
  /* the rest of the input depends on where the system is on the PES 
   */
  if(swap && bSA){ /* make a slide to the other surface */
    if(qm->CASorbitals>8){  /* use direct and no full diag */
      fprintf(out," 5/5=2,7=512,16=-2,17=10000000,28=2,32=2,38=6,97=100/10;\n");
    } 
    else {
      if(qm->cpmcscf){
	fprintf(out," 5/5=2,6=%d,7=512,17=31000200,28=2,32=2,38=6,97=100/10;\n",
		qm->accuracy);
	if(mm->nrMMatoms>0)
	  fprintf(out," 7/7=1,16=-2,30=1/1;\n");
	fprintf(out," 11/31=1,42=1,45=1/1;\n");
	fprintf(out," 10/6=1,10=700006,28=2,29=1,31=1,97=100/3;\n");
	fprintf(out," 7/30=1/16;\n 99/10=4/99;\n");
      }
      else{
	fprintf(out," 5/5=2,6=%d,7=512,17=11000000,28=2,32=2,38=6,97=100/10;\n",
		qm->accuracy);
	fprintf(out," 7/7=1,16=-2,30=1/1,2,3,16;\n 99/10=4/99;\n");
      }
    }
  }
  else if(bSA){ /* do a "state-averaged" CAS calculation */
    if(qm->CASorbitals>8){ /* no full diag */ 
      fprintf(out," 5/5=2,7=512,16=-2,17=10000000,28=2,32=2,38=6/10;\n");
    } 
    else {
      if(qm->cpmcscf){
	fprintf(out," 5/5=2,6=%d,7=512,17=31000200,28=2,32=2,38=6/10;\n",
		qm->accuracy);
	if(mm->nrMMatoms>0)
	  fprintf(out," 7/7=1,16=-2,30=1/1;\n");
	fprintf(out," 11/31=1,42=1,45=1/1;\n");
	fprintf(out," 10/6=1,10=700006,28=2,29=1,31=1/3;\n");
	fprintf(out," 7/30=1/16;\n 99/10=4/99;\n");
      }
      else{
      	fprintf(out," 5/5=2,6=%d,7=512,17=11000000,28=2,32=2,38=6/10;\n",
		qm->accuracy);
	fprintf(out," 7/7=1,16=-2,30=1/1,2,3,16;\n 99/10=4/99;\n");
      }
    }
  }
  else if(swap){/* do a "swapped" CAS calculation */
    if(qm->CASorbitals>8)
      fprintf(out," 5/5=2,7=512,16=-2,17=0,28=2,32=2,38=6,97=100/10;\n");
    else
      fprintf(out," 5/5=2,6=%d,7=512,17=1000000,28=2,32=2,38=6,97=100/10;\n",
	      qm->accuracy);
    fprintf(out," 7/7=1,16=-2,30=1/1,2,3,16;\n 99/10=4/99;\n");
  }
  else {/* do a "normal" CAS calculation */
    if(qm->CASorbitals>8)
      fprintf(out," 5/5=2,7=512,16=-2,17=0,28=2,32=2,38=6/10;\n");
    else
      fprintf(out," 5/5=2,6=%d,7=512,17=1000000,28=2,32=2,38=6/10;\n",
	      qm->accuracy);
    fprintf(out," 7/7=1,16=-2,30=1/1,2,3,16;\n 99/10=4/99;\n");
  }
  fprintf(out, "\ninput-file generated by gromacs\n\n");
  fprintf(out,"%2d%2d\n",qm->QMcharge,qm->multiplicity);
  for (i=0;i<qm->nrQMatoms;i++){
#ifdef GMX_DOUBLE
    fprintf(out,"%3d %10.7lf  %10.7lf  %10.7lf\n",
	    qm->atomicnumberQM[i],
	    qm->xQM[i][XX]/BOHR2NM,
	    qm->xQM[i][YY]/BOHR2NM,
	    qm->xQM[i][ZZ]/BOHR2NM);
#else
    fprintf(out,"%3d %10.7f  %10.7f  %10.7f\n",
	    qm->atomicnumberQM[i],
	    qm->xQM[i][XX]/BOHR2NM,
	    qm->xQM[i][YY]/BOHR2NM,
	    qm->xQM[i][ZZ]/BOHR2NM);
#endif
  }
  /* MM point charge data */
  if(QMMMrec->QMMMscheme!=eQMMMschemeoniom && mm->nrMMatoms){
    fprintf(out,"\n");
    for(i=0;i<mm->nrMMatoms;i++){
#ifdef GMX_DOUBLE
      fprintf(out,"%10.7lf  %10.7lf  %10.7lf %8.4lf\n",
	      mm->xMM[i][XX]/BOHR2NM,
	      mm->xMM[i][YY]/BOHR2NM,
	      mm->xMM[i][ZZ]/BOHR2NM,
	      mm->MMcharges[i]);
#else
      fprintf(out,"%10.7f  %10.7f  %10.7f %8.4f\n",
	      mm->xMM[i][XX]/BOHR2NM,
	      mm->xMM[i][YY]/BOHR2NM,
	      mm->xMM[i][ZZ]/BOHR2NM,
	      mm->MMcharges[i]);
#endif
    }
  }
  if(bSA) {/* put the SA coefficients at the end of the file */
#ifdef GMX_DOUBLE
    fprintf(out,"\n%10.8lf %10.8lf\n",
	    qm->SAstep*0.5/qm->SAsteps,
	    1-qm->SAstep*0.5/qm->SAsteps);
#else    
    fprintf(out,"\n%10.8f %10.8f\n",
	    qm->SAstep*0.5/qm->SAsteps,
	    1-qm->SAstep*0.5/qm->SAsteps);
#endif
    fprintf(stderr,"State Averaging level = %d/%d\n",qm->SAstep,qm->SAsteps);
  }
  fprintf(out,"\n");
  fclose(out);
}  /* write_gaussian_SH_input */

void write_gaussian_input(int step ,t_forcerec *fr, t_QMrec *qm, t_MMrec *mm)
{
  int
    i;
  t_QMMMrec
    *QMMMrec;
  FILE
    *out;
  
  QMMMrec = fr->qr;
  out = fopen("input.com","w");
  /* write the route */

  if(qm->QMmethod>=eQMmethodRHF)
    fprintf(out,"%s",
	    "%chk=input\n");
  else
    fprintf(out,"%s",
	    "%chk=se\n");
  if(qm->nQMcpus>1)
    fprintf(out,"%s%3d\n",
	    "%nprocshare=",qm->nQMcpus);
  fprintf(out,"%s%d\n",
	  "%mem=",qm->QMmem);
  /* use the modified links that include the LJ contribution at the QM level */
  if(qm->bTS||qm->bOPT){
    fprintf(out,"%s%s%s",
	    "%subst l701 ",qm->devel_dir,"/l701_LJ\n");
    fprintf(out,"%s%s%s",
	    "%subst l301 ",qm->devel_dir,"/l301_LJ\n");
  }
  else{
    fprintf(out,"%s%s%s",
	    "%subst l701 ",qm->devel_dir,"/l701\n");
    fprintf(out,"%s%s%s",
	    "%subst l301 ",qm->devel_dir,"/l301\n");
  }
  fprintf(out,"%s%s%s",
	  "%subst l9999 ",qm->devel_dir,"/l9999\n");
  if(step){
    fprintf(out,"%s",
	    "#T ");
  }else{
    fprintf(out,"%s",
	    "#P ");
  }
  if(qm->QMmethod==eQMmethodB3LYPLAN){
    fprintf(out," %s", 
	    "B3LYP/GEN Pseudo=Read");
  }
  else{
    fprintf(out," %s", 
	    eQMmethod_names[qm->QMmethod]);
    
    if(qm->QMmethod>=eQMmethodRHF){
      fprintf(out,"/%s",
	      eQMbasis_names[qm->QMbasis]);
      if(qm->QMmethod==eQMmethodCASSCF){
	/* in case of cas, how many electrons and orbitals do we need?
	 */
	fprintf(out,"(%d,%d)",
		qm->CASelectrons,qm->CASorbitals);
      }
    }
  }
  if(QMMMrec->QMMMscheme==eQMMMschemenormal){
    fprintf(out," %s",
	    "Charge ");
  }
  if (step || qm->QMmethod==eQMmethodCASSCF){
    /* fetch guess from checkpoint file, always for CASSCF */
    fprintf(out,"%s"," guess=read");
  }
  fprintf(out,"\nNosymm units=bohr\n");
  
  if(qm->bTS){
    fprintf(out,"OPT=(Redundant,TS,noeigentest,ModRedundant) Punch=(Coord,Derivatives) ");
  }
  else if (qm->bOPT){
    fprintf(out,"OPT=(Redundant,ModRedundant) Punch=(Coord,Derivatives) ");
  }
  else{
    fprintf(out,"FORCE Punch=(Derivatives) ");
  }
  fprintf(out,"iop(3/33=1)\n\n");
  fprintf(out, "input-file generated by gromacs\n\n");
  fprintf(out,"%2d%2d\n",qm->QMcharge,qm->multiplicity);
  for (i=0;i<qm->nrQMatoms;i++){
#ifdef GMX_DOUBLE
    fprintf(out,"%3d %10.7lf  %10.7lf  %10.7lf\n",
	    qm->atomicnumberQM[i],
	    qm->xQM[i][XX]/BOHR2NM,
	    qm->xQM[i][YY]/BOHR2NM,
	    qm->xQM[i][ZZ]/BOHR2NM);
#else
    fprintf(out,"%3d %10.7f  %10.7f  %10.7f\n",
	    qm->atomicnumberQM[i],
	    qm->xQM[i][XX]/BOHR2NM,
	    qm->xQM[i][YY]/BOHR2NM,
	    qm->xQM[i][ZZ]/BOHR2NM);
#endif
  }

  /* Pseudo Potential and ECP are included here if selected (MEthod suffix LAN) */
  if(qm->QMmethod==eQMmethodB3LYPLAN){
    fprintf(out,"\n");
    for(i=0;i<qm->nrQMatoms;i++){
      if(qm->atomicnumberQM[i]<21){
	fprintf(out,"%d ",i+1);
      }
    }
    fprintf(out,"\n%s\n****\n",eQMbasis_names[qm->QMbasis]);
    
    for(i=0;i<qm->nrQMatoms;i++){
      if(qm->atomicnumberQM[i]>21){
	fprintf(out,"%d ",i+1);
      }
    }
    fprintf(out,"\n%s\n****\n\n","lanl2dz");    
    
    for(i=0;i<qm->nrQMatoms;i++){
      if(qm->atomicnumberQM[i]>21){
	fprintf(out,"%d ",i+1);
      }
    }
    fprintf(out,"\n%s\n","lanl2dz");    
  }    
    
  /* MM point charge data */
  if(QMMMrec->QMMMscheme!=eQMMMschemeoniom && mm->nrMMatoms){
    fprintf(stderr,"nr mm atoms in gaussian.c = %d\n",mm->nrMMatoms);
    fprintf(out,"\n");
    if(qm->bTS||qm->bOPT){
      /* freeze the frontier QM atoms and Link atoms. This is
       * important only if a full QM subsystem optimization is done
       * with a frozen MM environmeent. For dynamics, or gromacs's own
       * optimization routines this is not important.
       */
      for(i=0;i<qm->nrQMatoms;i++){
	if(qm->frontatoms[i]){
	  fprintf(out,"%d F\n",i+1); /* counting from 1 */
	}
      }
      /* MM point charges include LJ parameters in case of QM optimization
       */
      for(i=0;i<mm->nrMMatoms;i++){
#ifdef GMX_DOUBLE
	fprintf(out,"%10.7lf  %10.7lf  %10.7lf %8.4lf 0.0 %10.7lf %10.7lf\n",
		mm->xMM[i][XX]/BOHR2NM,
		mm->xMM[i][YY]/BOHR2NM,
		mm->xMM[i][ZZ]/BOHR2NM,
		mm->MMcharges[i],
		mm->c6[i],mm->c12[i]);
#else
	fprintf(out,"%10.7f  %10.7f  %10.7f %8.4f 0.0 %10.7f %10.7f\n",
		mm->xMM[i][XX]/BOHR2NM,
		mm->xMM[i][YY]/BOHR2NM,
		mm->xMM[i][ZZ]/BOHR2NM,
		mm->MMcharges[i],
		mm->c6[i],mm->c12[i]);
#endif
      }
      fprintf(out,"\n");
    }
    else{
      for(i=0;i<mm->nrMMatoms;i++){
#ifdef GMX_DOUBLE
	fprintf(out,"%10.7lf  %10.7lf  %10.7lf %8.4lf\n",
		mm->xMM[i][XX]/BOHR2NM,
		mm->xMM[i][YY]/BOHR2NM,
		mm->xMM[i][ZZ]/BOHR2NM,
		mm->MMcharges[i]);
#else
	fprintf(out,"%10.7f  %10.7f  %10.7f %8.4f\n",
		mm->xMM[i][XX]/BOHR2NM,
		mm->xMM[i][YY]/BOHR2NM,
		mm->xMM[i][ZZ]/BOHR2NM,
		mm->MMcharges[i]);
#endif
      }
    }
  }
  fprintf(out,"\n");
  

  fclose(out);

}  /* write_gaussian_input */


void write_gaussian_input_QED(t_commrec *cr,int step ,t_forcerec *fr, t_QMrec *qm, t_MMrec *mm){
  int
    i;
  t_QMMMrec
    *QMMMrec;
  FILE
    *out;
  
  QMMMrec = fr->qr;

  /* move to a new working directory! */
  if(MULTISIM(cr)){
    chdir (qm->subdir);
  }
  out = fopen("input.com","w");
  /* write the route */

  if(qm->QMmethod>=eQMmethodRHF)
    fprintf(out,"%s",
	    "%chk=input\n");
  else
    fprintf(out,"%s",
	    "%chk=se\n");
  if(qm->nQMcpus>1)
    fprintf(out,"%s%3d\n",
	    "%nprocshare=",qm->nQMcpus);
  fprintf(out,"%s%d\n",
	  "%mem=",qm->QMmem);
  /* use the modified links that include the LJ contribution at the QM level */
  if(qm->bTS||qm->bOPT){
    fprintf(out,"%s%s%s",
	    "%subst l701 ",qm->devel_dir,"/l701_LJ\n");
    fprintf(out,"%s%s%s",
	    "%subst l301 ",qm->devel_dir,"/l301_LJ\n");
  }
  else{
    fprintf(out,"%s%s%s",
	    "%subst l701 ",qm->devel_dir,"/l701\n");
    fprintf(out,"%s%s%s",
	    "%subst l301 ",qm->devel_dir,"/l301\n");
  }
  fprintf(out,"%s%s%s",
	  "%subst l9999 ",qm->devel_dir,"/l9999\n");
  if(step){
    fprintf(out,"%s",
	    "#T ");
  }else{
/* MOD 12.11.20116 */
    fprintf(out,"%s",
	    "#T ");
  }
  if(qm->QMmethod==eQMmethodB3LYPLAN){
    fprintf(out," %s", 
	    "B3LYP/GEN Pseudo=Read");
  }
  else{
    fprintf(out," %s", 
	    eQMmethod_names[qm->QMmethod]);
    
    if(qm->QMmethod>=eQMmethodRHF){
      fprintf(out,"/%s",
	      eQMbasis_names[qm->QMbasis]);
      if(qm->QMmethod==eQMmethodCASSCF){
	/* in case of cas, how many electrons and orbitals do we need?
	 */
	fprintf(out,"(%d,%d)",
		qm->CASelectrons,qm->CASorbitals);
      }
    }
  }
  if(QMMMrec->QMMMscheme==eQMMMschemenormal){
    fprintf(out," %s",
	    "Charge ");
  }
  if (qm->QMmethod==eQMmethodCASSCF){
    /* fetch guess from checkpoint file, always for CASSCF */
    fprintf(out,"%s"," guess=read");
  }
  fprintf(out,"\nNosymm units=bohr\n");
  
  if(qm->bTS){
    fprintf(out,"OPT=(Redundant,TS,noeigentest,ModRedundant) Punch=(Coord,Derivatives) ");
  }
  else if (qm->bOPT){
    fprintf(out,"OPT=(Redundant,ModRedundant) Punch=(Coord,Derivatives) ");
  }
  else{
    fprintf(out,"FORCE Punch=(Derivatives) ");
  }
  fprintf(out,"iop(3/33=1)\n\n");
  fprintf(out, "input-file generated by gromacs\n\n");
  fprintf(out,"%2d%2d\n",qm->QMcharge,qm->multiplicity);
  for (i=0;i<qm->nrQMatoms;i++){
#ifdef GMX_DOUBLE
    fprintf(out,"%3d %10.7lf  %10.7lf  %10.7lf\n",
	    qm->atomicnumberQM[i],
	    qm->xQM[i][XX]/BOHR2NM,
	    qm->xQM[i][YY]/BOHR2NM,
	    qm->xQM[i][ZZ]/BOHR2NM);
#else
    fprintf(out,"%3d %10.7f  %10.7f  %10.7f\n",
	    qm->atomicnumberQM[i],
	    qm->xQM[i][XX]/BOHR2NM,
	    qm->xQM[i][YY]/BOHR2NM,
	    qm->xQM[i][ZZ]/BOHR2NM);
#endif
  }

  /* Pseudo Potential and ECP are included here if selected (MEthod suffix LAN) */
  if(qm->QMmethod==eQMmethodB3LYPLAN){
    fprintf(out,"\n");
    for(i=0;i<qm->nrQMatoms;i++){
      if(qm->atomicnumberQM[i]<21){
	fprintf(out,"%d ",i+1);
      }
    }
    fprintf(out,"\n%s\n****\n",eQMbasis_names[qm->QMbasis]);
    
    for(i=0;i<qm->nrQMatoms;i++){
      if(qm->atomicnumberQM[i]>21){
	fprintf(out,"%d ",i+1);
      }
    }
    fprintf(out,"\n%s\n****\n\n","lanl2dz");    
    
    for(i=0;i<qm->nrQMatoms;i++){
      if(qm->atomicnumberQM[i]>21){
	fprintf(out,"%d ",i+1);
      }
    }
    fprintf(out,"\n%s\n","lanl2dz");    
  }    
  
  /* MM point charge data */
  if(QMMMrec->QMMMscheme!=eQMMMschemeoniom && mm->nrMMatoms){
//    fprintf(stderr,"nr mm atoms in gaussian.c = %d\n",mm->nrMMatoms);
    fprintf(out,"\n");
    if(qm->bTS||qm->bOPT){
      /* freeze the frontier QM atoms and Link atoms. This is
       * important only if a full QM subsystem optimization is done
       * with a frozen MM environmeent. For dynamics, or gromacs's own
       * optimization routines this is not important.
       */
      for(i=0;i<qm->nrQMatoms;i++){
	if(qm->frontatoms[i]){
	  fprintf(out,"%d F\n",i+1); /* counting from 1 */
	}
      }
      /* MM point charges include LJ parameters in case of QM optimization
       */
      for(i=0;i<mm->nrMMatoms;i++){
#ifdef GMX_DOUBLE
	fprintf(out,"%10.7lf  %10.7lf  %10.7lf %8.4lf 0.0 %10.7lf %10.7lf\n",
		mm->xMM[i][XX]/BOHR2NM,
		mm->xMM[i][YY]/BOHR2NM,
		mm->xMM[i][ZZ]/BOHR2NM,
		mm->MMcharges[i],
		mm->c6[i],mm->c12[i]);
#else
	fprintf(out,"%10.7f  %10.7f  %10.7f %8.4f 0.0 %10.7f %10.7f\n",
		mm->xMM[i][XX]/BOHR2NM,
		mm->xMM[i][YY]/BOHR2NM,
		mm->xMM[i][ZZ]/BOHR2NM,
		mm->MMcharges[i],
		mm->c6[i],mm->c12[i]);
#endif
      }
      fprintf(out,"\n");
    }
    else{
      for(i=0;i<mm->nrMMatoms;i++){
#ifdef GMX_DOUBLE
	fprintf(out,"%10.7lf  %10.7lf  %10.7lf %8.4lf\n",
		mm->xMM[i][XX]/BOHR2NM,
		mm->xMM[i][YY]/BOHR2NM,
		mm->xMM[i][ZZ]/BOHR2NM,
		mm->MMcharges[i]);
#else
	fprintf(out,"%10.7f  %10.7f  %10.7f %8.4f\n",
		mm->xMM[i][XX]/BOHR2NM,
		mm->xMM[i][YY]/BOHR2NM,
		mm->xMM[i][ZZ]/BOHR2NM,
		mm->MMcharges[i]);
#endif
      }
    }
  }
  fprintf(out,"\n");
  

  fclose(out);

}  /* write_gaussian_input_QED */

real read_gaussian_output_QED(t_commrec *cr,rvec QMgrad_S1[],rvec MMgrad_S1[],
			      rvec QMgrad_S0[],rvec MMgrad_S0[],int step,
			      t_QMrec *qm, t_MMrec *mm,rvec *tdm,
                              rvec tdmX[], rvec tdmY[], rvec tdmZ[],
                              rvec tdmXMM[], rvec tdmYMM[], rvec tdmZMM[],
                              real *Eground)
{
  int
    i,j,atnum;
  char
    buf[3000],*buf2;
  real
    QMener,rinv,qtdm,ri,ro,cosa;
  FILE
    *in_S1,*in_S0;

  if (MULTISIM(cr)){ 
    chdir (qm->subdir);
  } 
  in_S1=fopen("S1.7","r");
  
  /* the next line is the energy and in the case of CAS, the energy
   * difference between the two states.
   */
  if(NULL == fgets(buf,3000,in_S1)){
      gmx_fatal(FARGS,"Error reading Gaussian output");
  }

#ifdef GMX_DOUBLE
  sscanf(buf,"%lf\n",&QMener);
#else
  sscanf(buf,"%f\n", &QMener);
#endif
  /* next lines contain the excited state gradients of the QM atoms */
  for(i=0;i<qm->nrQMatoms;i++){
    if(NULL == fgets(buf,3000,in_S1)){
	gmx_fatal(FARGS,"Error reading Gaussian output");
    }
#ifdef GMX_DOUBLE
    sscanf(buf,"%lf %lf %lf\n",
	   &QMgrad_S1[i][XX],
	   &QMgrad_S1[i][YY],
	   &QMgrad_S1[i][ZZ]);
#else
    sscanf(buf,"%f %f %f\n",
	   &QMgrad_S1[i][XX],
	   &QMgrad_S1[i][YY],
	   &QMgrad_S1[i][ZZ]);
#endif     
  }
  /* the next lines are the gradients of the MM atoms */
  for(i=0;i<mm->nrMMatoms;i++){
    if(NULL==fgets(buf,3000,in_S1)){
      gmx_fatal(FARGS,"Error reading Gaussian output");
    }
#ifdef GMX_DOUBLE
    sscanf(buf,"%lf %lf %lf\n",
    &MMgrad_S1[i][XX],
    &MMgrad_S1[i][YY],
    &MMgrad_S1[i][ZZ]);
#else
    sscanf(buf,"%f %f %f\n",
    &MMgrad_S1[i][XX],
    &MMgrad_S1[i][YY],
    &MMgrad_S1[i][ZZ]);
#endif	
  }
  /* now comes the transition dipole moments  */
  if(NULL==fgets(buf,3000,in_S1)){
      gmx_fatal(FARGS,"Error reading Gaussian output");
    }
#ifdef GMX_DOUBLE
  sscanf(buf,"%lf %lf %lf\n",
	     &tdm[0][XX],
	     &tdm[0][YY],
	     &tdm[0][ZZ]);
#else
  sscanf(buf,"%f %f %f\n",
	 &tdm[0][XX],
         &tdm[0][YY],
	 &tdm[0][ZZ]);
#endif	

/* now we need to check if the dipole moment has changed sign. If so, we simply
 * also change the sign of the E field. We use the trick Dmitry used:
 */
  if (step){
    ri = sqrt(tdm[0][XX]*tdm[0][XX]+tdm[0][YY]*tdm[0][YY]+tdm[0][ZZ]*tdm[0][ZZ]);
    ro = sqrt(qm->tdmold[XX]*qm->tdmold[XX]+qm->tdmold[YY]*qm->tdmold[YY]+qm->tdmold[ZZ]*qm->tdmold[ZZ]);
    cosa = (tdm[0][XX]*qm->tdmold[XX]+tdm[0][YY]*qm->tdmold[YY]+tdm[0][ZZ]*qm->tdmold[ZZ]) / (ri * ro);
//    fprintf(stderr,"tdm = {%f,%f,%f}\n",tdm[0][XX],tdm[0][YY],tdm[0][ZZ]);
//    fprintf(stderr,"old = {%f,%f,%f}\n", qm->tdmold[XX], qm->tdmold[YY], qm->tdmold[ZZ]);
//    fprintf(stderr,"dotprod = %lf\n",cosa);
    if (cosa<0.0){
      fprintf(stderr, "Changing Efield sign\n");
      for (i=0;i<DIM;i++){
        qm->E[i]*=-1.0;
      }
    }
  }
  /* store the TDM in QMrec for the next step */
  qm->tdmold[XX] = tdm[0][XX];
  qm->tdmold[YY] = tdm[0][YY];
  qm->tdmold[ZZ] = tdm[0][ZZ];
  /* works only in combination with TeraChem
   */
  /* read in sequence nabla tdm[j]_ia
   * first X, then Y then Z
   */
  /* Read in the dipole moment gradients */

  for (i = 0 ; i< qm->nrQMatoms; i++ ){
    if(NULL==fgets(buf,3000,in_S1)){
      gmx_fatal(FARGS,"Error reading Gaussian output");
    }
#ifdef GMX_DOUBLE
    sscanf(buf,"%lf %lf %lf\n",&tdmX[i][0],&tdmX[i][1],&tdmX[i][2]);
#else
    sscanf(buf,"%f %f %f\n",&tdmX[i][0],&tdmX[i][1],&tdmX[i][2]);
#endif
  }
  for (i = 0 ; i< qm->nrQMatoms; i++ ){
    if(NULL==fgets(buf,3000,in_S1)){
      gmx_fatal(FARGS,"Error reading Gaussian output");
    }
#ifdef GMX_DOUBLE
    sscanf(buf,"%lf %lf %lf\n",&tdmY[i][0],&tdmY[i][1],&tdmY[i][2]);
#else
    sscanf(buf,"%f %f %f\n",&tdmY[i][0],&tdmY[i][1],&tdmY[i][2]);
#endif
  }
  for (i = 0 ; i< qm->nrQMatoms; i++ ){
    if(NULL==fgets(buf,3000,in_S1)){
      gmx_fatal(FARGS,"Error reading TDMZ from Gaussian output");
    }
#ifdef GMX_DOUBLE
    sscanf(buf,"%lf %lf %lf\n",&tdmZ[i][0],&tdmZ[i][1],&tdmZ[i][2]);
#else
    sscanf(buf,"%f %f %f\n",&tdmZ[i][0],&tdmZ[i][1],&tdmZ[i][2]);
#endif
  }

/* now comes the MM TDM gradients */
  for (i = 0 ; i< mm->nrMMatoms; i++ ){
    if(NULL==fgets(buf,3000,in_S1)){
      gmx_fatal(FARGS,"Error reading MM TDM grad from  Gaussian output");
    }
#ifdef GMX_DOUBLE
    sscanf(buf,"%lf %lf %lf\n",&tdmXMM[i][0],&tdmXMM[i][1],&tdmXMM[i][2]);
#else
    sscanf(buf,"%f %f %f\n",&tdmXMM[i][0],&tdmXMM[i][1],&tdmXMM[i][2]);
#endif
  }
  for (i = 0 ; i< mm->nrMMatoms; i++ ){
    if(NULL==fgets(buf,3000,in_S1)){
      gmx_fatal(FARGS,"Error reading Gaussian output");
    }
#ifdef GMX_DOUBLE
    sscanf(buf,"%lf %lf %lf\n",&tdmYMM[i][0],&tdmYMM[i][1],&tdmYMM[i][2]);
#else
    sscanf(buf,"%f %f %f\n",&tdmYMM[i][0],&tdmYMM[i][1],&tdmYMM[i][2]);
#endif
  }
  for (i = 0 ; i< mm->nrMMatoms; i++ ){
    if(NULL==fgets(buf,3000,in_S1)){
      gmx_fatal(FARGS,"Error reading TDMZ from Gaussian output");
    }
#ifdef GMX_DOUBLE
    sscanf(buf,"%lf %lf %lf\n",&tdmZMM[i][0],&tdmZMM[i][1],&tdmZMM[i][2]);
#else
    sscanf(buf,"%f %f %f\n",&tdmZMM[i][0],&tdmZMM[i][1],&tdmZMM[i][2]);
#endif
  }

  in_S0=fopen("S0.7","r");
  if (in_S0==NULL)
    gmx_fatal(FARGS,"Error reading Gaussian output");
  /* now read in ground state information from a second file */
  if(NULL == fgets(buf,3000,in_S0)){
      gmx_fatal(FARGS,"Error reading Gaussian output");
  }
  
#ifdef GMX_DOUBLE
  sscanf(buf,"%lf\n",Eground);
#else
  sscanf(buf,"%f\n", Eground);
#endif
  /* next lines contain the excited state gradients of the QM atoms */
  for(i=0;i<qm->nrQMatoms;i++){
    if(NULL == fgets(buf,3000,in_S0)){
	gmx_fatal(FARGS,"Error reading Gaussian output");
    }
#ifdef GMX_DOUBLE
    sscanf(buf,"%lf %lf %lf\n",
	   &QMgrad_S0[i][XX],
	   &QMgrad_S0[i][YY],
	   &QMgrad_S0[i][ZZ]);
#else
    sscanf(buf,"%f %f %f\n",
	   &QMgrad_S0[i][XX],
	   &QMgrad_S0[i][YY],
	   &QMgrad_S0[i][ZZ]);
#endif     
  }
  /* the next lines are the gradients of the MM atoms */
  for(i=0;i<mm->nrMMatoms;i++){
    if(NULL==fgets(buf,3000,in_S0)){
	gmx_fatal(FARGS,"Error reading Gaussian output");
    }
#ifdef GMX_DOUBLE
      sscanf(buf,"%lf %lf %lf\n",
	     &MMgrad_S0[i][XX],
	     &MMgrad_S0[i][YY],
	     &MMgrad_S0[i][ZZ]);
#else
      sscanf(buf,"%f %f %f\n",
	     &MMgrad_S0[i][XX],
	     &MMgrad_S0[i][YY],
	     &MMgrad_S0[i][ZZ]);
#endif	
  }
  fclose(in_S0);
  fclose(in_S1);
  return(QMener);  
} /* read_gaussian_output_QED */

real read_gaussian_output(rvec QMgrad[],rvec MMgrad[],int step,
			  t_QMrec *qm, t_MMrec *mm)
{
  int
    i,j,atnum;
  char
    buf[300];
  real
    QMener;
  FILE
    *in;
  
  in=fopen("fort.7","r");

  /* in case of an optimization, the coordinates are printed in the
   * fort.7 file first, followed by the energy, coordinates and (if
   * required) the CI eigenvectors.
   */
  if(qm->bTS||qm->bOPT){
    for(i=0;i<qm->nrQMatoms;i++){
      if( NULL == fgets(buf,300,in)){
	  gmx_fatal(FARGS,"Error reading Gaussian output - not enough atom lines?");
      }

#ifdef GMX_DOUBLE
      sscanf(buf,"%d %lf %lf %lf\n",
	     &atnum,
	     &qm->xQM[i][XX],
	     &qm->xQM[i][YY],
	     &qm->xQM[i][ZZ]);
#else
      sscanf(buf,"%d %f %f %f\n",
	     &atnum,
	     &qm->xQM[i][XX],
	     &qm->xQM[i][YY],
	     &qm->xQM[i][ZZ]);
#endif     
      for(j=0;j<DIM;j++){
	qm->xQM[i][j]*=BOHR2NM;
      }
    }
  }
  /* the next line is the energy and in the case of CAS, the energy
   * difference between the two states.
   */
  if(NULL == fgets(buf,300,in)){
      gmx_fatal(FARGS,"Error reading Gaussian output");
  }

#ifdef GMX_DOUBLE
  sscanf(buf,"%lf\n",&QMener);
#else
  sscanf(buf,"%f\n", &QMener);
#endif
  /* next lines contain the gradients of the QM atoms */
  for(i=0;i<qm->nrQMatoms;i++){
    if(NULL == fgets(buf,300,in)){
	gmx_fatal(FARGS,"Error reading Gaussian output");
    }
#ifdef GMX_DOUBLE
    sscanf(buf,"%lf %lf %lf\n",
	   &QMgrad[i][XX],
	   &QMgrad[i][YY],
	   &QMgrad[i][ZZ]);
#else
    sscanf(buf,"%f %f %f\n",
	   &QMgrad[i][XX],
	   &QMgrad[i][YY],
	   &QMgrad[i][ZZ]);
#endif     
  }
  /* the next lines are the gradients of the MM atoms */
  if(qm->QMmethod>=eQMmethodRHF){  
    for(i=0;i<mm->nrMMatoms;i++){
      if(NULL==fgets(buf,300,in)){
          gmx_fatal(FARGS,"Error reading Gaussian output");
      }
#ifdef GMX_DOUBLE
      sscanf(buf,"%lf %lf %lf\n",
	     &MMgrad[i][XX],
	     &MMgrad[i][YY],
	     &MMgrad[i][ZZ]);
#else
      sscanf(buf,"%f %f %f\n",
	     &MMgrad[i][XX],
	     &MMgrad[i][YY],
	     &MMgrad[i][ZZ]);
#endif	
    }
  }
  fclose(in);
  return(QMener);  
}

real read_gaussian_SH_output(rvec QMgrad[],rvec MMgrad[],int step,
			     gmx_bool swapped,t_QMrec *qm, t_MMrec *mm,real *DeltaE)
{
  int
    i;
  char
    buf[300];
  real
    QMener;
  FILE
    *in;
  
  in=fopen("fort.7","r");
  /* first line is the energy and in the case of CAS, the energy
   * difference between the two states.
   */
  if(NULL == fgets(buf,300,in)){
      gmx_fatal(FARGS,"Error reading Gaussian output");
  }

#ifdef GMX_DOUBLE
  sscanf(buf,"%lf %lf\n",&QMener,DeltaE);
#else
  sscanf(buf,"%f %f\n",  &QMener,DeltaE);
#endif
  
  /* switch on/off the State Averaging */
  
  if(*DeltaE > qm->SAoff){
    if (qm->SAstep > 0){
      qm->SAstep--;
    }
  }
  else if (*DeltaE < qm->SAon || (qm->SAstep > 0)){
    if (qm->SAstep < qm->SAsteps){
      qm->SAstep++;
    }
  }
  
  /* for debugging: */
  fprintf(stderr,"Gap = %5f,SA = %3d\n",*DeltaE,(qm->SAstep>0));
  /* next lines contain the gradients of the QM atoms */
  for(i=0;i<qm->nrQMatoms;i++){
    if(NULL==fgets(buf,300,in)){
	gmx_fatal(FARGS,"Error reading Gaussian output");
    }

#ifdef GMX_DOUBLE
    sscanf(buf,"%lf %lf %lf\n",
	   &QMgrad[i][XX],
	   &QMgrad[i][YY],
	   &QMgrad[i][ZZ]);
#else
    sscanf(buf,"%f %f %f\n",
	   &QMgrad[i][XX],
	   &QMgrad[i][YY],
	   &QMgrad[i][ZZ]);
#endif     
  }
  /* the next lines, are the gradients of the MM atoms */
  
  for(i=0;i<mm->nrMMatoms;i++){
    if(NULL==fgets(buf,300,in)){
	gmx_fatal(FARGS,"Error reading Gaussian output");
    }
#ifdef GMX_DOUBLE
    sscanf(buf,"%lf %lf %lf\n",
	   &MMgrad[i][XX],
	   &MMgrad[i][YY],
	   &MMgrad[i][ZZ]);
#else
    sscanf(buf,"%f %f %f\n",
	   &MMgrad[i][XX],
	   &MMgrad[i][YY],
	   &MMgrad[i][ZZ]);
#endif	
  }
  
  /* the next line contains the two CI eigenvector elements */
  if(NULL==fgets(buf,300,in)){
      gmx_fatal(FARGS,"Error reading Gaussian output");
  }
  if(!step){
    sscanf(buf,"%d",&qm->CIdim);
    snew(qm->CIvec1,qm->CIdim);
    snew(qm->CIvec1old,qm->CIdim);
    snew(qm->CIvec2,qm->CIdim);
    snew(qm->CIvec2old,qm->CIdim);
  } else {
    /* before reading in the new current CI vectors, copy the current
     * CI vector into the old one.
     */
    for(i=0;i<qm->CIdim;i++){
      qm->CIvec1old[i] = qm->CIvec1[i];
      qm->CIvec2old[i] = qm->CIvec2[i];
    }
  }
  /* first vector */
  for(i=0;i<qm->CIdim;i++){
    if(NULL==fgets(buf,300,in)){
	gmx_fatal(FARGS,"Error reading Gaussian output");
    }
#ifdef GMX_DOUBLE
    sscanf(buf,"%lf\n",&qm->CIvec1[i]);
#else
    sscanf(buf,"%f\n", &qm->CIvec1[i]);   
#endif
  }
  /* second vector */
  for(i=0;i<qm->CIdim;i++){
    if(NULL==fgets(buf,300,in)){
	gmx_fatal(FARGS,"Error reading Gaussian output");
    }
#ifdef GMX_DOUBLE
    sscanf(buf,"%lf\n",&qm->CIvec2[i]);
#else
    sscanf(buf,"%f\n", &qm->CIvec2[i]);   
#endif
  }
  fclose(in);
  return(QMener);  
}

real inproduct(real *a, real *b, int n){
  int
    i;
  real
    dot=0.0;
  
  /* computes the inner product between two vectors (a.b), both of
   * which have length n.
   */  
  for(i=0;i<n;i++){
    dot+=a[i]*b[i];
  }
  return(dot);
}

dplx inproduct_complex(dplx *a, dplx *b, int n){
  int
    i;
  dplx
    dot=0.0+IMAG*0.0;
  
  for(i=0;i<n;i++){
    dot+=conj(a[i])*b[i];
  }
  return(dot);
}

int hop(int step, t_QMrec *qm)
{
  int
    swap = 0;
  real
    d11=0.0,d12=0.0,d21=0.0,d22=0.0;
  
  /* calculates the inproduct between the current Ci vector and the
   * previous CI vector. A diabatic hop will be made if d12 and d21
   * are much bigger than d11 and d22. In that case hop returns true,
   * otherwise it returns false.
   */  
  if(step){ /* only go on if more than one step has been done */
    d11 = inproduct(qm->CIvec1,qm->CIvec1old,qm->CIdim);
    d12 = inproduct(qm->CIvec1,qm->CIvec2old,qm->CIdim);
    d21 = inproduct(qm->CIvec2,qm->CIvec1old,qm->CIdim);
    d22 = inproduct(qm->CIvec2,qm->CIvec2old,qm->CIdim);
  }
  fprintf(stderr,"-------------------\n");
  fprintf(stderr,"d11 = %13.8f\n",d11);
  fprintf(stderr,"d12 = %13.8f\n",d12);
  fprintf(stderr,"d21 = %13.8f\n",d21);
  fprintf(stderr,"d22 = %13.8f\n",d22);
  fprintf(stderr,"-------------------\n");
  
  if((fabs(d12)>0.2)&&(fabs(d21)>0.2))
    swap = 1;
  
  return(swap);
}

void do_gaussian(int step,char *exe)
{
  char
    buf[300];

  /* make the call to the gaussian binary through system()
   * The location of the binary will be picked up from the 
   * environment using getenv().
   */
  if(step) /* hack to prevent long inputfiles */
    sprintf(buf,"%s < %s > %s",
	    exe,
	    "input.com",
	    "input.log");
  else
    sprintf(buf,"%s < %s > %s",
	    exe,
            "input.com",
	    "input.log");
//  fprintf(stderr,"Calling '%s'\n",buf);
#ifdef GMX_NO_SYSTEM
  printf("Warning-- No calls to system(3) supported on this platform.");
  gmx_fatal(FARGS,"Call to '%s' failed\n",buf);
#else
  if ( system(buf) != 0 )
    gmx_fatal(FARGS,"Call to '%s' failed\n",buf);
#endif
}

real call_gaussian(t_commrec *cr,  t_forcerec *fr, 
		   t_QMrec *qm, t_MMrec *mm, rvec f[], rvec fshift[])
{
  /* normal gaussian jobs */
  static int
    step=0;
  int
    i,j;
  real
    QMener=0.0;
  rvec
    *QMgrad,*MMgrad;
  char
    *exe;
  
  snew(exe,30);
  sprintf(exe,"%s/%s",qm->gauss_dir,qm->gauss_exe);
  snew(QMgrad,qm->nrQMatoms);
  snew(MMgrad,mm->nrMMatoms);

  write_gaussian_input(step,fr,qm,mm);
  do_gaussian(step,exe);
  QMener = read_gaussian_output(QMgrad,MMgrad,step,qm,mm);
  /* put the QMMM forces in the force array and to the fshift
   */
  for(i=0;i<qm->nrQMatoms;i++){
    for(j=0;j<DIM;j++){
      f[i][j]      = HARTREE_BOHR2MD*QMgrad[i][j];
      fshift[i][j] = HARTREE_BOHR2MD*QMgrad[i][j];
    }
  }
  for(i=0;i<mm->nrMMatoms;i++){
    for(j=0;j<DIM;j++){
      f[i+qm->nrQMatoms][j]      = HARTREE_BOHR2MD*MMgrad[i][j];      
      fshift[i+qm->nrQMatoms][j] = HARTREE_BOHR2MD*MMgrad[i][j];
    }
  }
  QMener = QMener*HARTREE2KJ*AVOGADRO;
  step++;
  free(exe);
  free(QMgrad);
  free(MMgrad);
  return(QMener);
} /* call_gaussian */

typedef struct {
  int j;
  int i;
} t_perm;

void track_states(dplx *vecold, dplx *vecnew, int ndim){
  fprintf(stderr,"Call to track_states\n");
  int
    *stmap,i,j,k;
  double
    maxover,ri,ro,sina,cosa;

  snew(stmap,ndim);
  for(i=0;i<ndim;i++){
    maxover=-1.0;
    for(j=0;j<ndim;j++){
///      if (fabs(inproduct(&vecnew[i*ndim], &vecold[j*ndim], ndim))>maxover) {
///        maxover=fabs(inproduct(&vecnew[i*ndim], &vecold[j*ndim], ndim));
      if (cabs(inproduct_complex(&vecnew[i*ndim], &vecold[j*ndim], ndim))>maxover) {
	maxover=cabs(inproduct_complex(&vecnew[i*ndim], &vecold[j*ndim], ndim));
	stmap[i]=j;
//	fprintf(stderr,"From track_states: maxover_%d=%lf\n",j,maxover);
      }
    }
  }
  for(i=0;i<ndim;i++){
///    ri = sqrt(inproduct(&vecnew[i*ndim], &vecnew[i*ndim],ndim));
///    ro = sqrt(inproduct(&vecold[stmap[i]*ndim], &vecold[stmap[i]*ndim],ndim));
///    cosa = inproduct(&vecnew[i*ndim], &vecold[stmap[i]*ndim],ndim)/ (ri * ro);
    ri = sqrt(inproduct_complex(&vecnew[i*ndim], &vecnew[i*ndim],ndim));
    ro = sqrt(inproduct_complex(&vecold[stmap[i]*ndim], &vecold[stmap[i]*ndim],ndim));
    cosa = creal(inproduct_complex(&vecnew[i*ndim], &vecold[stmap[i]*ndim],ndim))/(ri*ro);
//    fprintf(stderr,"cosa=%lf\n",cosa);
    if (cosa<0.0){
      for(j=0;j<ndim;j++){
	vecnew[i*ndim+j]=-vecnew[i*ndim+j];
      }
    }
  }
  fprintf(stderr,"Done with track_states\n");
  sfree(stmap);
}
   
int QEDFSSHop(int step, t_QMrec *qm, dplx *eigvec, int ndim, double *eigval, real dt,t_QMMMrec *qr){
  fprintf(stderr,"Call to QEDFSSHop\n");
  int
    i,j,k,current,hopto;
  double
    *f,*p,b,rnr,ptot=0.0,invdt,overlap=0.0,btot=0.0;
  dplx 
     *g=NULL,*c=NULL,*cold=NULL,*U=NULL; 
//  FILE
//    *Cout=NULL;
//   char
//     buf[5000];

  invdt=1.0/dt*AU2PS;
  current = qm->polariton;
  hopto=qm->polariton;
  snew(c,ndim); 
  snew(cold,ndim);
  if (step){
    rnr = qm->rnr[step];
    for (i=0;i<ndim;i++){
       cold[i]=qm->creal[i]+IMAG*qm->cimag[i];
       c[i]=qm->creal[i]+IMAG*qm->cimag[i];
//       fprintf(stderr,"|c[%d]|^2 = %lf ;",i,conj(c[i])*c[i]);
//       fprintf(stderr,"|g[%d]|^2 = %lf\n",i,conj(cold[i])*cold[i]);
    }
    snew(p,ndim);
    
    /* check for trivial hops and trace the states 
     */
    track_states(qm->eigvec,eigvec,ndim);
//    hopto = trace_states(qm,c,eigvec,ndim);
    if (hopto != current){
      /* we thus have a diabatic hop, and enforce this hop.
       * We still propagae the wavefunction
       */
      rnr=1000;
    }
    /* make choice between hopping in adiabatic or diabatic basis i
     * will be added as an option to the mdp file
     */
    if (qr->SHmethod==eSHmethodGranucci) {
    /* implementation of Tully's FFSH in adiabatic basis
     * following Granucci, Persico and Toniolo
     * J. CHem. Phys. 114 (2001) 10608  
     */
      //track_states(qm->eigvec, eigvec, ndim);

      snew(U,ndim*ndim);
      /* we need to keep the coefficients at t, as we need both c(t) and
       * c(t+dt) to compute the hopping probanilities. We just make a copy
       */
      propagate_local_dia(ndim,dt,c,eigvec,qm->eigvec,eigval,qm->eigval,U);
      fprintf(stderr," population that leaves state %d: %lf\n",current,(conj(cold[current])*cold[current]-conj(c[current])*c[current]));
      fprintf(stderr, "probability to leave state %d is %lf\n",current,(conj(cold[current])*cold[current]-conj(c[current])*c[current])/(conj(cold[current])*cold[current]));
      ptot=(conj(cold[current])*cold[current]-conj(c[current])*c[current])/(conj(cold[current])*cold[current]);
      if (ptot<=0){
        for ( i = 0 ; i < ndim ; i++ ){
          p[i] = 0;
        }
      } 
      else{
        btot=0.0;
        for ( i = 0 ; i < ndim ; i++ ){
          if ( i != current ){
            b = conj(U[i*ndim+current]*cold[current])*U[i*ndim+current]*cold[current];
            if (b>0.0){
              btot+=b;
              p[i]=b;
            }
            fprintf(stderr,"from state %d to state %d, b = %lf\n",current,i,b);
          }
        }
        for (i = 0 ;i<ndim;i++){
          p[i]=p[i]/btot*ptot;
        }
      }
      free(U);
    }
    else{
      /* implementation of Tully's FSSH in adiabatic basis 
       * Following Fabiano, Keal and Thiel
       * Chem. Phys. 2008 
       */ 
      snew(f,ndim*ndim);
      for(i=0;i<ndim;i++){
	for(j=i+1;j<ndim;j++){
	  for (k=0;k<ndim;k++){
///	    f[i*ndim+j]+=0.5*invdt*(qm->eigvec[i*ndim+k]*eigvec[j*ndim+k]-eigvec[i*ndim+k]*qm->eigvec[j*ndim+k]);
///	    f[j*ndim+i]+=0.5*invdt*(qm->eigvec[j*ndim+k]*eigvec[i*ndim+k]-eigvec[j*ndim+k]*qm->eigvec[i*ndim+k]);
	    f[i*ndim+j]+=0.5*invdt*(conj(qm->eigvec[i*ndim+k])*eigvec[j*ndim+k]-conj(eigvec[i*ndim+k])*qm->eigvec[j*ndim+k]);
	    f[j*ndim+i]+=0.5*invdt*(conj(qm->eigvec[j*ndim+k])*eigvec[i*ndim+k]-conj(eigvec[j*ndim+k])*qm->eigvec[i*ndim+k]);
	    fprintf(stderr,"From Tully's FSSH in QEDFSSHop, copy_of_f[%d,%d]=%lf+%lfI\n",i,j,creal(0.5*invdt*(conj(qm->eigvec[i*ndim+k])*eigvec[j*ndim+k]-conj(eigvec[i*ndim+k])*qm->eigvec[j*ndim+k])),cimag(0.5*invdt*(conj(qm->eigvec[i*ndim+k])*eigvec[j*ndim+k]-conj(eigvec[i*ndim+k])*qm->eigvec[j*ndim+k])));
	    fprintf(stderr,"From Tully's FSSH in QEDFSSHop, copy_of_f[%d,%d]=%lf+%lfI\n",j,i,creal(0.5*invdt*(conj(qm->eigvec[j*ndim+k])*eigvec[i*ndim+k]-conj(eigvec[j*ndim+k])*qm->eigvec[i*ndim+k])),cimag(0.5*invdt*(conj(qm->eigvec[j*ndim+k])*eigvec[i*ndim+k]-conj(eigvec[j*ndim+k])*qm->eigvec[i*ndim+k])));
	  }  
	}
      }
      propagate(ndim,dt, c,eigvec,qm->eigvec,eigval,qm->eigval);
      /* following Tully, Thiel et al. seem to do the propagation after 
       * the computation of hoping 
       */ 
      for(i=0;i<ndim;i++){
        if(i != current ){
          b=2*creal(conj(c[current])*c[i]*f[current*ndim+i]);
          if(b>0){
            p[i]=b*dt/AU2PS/(conj(c[current])*c[current]);
          }
        }
      }
      free(f);
    }
    if (rnr < 1000){/* avoid double hopping in case of trivial diabatic hops */
      ptot=0.0;
      for(i=0;i<ndim;i++){
        if ( i != current && ptot < rnr ){
          fprintf(stderr,"probability to hop from %d to %d is %lf\n",current,i,p[i]);
          if ( ptot+p[i] > rnr ) {
            hopto = i;
            fprintf(stderr,"hopping at step %d with probability %lf\n",step,ptot+p[i]);
          }
          ptot+=p[i];
        }
      }
    }
    /* store the expansion coefficients for the next step
     */
    for ( i = 0 ; i < ndim ; i++ ){
      qm->creal[i] = creal(c[i]);
      qm->cimag[i] = cimag(c[i]);
    } 
    /* some writinig
     */
    fprintf(stderr,"step %d: C: ",step);
//    sprintf(buf,"%s/C.dat",qm->work_dir);
//    Cout=fopen (buf,"w");
//    fprintf(Cout,"%d\n",step);
    for(i=0;i<ndim;i++){
      fprintf (stderr," %.5lf ",conj(c[i])*c[i]);    
//      fprintf (Cout,"%.5lf %.5lf\n ",qm->creal[i],qm->cimag[i]);
    }
//    fclose(Cout);
    fprintf(stderr,"\n");
    free(p);
  }
  else{
    /* hack to read back the eigenvectors from file */
    if(qm->restart){
      track_states(qm->eigvec, eigvec, ndim);
    }
//    qm->creal[current]=1.0;
    fprintf(stderr,"step %d: C: ",step);
    for(i=0;i<ndim;i++){
      c[i] = qm->creal[i]+ IMAG*qm->cimag[i];
      fprintf (stderr,"%.5lf ",conj(c[i])*c[i]);
    }    
    fprintf(stderr,"\n");
  }
  free(c);
  free(cold);
  fprintf(stderr,"Done with QEDFSSHop\n");
  return(hopto);
}

int QEDhop(int step, t_QMrec *qm, dplx *eigvec, int ndim, double *eigval){
  fprintf(stderr,"call to QEDhop\n");
  dplx
    dii,dij,dij_max=0.0+IMAG*0.0;
  int
    i,k,current,hopto=0;

  /* check overlap with all states within the energy treshold that is
 *    * set by qm->SAon
 *       */
  hopto=qm->polariton;
  current=qm->polariton;
  for (i=0; i < ndim; i++){
    /* check if the energy gap is within the treshold */
    if(fabs(eigval[current]-eigval[i]) < qm->SAon){
      dii  = 0.0+IMAG*0.0;
      dij  = 0.0+IMAG*0.0;
      for (k=0;k<ndim;k++){
	dii+=conj(eigvec[current*ndim+k])*(qm->eigvec[current*ndim+k]);
	dij+=conj(eigvec[i*ndim+k])*(qm->eigvec[current*ndim+k]);
      }
      if (cabs(dij) > cabs(dii)){
	if(cabs(dij) > cabs(dij_max)){
	  hopto=i;
	  dij_max = dij;
	}
      }
      fprintf(stderr,"Overlap between %d and %d\n",current,i);
      fprintf(stderr,"-------------------\n");
///      fprintf(stderr,"dij = %13.8f\n",dij);
      fprintf(stderr,"dij = %13.8f+%13.8fI\n",creal(dij),cimag(dij));
      fprintf(stderr,"-------------------\n");
    }
  }
  if (current != hopto ){
/*    qm->polariton = hopto;*/
    fprintf (stderr,"hopping from state %d to state %d\n",current,hopto);
  }
  /* copy the current vectors to the old vectors!
   */
  for(i=0;i<ndim*ndim;i++){
    qm->eigvec[i]=eigvec[i];
  }
  fprintf(stderr,"QEDhop done\n");
  return (hopto);
} /* QEDhop */


void   propagate_TDSE(int step, t_QMrec *qm, dplx *eigvec, int ndim, double *eigval, real dt, t_QMMMrec *qr){
  fprintf(stderr,"Call to propagate_TDSE\n");
  int
    i;
  dplx 
    *c=NULL,*U=NULL; 

  snew(c,ndim); 


  if(step){
    for (i=0;i<ndim;i++){
      c[i]=qm->creal[i]+IMAG*qm->cimag[i];
    }
    track_states(qm->eigvec, eigvec, ndim);
    /* we propagate the wave function in the local diabatic basis, i.e.
     * diabatic along the direction in which the atoms moved.
     * J. CHem. Phys. 114 (2001) 10608  
     *
     */
    snew(U,ndim*ndim);
    propagate_local_dia(ndim,dt,c,eigvec,qm->eigvec,eigval,qm->eigval,U);

    for ( i = 0 ; i < ndim ; i++ ){
      
      qm->creal[i] = creal(c[i]);
      qm->cimag[i] = cimag(c[i]);
    } 
    /* some writinig
     */
    fprintf(stderr,"step %d: C: ",step);
    for(i=0;i<ndim;i++){
      fprintf (stderr," %.5lf ",conj(c[i])*c[i]);    
    }
    fprintf(stderr,"\n");
    free(U);
  }
  else{
    /* hack to read back the eigenvectors from file */
    if(qm->restart){
      track_states(qm->eigvec, eigvec, ndim);
    }
    fprintf(stderr,"step %d: |C|^2: ",step);
    for(i=0;i<ndim;i++){
      c[i] = qm->creal[i]+ IMAG*qm->cimag[i];
      fprintf (stderr,"%.5lf ",conj(c[i])*c[i]);
    }    
    fprintf(stderr,"\n");
  }
  free(c);
  fprintf(stderr,"propagate_TDSE done\n");
} /* propagatate_TDSE */

void decoherence(t_commrec *cr, t_QMrec *qm, t_MMrec *mm, int ndim, double *eigval){

/* decoherence correction by Granucci et al. (J. Chem. Phys. 126, 134114 (2007) */
  int
    state,i;
  double 
    sum,decay=0.0,tau,ekin[1];
  
  ekin[0] = 0.0;
  /* kinetic energy of the qm nuclei and mm pointcharges on each node */
  for(i=0;i<qm->nrQMatoms;i++){
    ekin[0]+=qm->ffmass[i]*dot(DIM,qm->vQM[i],qm->vQM[i]);
  }
  for (i = 0; i< mm->nrMMatoms;i++){
    ekin[0]+=mm->ffmass[i]*dot(DIM,mm->vMM[i],mm->vMM[i]);
  }
  ekin[0] *= (0.5/(HARTREE2KJ*AVOGADRO));/* in atomic units */
  /* now we have ekin per node. Now send around the total kinetic energy */
  /* send around */
  if(MULTISIM(cr)){
    gmx_sumd_sim(1,ekin,cr->ms);
  }
  /* apply */ 
  sum = 0.0;
  for (state = 0; state < ndim; state++){
    if (state != qm->polariton){
      tau = (1.0+(qm->QEDdecoherence)/ekin[0])/fabs(eigval[state]-eigval[qm->polariton]);
//      if(MULTISIM(cr)){
//        if (cr->ms->sim==0){
//          fprintf(stderr,"node %d: tau  = %lf, exp(-dt/tau)=%lf\n",cr->ms->sim,tau,exp(-(qm->dt)/(tau*AU2PS)));
//        }
//      }
//      else{
//        fprintf(stderr,"tau = %lf, exp(-dt/tau)=%lf\n",tau,exp(-(qm->dt)/(tau*AU2PS)));
//      }
      /* do this for both c and d */
      qm->creal[state]*=exp(-(qm->dt)/(tau*AU2PS));
      qm->cimag[state]*=exp(-(qm->dt)/(tau*AU2PS));
      //      qm->groundstate*=exp(-(qm->dt)/(tau*AU2PS));;
      sum += qm->creal[state]*qm->creal[state]+qm->cimag[state]*qm->cimag[state];
      

      
//      if(MULTISIM(cr)){
//        if (cr->ms->sim==0){
//          fprintf(stderr,"node %d: state %d, |c_m|^2=%lf,sum = %lf\n",cr->ms->sim,state,qm->creal[state]*qm->creal[state]+qm->cimag[state]*qm->cimag[state],sum);
//        }
//      }
//      else{
//        fprintf(stderr,"state %d, sum = %lf\n",cr->ms->sim,state,sum);
//      }
    }
  }
  
//  fprintf(stderr,"sum = %lf, |c_M(0)|^2 = %lf; sum+|c_M(0)|^2=%lf\n",sum,(qm->creal[qm->polariton])*(qm->creal[qm->polariton])+(qm->cimag[qm->polariton])*(qm->cimag[qm->polariton]),sum+(qm->creal[qm->polariton])*(qm->creal[qm->polariton])+(qm->cimag[qm->polariton])*(qm->cimag[qm->polariton]));
  /* add the contribution of the ground state too */
  sum+=qm->groundstate;
  decay = sqrt((1.0-sum)/((qm->creal[qm->polariton])*(qm->creal[qm->polariton])+(qm->cimag[qm->polariton])*(qm->cimag[qm->polariton])));
  qm->creal[qm->polariton]*=decay;
  qm->cimag[qm->polariton]*=decay;
  if(MULTISIM(cr)){
    if (cr->ms->sim==0){
      fprintf(stderr,"node %d: decoherence done, sum = %lf, tau = %lf, decay = %lf\n",cr->ms->sim,sum,tau,decay);
    }
  }
  else{
    fprintf(stderr,"decoherence done, decay = %lf\n",decay);
  }
} /* decoherence */

double cavity_dispersion(int n, t_QMrec *qm){
  return sqrt(qm->omega*qm->omega+SPEED_OF_LIGHT_AU*SPEED_OF_LIGHT_AU*(2*M_PI*n/(qm->L*microM2BOHR))*(2*M_PI*n/(qm->L*microM2BOHR))/(qm->n_index*qm->n_index));
} /* cavity_dispersion */

void get_NAC(int ndim, int nmol,dplx  *eigvec,double *eigval,rvec *tdmX,
	     rvec *tdmY, rvec *tdmZ, rvec *tdmXMM, rvec *tdmYMM,
	     rvec *tdmZMM,t_QMrec *qm,t_MMrec *mm,int mol,rvec *QMgrad_S0,
	     rvec *QMgrad_S1,rvec *MMgrad_S0,rvec *MMgrad_S1,int p, int q,
	     rvec *nacQM,rvec *nacMM){
  /* obtain the NAC vector between state p and q
   */  
    /* NOTE: this assumes real NAC, whereas NAC can be complex. This needs to be fixed at some point However, for the purpose of computing the velocity corrections, it shoudl not matter because the quantity of interest is the v.F + F.v, meaning that with F = a+bi, it reduces to
     2Re[F.v], which is what we compute.
     */
  int
    m,i,j;
  dplx
    fij,bpaq,apbq,bqap,aqbp,a_sump,a_sumq,betasq;
  double
    gap;
  double E0_norm_sq;
  E0_norm_sq = iprod(qm->E,qm->E);
  // Square of the magitud of the E-field at k=0
  double V0_2EP = qm->omega/(E0_norm_sq),L_au=qm->L*microM2BOHR;
  double u[3];
  u[0]=qm->E[0]/sqrt(E0_norm_sq);
  u[1]=qm->E[1]/sqrt(E0_norm_sq);
  u[2]=qm->E[2]/sqrt(E0_norm_sq);
  
  
  m=mol;
  a_sump = 0.0+IMAG*0.0;
  gap = eigval[q]-eigval[p];
  //  if (gap > 0.0001 || gap < -0.0001){
  for (i=0;i<(qm->n_max)+1;i++){
    a_sump += eigvec[p*ndim+nmol+i]*sqrt(cavity_dispersion(i,qm)/V0_2EP)*cexp(IMAG*2*M_PI*i/L_au*m*L_au/((double) nmol));
  }
  betasq = conj(eigvec[p*ndim+m])*eigvec[q*ndim+m];
  a_sumq = 0.0+IMAG*0.0;
  for (i=0;i<(qm->n_max)+1;i++){
    a_sumq += eigvec[q*ndim+nmol+i]*sqrt(cavity_dispersion(i,qm)/V0_2EP)*cexp(IMAG*2*M_PI*i/L_au*m*L_au/((double) nmol));
  }
  bpaq = a_sumq*conj(eigvec[p*ndim+m]);
  apbq = conj(a_sump)*eigvec[q*ndim+m];
  bqap = conj(eigvec[q*ndim+m])*(a_sump); /* conj(apbq) */
  aqbp = conj(a_sumq)*eigvec[p*ndim+m]; /* conj(bpaq) */
  for(i=0;i<qm->nrQMatoms;i++){
    for(j=0;j<DIM;j++){
	  /* diagonal term
	   */
	  fij = betasq*(QMgrad_S1[i][j]-QMgrad_S0[i][j]);
	  /* off-diagonal term
	   */
      fij-= (bpaq+apbq)*tdmX[i][j]*u[0];
	  fij-= (bpaq+apbq)*tdmY[i][j]*u[1];
	  fij-= (bpaq+apbq)*tdmZ[i][j]*u[2];
	  fij*=HARTREE_BOHR2MD;
      fij/=(HARTREE2KJ*AVOGADRO*gap);
	  nacQM[i][j] +=creal(fij);
    }
  }
  for(i=0;i<mm->nrMMatoms;i++){
    for(j=0;j<DIM;j++){
	  fij = betasq*(MMgrad_S1[i][j]-MMgrad_S0[i][j]);
	  fij-= (bpaq+apbq)*tdmXMM[i][j]*u[0];
      fij-= (bpaq+apbq)*tdmYMM[i][j]*u[1];
	  fij-= (bpaq+apbq)*tdmZMM[i][j]*u[2];
	  fij*=HARTREE_BOHR2MD;
      fij/=(HARTREE2KJ*AVOGADRO*gap);
	  nacMM[i][j] +=creal(fij);
    }
  }
    //  }
} /* get_NAC */

static int check_vel(t_commrec *cr,
			  double *QMener,
			  rvec *QMnac, rvec *MMnac,
			  t_QMrec *qm, t_MMrec *mm,
			  int J,int K, double *g){
  /* rescale velocities after an hop occured to conserve energy
   *
   * see Fabiano, Groenhof, Thiel; Chemical Physics 351 (2008) 111-116
   */
  int i;
  double a=0, b=0, dE;
  
  
  /* we hopped 
   * short J -> K
   */

  /* all nodes have this? */
  /* NAC in principle is complex antisymmetric / antihermitian. We shoudl update therefore from rvec to complex rvec types */
  dE = (QMener[K] - QMener[J])*HARTREE2KJ*AVOGADRO;
  fprintf(stderr,"The energy gap for hopping from state %d to state %d is %lf\n",J,K,dE);
  /* \sum_A M_A (\vec d^IJ_A)^2 
   */
  for(i=0;i<qm->nrQMatoms;i++){
    /* check if the atom has mass. E.g. link atoms have no mass, and hence
     * cannot contribute to the kinetic energy
     */
    if(qm->ffmass[i]>0.){
      a+=1/qm->ffmass[i]*dot(3,QMnac[i],QMnac[i]);
    }
  }
  fprintf(stderr,"before MM, a = %lf\n",a);
  for(i=0;i<mm->nrMMatoms;i++){
    if(mm->ffmass[i]>0.){
      a+=1/mm->ffmass[i]*dot(3,MMnac[i],MMnac[i]);
    }
  }
  a*=0.5;
  for(i=0;i<qm->nrQMatoms;i++){
    if(qm->ffmass[i]>0.){
    //b+=dot(3,qm->vQM[i],QMnac[i]);
      b+=dot(3,QMnac[i],qm->vQM[i]);
    }
  }
  fprintf(stderr,"before MM, b= %lf\n",b);
  for(i=0;i<mm->nrMMatoms;i++){
    if(mm->ffmass[i]>0.){
//      b+=dot(3,mm->vMM[i],MMnac[i]);
        b+=dot(3,MMnac[i],mm->vMM[i]);
    }
  }
  if(MULTISIM(cr)){
    gmx_sumd_sim(1,&a,cr->ms);
    gmx_sumd_sim(1,&b,cr->ms);
    if(cr->ms->sim==0)
      fprintf(stderr,"in check_vel: a = %lf, b = %lf, dE= %lf, (b*b - 4.0*a*dE) = %lf, g = %lf / %lf / %lf\n",a,b,dE,b*b - 4.0*a*dE,( b + sqrt(b*b - 4.0*a*dE))/(2*a),( b - sqrt(b*b - 4.0*a*dE))/(2*a),b/a);
  }
 
  /* hop energy allowed 
   */
  if (b*b - 4.0*a*dE >= 0.0 ){
    /* take the solution with the smaller modulus 
     */
    if (b <  0){
      g[0] = ( b + sqrt(b*b - 4.0*a*dE))/(2*a);
    }
    else{
      g[0] = ( b - sqrt(b*b - 4.0*a*dE))/(2*a);
    }
    return (1);
  }
  else{
    g[0] = b/a;
    return (0);
  }

} /* check_vel */

void adjust_vel(int m, t_QMMMrec *qr, t_QMrec *qm, t_MMrec *mm, double f,
		rvec *QMnac, rvec *MMnac){
  int at,i;
  real ekin=0;
  /* print it for debuggin */

  for (at=0; at<qm->nrQMatoms; at++){
    for(i=0;i<DIM;i++){
      ekin+=(qr->v[qm->indexQM[at]][i])*(qr->v[qm->indexQM[at]][i])*0.5*qm->ffmass[at];
    }
  }
  for (at=0; at<mm->nrMMatoms; at++){
    for(i=0;i<DIM;i++){
      ekin+=(qr->v[mm->indexMM[at]][i])*(qr->v[mm->indexMM[at]][i])*0.5*mm->ffmass[at];
    }
  }
  fprintf(stderr,"\nkinetic energy of molecule %d BEFORE correction: %lf\n",m,ekin);
  for (at=0; at<qm->nrQMatoms; at++){
    //    fprintf(stderr,"before correction v[%d]: ( %lf, %lf, %lf)\n",
    //	    qm->indexQM[at],qr->v[qm->indexQM[at]][XX],
    //	    qr->v[qm->indexQM[at]][YY],
    //	    qr->v[qm->indexQM[at]][ZZ]);
    for(i=0;i<DIM;i++){
      if (qm->ffmass[at]>0.&& QMnac[at][i]*QMnac[at][i]>0.){
	qr->v[qm->indexQM[at]][i] -= f*QMnac[at][i]/(qm->ffmass[at]);
	qm->vQM[at][i]= qr->v[qm->indexQM[at]][i];
      }      
    }
  }
  for (at=0; at<mm->nrMMatoms; at++){
    for(i=0;i<DIM;i++){
      if (mm->ffmass[at]>0.&& MMnac[at][i]*MMnac[at][i]>0.){
	qr->v[mm->indexMM[at]][i] -= f*MMnac[at][i]/(mm->ffmass[at]);
	mm->vMM[at][i]= qr->v[mm->indexMM[at]][i];
      }
    }
  }
  //    fprintf(stderr,"after correction v[%d]: ( %lf, %lf, %lf)\n",qm->indexQM[at],
  //	    qr->v[qm->indexQM[at]][XX], qr->v[qm->indexQM[at]][YY], qr->v[qm->indexQM[at]][ZZ]);
  
  ekin=0;
  for (at=0; at<qm->nrQMatoms; at++){
    for(i=0;i<DIM;i++){
      ekin+=(qr->v[qm->indexQM[at]][i])*(qr->v[qm->indexQM[at]][i])*0.5*qm->ffmass[at];
    }
  }
  for (at=0; at<mm->nrMMatoms; at++){
    for(i=0;i<DIM;i++){
      ekin+=(qr->v[mm->indexMM[at]][i])*(qr->v[mm->indexMM[at]][i])*0.5*mm->ffmass[at];
    }
  }
  fprintf(stderr,"\nkinetic energy of molecule %d AFTER correction: %lf\n",m,ekin);
} /* adjust_vel */


void print_NAC(int ndim, int nmol,dplx  *eigvec,double *eigval,rvec *tdmX, rvec *tdmY, rvec *tdmZ, rvec *tdmXMM, rvec *tdmYMM, rvec *tdmZMM,t_QMrec *qm,t_MMrec *mm,int mol,rvec *QMgrad_S0,rvec *QMgrad_S1,rvec *MMgrad_S0,rvec *MMgrad_S1){

  int
    m,p,q,i,j;
  dplx
    fij,bpaq,apbq,bqap,aqbp,a_sump,a_sumq,betasq;
  FILE
    *ev=NULL;
  char
    evfile[300];
  double
    gap;
  double E0_norm_sq;  E0_norm_sq = iprod(qm->E,qm->E); // Square of the magitud of the E-field at k=0
  double V0_2EP = qm->omega/(E0_norm_sq),L_au=qm->L*microM2BOHR;
  double u[3];
    u[0]=qm->E[0]/sqrt(E0_norm_sq);
    u[1]=qm->E[1]/sqrt(E0_norm_sq);
    u[2]=qm->E[2]/sqrt(E0_norm_sq);


  m=mol;
  
  for (p=0;p<ndim;p++){
    for (q=0;q<ndim;q++){
      if (q==p) continue;
      sprintf(evfile,"%s/NAC_mol_%d_state_%d_state_%d.dat",qm->subdir,mol,q,p);
      ev = fopen(evfile,"w");
      a_sump = 0.0+IMAG*0.0;
      gap = eigval[q]-eigval[p];
      if (gap > 0.0001 || gap < -0.0001){
        for (i=0;i<(qm->n_max)+1;i++){
          a_sump += eigvec[p*ndim+nmol+i]*sqrt(cavity_dispersion(i,qm)/V0_2EP)*cexp(-IMAG*2*M_PI*i/L_au*m*L_au/((double) nmol));
        }
        betasq = conj(eigvec[p*ndim+m])*eigvec[q*ndim+m];
        a_sumq = 0.0+IMAG*0.0;
        for (i=0;i<(qm->n_max)+1;i++){
          a_sumq += eigvec[q*ndim+nmol+i]*sqrt(cavity_dispersion(i,qm)/V0_2EP)*cexp(-IMAG*2*M_PI*i/L_au*m*L_au/((double) nmol));
        }
        bpaq = a_sumq*conj(eigvec[p*ndim+m]);
        apbq = conj(a_sump)*eigvec[q*ndim+m];
        bqap = conj(eigvec[q*ndim+m])*(a_sump); /* conj(apbq) */
        aqbp = conj(a_sumq)*eigvec[p*ndim+m]; /* conj(bpaq) */
        for(i=0;i<qm->nrQMatoms;i++){
          for(j=0;j<DIM;j++){
          /* diagonal term */
//            fprintf(stderr,"molecule %d: betasq = %lf + %lfI; conj(betasq) = %lf + %lfI\n",m,creal(betasq),cimag(betasq),creal(conj(betasq)),cimag(conj(betasq)));
            fij = betasq*(QMgrad_S1[i][j]-QMgrad_S0[i][j]);
           // fij+= conj(betasq)*(QMgrad_S1[i][j]-QMgrad_S0[i][j]);
          /* off-diagonal term */
  //          fprintf(stderr,"molecule %d: betasq*(QMgrad_S1[i][j]-QMgrad_S0[i][j]) = %lf + %lfI\n",m,creal(betasq*(QMgrad_S1[i][j]-QMgrad_S0[i][j])),cimag(betasq*(QMgrad_S1[i][j]-QMgrad_S0[i][j])));
            fij-= (bpaq+apbq)*tdmX[i][j]*u[0];
            fij-= (bpaq+apbq)*tdmY[i][j]*u[1];
            fij-= (bpaq+apbq)*tdmZ[i][j]*u[2];
   //         fprintf(stderr,"molecule %d: fij = %lf + %lfI\n",m,creal(fij),cimag(fij));
            fij*=HARTREE_BOHR2MD;
            fij/=(HARTREE2KJ*AVOGADRO*gap);
//            fprintf(stderr,"%12.8lf + %12.8lfI\n",creal(fij),cimag(fij));
            fprintf(ev,"%12.8lf + %12.8lf I",creal(fij),cimag(fij));
          }
          fprintf(ev,"\n");
        }
        for(i=0;i<mm->nrMMatoms;i++){
          for(j=0;j<DIM;j++){
            fij = betasq*(MMgrad_S1[i][j]-MMgrad_S0[i][j]);
            fij-= (bpaq+apbq)*tdmXMM[i][j]*u[0];
            fij-= (bpaq+apbq)*tdmYMM[i][j]*u[1];
            fij-= (bpaq+apbq)*tdmZMM[i][j]*u[2];
            fij*=HARTREE_BOHR2MD;
            fij/=(HARTREE2KJ*AVOGADRO*gap);
          }
        }  
      }
      fclose(ev);
    }
  }
} /* print_NAC */

int compute_hopping_probability(int step,t_QMrec *qm, dplx *c, dplx *U, int ndim){
  double
    *p,b,btot=0.0,ptot=0.,rnr;
  int
    i,j,k,current,hopto;
  dplx
    *cold;

  snew(cold,ndim);
  snew(p,ndim);
  if (qm->bMASH){
    /* J. R. Mannouch and J. O. Richardson, “A mapping approach to surface hopping,” J. Chem. Phys. 158, 104111 (2023). */
    double max=0.0;
    
    for (i = 0 ; i < ndim ; i++ ){
      b=conj(c[i])*c[i];
      if ( b > max ){
	max = b;	  
	hopto = i;
      }
    }
  }
  else{ /* Granucci */
    for (i=0;i<ndim;i++){
      cold[i]=qm->creal[i]+IMAG*qm->cimag[i];
    }
    
    current = hopto = qm->polariton;
    
    fprintf(stderr," population that leaves state %d: %lf\n",
	    current,(conj(cold[current])*cold[current]-conj(c[current])*c[current]));
    fprintf(stderr, "probability to leave state %d is %lf\n",current,
	    (conj(cold[current])*cold[current]-conj(c[current])*c[current])/(conj(cold[current])*cold[current]));
    /* total probability to leave 
     */ 
    ptot=(conj(cold[current])*cold[current]-conj(c[current])*c[current])/(conj(cold[current])*cold[current]);
    if (ptot<=0){
      for ( i = 0 ; i < ndim ; i++ ){
	p[i] = 0;
      }
    } 
    else{
      btot=0.0;
      for ( i = 0 ; i < ndim ; i++ ){
	if ( i != current ){
	  b = conj(U[i*ndim+current]*cold[current])*U[i*ndim+current]*cold[current];
	  if (b>0.0){
	    btot+=b;
	    p[i]=b;
	  }
	  fprintf(stderr,"from state %d to state %d, b = %lf\n",current,i,b);
	}
      }
      for (i = 0 ;i<ndim;i++){
	p[i]=p[i]/btot*ptot;
      }
    }
    ptot=0.0;
    rnr = qm->rnr[step];
    for(i=0;i<ndim;i++){
      if ( i != current && ptot < rnr ){
	fprintf(stderr,"probability to hop from %d to %d is %lf\n",current,i,p[i]);
	if ( ptot+p[i] > rnr ) {
	  hopto = i;
	  fprintf(stderr,"hopping at step %d with probability %lf\n",step,ptot+p[i]);
	}
	ptot+=p[i];
      }
    }
  }
  free(p);
  free(cold);
  return(hopto);
} /* compute_hopping_probability */

double do_hybrid_non_herm(t_commrec *cr,  t_forcerec *fr, 
			  t_QMrec *qm, t_MMrec *mm, rvec f[], rvec fshift[],
			  dplx *matrix, int step,
			  rvec QMgrad_S1[],rvec MMgrad_S1[],
			  rvec QMgrad_S0[],rvec MMgrad_S0[],
			  rvec tdmX[], rvec tdmY[], rvec tdmZ[],
			  rvec tdmXMM[], rvec tdmYMM[], rvec tdmZMM[],
			  double *energies)
{
  double
    decay,L_au=qm->L*microM2BOHR,
    E0_norm_sq,V0_2EP,u[3],QMener=0.,totpop=0.,
    *eigvec_real,*eigvec_imag,*eigval,ctot=0.,dtot=0.,fcorr;
  dplx
    fij,csq,cmcp,*ham,
    *expH,*ctemp,*c,cicj,ener=0.,*d,*dtemp,
    *eigvec,cpcq,bpaq,apbq,bqap,aqbp,a_sump,a_sumq,
    a_sum,a_sum2,betasq,ab,ba,*temp,*uold,*udagger,*umatrix;
  int
    dodiag=0,doprop=0,*state,i,j,k,p,q,m,nmol,ndim,prop,dohop[1],hopto[1];
  char
    *eigenvectorfile,*coefficientfile,*energyfile,buf[3000];
  FILE
    *evout=NULL,*Cout=NULL;
    /* I think these shoudl be complex... for sigle mode it won't matter though */
  rvec
    *nacQM=NULL,*nacMM=NULL;
  

  if (fr->qr->SHmethod != eSHmethodGranucci && fr->qr->SHmethod != eSHmethodEhrenfest){
    gmx_fatal(FARGS,
	      "Running in hybrid diabatic/adiabatic only possible for Ehrenfest of Surface hopping with local diabatization\n");
  }

  /* information on field, was already calcualted above...
   */
  E0_norm_sq = iprod(qm->E,qm->E); // Square of the magitud of the E-field at k=0
  V0_2EP = qm->omega/(E0_norm_sq); //2*Epsilon0*V_cav at k=0 (in a.u.)
  u[0]=qm->E[0]/sqrt(E0_norm_sq);
  u[1]=qm->E[1]/sqrt(E0_norm_sq);
  u[2]=qm->E[2]/sqrt(E0_norm_sq);
  /* silly array to communicate the current state
   */
  snew(state,1);
  /* decide which node does propagation and which node does diagonalization
   */
  if(MULTISIM(cr)){
    ndim=cr->ms->nsim+(qm->n_max-qm->n_min)+1;
    m=cr->ms->sim;
    if(m==0){
      doprop=1;
    }
    else if (m==1){
      dodiag=1;
    }
    nmol=cr->ms->nsim;
  }
  else{
    ndim=1+(qm->n_max-qm->n_min)+1;
    m=0;
    dodiag=1;
    doprop=1;
    nmol=1;
  }
  snew(expH, ndim*ndim);
  snew(ctemp,ndim);
  snew(c,ndim);
  snew(d,ndim);
  snew(dtemp,ndim*ndim);
  snew(ham,ndim*ndim);
  snew(temp,ndim*ndim);
  snew(eigval,ndim);
  snew(eigvec,ndim*ndim);
  snew(eigvec_real,ndim*ndim);
  snew(eigvec_imag,ndim*ndim);
  snew(uold,ndim*ndim);
  snew(udagger,ndim*ndim);
  snew(umatrix,ndim*ndim);
  hopto[0]=qm->polariton;
  if(dodiag){
    /* node 1 diagonalizes the matrix 
     */
    fprintf(stderr,"\n\ndiagonalizing matrix on node %d\n",m);
    diag(ndim,eigval,eigvec,matrix);
    fprintf(stderr,"step %d Eigenvalues: ",step);
    for ( i = 0 ; i<ndim;i++){
      fprintf(stderr,"%lf ",eigval[i]);
      qm->eigval[i]=eigval[i]; 
    }
    fprintf(stderr,"\n");
    for(i=0;i<ndim*ndim;i++){
      eigvec_real[i]=creal(eigvec[i]);
      eigvec_imag[i]=cimag(eigvec[i]);
    }
  }
  /* while node 0 performs propagation in the diabatic basis
   */
  if(doprop){
    if(step){
      for (i=0;i<ndim;i++){
        d[i]=qm->dreal[i]+IMAG*qm->dimag[i];
      }
      /* interpolate the Hamiltonian 
       */
      for(i=0;i<ndim*ndim;i++){
        ham[i]=matrix[i]+qm->matrix[i];
      }
      /* Add the losses -i\gamma\hat{a}^\dagger\hat{a}
       * assuming these do not depend on R or t 
       * The factor 2 is handled in the expM_non_herm function 
       */
      for(i=nmol;i<ndim;i++){
        /* hbar*decay rate 6.582119569*10^(\[Minus]16)/10^(-12) into AU
         */
        ham[i*ndim+i]-=IMAG*(qm->QEDdecay)*0.0006582119569/27.2114;
      }
      /* propagate the coefficients 
       */
      expM_non_herm(ndim,ham,expH, qm->dt);
      MtimesV_complex(ndim,expH,d,dtemp);
      for( i=0;i<ndim;i++){
        d[i]=dtemp[i];
      }
      for ( i = 0 ; i < ndim ; i++ ){	
        qm->dreal[i] = creal(d[i]);
        qm->dimag[i] = cimag(d[i]);
      }
      /* some writing 
       */
      fprintf(stderr,"step %d: |D|^2: ",step);
      for(i=0;i<ndim;i++){
        fprintf (stderr," %.5lf ",conj(d[i])*d[i]);
      }
      fprintf(stderr,"\n");
    }
  }
  else {
    /* All other nodes: just set d to zero, these will be filled in the next communication 
     * step next with the values computed by node 0
     */
    for(i=0;i<ndim;i++){
      qm->dimag[i]=0.;
      qm->dreal[i]=0.;
    }
  }
  if (!step){
    /* first step is special. We check if the user has provided adiabatic expansion
     * coefficeints (C.dat), or diabatic coeficients (D.dat). The adiabatic coefficients are 
     * transform into the diabatic coefficients. Because the unitary matrix required for the 
     * transformation is present only on the node that does the diagonaliation at this point, we 
     * also let that node handle the transformation. If needed. To undertand if diabatic or 
     * adiabatic coefficients are supplied in init_QMMM, we check this by computing the norms.
     */
    if(MULTISIM(cr)){
      gmx_sumd_sim(ndim,qm->dreal ,cr->ms);
      gmx_sumd_sim(ndim,qm->dimag ,cr->ms);
    }
    if(dodiag){
      ctot=dtot=0.0;
      for(i=0;i<ndim;i++){
        dtot  += qm->dreal[i]*qm->dreal[i]+ qm->dimag[i]*qm->dimag[i];
        ctot  += qm->creal[i]*qm->creal[i]+ qm->cimag[i]*qm->cimag[i];
      }
      /* one of these should be 0. If that is
       * c, then we need to compute the d, otherwise we need to compute c.
       */
      for (i=0;i<ndim*ndim;i++){
	/* hermitian adjoint of U. Because the matrix is stored in row 
	 * format we need to transpose it. We do that in two steps:
	 * first make the conjugate() of all 
	 * elements, and then make the adjoint
	 */
        udagger[i]=eigvec_real[i]-IMAG*eigvec_imag[i];
      }
      if(ctot>dtot){      
        for(i=0;i<ndim;i++){
          c[i] = qm->creal[i]+ IMAG*qm->cimag[i];
        }
      transposeM_complex(ndim,udagger,umatrix);
	  MtimesV_complex(ndim,umatrix,c,d);
	  for ( i = 0 ; i < ndim ; i++ ){
	    qm->dreal[i] = creal(d[i]);
	    qm->dimag[i] = cimag(d[i]);
      }
	  /* keep C */
    }
    /* the user has supplied d at t=0, we tranform those in the c(0)
     */
    else{
	  for(i=0;i<ndim;i++){
	    d[i] = qm->dreal[i]+ IMAG*qm->dimag[i];
	  }
	  MtimesV_complex(ndim,udagger,d,c);
	  for ( i = 0 ; i < ndim ; i++ ){
	    qm->creal[i] = creal(c[i]);
	    qm->cimag[i] = cimag(c[i]);
	  }
	/* keep D */
    }
    /* some writing
     */
    fprintf(stderr,"step %d: |D|^2: ",step);
    for(i=0;i<ndim;i++){
	  fprintf (stderr," %.5lf ",
      		 (qm->dreal[i])*(qm->dreal[i])+(qm->dimag[i])*(qm->dimag[i]) );    
    }
    fprintf(stderr,"\nstep %d: |C|^2: ",step);
    for(i=0;i<ndim;i++){
	  fprintf (stderr," %.5lf ",
      		 (qm->creal[i])*(qm->creal[i])+(qm->cimag[i])*(qm->cimag[i]) );    
    }
    fprintf(stderr,"\n");
  }
  else{
    /* reset all coefficients on the other nodes to 0, even if they
     * had them, because these will be communcicated from node "dodiag"
     */
    for(i=0;i<ndim;i++){
	  qm->dreal[i] = qm->dimag[i] = qm->creal[i] = qm->cimag[i]= 0.;
    }
  }
  if(MULTISIM(cr)){
    gmx_sumd_sim(ndim,qm->creal ,cr->ms);
    gmx_sumd_sim(ndim,qm->cimag ,cr->ms);
  }
}
/* communicate eigenvectors from node 0 and diabatic expansion coefficients from node1
 */
if(MULTISIM(cr)){
  /* One processor has the eigenvectors. Another one has the
   * time-dependent diabatic expansion coefficients.
   * Eigenvectors, and coefficients are now communicates to the other
   * nodes, so that every one can compute the mean-field forces to the
   * other nodes, etc. We also communication the eigenvalues,
   * even we don't seem to need them.
   */
  gmx_sumd_sim(ndim*ndim,eigvec_real,cr->ms);
  gmx_sumd_sim(ndim*ndim,eigvec_imag,cr->ms);
  gmx_sumd_sim(ndim,eigval,cr->ms);
  gmx_sumd_sim(ndim,qm->dreal ,cr->ms);
  gmx_sumd_sim(ndim,qm->dimag ,cr->ms);
}
for(i=0;i<ndim*ndim;i++){
  eigvec[i]=eigvec_real[i]+IMAG*eigvec_imag[i];
}
  
/* now all MPI tasks have the diabatic expansion coefficients,
 * the adiabatic eigenvectors
 * and eigenvalues at the current timestep. We now transform the diabatic
 * coefficients that are propagated in time, to the
 * adiabatic coefficients and perform the Molecular Dynamics in
 * the adiabatic representation,
 * using either Ehrenfest or surface hoping
 */
 
/* Step 1: Create the total adiabatic wavefunction
 *
 * we do this on the node that has the expH propagator in the diabatic basis.
 */
if (doprop){
  /* Eigenvectors are stored as array of rows, where rows are the vectors,
   * this is thus the transpose of the U. To make the complex transpose
   * we therefore need to take the complex conjute. We do that first.
   */
  for(i=0;i<ndim;i++){
    for (j=0;j<ndim;j++){
      /* hermitian adjoint of U
       */
      udagger[i*ndim+j]=eigvec_real[i*ndim+j]-IMAG*eigvec_imag[i*ndim+j];
      /* transpose of U, i.e. eigenvectors are columns
	   */
      uold[i*ndim+j] = qm->eigvec[j*ndim+i];
    }
  }
  if(step){
    for (i=0;i<ndim;i++){
	  d[i]=qm->dreal[i]+IMAG*qm->dimag[i];
	  c[i]=qm->creal[i]+IMAG*qm->cimag[i];
    }
    M_complextimesM_complex(ndim,udagger,expH,temp);
    M_complextimesM_complex(ndim,temp,uold,expH);
    MtimesV_complex(ndim,expH,c,ctemp);
    for( i=0;i<ndim;i++){
	  c[i]=ctemp[i];
    }
    if (fr->qr->SHmethod == eSHmethodGranucci){
	  //qm->polariton = state[0] =
	  hopto[0]= compute_hopping_probability(step,qm,c,expH,ndim);
    }
    for ( i = 0 ; i < ndim ; i++ ){
      qm->creal[i] = creal(c[i]);
	  qm->cimag[i] = cimag(c[i]);
    }
    /* some writing
    */
    fprintf(stderr,"step %d: |C|^2: ",step);
    for(i=0;i<ndim;i++){
	    fprintf (stderr," %.5lf ",conj(c[i])*c[i]);
    }
      fprintf(stderr,"\n");
    }
  }
  else{
    /* zero the expansion coefficient on the other nodes. They get filled in
     * in the next communication step 
     */
    hopto[0]=0;
    for(i=0;i<ndim;i++){
      qm->cimag[i]=0.;
      qm->creal[i]=0.;
    }
  }
  /* comunicate the information on adiabatic coefficient to all
   * nodes to compute forces... 
   */
  if(MULTISIM(cr)){
    gmx_sumi_sim(1,state,cr->ms);
    gmx_sumi_sim(1,hopto,cr->ms);
    /* not sure abotu the next line... */
    //    qm->polariton = state[0];
    gmx_sumd_sim(ndim,qm->creal ,cr->ms);
    gmx_sumd_sim(ndim,qm->cimag ,cr->ms);
  }
  /* compute the norm of the wavefunction when there are losses
   */
  for(i=0;i<ndim;i++){
    totpop+=qm->creal[i]*qm->creal[i]+qm->cimag[i]*qm->cimag[i];
  }
  qm->groundstate=1-totpop;
  /* copy the new eigenvectors to qmrec (become old vectors)
   */
  for(i=0;i<ndim*ndim;i++){
    qm->eigvec[i]=eigvec[i];
  }
  /* Step 2b: If we want to make a hop, we check if there is sufficent kinetic energy
   * for that. If we do hop we also need to adjust the velocities, as the
   * work is done in the direction of the NAC. We thus first compute
   * the contributions of each molecule to the total NAC vector
   */ 
  if(hopto[0] != qm->polariton){
    fprintf(stderr,"checking if there is sufficient energy to hop from %d to %d\n",
	      qm->polariton,hopto[0]);
    snew(nacQM,qm->nrQMatoms);
    snew(nacMM,mm->nrMMatoms);
    get_NAC(ndim,nmol,eigvec,eigval,tdmX,tdmY,tdmZ,tdmXMM,tdmYMM,tdmZMM,
	    qm,mm,m,QMgrad_S0,QMgrad_S1,MMgrad_S0,MMgrad_S1,qm->polariton,hopto[0],
	    nacQM, nacMM);
    dohop[0] = check_vel(cr,eigval,nacQM,nacMM,qm,mm,
			 qm->polariton,hopto[0],&fcorr);
    if(MULTISIM(cr)){
      gmx_sumi_sim(1,dohop,cr->ms);
    }
    /* Only if there is sufficient kinetic energy on all nodes, we hop
     */
    if(dohop[0]==nmol){
      fprintf(stderr,"hop from %d to %d energetically allowed, adjusting velocities\n",
	      qm->polariton,hopto[0]);
      
      qm->polariton = state[0] = hopto[0];
    }
    else{
      fprintf(stderr,"hop attempted, but there is not sufficient kinetic energy -> frustrated hop\nReversing velocities...\n");
    }
    adjust_vel(m,fr->qr,qm,mm,fcorr,nacQM,nacMM);
    
    free(nacQM);
    free(nacMM);
  }
  /* step 3: compute the gradients and sum up the energy
   */
  if(fr->qr->SHmethod != eSHmethodEhrenfest){
    p=qm->polariton;
    betasq = conj(eigvec[p*ndim+m])*eigvec[p*ndim+m];
    a_sump = 0.0+IMAG*0.0;
    for (i=0;i<(qm->n_max-qm->n_min)+1;i++){
      a_sump += eigvec[p*ndim+nmol+i]*sqrt(cavity_dispersion(i+qm->n_min,qm)/V0_2EP)*cexp(IMAG*2*M_PI*(i+qm->n_min)/L_au*m*L_au/((double) nmol));
    }
    ab = conj(eigvec[p*ndim+m])*a_sump; //actually sum of alphas * beta_j
    ab += conj(a_sump)*eigvec[p*ndim+m];
    for(i=0;i<qm->nrQMatoms;i++){
      for(j=0;j<DIM;j++){
        /* diagonal term
         */
        fij =(betasq*QMgrad_S1[i][j]+(1-betasq)*QMgrad_S0[i][j]);
        /* off-diagonal term
         */
        fij-= ab*tdmX[i][j]*u[0];
        fij-= ab*tdmY[i][j]*u[1];
        fij-= ab*tdmZ[i][j]*u[2];
        fij*=HARTREE_BOHR2MD;
        f[i][j]      += creal(fij);
        fshift[i][j] += creal(fij);
      }
    }
    for(i=0;i<mm->nrMMatoms;i++){
      for(j=0;j<DIM;j++){
        /* diagonal term
         */
        fij =(betasq*MMgrad_S1[i][j]+(1-betasq)*MMgrad_S0[i][j]);
        /* off-diagonal term
         */
        fij-= ab*tdmXMM[i][j]*u[0];
        fij-= ab*tdmYMM[i][j]*u[1];
        fij-= ab*tdmZMM[i][j]*u[2];
        fij*=HARTREE_BOHR2MD;
        f[i+qm->nrQMatoms][j]      += creal(fij);
        fshift[i+qm->nrQMatoms][j] += creal(fij);
      }
    }
    QMener = eigval[p]*HARTREE2KJ*AVOGADRO;
  }
  else {
    /* Ehrenfest dynamics, need to compute gradients of all polaritonic
     * states and weigh them with weights of the states. Also the
     * nonadiabatic couplings between polaritonic states are needed now
     */
    QMener=0.0; 
    for (p=0;p<ndim;p++){
      /* do the diagonal terms first p=q. These terems are same as above for 
       * single state procedure. If losses are included, we divide by the norm of 
       * the wavefunction: totpop
       */
      csq = conj(qm->creal[p]+IMAG*qm->cimag[p])*(qm->creal[p]+IMAG*qm->cimag[p]);
      betasq = conj(eigvec[p*ndim+m])*eigvec[p*ndim+m];
      a_sump = 0.0+IMAG*0.0;
      for (i=0;i<(qm->n_max-qm->n_min)+1;i++){
        a_sump += eigvec[p*ndim+nmol+i]*sqrt(cavity_dispersion(i+qm->n_min,qm)/V0_2EP)*cexp(IMAG*2*M_PI*(i+qm->n_min)/L_au*m*L_au/((double) nmol));
      }
      ab = conj(eigvec[p*ndim+m])*a_sump; //actually sum of alphas * beta_j
      ab += conj(a_sump)*eigvec[p*ndim+m]; // ADDED GG, this accounts for the 3rd and 4th term together in equation 13.
      /* we normalize the total energy: 
       */
      QMener += csq*eigval[p]*HARTREE2KJ*AVOGADRO/totpop;
      for(i=0;i<qm->nrQMatoms;i++){
        for(j=0;j<DIM;j++){
          /* diagonal term
           */
          fij =(betasq*QMgrad_S1[i][j]+(1-betasq)*QMgrad_S0[i][j]);
          /* off-diagonal term, Because coeficients are real: ab = ba
           */
          fij-= ab*tdmX[i][j]*u[0];
          fij-= ab*tdmY[i][j]*u[1];
          fij-= ab*tdmZ[i][j]*u[2];
          fij*=HARTREE_BOHR2MD*csq/totpop;
          f[i][j]      += creal(fij);
          fshift[i][j] += creal(fij);
        }
      }
      for(i=0;i<mm->nrMMatoms;i++){
        for(j=0;j<DIM;j++){
          /* diagonal terms
           */
          fij =(betasq*MMgrad_S1[i][j]+(1-betasq)*MMgrad_S0[i][j]);
          /* off-diagonal term
           */
          fij-= ab*tdmXMM[i][j]*u[0];
          fij-= ab*tdmYMM[i][j]*u[1];
          fij-= ab*tdmZMM[i][j]*u[2];
          fij*=HARTREE_BOHR2MD*csq/totpop;
          f[i+qm->nrQMatoms][j]      += creal(fij);
          fshift[i+qm->nrQMatoms][j] += creal(fij);
        }
      }    
      /* now off-diagonals 
       */
      for (q=p+1;q<ndim;q++){
	    /* normalize
	     */
        cpcq = conj(qm->creal[p]+IMAG*(qm->cimag[p]))*(qm->creal[q]+IMAG*(qm->cimag[q]));
        betasq = conj(eigvec[p*ndim+m])*eigvec[q*ndim+m];
        bpaq = conj(a_sum)*eigvec[q*ndim+m];
        a_sumq = 0.0+IMAG*0.0;
        for (i=0;i<(qm->n_max)+1;i++){
          a_sumq += eigvec[q*ndim+nmol+i]*sqrt(cavity_dispersion(i+qm->n_min,qm)/V0_2EP)*cexp(IMAG*2*M_PI*(i+qm->n_min)/L_au*m*L_au/((double) nmol));
        }
        bpaq = conj(eigvec[p*ndim+m])*a_sumq;
        apbq = conj(a_sump)*eigvec[q*ndim+m];
        bqap = conj(eigvec[q*ndim+m])*a_sump; /* conj(apbq) */
        aqbp = conj(a_sumq)*eigvec[p*ndim+m]; /* conj(bpaq) */
        for(i=0;i<qm->nrQMatoms;i++){
          for(j=0;j<DIM;j++){
            /* diagonal term
             */
            fij=0;
	        fij = cpcq*betasq*(QMgrad_S1[i][j]-QMgrad_S0[i][j]);
            fij+= conj(cpcq)*conj(betasq)*(QMgrad_S1[i][j]-QMgrad_S0[i][j]);
	        /* off-diagonal term
	         */
            fij-= cpcq*(bpaq+apbq)*tdmX[i][j]*u[0];
            fij-= cpcq*(bpaq+apbq)*tdmY[i][j]*u[1];
            fij-= cpcq*(bpaq+apbq)*tdmZ[i][j]*u[2];
            fij-= conj(cpcq)*(bqap+aqbp)*tdmX[i][j]*u[0];
            fij-= conj(cpcq)*(bqap+aqbp)*tdmY[i][j]*u[1];
            fij-= conj(cpcq)*(bqap+aqbp)*tdmZ[i][j]*u[2];
            fij*=HARTREE_BOHR2MD/totpop;
	        f[i][j]      += creal(fij);
            fshift[i][j] += creal(fij);
	      }
        }
        for(i=0;i<mm->nrMMatoms;i++){
          for(j=0;j<DIM;j++){
            /* diagonal term
             */
            fij = cpcq*betasq*(MMgrad_S1[i][j]-MMgrad_S0[i][j]);
            fij+= conj(cpcq)*conj(betasq)*(MMgrad_S1[i][j]-MMgrad_S0[i][j]);
            /* off-diagonal term
             */
            fij-= cpcq*(bpaq+apbq)*tdmXMM[i][j]*u[0];
            fij-= cpcq*(bpaq+apbq)*tdmYMM[i][j]*u[1];
            fij-= cpcq*(bpaq+apbq)*tdmZMM[i][j]*u[2];
            fij-= conj(cpcq)*(bqap+aqbp)*tdmXMM[i][j]*u[0];
            fij-= conj(cpcq)*(bqap+aqbp)*tdmYMM[i][j]*u[1];
            fij-= conj(cpcq)*(bqap+aqbp)*tdmZMM[i][j]*u[2];
            fij*=HARTREE_BOHR2MD/totpop;
            f[i+qm->nrQMatoms][j]      += creal(fij);
            fshift[i+qm->nrQMatoms][j] += creal(fij);
          }
        }
      }
    }
  }
  /* printing the coefficients to C.dat 
   * print the adiabatic eigenvectors to a file 
   */
  if( dodiag ){
    snew(eigenvectorfile,3000);
    sprintf(eigenvectorfile,"%s/eigenvectors.dat",qm->work_dir);
    evout=fopen(eigenvectorfile,"a");
    for(i=0;i<ndim;i++){
      fprintf(evout,
	      "step %4d Eigenvector %4d gap %12.8lf (c: %12.8lf + %12.8lf I):",
	      step,i,eigval[i]-(
				energies[ndim-1]-cavity_dispersion(qm->n_max,qm)),
	      qm->creal[i],qm->cimag[i]);
      for(k=0;k<ndim;k++){
        fprintf(evout," %12.8lf + %12.8lf I",creal(eigvec[i*ndim+k]),cimag(eigvec[i*ndim+k]));
      }
      fprintf(evout,"\n");
    }
    fclose(evout);
    free(eigenvectorfile);
    snew(coefficientfile,3000);
    sprintf(coefficientfile,"%s/coefficients.dat",qm->work_dir);
    evout=fopen(coefficientfile,"a");
    fprintf(evout,
	    "step %4d energy: %12.8lf coeff: ", step,QMener);
    for(k=0;k<ndim;k++){
      fprintf(evout," %12.8lf + %12.8lf I",qm->dreal[k],qm->dimag[k]);
      //      totpop+=qm->creal[k]*qm->creal[k]+qm->cimag[k]*qm->cimag[k];
    }
    fprintf(evout,"\n");
    fclose(evout);
    free(coefficientfile);    
    fprintf(stderr,"rho0 (%d) = %lf, Energy = %lf\n",step,qm->groundstate,energies[ndim-1]-cavity_dispersion(qm->n_max,qm));
    sprintf(buf,"%s/C.dat",qm->work_dir);
    Cout= fopen(buf,"w");
    fprintf(Cout,"%d\n",step);
    for(i=0;i<ndim;i++){
      fprintf (Cout,"%.5lf %.5lf\n ",qm->creal[i],qm->cimag[i]);
    }
    fprintf (Cout,"%.5lf\n",qm->groundstate);
    fclose(Cout);
  }
  /* now do the decoherence that will happen in the next timestep 
   * we thus use the current total kinetic energy. I suppose this is the kinetic energy 
   * after the velocity correct? 
   * We need to send this 
   * around we wrote separate routine. Decoherence corrections make sense
   * only for surface hopping methods, so we check for that.
   */
  if ( ( fr->qr->SHmethod == eSHmethodGranucci )
       && (qm->QEDdecoherence > 0.) ){
    decoherence(cr,qm,mm,ndim,eigval);
    /* to capture the effect on d, we transform: d=Uc;
     */
    if (doprop){ 
      for(i=0;i<ndim;i++){
        c[i] = qm->creal[i]+ IMAG*qm->cimag[i];
      }
      transposeM_complex(ndim,udagger,umatrix);
      MtimesV_complex(ndim,umatrix,c,d);
      for(i=0;i<ndim;i++){
        qm->dreal[i] = creal(d[i]);
        qm->dimag[i] = cimag(d[i]);
      }
    }
    
    else{
      /* set elements to zero and sum them up later
       */
      for(i=0;i<ndim;i++){
        qm->dreal[i] = 0;
        qm->dimag[i] = 0;
        qm->dreal[i] = 0;
        qm->dimag[i] = 0;
      }
    } 
    if(MULTISIM(cr)){
      gmx_sumd_sim(ndim,qm->dreal ,cr->ms);
      gmx_sumd_sim(ndim,qm->dimag ,cr->ms);
    }
  }
  free (eigval);
  free (eigvec);
  free (eigvec_real);
  free (eigvec_imag);
  free (state);
  free(umatrix);
  free(uold);
  free(udagger);
  free(expH);
  free(ctemp);
  free(c);
  free(d);
  free(dtemp);
  free(ham);
  free(temp);  
  return (QMener);
}/* do_hybrid_non_herm */

double do_hybrid(t_commrec *cr,  t_forcerec *fr, 
		   t_QMrec *qm, t_MMrec *mm, rvec f[], rvec fshift[],
		   dplx *matrix, int step,
		   rvec QMgrad_S1[],rvec MMgrad_S1[],
		   rvec QMgrad_S0[],rvec MMgrad_S0[],
		   rvec tdmX[], rvec tdmY[], rvec tdmZ[],
		   rvec tdmXMM[], rvec tdmYMM[], rvec tdmZMM[],double *energies)
{
  double
    decay,L_au=qm->L*microM2BOHR,
    E0_norm_sq,V0_2EP,u[3],QMener=0.,totpop=0.,
    *eigvec_real,*eigvec_imag,*eigval,ctot=0.,dtot=0.,fcorr;
  dplx
    fij,csq,cmcp,*ham,
    *expH,*ctemp,*c,cicj,ener=0.,*d,*dtemp,
    *eigvec,cpcq,bpaq,apbq,bqap,aqbp,a_sump,a_sumq,
    a_sum,a_sum2,betasq,ab,ba,*temp,*uold,*udagger,*umatrix;
  time_t 
    start,end,interval;
  int
    dodiag=0,doprop=0,*state,i,j,k,p,q,m,nmol,ndim,prop,hopto[1],dohop[1];
  char
    *eigenvectorfile,*coefficientfile,*energyfile,buf[3000];
  FILE
    *evout=NULL,*Cout=NULL;
  rvec
    *nacQM=NULL,*nacMM=NULL;
  start = time(NULL);
  if (fr->qr->SHmethod != eSHmethodGranucci && fr->qr->SHmethod != eSHmethodEhrenfest){
    gmx_fatal(FARGS,
	      "Running in hybrid diabatic/adiabatic only possible for Ehrenfest of Surface hopping with local diabatization\n");
  }
  snew(eigenvectorfile,3000);
  sprintf(eigenvectorfile,"%s/eigenvectors.dat",qm->work_dir);
  /* information on field, was already calcualted above...
   */
  E0_norm_sq = iprod(qm->E,qm->E); // Square of the magitud of the E-field at k=0
  V0_2EP = qm->omega/(E0_norm_sq); //2*Epsilon0*V_cav at k=0 (in a.u.)
  u[0]=qm->E[0]/sqrt(E0_norm_sq);
  u[1]=qm->E[1]/sqrt(E0_norm_sq);
  u[2]=qm->E[2]/sqrt(E0_norm_sq);

  /* silly array to communicate the courrent state
   */
  snew(state,1);

  if(MULTISIM(cr)){
    ndim=cr->ms->nsim+(qm->n_max-qm->n_min)+1;
    m=cr->ms->sim;
    if(m==0){
      doprop=1;
    }
    else if (m==1){
      dodiag=1;
    }
    nmol=cr->ms->nsim;
  }
  else{
    ndim=1+(qm->n_max-qm->n_min)+1;
    m=0;
    dodiag=1;
    doprop=1;
    nmol=1;
  }
  snew(expH, ndim*ndim);
  snew(ctemp,ndim);
  snew(c,ndim);
  snew(d,ndim);
  snew(dtemp,ndim*ndim);
  snew(ham,ndim*ndim);
  snew(temp,ndim*ndim);
  snew(eigval,ndim);
  snew(eigvec,ndim*ndim);
  snew(eigvec_real,ndim*ndim);
  snew(eigvec_imag,ndim*ndim);
  snew(uold,ndim*ndim);
  snew(udagger,ndim*ndim);
  snew(umatrix,ndim*ndim);
  hopto[0]=qm->polariton;

  if(dodiag){
    /* diagonalize the matrix to get the adiabatic basis states
     */
    fprintf(stderr,"\n\ndiagonalizing matrix on node %d\n",m);
    diag(ndim,eigval,eigvec,matrix);
    fprintf(stderr,"step %d Eigenvalues: ",step);
    for ( i = 0 ; i<ndim;i++){
      fprintf(stderr,"%lf ",eigval[i]);
      qm->eigval[i]=eigval[i];
    }
    fprintf(stderr,"\n");
    for(i=0;i<ndim*ndim;i++){
      eigvec_real[i]=creal(eigvec[i]);
      eigvec_imag[i]=cimag(eigvec[i]);
    }
  }
  /* while node 0 performs propagation in the diabatic basis
   */
  if(doprop){
    if(step){
      for (i=0;i<ndim;i++){
        d[i]=qm->dreal[i]+IMAG*qm->dimag[i];
      }
      /* interpolate the Hamiltonian. The factor 2 is handled in expM_complex2
       */
      for(i=0;i<ndim*ndim;i++){
        ham[i]=matrix[i]+qm->matrix[i];
      }
      /* propagate the coefficients 
       */
      expM_complex2(ndim,ham,expH, qm->dt);
      MtimesV_complex(ndim,expH,d,dtemp);
      for( i=0;i<ndim;i++){
        d[i]=dtemp[i];
      }
      for ( i = 0 ; i < ndim ; i++ ){	
        qm->dreal[i] = creal(d[i]);
        qm->dimag[i] = cimag(d[i]);
      }
      /* some writing 
       */
      fprintf(stderr,"step %d: |D|^2: ",step);
      for(i=0;i<ndim;i++){
        fprintf (stderr," %.5lf ",conj(d[i])*d[i]);
      }
      fprintf(stderr,"\n");
    }
  }
  else {
    /* other nodes, just set d to zero, these will be filled next with
     * the values on node 0
     */
    for(i=0;i<ndim;i++){
      qm->dimag[i]=0.;
      qm->dreal[i]=0.;
    }
  }
  
  if (!step){
    /* first step is special. We check if the user has provided adiabatic expansion
     * coefficeints (C.dat), or diabatic coeficients (D.dat). The adiabatic coefficients are 
     * transform into the diabatic coefficients. Because the unitary matrix required for the 
     * transformation is present only on the node that does the diagonaliation at this point, we 
     * also let that node handle the transformation. If needed. To undertand if diabatic or 
     * adiabatic coefficients are supplied in init_QMMM, we check this by computing the norms.
     */
    if(MULTISIM(cr)){
      gmx_sumd_sim(ndim,qm->dreal ,cr->ms);
      gmx_sumd_sim(ndim,qm->dimag ,cr->ms);
    }
    if(dodiag){
      ctot=dtot=0.0;
      for(i=0;i<ndim;i++){
        dtot  += qm->dreal[i]*qm->dreal[i]+ qm->dimag[i]*qm->dimag[i];
        ctot  += qm->creal[i]*qm->creal[i]+ qm->cimag[i]*qm->cimag[i];
      }
      /* one of these should be 0. If that is
       * c, then we need to compute the d, otherwise we need to compute c.
       */
      for (i=0;i<ndim*ndim;i++){
        /* hermitian adjoint of U. Because the matrix is stored in row
         * format we need to transpose it. We do that in two steps:
         * first make the conjugate() of all
         * elements, and then make the adjoint
         */
        udagger[i]=eigvec_real[i]-IMAG*eigvec_imag[i];
      }
      if(ctot>dtot){      
        for(i=0;i<ndim;i++){
          c[i] = qm->creal[i]+ IMAG*qm->cimag[i];
        }
        transposeM_complex(ndim,udagger,umatrix);
        MtimesV_complex(ndim,umatrix,c,d);
        for ( i = 0 ; i < ndim ; i++ ){
          qm->dreal[i] = creal(d[i]);
          qm->dimag[i] = cimag(d[i]);
        }
        /* keep C */
      }
      /* the user has supplied d at t=0, we tranform those in the c(0)
       */
      else{
        for(i=0;i<ndim;i++){
          d[i] = qm->dreal[i]+ IMAG*qm->dimag[i];
        }
        MtimesV_complex(ndim,udagger,d,c);
        for ( i = 0 ; i < ndim ; i++ ){
          qm->creal[i] = creal(c[i]);
          qm->cimag[i] = cimag(c[i]);
        }
        /* keep D */
      }     
      /* some writing 
       */
      fprintf(stderr,"step %d: |D|^2: ",step);
      for(i=0;i<ndim;i++){
        fprintf (stderr," %.5lf ",
                 (qm->dreal[i])*(qm->dreal[i])+(qm->dimag[i])*(qm->dimag[i]) );
      }
      fprintf(stderr,"\nstep %d: |C|^2: ",step);
      for(i=0;i<ndim;i++){
        fprintf (stderr," %.5lf ",
          (qm->creal[i])*(qm->creal[i])+(qm->cimag[i])*(qm->cimag[i]) );
      }
      fprintf(stderr,"\n");
    }
    else{
      /* reset all coefficients on the other nodes to 0, even if they 
       * had them, because these will be communcicated from node "dodiag"
       */
      for(i=0;i<ndim;i++){
        qm->dreal[i] = qm->dimag[i] = qm->creal[i] = qm->cimag[i]= 0.;
      }
    }
    if(MULTISIM(cr)){
      gmx_sumd_sim(ndim,qm->creal ,cr->ms);
      gmx_sumd_sim(ndim,qm->cimag ,cr->ms);
    }
  }

  /* communicate eigenvectors from node 0 and diabatic expansion coefficients from node1
   */
  if(MULTISIM(cr)){
    /* One processor has the eigenvectors. Another one has the 
     * time-dependent diabatic expansion coefficients.
     * Eigenvectors, and coefficients are now communicates to the other 
     * nodes, so that every one can compute the mean-field forces to the 
     * other nodes, etc. We also communication the eigenvalues, 
     * even we don't seem to need them. 
     */
    gmx_sumd_sim(ndim*ndim,eigvec_real,cr->ms);
    gmx_sumd_sim(ndim*ndim,eigvec_imag,cr->ms);
    gmx_sumd_sim(ndim,eigval,cr->ms);
    gmx_sumd_sim(ndim,qm->dreal ,cr->ms);
    gmx_sumd_sim(ndim,qm->dimag ,cr->ms);  
  }
  for(i=0;i<ndim*ndim;i++){
    eigvec[i]=eigvec_real[i]+IMAG*eigvec_imag[i];
  }

  /* now all MPI tasks have the diabatic expansion coefficients, the 
   * adiabatic eigenvectors and eigenvalues at the current timestep.
   * We now switch the diabatic coefficients to the 
   * adiabatic coefficients and perform the Molecular Dynamics in the 
   * adiabatic representation,
   * using either Ehrenfest or surface hoping
   */

  /* Step 1: Create the total adiabatic wavefunction     
   * we do this on node, which has the expH propagator in the diabatic basis.
   */
  if (doprop){
    /* Eigenvectors are stored as array of rows, where rows are the vectors, 
     * this is thus the transpose of the U. To make the complex transpose 
     * we therefore need to take the complex conjute. We do that first. 
     */
    for(i=0;i<ndim;i++){
      for (j=0;j<ndim;j++){
        /* hermitian adjoint of U
         */
        udagger[i*ndim+j]=eigvec_real[i*ndim+j]-IMAG*eigvec_imag[i*ndim+j];
        /* transpose of U, i.e. eigenvectors are columns
         */
        uold[i*ndim+j] = qm->eigvec[j*ndim+i];
      }
    }   
    if(step){
      for (i=0;i<ndim;i++){
        d[i]=qm->dreal[i]+IMAG*qm->dimag[i];
        c[i]=qm->creal[i]+IMAG*qm->cimag[i];
      }
      M_complextimesM_complex(ndim,udagger,expH,temp);
      M_complextimesM_complex(ndim,temp,uold,expH);
      MtimesV_complex(ndim,expH,c,ctemp);
      for( i=0;i<ndim;i++){
        c[i]=ctemp[i];
      }
      if (fr->qr->SHmethod == eSHmethodGranucci){
          //qm->polariton = state[0] =
        hopto[0]= compute_hopping_probability(step,qm,c,expH,ndim);
      }
      for ( i = 0 ; i < ndim ; i++ ){	
        qm->creal[i] = creal(c[i]);
        qm->cimag[i] = cimag(c[i]);
      }
      /* some writing 
       */
      fprintf(stderr,"step %d: |C|^2: ",step);
      for(i=0;i<ndim;i++){
        fprintf (stderr," %.5lf ",conj(c[i])*c[i]);
      }
      fprintf(stderr,"\n");
    }
  }
  else{
    /* zero the expansion coefficient on the other nodes. They get filled in
     * in the next communication step 
     */
    hopto[0]=0;
    for(i=0;i<ndim;i++){
      qm->cimag[i]=0.;
      qm->creal[i]=0.;
    }
  }
  /* comunicate the information on adiabatic coefficient to all nodes to
   * compute forces... 
   */
  if(MULTISIM(cr)){
    gmx_sumi_sim(1,state,cr->ms);
    gmx_sumi_sim(1,hopto,cr->ms);
    gmx_sumd_sim(ndim,qm->creal ,cr->ms);
    gmx_sumd_sim(ndim,qm->cimag ,cr->ms);
  }
  /* compute norm of the wavefunction 
   */
  for(i=0;i<ndim;i++){
    totpop+=qm->creal[i]*qm->creal[i]+qm->cimag[i]*qm->cimag[i];
  }
  qm->groundstate=1-totpop;
  /* copy the eigenvectors to qmrec 
   */
  for(i=0;i<ndim*ndim;i++){
    qm->eigvec[i]=eigvec[i];
  }
  /* If we want to make a hop, we check if there is sufficent kinetic energy
   * for that. If we do hop we also need to adjust the velocities, as the
   * work is done in the direction of the NAC. We thus first compute
   * the contributions of each molecule to the total NAC vector
   */ 
  if(hopto[0] != qm->polariton){
    snew(nacQM,qm->nrQMatoms);
    snew(nacMM,mm->nrMMatoms);
    get_NAC(ndim,nmol,eigvec,eigval,tdmX,tdmY,tdmZ,tdmXMM,tdmYMM,tdmZMM,
            qm,mm,m,QMgrad_S0,QMgrad_S1,MMgrad_S0,MMgrad_S1,qm->polariton,hopto[0],
            nacQM, nacMM);
    dohop[0] = check_vel(cr,eigval,nacQM,nacMM,qm,mm,qm->polariton,hopto[0],&fcorr);
    if(MULTISIM(cr)){
      gmx_sumi_sim(1,dohop,cr->ms);
    }
    /* Only if there is sufficient kinetic energy on all nodes, we hop
     */
    if(dohop[0]==nmol){
      fprintf(stderr,"hop from %d to %d energetically allowed, adjusting velocities\n",
	      qm->polariton,hopto[0]);
      qm->polariton = state[0] = hopto[0];
    }   
    else{
      fprintf(stderr,"hop attempted, but there is not sufficient kinetic energy -> frustrated hop\nDoing nothing...\n");
    }
    adjust_vel(m,fr->qr,qm,mm,fcorr,nacQM,nacMM);
    free(nacQM);
    free(nacMM);
  }
  /* step 3: compute the gradients and sum up the energy
   */
  if(fr->qr->SHmethod != eSHmethodEhrenfest){
    p=qm->polariton;
    betasq = conj(eigvec[p*ndim+m])*eigvec[p*ndim+m];
    a_sump = 0.0+IMAG*0.0;
    for (i=0;i<(qm->n_max)+1;i++){
      a_sump += eigvec[p*ndim+nmol+i]*sqrt(cavity_dispersion(i+qm->n_min,qm)/V0_2EP)*cexp(IMAG*2*M_PI*(i+qm->n_min)/L_au*m*L_au/((double) nmol));
    }
    ab = conj(eigvec[p*ndim+m])*a_sump; //actually sum of alphas * beta_j
    ab += conj(a_sump)*eigvec[p*ndim+m];
    for(i=0;i<qm->nrQMatoms;i++){
      for(j=0;j<DIM;j++){
        /* diagonal term
         */
        fij =(betasq*QMgrad_S1[i][j]+(1-betasq)*QMgrad_S0[i][j]);
        /* off-diagonal term
         */
        fij-= ab*tdmX[i][j]*u[0];
        fij-= ab*tdmY[i][j]*u[1];
        fij-= ab*tdmZ[i][j]*u[2];
        fij*=HARTREE_BOHR2MD;
        f[i][j]      += creal(fij);
        fshift[i][j] += creal(fij);
      }
    }
    for(i=0;i<mm->nrMMatoms;i++){
      for(j=0;j<DIM;j++){
        /* diagonal term
         */
        fij =(betasq*MMgrad_S1[i][j]+(1-betasq)*MMgrad_S0[i][j]);
        /* off-diagonal term
         */
        fij-= ab*tdmXMM[i][j]*u[0];
        fij-= ab*tdmYMM[i][j]*u[1];
        fij-= ab*tdmZMM[i][j]*u[2];
        fij*=HARTREE_BOHR2MD;
        f[i+qm->nrQMatoms][j]      += creal(fij);
        fshift[i+qm->nrQMatoms][j] += creal(fij);
      }
    }
    QMener = eigval[p]*HARTREE2KJ*AVOGADRO;
  }
  else {
    /* Ehrenfest dynamics, need to compute gradients of all polaritonic
     * states and weight them with weights of the states. Also the
     * nonadiabatic couplings between polaritonic states are needed now
     */
    QMener=0.0; 
    for (p=0;p<ndim;p++){
      /* do the diagonal terms first p=q. These terems are same as above for 
       * single state procedred  
       *
       * We normalize by the sq of the wave function: totpop
       */
      csq = conj(qm->creal[p]+IMAG*qm->cimag[p])*(qm->creal[p]+IMAG*qm->cimag[p]);
      betasq = conj(eigvec[p*ndim+m])*eigvec[p*ndim+m];
      a_sump = 0.0+IMAG*0.0;
      for (i=0;i<(qm->n_max)+1;i++){
        a_sump += eigvec[p*ndim+nmol+i]*sqrt(cavity_dispersion(i+qm->n_min,qm)/V0_2EP)*cexp(IMAG*2*M_PI*(i+qm->n_min)/L_au*m*L_au/((double) nmol));
      }
      ab = conj(eigvec[p*ndim+m])*a_sump; //actually sum of alphas * beta_j
      ab += conj(a_sump)*eigvec[p*ndim+m]; // ADDED GG, this accounts for the 3rd and 4th term together in equation 13. 
      QMener += csq*eigval[p]*HARTREE2KJ*AVOGADRO/totpop;
      for(i=0;i<qm->nrQMatoms;i++){
        for(j=0;j<DIM;j++){
          /* diagonal term
           */
          fij =(betasq*QMgrad_S1[i][j]+(1-betasq)*QMgrad_S0[i][j]);
          /* off-diagonal term, Because coeficients are real: ab = ba
           */
          fij-= ab*tdmX[i][j]*u[0];
          fij-= ab*tdmY[i][j]*u[1];
          fij-= ab*tdmZ[i][j]*u[2];
          fij*=HARTREE_BOHR2MD*csq/totpop;
          f[i][j]      += creal(fij);
          fshift[i][j] += creal(fij);
        }
      }
      for(i=0;i<mm->nrMMatoms;i++){
        for(j=0;j<DIM;j++){
          /* diagonal terms
           */
          fij =(betasq*MMgrad_S1[i][j]+(1-betasq)*MMgrad_S0[i][j]);
          /* off-diagonal term
           */
          fij-= ab*tdmXMM[i][j]*u[0];
	      fij-= ab*tdmYMM[i][j]*u[1];
          fij-= ab*tdmZMM[i][j]*u[2];
          fij*=HARTREE_BOHR2MD*csq/totpop;
          f[i+qm->nrQMatoms][j]      += creal(fij);
          fshift[i+qm->nrQMatoms][j] += creal(fij);
        }
      }    
      /* now off-diagonals 
       */
      for (q=p+1;q<ndim;q++){
        /* normalize
         */
        cpcq = conj(qm->creal[p]+IMAG*(qm->cimag[p]))*(qm->creal[q]+IMAG*(qm->cimag[q]));
        betasq = conj(eigvec[p*ndim+m])*eigvec[q*ndim+m];
        bpaq = conj(a_sum)*eigvec[q*ndim+m];
        a_sumq = 0.0+IMAG*0.0;
        for (i=0;i<(qm->n_max)+1;i++){
          a_sumq += eigvec[q*ndim+nmol+i]*sqrt(cavity_dispersion(i-qm->n_max,qm)/V0_2EP)*cexp(IMAG*2*M_PI*(i-qm->n_max)/L_au*m*L_au/((double) nmol));
        }
        bpaq = conj(eigvec[p*ndim+m])*a_sumq;
        apbq = conj(a_sump)*eigvec[q*ndim+m];
        bqap = conj(eigvec[q*ndim+m])*a_sump; /* conj(apbq) */
        aqbp = conj(a_sumq)*eigvec[p*ndim+m]; /* conj(bpaq) */
        for(i=0;i<qm->nrQMatoms;i++){
          for(j=0;j<DIM;j++){
	        /* diagonal term
             */
            fij=0;
            fij = cpcq*betasq*(QMgrad_S1[i][j]-QMgrad_S0[i][j]);
            fij+= conj(cpcq)*conj(betasq)*(QMgrad_S1[i][j]-QMgrad_S0[i][j]);
            /* off-diagonal term
             */
            fij-= cpcq*(bpaq+apbq)*tdmX[i][j]*u[0];
            fij-= cpcq*(bpaq+apbq)*tdmY[i][j]*u[1];
            fij-= cpcq*(bpaq+apbq)*tdmZ[i][j]*u[2];
            fij-= conj(cpcq)*(bqap+aqbp)*tdmX[i][j]*u[0];
            fij-= conj(cpcq)*(bqap+aqbp)*tdmY[i][j]*u[1];
            fij-= conj(cpcq)*(bqap+aqbp)*tdmZ[i][j]*u[2];
            fij*=HARTREE_BOHR2MD/totpop;
            f[i][j]      += creal(fij);
            fshift[i][j] += creal(fij);
          }
        }
        for(i=0;i<mm->nrMMatoms;i++){
          for(j=0;j<DIM;j++){
            /* diagonal term
             */
            fij = cpcq*betasq*(MMgrad_S1[i][j]-MMgrad_S0[i][j]);
            fij+= conj(cpcq)*conj(betasq)*(MMgrad_S1[i][j]-MMgrad_S0[i][j]);
            /* off-diagonal term
             */
            fij-= cpcq*(bpaq+apbq)*tdmXMM[i][j]*u[0];
            fij-= cpcq*(bpaq+apbq)*tdmYMM[i][j]*u[1];
            fij-= cpcq*(bpaq+apbq)*tdmZMM[i][j]*u[2];
            fij-= conj(cpcq)*(bqap+aqbp)*tdmXMM[i][j]*u[0];
            fij-= conj(cpcq)*(bqap+aqbp)*tdmYMM[i][j]*u[1];
            fij-= conj(cpcq)*(bqap+aqbp)*tdmZMM[i][j]*u[2];
            fij*=HARTREE_BOHR2MD/totpop;
            f[i+qm->nrQMatoms][j]      += creal(fij);
            fshift[i+qm->nrQMatoms][j] += creal(fij);
          }
        }
      }
    }
  }
  /* printing the coefficients to C.dat 
   * print the adiabatic eigenvectors to a file 
   */
  if( dodiag ){
    snew(eigenvectorfile,3000);
    sprintf(eigenvectorfile,"%s/eigenvectors.dat",qm->work_dir);
    evout=fopen(eigenvectorfile,"a");
    for(i=0;i<ndim;i++){
      fprintf(evout,
	      "step %4d Eigenvector %4d gap %12.8lf (c: %12.8lf + %12.8lf I):",
	      step,i,eigval[i]-(
				energies[ndim-1]-cavity_dispersion(qm->n_max,qm)),
	      qm->creal[i],qm->cimag[i]);
      for(k=0;k<ndim;k++){
        fprintf(evout," %12.8lf + %12.8lf I",creal(eigvec[i*ndim+k]),cimag(eigvec[i*ndim+k]));
      }
      fprintf(evout,"\n");
    }
    fclose(evout);
    free(eigenvectorfile);
    snew(coefficientfile,3000);
    sprintf(coefficientfile,"%s/coefficients.dat",qm->work_dir);
    evout=fopen(coefficientfile,"a");
    fprintf(evout,
	    "step %4d energy: %12.8lf coeff: ", step,QMener);
    for(k=0;k<ndim;k++){
      fprintf(evout," %12.8lf + %12.8lf I",qm->dreal[k],qm->dimag[k]);
      //      totpop+=qm->creal[k]*qm->creal[k]+qm->cimag[k]*qm->cimag[k];
    }
    fprintf(evout,"\n");
    fclose(evout);
    free(coefficientfile);    
    fprintf(stderr,"rho0 (%d) = %lf, Energy = %lf\n",step,qm->groundstate,energies[ndim-1]-cavity_dispersion(qm->n_max,qm));
    sprintf(buf,"%s/C.dat",qm->work_dir);
    Cout= fopen(buf,"w");
    fprintf(Cout,"%d\n",step);
    for(i=0;i<ndim;i++){
      fprintf (Cout,"%.5lf %.5lf\n ",qm->creal[i],qm->cimag[i]);
    }
    fprintf (Cout,"%.5lf\n",qm->groundstate);
    fclose(Cout);
  }
  /* now account for the decay that will happen in the next timestep 
   */
  if(qm->QEDdecay > 0){
    qm->groundstate = 0;
    decay=exp(-0.5*(qm->QEDdecay)*(qm->dt)); 
    for ( i = nmol ; i < ndim ; i++ ){
      qm->groundstate-=conj(qm->dreal[i]+IMAG*qm->dimag[i])*(qm->dreal[i]+IMAG*qm->dimag[i])*(decay*decay-1);
      qm->dreal[i] *= decay;
      qm->dimag[i] *= decay;
    }
    for(i=0;i<ndim;i++){
      d[i] = qm->dreal[i]+ IMAG*qm->dimag[i];
    }
    if(doprop){
      MtimesV_complex(ndim,udagger,d,c);
      for(i=0;i<ndim;i++){
        qm->creal[i] = creal(c[i]);
        qm->cimag[i] = cimag(c[i]);
      }
    }
    else{
      /* set elements to zero and sum them up later
       */
      for(i=0;i<ndim;i++){
        qm->creal[i] = 0;
        qm->cimag[i] = 0;
      }      
    }
    if(MULTISIM(cr)){
      gmx_sumd_sim(ndim,qm->creal ,cr->ms);
      gmx_sumd_sim(ndim,qm->cimag ,cr->ms);
    }
  }
  /* now do the decoherence that will happen in the next timestep 
   * we thus use the current total kinetic energy. I suppose this is the kinetic energy 
   * after the velocity correct? 
   * We need to send this 
   * around we wrote separate routine. Decoherence corrections make sense
   * only for surface hopping methods, so we check for that.
   */  
  if ( ( fr->qr->SHmethod == eSHmethodGranucci )
       && (qm->QEDdecoherence > 0.) ){
    decoherence(cr,qm,mm,ndim,eigval);
    /* to capture the effect on d, we transform: d=Uc;
     */
    if (doprop){
      for(i=0;i<ndim;i++){
        c[i] = qm->creal[i]+ IMAG*qm->cimag[i];
      }
      transposeM_complex(ndim,udagger,umatrix);
      MtimesV_complex(ndim,umatrix,c,d);
      for(i=0;i<ndim;i++){
        qm->dreal[i] = creal(d[i]);
        qm->dimag[i] = cimag(d[i]);
      }
    }
    
    else{
      /* set elements to zero and sum them up later
       */
      for(i=0;i<ndim;i++){
        qm->dreal[i] = 0;
        qm->dimag[i] = 0;
        qm->dreal[i] = 0;
        qm->dimag[i] = 0;
      }
    } 
    if(MULTISIM(cr)){
      gmx_sumd_sim(ndim,qm->dreal ,cr->ms);
      gmx_sumd_sim(ndim,qm->dimag ,cr->ms);
    }
  }  
  free (eigval);
  free (eigvec);
  free (eigvec_real);
  free (eigvec_imag);
  free (state);
  free(umatrix);
  free(uold);
  free(udagger);
  free(expH);
  free(ctemp);
  free(c);
  free(d);
  free(dtemp);
  free(ham);
  free(temp);  
  return (QMener);
} /* do_hybrid */


double do_diabatic_non_herm(t_commrec *cr,  t_forcerec *fr, 
			    t_QMrec *qm, t_MMrec *mm, rvec f[], rvec fshift[],
			    dplx *matrix, int step,
			    rvec QMgrad_S1[],rvec MMgrad_S1[],
			    rvec QMgrad_S0[],rvec MMgrad_S0[],
			    rvec tdmX[], rvec tdmY[], rvec tdmZ[],
			    rvec tdmXMM[], rvec tdmYMM[], rvec tdmZMM[],double *energies)
{
  double
    decay,asq,L_au=qm->L*microM2BOHR,
    E0_norm_sq,V0_2EP,u[3],QMener=0.,totpop=0.;
  dplx
    fij,csq,cmcp,*ham,
    *expH,*ctemp,*c,cicj,ener;
  time_t 
    start,end,interval;
  int
    dodia=1,*state,i,j,k,p,q,m,nmol,ndim;
  char
    *eigenvectorfile,*final_eigenvecfile,*energyfile,buf[3000];
  FILE
    *evout=NULL,*final_evout=NULL,*Cout=NULL;
  
  start = time(NULL);

  if (fr->qr->SHmethod != eSHmethodGranucci && fr->qr->SHmethod != eSHmethodEhrenfest){
    gmx_fatal(FARGS, "Running in diabatic basis only possible for Ehrenfest of Surface hopping. Latter  local diabatization\n");
  }
  
  
  /* information on field, was already calcualted above...
   */
  E0_norm_sq = iprod(qm->E,qm->E); // Square of the magitud of the E-field at k=0
  V0_2EP = qm->omega/(E0_norm_sq); //2*Epsilon0*V_cav at k=0 (in a.u.)
  u[0]=qm->E[0]/sqrt(E0_norm_sq);
  u[1]=qm->E[1]/sqrt(E0_norm_sq);
  u[2]=qm->E[2]/sqrt(E0_norm_sq);

  /* silly array to communicate the current state
   */
  snew(state,1);

  if(MULTISIM(cr)){
    ndim=cr->ms->nsim+(qm->n_max-qm->n_min)+1;
    m=cr->ms->sim;
    nmol=cr->ms->nsim;
    if (!MASTERSIM(cr->ms)){
      dodia = 0;
    }
  }
  else{
    ndim=1+(qm->n_max-qm->n_min)+1;
    m=0;
    nmol=1;
    interval=time(NULL);
  }
  snew(expH, ndim*ndim);
  snew(ctemp,ndim);
  snew(c,ndim);
  snew(ham,ndim*ndim);
    
  if(dodia){
    if (step){
      for (i=0;i<ndim;i++){
        c[i]=qm->creal[i]+IMAG*qm->cimag[i];
      }
      /* interpolate the non-hermitian Hamiltonian 
       */
      for(i=0;i<ndim*ndim;i++){
        ham[i]=matrix[i]+qm->matrix[i];
      }
      /* Add the losses -i\gamma\hat{a}^\dagger\hat{a}
       * assuming these do not depend on R or t 
       * The factor 2 is handled in the exp function 
       */
      for(i=nmol;i<ndim;i++){
        /* hbar*decay rate 6.582119569*10^(\[Minus]16)/10^(-12) into AU
         */
        ham[i*ndim+i]-=IMAG*(qm->QEDdecay)*0.0006582119569/27.2114;
      }
      /* propagate the coefficients 
       */
      expM_non_herm(ndim,ham,expH, qm->dt);
      MtimesV_complex(ndim,expH,c,ctemp);
      for( i=0;i<ndim;i++){
        c[i]=ctemp[i];
      }
      if (fr->qr->SHmethod == eSHmethodGranucci){
        qm->polariton = state[0] = compute_hopping_probability(step,qm,c,expH,ndim);
      }
      for ( i = 0 ; i < ndim ; i++ ){	
        qm->creal[i] = creal(c[i]);
        qm->cimag[i] = cimag(c[i]);
      }
      /* some writing */
      fprintf(stderr,"step %d: |D|^2: ",step);
      for(i=0;i<ndim;i++){
        fprintf (stderr," %.5lf ",conj(c[i])*c[i]);
      }
      fprintf(stderr,"\n");
    }
    else{
      /* first step, keep coefficients unchanged */
      fprintf(stderr,"step %d: |D|^2: ",step);
      for(i=0;i<ndim;i++){
        c[i] = qm->creal[i]+ IMAG*qm->cimag[i];
        fprintf (stderr,"%.5lf ",conj(c[i])*c[i]);
      }    
      fprintf(stderr,"\n");
      state[0]=qm->polariton;
    }
  }
  else {
    /* other nodes, just set c to zero, these will be filled next with
     * the values on node 0
     */
    for(i=0;i<ndim;i++){
      qm->cimag[i]=0.;
      qm->creal[i]=0.;
    }
  }
  if(MULTISIM(cr)){
    /* communicate the time-dependent expansion coefficients needed for computing mean-field forces */
    gmx_sumd_sim(ndim,qm->creal ,cr->ms);
    gmx_sumd_sim(ndim,qm->cimag ,cr->ms);
    if(fr->qr->SHmethod != eSHmethodEhrenfest){
      gmx_sumi_sim(1,state,cr->ms);
      qm->polariton = state[0];
    }
  }
  /* compute the norm
   */
  for(i=0;i<ndim;i++){
    totpop+=qm->creal[i]*qm->creal[i]+qm->cimag[i]*qm->cimag[i];
  }
  qm->groundstate=1-totpop;
  /* compute the total energy 
   */
  if(fr->qr->SHmethod == eSHmethodGranucci){
    ener = energies[qm->polariton];
    if (dodia){
      fprintf(stderr,"Step %d, state: %d, Energy: %12.8lf + %12.8lf I\n",step,qm->polariton,creal(ener),cimag(ener));
    }
  }
  else{ /* Ehrenfest */
    for(i=0;i<ndim;i++){
      for(j=0;j<ndim;j++){
        cicj=conj(qm->creal[i]+IMAG*qm->cimag[i])*(qm->creal[j]+IMAG*qm->cimag[j]);
        ener += creal(cicj*matrix[i*ndim+j])+cimag(cicj*matrix[i*ndim+j])*IMAG;
      }
    }
    ener/=totpop;
    if (dodia){
      fprintf(stderr,"Step %d, Energy: %12.8lf + %12.8lf I\n",step,creal(ener),cimag(ener));
    }
  }
  QMener = creal(ener)*HARTREE2KJ*AVOGADRO;

  /* write the coefficients to a file */
  if(dodia){
    snew(eigenvectorfile,3000);
    sprintf(eigenvectorfile,"%s/eigenvectors.dat",qm->work_dir);
    evout=fopen(eigenvectorfile,"a");
    fprintf(evout,
	    "step %4d energy: %12.8lf coeff: ", step,QMener);
    for(k=0;k<ndim;k++){
      fprintf(evout," %12.8lf + %12.8lf I",qm->creal[k],qm->cimag[k]);
      //      totpop+=qm->creal[k]*qm->creal[k]+qm->cimag[k]*qm->cimag[k];
    }
    fprintf(evout,"\n");
    fclose(evout);
    free(eigenvectorfile);
  }
  /* compute Hellman-Feynman forces. */
  if(fr->qr->SHmethod == eSHmethodGranucci){
    if(m==qm->polariton){
      /* excited state gradients for m */
      for(i=0;i<qm->nrQMatoms;i++){
        for(j=0;j<DIM;j++){
          fij =QMgrad_S1[i][j];
          fij*=HARTREE_BOHR2MD;
          f[i][j]      += creal(fij);
          fshift[i][j] += creal(fij);
        }
      }
      for(i=0;i<mm->nrMMatoms;i++){
        for(j=0;j<DIM;j++){
          fij =MMgrad_S1[i][j];
	      fij*=HARTREE_BOHR2MD;
	      f[i+qm->nrQMatoms][j]      += creal(fij);
     	  fshift[i+qm->nrQMatoms][j] += creal(fij);
	    }
      }
    }
    else{
      /* ground state gradients for m */
      for(i=0;i<qm->nrQMatoms;i++){
        for(j=0;j<DIM;j++){
          fij =QMgrad_S0[i][j];
          fij*=HARTREE_BOHR2MD;
          f[i][j]      += creal(fij);
          fshift[i][j] += creal(fij);
        }
      }
      for(i=0;i<mm->nrMMatoms;i++){
        for(j=0;j<DIM;j++){
          fij =MMgrad_S0[i][j];
          fij*=HARTREE_BOHR2MD;
          f[i+qm->nrQMatoms][j]      += creal(fij);
          fshift[i+qm->nrQMatoms][j] += creal(fij);
        }
      }
    }
  }
  else{ /* Ehrenfest, mean field gradient, normalized in case losses are included */
    csq = conj(qm->creal[m]+IMAG*qm->cimag[m])*(qm->creal[m]+IMAG*qm->cimag[m]);
    for(i=0;i<qm->nrQMatoms;i++){
      for(j=0;j<DIM;j++){
        /* the states are no longer orthonormal, as the norm |c_m(t)|^2 = totpop <= 1
         */
        fij = csq*QMgrad_S1[i][j]+(totpop-csq)*QMgrad_S0[i][j];
        for(p=nmol;p<ndim;p++){
          cmcp=conj(qm->creal[m]+IMAG*qm->cimag[m])*(qm->creal[p]+IMAG*qm->cimag[p]);
            /* replace the m*L_au by the actual position z[m]
             */
	      cmcp*=sqrt(cavity_dispersion((p-nmol)+qm->n_min,qm)/V0_2EP)*cexp(IMAG*2*M_PI*(p-nmol+qm->n_min)/L_au*m*L_au/((double) nmol));
            /* safe some cycles by 2Re() */
          fij -= cmcp*tdmX[i][j]*u[0];
          fij -= cmcp*tdmY[i][j]*u[1];
          fij -= cmcp*tdmZ[i][j]*u[2];
          fij -= conj(cmcp)*tdmX[i][j]*u[0];
          fij -= conj(cmcp)*tdmY[i][j]*u[1];
          fij -= conj(cmcp)*tdmZ[i][j]*u[2];
        }
        fij*=HARTREE_BOHR2MD/totpop;
        f[i][j]      += creal(fij);
        fshift[i][j] += creal(fij);
      }
    }
    for(i=0;i<mm->nrMMatoms;i++){
      for(j=0;j<DIM;j++){
        fij = csq*MMgrad_S1[i][j]+(totpop-csq)*MMgrad_S0[i][j];
        for(p=nmol;p<ndim;p++){
          cmcp=conj(qm->creal[m]+IMAG*qm->cimag[m])*(qm->creal[p]+IMAG*qm->cimag[p]);
          cmcp*=sqrt(cavity_dispersion((p-nmol)+qm->n_min,qm)/V0_2EP)*cexp(IMAG*2*M_PI*(p-nmol+qm->n_min)/L_au*m*L_au/((double) nmol));
          fij -= cmcp*tdmXMM[i][j]*u[0];
          fij -= cmcp*tdmYMM[i][j]*u[1];
          fij -= cmcp*tdmZMM[i][j]*u[2];
          fij -= conj(cmcp)*tdmXMM[i][j]*u[0];
          fij -= conj(cmcp)*tdmYMM[i][j]*u[1];
          fij -= conj(cmcp)*tdmZMM[i][j]*u[2];
        }
        fij*=HARTREE_BOHR2MD/totpop;
        f[i+qm->nrQMatoms][j]      += creal(fij);
        fshift[i+qm->nrQMatoms][j] += creal(fij);
      }
    }
  }
  if (dodia){
    ///      fprintf(stderr,"rho0 (%d) = %lf, Energy = %lf\n",step,qm->groundstate,energies[ndim-1]-(qm->omega));
    fprintf(stderr,"rho0 (%d) = %lf, Energy = %lf\n",step,qm->groundstate,energies[ndim-1]-cavity_dispersion(qm->n_max,qm));
    sprintf(buf,"%s/C.dat",qm->work_dir);
    Cout= fopen(buf,"w");
    fprintf(Cout,"%d\n",step);
    for(i=0;i<ndim;i++){
      fprintf (Cout,"%.5lf %.5lf\n ",qm->creal[i],qm->cimag[i]);
    }
    fprintf (Cout,"%.5lf\n",qm->groundstate);
    fclose(Cout);
  }
  /* now account also for the decoherence that will also happen in the next timestep */
  /* we thus use the current total kinetic energy. We need to send this around we wrote separate routine */
  /* Decoherence corrections make sense only for surface hopping methods, so we check for that.
   */
  if ( (fr->qr->SHmethod == eSHmethodGranucci) && (qm->QEDdecoherence > 0.) ){ 
    decoherence(cr,qm,mm,ndim,energies);
  }
  free(expH);
  free(c);
  free(ham);
  free(ctemp);
  free(state);
  return(QMener);
} /* do_diabatic_non_herm */

double do_diabatic(t_commrec *cr,  t_forcerec *fr, 
		   t_QMrec *qm, t_MMrec *mm, rvec f[], rvec fshift[],
		   dplx *matrix, int step,
		   rvec QMgrad_S1[],rvec MMgrad_S1[],
		   rvec QMgrad_S0[],rvec MMgrad_S0[],
		   rvec tdmX[], rvec tdmY[], rvec tdmZ[],
		   rvec tdmXMM[], rvec tdmYMM[], rvec tdmZMM[],
		   double *energies)
{
  double
    decay,L_au=qm->L*microM2BOHR,
    E0_norm_sq,V0_2EP,u[3],QMener=0.,totpop=0.0;
  dplx
    fij,csq,cmcp,*ham,
    *expH,*ctemp,*c,cicj,ener=0.;
  time_t 
    start,end,interval;
  int
    dodia=1,*state,i,j,k,p,q,m,nmol,ndim;
  char
    *eigenvectorfile,*final_eigenvecfile,*energyfile,buf[3000];
  FILE
    *evout=NULL,*final_evout=NULL,*Cout=NULL;
  
  start = time(NULL);
  if (fr->qr->SHmethod != eSHmethodGranucci && fr->qr->SHmethod != eSHmethodEhrenfest){
    gmx_fatal(FARGS,
	      "Running in diabatic basis only possible for Ehrenfest of Surface hopping with local diabatization\n");
  }
  
  /* information on field, was already calcualted above...*/
  E0_norm_sq = iprod(qm->E,qm->E); // Square of the magitud of the E-field at k=0
  if (E0_norm_sq>0.000000000){
    V0_2EP = qm->omega/(E0_norm_sq); //2*Epsilon0*V_cav at k=0 (in a.u.)
    u[0]=qm->E[0]/sqrt(E0_norm_sq);
    u[1]=qm->E[1]/sqrt(E0_norm_sq);
    u[2]=qm->E[2]/sqrt(E0_norm_sq);
  } 
  else {
      /* we set V to 1, even if it sbould be infinite, but simply put u, the vector along whcih the field is directed to zero */
    V0_2EP=1;
    u[0]=u[1]=u[2]=0;
  }
  /* silly array to communicate the courrent state*/
  snew(state,1);

  
  if(MULTISIM(cr)){
    ndim=cr->ms->nsim+(qm->n_max-qm->n_min)+1;
    m=cr->ms->sim;
    nmol=cr->ms->nsim;
    if (!MASTERSIM(cr->ms)){
      dodia = 0;
    }
  }
  else{
    ndim=1+(qm->n_max-qm->n_min)+1;
    m=0;
    nmol=1;
    interval=time(NULL);
  }
  snew(expH, ndim*ndim);
  snew(ctemp,ndim);
  snew(c,ndim);
  snew(ham,ndim*ndim);
    
  if(dodia){
    if (step){
      for (i=0;i<ndim;i++){
        c[i]=qm->creal[i]+IMAG*qm->cimag[i];
      }
      /* interpolate the Hamiltonian */
      for(i=0;i<ndim*ndim;i++){
        ham[i]=matrix[i]+qm->matrix[i];
      }
      /* propagate the coefficients */
      expM_complex2(ndim,ham,expH, qm->dt);
      MtimesV_complex(ndim,expH,c,ctemp);
      for( i=0;i<ndim;i++){
        c[i]=ctemp[i];
      }
      if (fr->qr->SHmethod == eSHmethodGranucci){
        qm->polariton = state[0]= compute_hopping_probability(step,qm,c,expH,ndim);
      }
      for ( i = 0 ; i < ndim ; i++ ){	
        qm->creal[i] = creal(c[i]);
        qm->cimag[i] = cimag(c[i]);
      }
      /* some writing */
      fprintf(stderr,"step %d: |C|^2: ",step);
      for(i=0;i<ndim;i++){
        fprintf (stderr," %.5lf ",conj(c[i])*c[i]);
      }
      fprintf(stderr,"\n");
    }
    else{
      /* first step, keep coefficients unchanged */
      fprintf(stderr,"step %d: |C|^2: ",step);
      for(i=0;i<ndim;i++){
        c[i] = qm->creal[i]+ IMAG*qm->cimag[i];
        fprintf (stderr,"%.5lf ",conj(c[i])*c[i]);
      }    
      fprintf(stderr,"\n");
      state[0]=qm->polariton;
    }
  }
  else {
    /* other nodes, just set c to zero, these will be filled next with
     * the values on node 0
     */
    for(i=0;i<ndim;i++){
      qm->cimag[i]=0.;
      qm->creal[i]=0.;
    }
  }
  if(MULTISIM(cr)){
    /* communicate the time-dependent expansion coefficients needed for computing mean-field forces */
    gmx_sumd_sim(ndim,qm->creal ,cr->ms);
    gmx_sumd_sim(ndim,qm->cimag ,cr->ms);
    if(fr->qr->SHmethod != eSHmethodEhrenfest){
      gmx_sumi_sim(1,state,cr->ms);
      qm->polariton = state[0];
    }
  }
  /* compute the total energy */
  if(fr->qr->SHmethod == eSHmethodGranucci){
    ener = energies[qm->polariton];
    if (dodia){
      fprintf(stderr,"Step %d, state: %d, Energy: %12.8lf + %12.8lf I\n",step,qm->polariton,creal(ener),cimag(ener));
    }
  }
  else { /* Ehrenfest */
    for(i=0;i<ndim;i++){
      for(j=0;j<ndim;j++){
        cicj=conj(qm->creal[i]+IMAG*qm->cimag[i])*(qm->creal[j]+IMAG*qm->cimag[j]);
        ener += creal(matrix[i*ndim+j]*cicj)+cimag(matrix[i*ndim+j]*cicj)*IMAG;
      }
    }
    /* determine the normalization and (virtual) ground state population
     */
    for(i=0;i<ndim;i++){
      totpop+=qm->creal[i]*qm->creal[i]+qm->cimag[i]*qm->cimag[i];
    }
    qm->groundstate=1-totpop;
    ener/=totpop;
    if (dodia){
      fprintf(stderr,"Step %d, Energy: %12.8lf + %12.8lf I\n",step,creal(ener),cimag(ener));
    }
  }
  QMener = creal(ener)*HARTREE2KJ*AVOGADRO/totpop;
  
  /* write the coefficients to a file */
  if(dodia){
    snew(eigenvectorfile,3000);
    sprintf(eigenvectorfile,"%s/eigenvectors.dat",qm->work_dir);
    evout=fopen(eigenvectorfile,"a");
    fprintf(evout,
	    "step %4d energy: %12.8lf coeff: ", step,QMener);
    for(k=0;k<ndim;k++){
      fprintf(evout," %12.8lf + %12.8lf I",qm->creal[k],qm->cimag[k]);
    }
    fprintf(evout,"\n");
    fclose(evout);
    free(eigenvectorfile);
  }
  /* compute Hellman-Feynman forces.  */
  if(fr->qr->SHmethod == eSHmethodGranucci){
    if(m==qm->polariton){
      /* excited state gradients for m */
      for(i=0;i<qm->nrQMatoms;i++){
        for(j=0;j<DIM;j++){
          fij =QMgrad_S1[i][j];
          fij*=HARTREE_BOHR2MD;
          f[i][j]      += creal(fij);
          fshift[i][j] += creal(fij);
        }
      }
      for(i=0;i<mm->nrMMatoms;i++){
        for(j=0;j<DIM;j++){
          /* diagonal term */
          fij =MMgrad_S1[i][j];
          /* off-diagonal term */
          fij*=HARTREE_BOHR2MD;
	      f[i+qm->nrQMatoms][j]      += creal(fij);
	      fshift[i+qm->nrQMatoms][j] += creal(fij);
        }
      }
    }
    else{
      /* ground state gradients for m */
      for(i=0;i<qm->nrQMatoms;i++){
        for(j=0;j<DIM;j++){
          fij =QMgrad_S0[i][j];
          fij*=HARTREE_BOHR2MD;
          f[i][j]      += creal(fij);
          fshift[i][j] += creal(fij);
        }
      }
      for(i=0;i<mm->nrMMatoms;i++){
        for(j=0;j<DIM;j++){
          fij =MMgrad_S0[i][j];
          fij*=HARTREE_BOHR2MD;
          f[i+qm->nrQMatoms][j]      += creal(fij);
          fshift[i+qm->nrQMatoms][j] += creal(fij);
        }
      }
    }
  }
  else{
    /* Hellman Feynman terms, see https://www.overleaf.com/read/wkbkwybcjtdb
     * Every processor for him/her self!
     */
    csq = conj(qm->creal[m]+IMAG*qm->cimag[m])*(qm->creal[m]+IMAG*qm->cimag[m]);
    for(i=0;i<qm->nrQMatoms;i++){
      for(j=0;j<DIM;j++){
        fij = csq*QMgrad_S1[i][j]+(totpop-csq)*QMgrad_S0[i][j];
        for(p=nmol;p<ndim;p++){
          cmcp=conj(qm->creal[m]+IMAG*qm->cimag[m])*(qm->creal[p]+IMAG*qm->cimag[p]);
          cmcp*=sqrt(cavity_dispersion((p-nmol)+qm->n_min,qm)/V0_2EP)*cexp(IMAG*2*M_PI*((p-nmol)+qm->n_min)/L_au*m*L_au/((double) nmol));
          fij -= cmcp*tdmX[i][j]*u[0];
          fij -= cmcp*tdmY[i][j]*u[1];
          fij -= cmcp*tdmZ[i][j]*u[2];
	      fij -= conj(cmcp)*tdmX[i][j]*u[0];
	      fij -= conj(cmcp)*tdmY[i][j]*u[1];
	      fij -= conj(cmcp)*tdmZ[i][j]*u[2];
        }
        fij*=HARTREE_BOHR2MD/totpop;
        f[i][j]      += creal(fij);
        fshift[i][j] += creal(fij);
      }
    }
    for(i=0;i<mm->nrMMatoms;i++){
      for(j=0;j<DIM;j++){
        fij = csq*MMgrad_S1[i][j]+(totpop-csq)*MMgrad_S0[i][j];
        for(p=nmol;p<ndim;p++){
          cmcp=conj(qm->creal[m]+IMAG*qm->cimag[m])*(qm->creal[p]+IMAG*qm->cimag[p]);
	      cmcp*=sqrt(cavity_dispersion((p-nmol)+qm->n_min,qm)/V0_2EP)*cexp(IMAG*2*M_PI*((p-nmol)+qm->n_min)/L_au*m*L_au/((double) nmol));
	      fij -= cmcp*tdmXMM[i][j]*u[0];
	      fij -= cmcp*tdmYMM[i][j]*u[1];
	      fij -= cmcp*tdmZMM[i][j]*u[2];
	      fij -= conj(cmcp)*tdmXMM[i][j]*u[0];
	      fij -= conj(cmcp)*tdmYMM[i][j]*u[1];
	      fij -= conj(cmcp)*tdmZMM[i][j]*u[2];
        }
        fij*=HARTREE_BOHR2MD/totpop;
        f[i+qm->nrQMatoms][j]      += creal(fij);
        fshift[i+qm->nrQMatoms][j] += creal(fij);
      }
    }
  }
  /* printing the coefficients to C.dat */
  if (dodia){
    fprintf(stderr,"rho0 (%d) = %lf, Energy = %lf\n",step,qm->groundstate,energies[ndim-1]-cavity_dispersion(qm->n_max,qm));
    sprintf(buf,"%s/C.dat",qm->work_dir);
    Cout= fopen(buf,"w");
    fprintf(Cout,"%d\n",step);
    for(i=0;i<ndim;i++){
      fprintf (Cout,"%.5lf %.5lf\n ",qm->creal[i],qm->cimag[i]);
    }
    fprintf (Cout,"%.5lf\n",qm->groundstate);
    fclose(Cout);
  }
  /* only the cavity modes can decay. Decay will occur in the next timestep */
  decay=exp(-0.5*(qm->QEDdecay)*(qm->dt));
  qm->groundstate=0;
  for ( i = nmol ; i < ndim ; i++ ){
    qm->groundstate-=conj(qm->creal[i]+IMAG*qm->cimag[i])*(qm->creal[i]+IMAG*qm->cimag[i])*(decay*decay-1);
    qm->creal[i] *= decay;
    qm->cimag[i] *= decay;
  }    
  /* now account also for the decoherence that will also happen in the next timestep */
  /* we thus use the current total kinetic energy. We need to send this around we wrote separate routine */
  /* Decoherence corrections make sense only for surface hopping methods, so we check for that.
   */
  if (fr->qr->SHmethod == eSHmethodGranucci && (qm->QEDdecoherence > 0.) ){
    decoherence(cr,qm,mm,ndim,energies);
  }
  
  free(expH);
  free(c);
  free(ham);
  free(ctemp);
  free(state);
  return(QMener);
} /* do_diabatic */
  

double do_adiabatic(t_commrec *cr,  t_forcerec *fr, 
                    t_QMrec *qm, t_MMrec *mm, rvec f[], rvec fshift[],
                    dplx *matrix, int step,
                    rvec QMgrad_S1[],rvec MMgrad_S1[],
                    rvec QMgrad_S0[],rvec MMgrad_S0[],
                    rvec tdmX[], rvec tdmY[], rvec tdmZ[],
                    rvec tdmXMM[], rvec tdmYMM[], rvec tdmZMM[],double *energies)
{
  double
    *eigvec_real,*eigvec_imag,*eigval,decay,asq,L_au=qm->L*microM2BOHR,
    E0_norm_sq,V0_2EP,u[3],QMener=0.,totpop=0.0;
  dplx
    *eigvec,cpcq,bpaq,apbq,bqap,aqbp,a_sump,a_sumq,
    a_sum,a_sum2,betasq,ab,ba,fij,csq;
  time_t 
    start,end,interval;
  int
    dodia=1,*state,i,j,k,p,q,m,nmol,ndim;
  char
    *eigenvectorfile,*final_eigenvecfile,*energyfile,buf[3000];
  FILE
    *evout=NULL,*final_evout=NULL,*Cout=NULL;;
  
  start = time(NULL);

  snew(eigenvectorfile,3000);
  sprintf(eigenvectorfile,"%s/eigenvectors.dat",qm->work_dir);
  snew(final_eigenvecfile,3000);
  sprintf(final_eigenvecfile,"%s/final_eigenvecs.dat",qm->work_dir);

  /* information on field, was already calcualted above...*/
  E0_norm_sq = iprod(qm->E,qm->E); // Square of the magitud of the E-field at k=0
  V0_2EP = qm->omega/(E0_norm_sq); //2*Epsilon0*V_cav at k=0 (in a.u.)
  u[0]=qm->E[0]/sqrt(E0_norm_sq);
  u[1]=qm->E[1]/sqrt(E0_norm_sq);
  u[2]=qm->E[2]/sqrt(E0_norm_sq);

  /* silly array to communicate the current state */
  snew(state,1);
  
  if(MULTISIM(cr)){
    ndim=cr->ms->nsim+(qm->n_max-qm->n_min)+1;
    m=cr->ms->sim;
    nmol=cr->ms->nsim;
    if (!MASTERSIM(cr->ms)){
      dodia = 0;
    }
  }
  else{
    ndim=1+(qm->n_max-qm->n_min)+1;
    m=0;
    nmol=1;
    interval=time(NULL);
  }
  snew(eigval,ndim);
  snew(eigvec,ndim*ndim);
  snew(eigvec_real,ndim*ndim);
  snew(eigvec_imag,ndim*ndim);
  if(dodia){
    fprintf(stderr,"\n\ndiagonalizing matrix\n");
    diag(ndim,eigval,eigvec,matrix);
    fprintf(stderr,"step %d Eigenvalues: ",step);
    for ( i = 0 ; i<ndim;i++){
      fprintf(stderr,"%lf ",eigval[i]);
      qm->eigval[i]=eigval[i]; 
    }
    fprintf(stderr,"\n");
    interval=time(NULL);
    if(MULTISIM(cr)){
      fprintf(stderr,"node %d: eigensolver done at %ld\n",cr->ms->sim,interval-start);
    }
    else{
      fprintf(stderr,"eigensolver done at %ld\n",interval-start);
    }
    /* lots of duplicate code now... we should use switch() instead
     */
    if(fr->qr->SHmethod != eSHmethodEhrenfest){
      if(fr->qr->SHmethod == eSHmethoddiabatic){
        qm->polariton = QEDhop(step,qm,eigvec,ndim,eigval);
      }
      else{
        qm->polariton = QEDFSSHop(step,qm,eigvec,ndim,eigval,qm->dt,fr->qr);
      }
      state[0]=qm->polariton;
    } 
    else{
      propagate_TDSE(step,qm,eigvec,ndim,eigval,qm->dt,fr->qr);
    } 
    interval=time(NULL);
    if(MULTISIM(cr)){
      fprintf(stderr,"node %d: wavefunction propagation done at %ld\n",cr->ms->sim,interval-start); 
    }
    else{
      fprintf(stderr,"wavefunction propagation done at %ld\n",interval-start);
    }
  }
  else{/* zero the expansion coefficient on all other nodes */
    for(i=0;i<ndim;i++){
      qm->cimag[i]=0.;
      qm->creal[i]=0.;
    }
  }  
  /* send the eigenvalues and eigenvectors around */
  for(i=0;i<ndim*ndim;i++){
    eigvec_real[i]=creal(eigvec[i]);
    eigvec_imag[i]=cimag(eigvec[i]);
  }
  if(MULTISIM(cr)){
    gmx_sumd_sim(ndim*ndim,eigvec_real,cr->ms);
    gmx_sumd_sim(ndim*ndim,eigvec_imag,cr->ms);
    gmx_sumd_sim(ndim,eigval,cr->ms);
    if(fr->qr->SHmethod != eSHmethodEhrenfest){
      gmx_sumi_sim(1,state,cr->ms);
      qm->polariton = state[0];
    }
    /* communicate the time-dependent expansion coefficients needed for computing mean-field forces */
    gmx_sumd_sim(ndim,qm->creal ,cr->ms);
    gmx_sumd_sim(ndim,qm->cimag ,cr->ms);
  }
    /* compute norm of the wavefunction 
   */
  for(i=0;i<ndim;i++){
    totpop+=qm->creal[i]*qm->creal[i]+qm->cimag[i]*qm->cimag[i];
  }
  qm->groundstate=1-totpop;
  for(i=0;i<ndim*ndim;i++){
    eigvec[i]=eigvec_real[i]+IMAG*eigvec_imag[i];
  }
  interval=time(NULL);
  if(MULTISIM(cr)){
    if (cr->ms->sim==0)
      fprintf(stderr,"node %d: gmx_sumd_sim  done at %ld\n",cr->ms->sim,interval-start); 
  } 
  else{
    fprintf(stderr,"node 0: gmx_sumd_sim done at %ld\n",interval-start);
  }
  /* copy the eigenvectors to qmrec */
  for(i=0;i<ndim*ndim;i++){
    qm->eigvec[i]=eigvec[i];
  }
  if((dodia) ){
    evout=fopen(eigenvectorfile,"a");
    for(i=0;i<ndim;i++){
      fprintf(evout,
	      "step %4d Eigenvector %4d gap %12.8lf (c: %12.8lf + %12.8lf I):",
	      step,i,eigval[i]-(
				energies[ndim-1]-cavity_dispersion(qm->n_max,qm)),
	      qm->creal[i],qm->cimag[i]);
      for(k=0;k<ndim;k++){
        fprintf(evout," %12.8lf + %12.8lf I",creal(eigvec[i*ndim+k]),cimag(eigvec[i*ndim+k]));
      }
      fprintf(evout,"\n");
    }
    fclose(evout);
  }
  interval=time(NULL);
  
  /* compute Hellman Feynman forces. 
   */
 
  if(fr->qr->SHmethod != eSHmethodEhrenfest){
    p=qm->polariton;
    betasq = conj(eigvec[p*ndim+m])*eigvec[p*ndim+m];
    a_sump = 0.0+IMAG*0.0;
    for (i=0;i<(qm->n_max)+1;i++){
      a_sump += eigvec[p*ndim+nmol+i]*sqrt(cavity_dispersion((i+qm->n_min),qm)/V0_2EP)*cexp(IMAG*2*M_PI*(i+qm->n_min)/L_au*m*L_au/((double) nmol));
    }
    ab = conj(eigvec[p*ndim+m])*a_sump; //actually sum of alphas * beta_j
    ab += conj(a_sump)*eigvec[p*ndim+m];
    for(i=0;i<qm->nrQMatoms;i++){
      for(j=0;j<DIM;j++){
        /* diagonal term */
        fij =(betasq*QMgrad_S1[i][j]+(1-betasq)*QMgrad_S0[i][j]);
        /* off-diagonal term */
        fij-= ab*tdmX[i][j]*u[0];
        fij-= ab*tdmY[i][j]*u[1];
        fij-= ab*tdmZ[i][j]*u[2];
        fij*=HARTREE_BOHR2MD;
        f[i][j]      += creal(fij);
        fshift[i][j] += creal(fij);
      }
    }
    for(i=0;i<mm->nrMMatoms;i++){
      for(j=0;j<DIM;j++){
        /* diagonal term */
        fij =(betasq*MMgrad_S1[i][j]+(1-betasq)*MMgrad_S0[i][j]);
        /* off-diagonal term */
        fij-= ab*tdmXMM[i][j]*u[0];
        fij-= ab*tdmYMM[i][j]*u[1];
        fij-= ab*tdmZMM[i][j]*u[2];
        fij*=HARTREE_BOHR2MD;
        f[i+qm->nrQMatoms][j]      += creal(fij);
        fshift[i+qm->nrQMatoms][j] += creal(fij);
      }
    }
    QMener = eigval[p]*HARTREE2KJ*AVOGADRO;
  }
  else {
    /* Ehrenfest dynamics, need to compute gradients of all polaritonic
     * states and weight them with weights of the states. Also the
     * nonadiabatic couplings between polaritonic states are needed now
     */
    QMener=0.0; 
    for (p=0;p<ndim;p++){
      /* do the diagonal terms first p=q. These terems are same as above for 
       * single state procedred  
       */
      csq = conj(qm->creal[p]+IMAG*qm->cimag[p])*(qm->creal[p]+IMAG*qm->cimag[p])/totpop;
      betasq = conj(eigvec[p*ndim+m])*eigvec[p*ndim+m];
      a_sump = 0.0+IMAG*0.0;
      for (i=0;i<(qm->n_max)+1;i++){
        a_sump += eigvec[p*ndim+nmol+i]*sqrt(cavity_dispersion((i+qm->n_min),qm)/V0_2EP)*cexp(IMAG*2*M_PI*(i+qm->n_min)/L_au*m*L_au/((double) nmol));
      }
      ab = conj(eigvec[p*ndim+m])*a_sump; //actually sum of alphas * beta_j
      ab += conj(a_sump)*eigvec[p*ndim+m]; // ADDED GG, this accounts for the 3rd and 4th term together in equation 13. 
      QMener += csq*eigval[p]*HARTREE2KJ*AVOGADRO;
      for(i=0;i<qm->nrQMatoms;i++){
        for(j=0;j<DIM;j++){
          /* diagonal term
           */
          fij =(betasq*QMgrad_S1[i][j]+(1-betasq)*QMgrad_S0[i][j]);
          /* off-diagonal term, Because coeficients are real: ab = ba
           */
          fij-= ab*tdmX[i][j]*u[0];
          fij-= ab*tdmY[i][j]*u[1];
          fij-= ab*tdmZ[i][j]*u[2];
          fij*=HARTREE_BOHR2MD*csq;
          f[i][j]      += creal(fij);
          fshift[i][j] += creal(fij);
        }
      }
      for(i=0;i<mm->nrMMatoms;i++){
        for(j=0;j<DIM;j++){
          /* diagonal terms
	       */
          fij =(betasq*MMgrad_S1[i][j]+(1-betasq)*MMgrad_S0[i][j]);
           /* off-diagonal term
            */
	      fij-= ab*tdmXMM[i][j]*u[0];
	      fij-= ab*tdmYMM[i][j]*u[1];
	      fij-= ab*tdmZMM[i][j]*u[2];
	      fij*=HARTREE_BOHR2MD*csq;
	      f[i+qm->nrQMatoms][j]      += creal(fij);
	      fshift[i+qm->nrQMatoms][j] += creal(fij);
        }
      }    
      /* now off-diagonals . Normalized
       */
      for (q=p+1;q<ndim;q++){
        cpcq = conj(qm->creal[p]+IMAG*(qm->cimag[p]))*(qm->creal[q]+IMAG*(qm->cimag[q]))/totpop;
        betasq = conj(eigvec[p*ndim+m])*eigvec[q*ndim+m];

        bpaq = conj(a_sum)*eigvec[q*ndim+m];
        a_sumq = 0.0+IMAG*0.0;
	
        for (i=0;i<(qm->n_max)+1;i++){
          a_sumq += eigvec[q*ndim+nmol+i]*sqrt(cavity_dispersion((i+qm->n_min),qm)/V0_2EP)*cexp(IMAG*2*M_PI*(i+qm->n_min)/L_au*m*L_au/((double) nmol));
        }
        bpaq = conj(eigvec[p*ndim+m])*a_sumq;
        apbq = conj(a_sump)*eigvec[q*ndim+m];
        bqap = conj(eigvec[q*ndim+m])*a_sump; /* conj(apbq) */
        aqbp = conj(a_sumq)*eigvec[p*ndim+m]; /* conj(bpaq) */
        for(i=0;i<qm->nrQMatoms;i++){
          for(j=0;j<DIM;j++){
	        /* diagonal term
             */
            fij=0;
            fij = cpcq*betasq*(QMgrad_S1[i][j]-QMgrad_S0[i][j]);
            fij+= conj(cpcq)*conj(betasq)*(QMgrad_S1[i][j]-QMgrad_S0[i][j]);
	        /* off-diagonal term
	         */
            fij-= cpcq*(bpaq+apbq)*tdmX[i][j]*u[0];
            fij-= cpcq*(bpaq+apbq)*tdmY[i][j]*u[1];
            fij-= cpcq*(bpaq+apbq)*tdmZ[i][j]*u[2];
            fij-= conj(cpcq)*(bqap+aqbp)*tdmX[i][j]*u[0];
            fij-= conj(cpcq)*(bqap+aqbp)*tdmY[i][j]*u[1];
            fij-= conj(cpcq)*(bqap+aqbp)*tdmZ[i][j]*u[2];
            fij*=HARTREE_BOHR2MD;
            f[i][j]      += creal(fij);
            fshift[i][j] += creal(fij);
          }
        }
        for(i=0;i<mm->nrMMatoms;i++){
          for(j=0;j<DIM;j++){
            /* diagonal term
             */
            fij = cpcq*betasq*(MMgrad_S1[i][j]-MMgrad_S0[i][j]);
            fij+= conj(cpcq)*conj(betasq)*(MMgrad_S1[i][j]-MMgrad_S0[i][j]);
            /*l off-diagonal term
             */
            fij-= cpcq*(bpaq+apbq)*tdmXMM[i][j]*u[0];
            fij-= cpcq*(bpaq+apbq)*tdmYMM[i][j]*u[1];
            fij-= cpcq*(bpaq+apbq)*tdmZMM[i][j]*u[2];
            fij-= conj(cpcq)*(bqap+aqbp)*tdmXMM[i][j]*u[0];
            fij-= conj(cpcq)*(bqap+aqbp)*tdmYMM[i][j]*u[1];
            fij-= conj(cpcq)*(bqap+aqbp)*tdmZMM[i][j]*u[2];
            fij*=HARTREE_BOHR2MD;
            f[i+qm->nrQMatoms][j]      += creal(fij);
            fshift[i+qm->nrQMatoms][j] += creal(fij);
          }
        }
      }
    }
  }
  interval=time(NULL);
  if(MULTISIM(cr)){
    if (cr->ms->sim==0) 
      fprintf(stderr,"node %d: Forces done at %ld\n",cr->ms->sim,interval-start);
  }
  else{
    fprintf(stderr,"Forces done at %ld\n",interval-start);
  }
  if ( fr->qr->SHmethod !=  eSHmethoddiabatic ){
    /* printing the coefficients to C.dat */
    if (dodia){
      fprintf(stderr,"rho0 (%d) = %lf, Energy = %lf\n",step,qm->groundstate,energies[ndim-1]-cavity_dispersion(qm->n_max,qm));
      sprintf(buf,"%s/C.dat",qm->work_dir);
      Cout= fopen(buf,"w");
      fprintf(Cout,"%d\n",step);
      for(i=0;i<ndim;i++){
        fprintf (Cout,"%.5lf %.5lf\n ",qm->creal[i],qm->cimag[i]);
      }
      fprintf (Cout,"%.5lf\n",qm->groundstate);
      fclose(Cout);
    }
    /* now account for the decay that will happen in the next timestep */
    if(qm->QEDdecay>0.0){
      qm->groundstate=0;
      for ( i = 0 ; i < ndim ; i++ ){
        asq = 0.0;
        for (j=0;j<qm->n_max+1;j++){
          asq += conj(eigvec[i*ndim+nmol+j])*eigvec[i*ndim+nmol+j];
        }
        decay = exp(-0.5*(qm->QEDdecay)*asq*(qm->dt));
        qm->groundstate-=conj(qm->creal[i]+IMAG*qm->cimag[i])*(qm->creal[i]+IMAG*qm->cimag[i])*(decay*decay-1);
        qm->creal[i] *= decay;
        qm->cimag[i] *= decay;
      }
    }
    /* now account also for the decoherence that will also happen in the next timestep */
    /* we thus use the current total kinetic energy. We need to send this around we wrote separate routine */
    /* Decoherence corrections make sense only for surface hopping methods, so we check for that.
     */
    if ( (fr->qr->SHmethod == eSHmethodTully ||
	  fr->qr->SHmethod == eSHmethodGranucci ) && (qm->QEDdecoherence > 0.) ){
      decoherence(cr,qm,mm,ndim,eigval);
    }
  }
  interval=time(NULL);
  if(MULTISIM(cr)){
    if (cr->ms->sim==0)
      fprintf(stderr,"node %d: decay done at %ld\n",cr->ms->sim,interval-start);
  }
  else{
    fprintf(stderr,"decay done at %ld\n",interval-start);
  }
  free(eigenvectorfile);
  free (final_eigenvecfile);
  
  free (eigval);
  free (eigvec);
  free (eigvec_real);
  free (eigvec_imag);
  free (state);
  return (QMener);
} /* do_adiabatic */

   
real call_gaussian_QED(t_commrec *cr,  t_forcerec *fr, 
		   t_QMrec *qm, t_MMrec *mm, rvec f[], rvec fshift[])
{
  /* multiple gaussian jobs for QED */
  static int
    step=0;
  int
    i,j=0,k,m,ndim,nmol;
  double
    *energies,Eground,c, QMener=0.0;
  rvec
    *QMgrad_S0,*MMgrad_S0,*QMgrad_S1,*MMgrad_S1,tdm;
  rvec
    *tdmX,*tdmY,*tdmZ,*tdmXMM,*tdmYMM,*tdmZMM;
  char
    *exe,*energyfile,buf[3000];
  double
    *tmp=NULL,L_au=qm->L*microM2BOHR;
  dplx
    *matrix=NULL,*couplings=NULL;
  double
    *send_couple_real,*send_couple_imag;
  int
    dodia=1,p,q;
  FILE
    *enerout=NULL;
  time_t 
    start,end,interval;

  start = time(NULL);
  snew(exe,300000);
  sprintf(exe,"%s/%s",qm->gauss_dir,qm->gauss_exe);

  /*  excited state forces */
  snew(QMgrad_S1,qm->nrQMatoms);
  snew(MMgrad_S1,mm->nrMMatoms);

  /* ground state forces */
  snew(QMgrad_S0,qm->nrQMatoms);
  snew(MMgrad_S0,mm->nrMMatoms);
  snew(tdmX,qm->nrQMatoms);
  snew(tdmY,qm->nrQMatoms);
  snew(tdmZ,qm->nrQMatoms);
  snew(tdmXMM,mm->nrMMatoms);
  snew(tdmYMM,mm->nrMMatoms);
  snew(tdmZMM,mm->nrMMatoms);
  write_gaussian_input_QED(cr,step,fr,qm,mm);



  /* we use the script to use QM code */ 
  do_gaussian(step,exe);
  interval=time(NULL);
  if (MULTISIM(cr)){
    if (cr->ms->sim==0)
      fprintf(stderr,"node %d: do_gaussian done at %ld\n",cr->ms->sim,interval-start);
  }
  else{
    fprintf(stderr,"node 0: read_gaussian done at %ld\n",interval-start);
  }
  QMener = read_gaussian_output_QED(cr,QMgrad_S1,MMgrad_S1,QMgrad_S0,MMgrad_S0,
				    step,qm,mm,&tdm,tdmX,tdmY,tdmZ,
                                    tdmXMM,tdmYMM,tdmZMM,&Eground);
  interval=time(NULL);
  if(MULTISIM(cr)){
    if (cr->ms->sim==0){
      fprintf(stderr,"node %d: read_gaussian done at %ld\n",cr->ms->sim,interval-start);
    }
    ndim=cr->ms->nsim+(qm->n_max-qm->n_min)+1;
    m=cr->ms->sim;
    nmol=cr->ms->nsim;
  }
  else{
    fprintf(stderr,"read_gaussian done at %ld\n",interval-start);
    ndim=1+(qm->n_max-qm->n_min)+1;
    m=0;
    nmol=1;
  }
  snew(energyfile,3000);
  sprintf(energyfile,"%s/%s%d.dat",qm->work_dir,"energies",m);
  enerout=fopen(energyfile,"a");
  fprintf(enerout,"step %d E(S0): %12.8lf E(S1) %12.8lf TDM: %12.8lf %12.8lf %12.8lf\n",step,Eground, QMener, tdm[XX],tdm[YY],tdm[ZZ]);
  fclose(enerout);

  snew(energies,ndim);
  /* on the diagonal there is the excited state energy of the molecule
   * plus the ground state energies of all other molecules
   */
  for (i=0;i<ndim;i++){
    energies[i]=Eground;
  }
  energies[m]=QMener; /* the excited state energy, overwrites
			 the ground state energie */
  /* send around */
  if(MULTISIM(cr)){
    gmx_sumd_sim(ndim,energies,cr->ms);
  }
//  for (i=qm->n_max;i>0;i--){
//    energies[ndim-(qm->n_max+1)-i]+=cavity_dispersion(i,qm);
//  }
  for (i=0;i< (qm->n_max-qm->n_min)+1;i++){
    energies[nmol+i]+=cavity_dispersion(qm->n_min+i,qm);
  } /* after summing the ground state energies, the photon energy of the cavity 
      (such that w[k]=w[-k]) is added to the last 2*n_max+1 diagonal terms,  */

  /* now we fill up the off-diagonals, basically the dot product of the dipole
     moment with the unit vector of the E-field of the cavity/plasmon times the
     E-field magnitud that is now k-dependent through w(k)
  */
  snew(couplings,nmol*((qm->n_max-qm->n_min)+1));
  double E0_norm_sq;
  E0_norm_sq = iprod(qm->E,qm->E); // Square of the magitud of the E-field at k=0
  double V0_2EP = qm->omega/(E0_norm_sq); //2*Epsilon0*V_cav at k=0 (in a.u.)
  double u[3];
//  fprintf(stderr,"E0_norm_sq = %lf\n",E0_norm_sq);
    if(E0_norm_sq>0.0){
        u[0]=qm->E[0]/sqrt(E0_norm_sq);
        u[1]=qm->E[1]/sqrt(E0_norm_sq);
        u[2]=qm->E[2]/sqrt(E0_norm_sq); //unit vector in E=Ex*u1+Ey*u2+Ez*u3
    }
    else{
        V0_2EP=1;
        u[0]=u[1]=u[2]=0.0;
    }
      
  for (i=0;i<(qm->n_max-qm->n_min+1);i++){
//    couplings[m*((qm->n_max)+1)+i] = -iprod(tdm,u)*sqrt(cavity_dispersion(i,qm)/V0_2EP)*cexp(IMAG*2*M_PI*i/L_au*m*L_au/((double) nmol));
    couplings[m*((qm->n_max-qm->n_min)+1)+i] = -iprod(tdm,u)*sqrt(cavity_dispersion(qm->n_min+i,qm)/V0_2EP)*cexp(IMAG*2*M_PI*(qm->n_min+i)/L_au*m*L_au/((double) nmol));
  }
  /* send couplings around */
  snew(send_couple_real,nmol*((qm->n_max-qm->n_min)+1));
  snew(send_couple_imag,nmol*((qm->n_max-qm->n_min)+1));
  for (i=0;i<nmol*((qm->n_max-qm->n_min)+1);i++){
    send_couple_real[i]=creal(couplings[i]);
    send_couple_imag[i]=cimag(couplings[i]);
  }
  if(MULTISIM(cr)){
    gmx_sumd_sim(nmol*((qm->n_max-qm->n_min)+1),send_couple_real,cr->ms);
    gmx_sumd_sim(nmol*((qm->n_max-qm->n_min)+1),send_couple_imag,cr->ms);
  }
  for (i=0;i<nmol*((qm->n_max-qm->n_min)+1);i++){
    couplings[i]=send_couple_real[i]+IMAG*send_couple_imag[i];
  }



  snew(matrix,ndim*ndim);
  for (i=0;i<ndim;i++){
    matrix[i+(i*ndim)]=energies[i];
  }
  for (k=0;k<nmol;k++){
    for (j=0;j<((qm->n_max-qm->n_min)+1);j++){
      /* GG @ 5.1.2023: altered which block we take the complex conjugate
       * of. Turns out that when running in the diabatic
       * representation, with the adjoint of the upper right block, the
       * molecular wavepacket is moving in the wrong directon.
       */
      matrix[nmol+j+(k*ndim)]= (couplings[k*((qm->n_max-qm->n_min)+1)+j]);
      matrix[ndim*nmol+k+(j*ndim)]=conj(couplings[k*((qm->n_max-qm->n_min)+1)+j]);
    }
  }
  
  
  //  if (m==0){
  //  fprintf(stderr,"in main routine Matrix:\n");
  //  printM_complex(ndim,matrix);
  // }
  
  /* Matrix build, now let's do something with it. For the diabatic
     code, we directly propagate, whereas for the adiabatic we diagonalize it
  */
  switch (fr->qr->QEDrepresentation){
    case ( eQEDrepresentationadiabatic ):
      
      //if(fr->qr->QEDrepresentation==eQEDrepresentationadiabatic){
      QMener=do_adiabatic(cr, fr, qm, mm, f, fshift,matrix,step,
			  QMgrad_S1, MMgrad_S1, QMgrad_S0,MMgrad_S0,
			  tdmX, tdmY, tdmZ,tdmXMM,tdmYMM,tdmZMM,energies);
      break;
    case ( eQEDrepresentationdiabatic ):
      QMener = do_diabatic(cr, fr, qm, mm, f, fshift,matrix,step,
			   QMgrad_S1, MMgrad_S1, QMgrad_S0,MMgrad_S0,
			   tdmX, tdmY, tdmZ,tdmXMM,tdmYMM,tdmZMM,energies);
      break;
    case ( eQEDrepresentationdiabaticNonHerm ):
      QMener = do_diabatic_non_herm(cr, fr, qm, mm, f, fshift,matrix,step,
				    QMgrad_S1, MMgrad_S1, QMgrad_S0,MMgrad_S0,
				    tdmX, tdmY, tdmZ,tdmXMM,tdmYMM,tdmZMM,energies);
      break;
    case ( eQEDrepresentationHybrid ):
      QMener = do_hybrid(cr, fr, qm, mm, f, fshift,matrix,step,
			   QMgrad_S1, MMgrad_S1, QMgrad_S0,MMgrad_S0,
			   tdmX, tdmY, tdmZ,tdmXMM,tdmYMM,tdmZMM,energies);
      break;
    case ( eQEDrepresentationHybridNonHerm ):
      QMener = do_hybrid_non_herm(cr, fr, qm, mm, f, fshift,matrix,step,
				    QMgrad_S1, MMgrad_S1, QMgrad_S0,MMgrad_S0,
				    tdmX, tdmY, tdmZ,tdmXMM,tdmYMM,tdmZMM,energies);
      break;
  }
  
  /* store the Hamiltonian for the next step in QMrec */
  for(i=0;i<ndim*ndim;i++){
    qm->matrix[i]=matrix[i];
  }
  step++;
  free(exe);
  free (matrix);
  

  free(energyfile);
  free (MMgrad_S0);
  free (QMgrad_S1);
  free (MMgrad_S1);
  free (QMgrad_S0);
  free (tdmX);
  free (tdmY);
  free (tdmZ);
  free (tdmXMM);
  free (tdmYMM);
  free (tdmZMM);
  free (couplings);
  free (send_couple_real);
  free (send_couple_imag);
  free(energies);
  return(QMener);
} /* call_gaussian_QED */


real call_gaussian_SH(t_commrec *cr, t_forcerec *fr, t_QMrec *qm, t_MMrec *mm, 
		      rvec f[], rvec fshift[])
{ 
  /* a gaussian call routine intended for doing diabatic surface
   * "sliding". See the manual for the theoretical background of this
   * TSH method.  
   */
  static int
    step=0;
  int
    state,i,j;
  real
    QMener=0.0;
  static  gmx_bool
    swapped=FALSE; /* handle for identifying the current PES */
  gmx_bool
    swap=FALSE; /* the actual swap */
  rvec
    *QMgrad,*MMgrad;
  char
    *buf;
  char
    *exe;
  real
    deltaE = 0.0;
  
  snew(exe,30);
  sprintf(exe,"%s/%s",qm->gauss_dir,qm->gauss_exe);
  /* hack to do ground state simulations */
  if(!step){
    snew(buf,20);
    buf = getenv("STATE");
    if (buf)
      sscanf(buf,"%d",&state);
    else
      state=2;
    if(state==1)
      swapped=TRUE;
  }
  /* end of hack */

  /* copy the QMMMrec pointer */
  snew(QMgrad,qm->nrQMatoms);
  snew(MMgrad,mm->nrMMatoms);
  /* at step 0 there should be no SA */
  /*  if(!step)
   * qr->bSA=FALSE;*/
  /* temporray set to step + 1, since there is a chk start */
  write_gaussian_SH_input(step,swapped,fr,qm,mm);

  do_gaussian(step,exe);
  QMener = read_gaussian_SH_output(QMgrad,MMgrad,step,swapped,qm,mm,&deltaE);

  /* check for a surface hop. Only possible if we were already state
   * averaging.
   */
  if(qm->SAstep>0 && deltaE < 0.01){
    if(!swapped){
      swap    = (step && hop(step,qm));
      swapped = swap;
    } 
    else { /* already on the other surface, so check if we go back */
      swap    = (step && hop(step,qm));
      swapped =!swap; /* so swapped shoud be false again */
    }
    if (swap){/* change surface, so do another call */
      write_gaussian_SH_input(step,swapped,fr,qm,mm);
      do_gaussian(step,exe);
      QMener = read_gaussian_SH_output(QMgrad,MMgrad,step,swapped,qm,mm,&deltaE);
    }
  }
  /* add the QMMM forces to the gmx force array and fshift
   */
  for(i=0;i<qm->nrQMatoms;i++){
    for(j=0;j<DIM;j++){
      f[i][j]      = HARTREE_BOHR2MD*QMgrad[i][j];
      fshift[i][j] = HARTREE_BOHR2MD*QMgrad[i][j];
    }
  }
  for(i=0;i<mm->nrMMatoms;i++){
    for(j=0;j<DIM;j++){
      f[i+qm->nrQMatoms][j]      = HARTREE_BOHR2MD*MMgrad[i][j];
      fshift[i+qm->nrQMatoms][j] = HARTREE_BOHR2MD*MMgrad[i][j];
    }
  }
  QMener = QMener*HARTREE2KJ*AVOGADRO;
  fprintf(stderr,"step %5d, SA = %5d, swap = %5d\n",
	  step,(qm->SAstep>0),swapped);
  step++;
  free(exe);
  return(QMener);
  
} /* call_gaussian_SH */

/* end of gaussian sub routines */

#else
int
gmx_qmmm_gaussian_empty;
#endif

