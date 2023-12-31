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
 * Gromacs Runs On Most of All Computer Systems
 */

#ifndef _names_h
#define _names_h


#include "typedefs.h"

#ifdef __cplusplus
extern "C" {
#endif

/* All string arrays are NULL terminated, and therefore have an
 * extra argument (the +1)
 * these should correspond to names.c and include/types/enums.h
 */
extern const char *epbc_names[epbcNR+1];
extern const char *etcoupl_names[etcNR+1];
extern const char *epcoupl_names[epcNR+1];
extern const char *epcoupltype_names[epctNR+1];
extern const char *erefscaling_names[erscNR+1];
extern const char *ens_names[ensNR+1];
extern const char *ei_names[eiNR+1];
extern const char *yesno_names[BOOL_NR+1];
extern const char *bool_names[BOOL_NR+1];
extern const char *eel_names[eelNR+1];
extern const char *eewg_names[eewgNR+1];
extern const char *evdw_names[evdwNR+1];
extern const char *econstr_names[econtNR+1];
extern const char *ptype_str[eptNR+1];
extern const char *egrp_nm[egNR+1];
extern const char *edisre_names[edrNR+1];
extern const char *edisreweighting_names[edrwNR+1];
extern const char *enbf_names[eNBF_NR+1];
extern const char *ecomb_names[eCOMB_NR+1];
extern const char *gtypes[egcNR+1];
extern const char *efep_names[efepNR+1];
extern const char *separate_dhdl_file_names[efepNR+1];
extern const char *dhdl_derivatives_names[efepNR+1];
extern const char *efep_names[efepNR+1];
extern const char *esol_names[esolNR+1];
extern const char *enlist_names[enlistNR+1];
extern const char *edispc_names[edispcNR+1];
extern const char *ecm_names[ecmNR+1];
extern const char *eann_names[eannNR+1];
extern const char *egb_names[egbNR+1];
extern const char *eis_names[eisNR+1];
extern const char *esa_names[esaNR+1];
extern const char *ewt_names[ewtNR+1];
extern const char *epull_names[epullNR+1];
extern const char *epullg_names[epullgNR+1];
extern const char *eQMmethod_names[eQMmethodNR+1];
extern const char *eQMbasis_names[eQMbasisNR+1];
extern const char *eQMMMscheme_names[eQMMMschemeNR+1];
extern const char *eMultentOpt_names[eMultentOptNR+1];
extern const char *eSHmethod_names[eSHmethodNR+1];
extern const char *eQEDrepresentation_names[eQEDrepresentationNR+1];
#define	UNDEFINED		"UNDEFINED"
#define ENUM_NAME(e,max,names)	((((e)<0)||((e)>=(max)))?UNDEFINED:(names)[e])

#define BOOL(e)        ENUM_NAME(e,BOOL_NR,bool_names)
#define ENS(e)         ENUM_NAME(e,ensNR,ens_names)
#define EI(e)          ENUM_NAME(e,eiNR,ei_names)
#define EPBC(e)        ENUM_NAME(e,epbcNR,epbc_names)
#define ETCOUPLTYPE(e) ENUM_NAME(e,etcNR,etcoupl_names)
#define EPCOUPLTYPE(e) ENUM_NAME(e,epcNR,epcoupl_names)
#define EPCOUPLTYPETYPE(e) ENUM_NAME(e,epctNR,epcoupltype_names)
#define EREFSCALINGTYPE(e) ENUM_NAME(e,erscNR,erefscaling_names)
#define EBLOCKS(e)     ENUM_NAME(e,ebNR,eblock_names)
#define EPARAM(e)      ENUM_NAME(e,epNR,eparam_names)
#define EELTYPE(e)     ENUM_NAME(e,eelNR,eel_names)
#define EVDWTYPE(e)    ENUM_NAME(e,evdwNR,evdw_names)
#define ECONSTRTYPE(e) ENUM_NAME(e,econtNR,econstr_names)
#define EDISRETYPE(e)  ENUM_NAME(e,edrNR,edisre_names)
#define EDISREWEIGHTING(e)  ENUM_NAME(e,edrwNR,edisreweighting_names)
#define ENBFNAME(e)    ENUM_NAME(e,eNBF_NR,enbf_names)
#define ECOMBNAME(e)   ENUM_NAME(e,eCOMB_NR,ecomb_names)
#define EFEPTYPE(e)    ENUM_NAME(e,efepNR,efep_names)
#define SEPDHDLFILETYPE(e) ENUM_NAME(e,sepdhdlfileNR,separate_dhdl_file_names)
#define DHDLDERIVATIVESTYPE(e) ENUM_NAME(e,dhdlderivativesNR,dhdl_derivatives_names)
#define ESOLTYPE(e)    ENUM_NAME(e,esolNR,esol_names)
#define ENLISTTYPE(e)  ENUM_NAME(e,enlistNR,enlist_names)
#define EDISPCORR(e)   ENUM_NAME(e,edispcNR,edispc_names)
#define ECOM(e)        ENUM_NAME(e,ecmNR,ecm_names)
#define EANNEAL(e)      ENUM_NAME(e,eannNR,eann_names)
#define EGBALGORITHM(e) ENUM_NAME(e,egbNR,egb_names)
#define ESAALGORITHM(e) ENUM_NAME(e,esaNR,esa_names)
#define EIMPLICITSOL(e) ENUM_NAME(e,eisNR,eis_names)
#define EWALLTYPE(e)   ENUM_NAME(e,ewtNR,ewt_names)
#define EPULLTYPE(e)   ENUM_NAME(e,epullNR,epull_names)
#define EPULLGEOM(e)   ENUM_NAME(e,epullgNR,epullg_names)
#define EQMMETHOD(e)   ENUM_NAME(e,eQMmethodNR,eQMmethod_names)
#define EQMBASIS(e)    ENUM_NAME(e,eQMbasisNR,eQMbasis_names)
#define EQMMMSCHEME(e) ENUM_NAME(e,eQMMMschemeNR,eQMMMscheme_names)
#define ESHMETHOD(e)   ENUM_NAME(e,eSHmethodNR,eSHmethod_names)
#define EQEDREPRESENTATION(e)   ENUM_NAME(e,eQEDrepresentationNR,eQEDrepresentation_names)
#define EMULTENTOPT(e) ENUM_NAME(e,eMultentOptNR,eMultentOpt_names)

#ifdef __cplusplus
}
#endif

#endif	/* _names_h */
