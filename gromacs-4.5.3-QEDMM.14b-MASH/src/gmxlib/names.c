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
 * GROningen Mixture of Alchemy and Childrens' Stories
 */
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "typedefs.h"
#include "names.h"

/* note: these arrays should correspond to enums in include/types/enums.h */

const char *epbc_names[epbcNR+1]=
{
  "xyz", "no", "xy", "screw", NULL
};

const char *ens_names[ensNR+1]=
{
  "Grid","Simple", NULL
};

const char *ei_names[eiNR+1]=
{
  "md", "steep", "cg", "bd", "sd", "nm", "l-bfgs", "tpi", "tpic", "sd1", "md-vv", "md-vv-avek",NULL 
};

const char *bool_names[BOOL_NR+1]=
{
  "FALSE","TRUE", NULL
};

const char *yesno_names[BOOL_NR+1]=
{
  "no","yes", NULL
};

const char *ptype_str[eptNR+1] = {
  "Atom", "Nucleus", "Shell", "Bond", "VSite", NULL
};

const char *eel_names[eelNR+1] = {
  "Cut-off", "Reaction-Field", "Generalized-Reaction-Field",
  "PME", "Ewald", "PPPM", "Poisson", "Switch", "Shift", "User", 
  "Generalized-Born", "Reaction-Field-nec", "Encad-shift", 
  "PME-User", "PME-Switch", "PME-User-Switch", 
  "Reaction-Field-zero", NULL
};

const char *eewg_names[eewgNR+1] = {
  "3d", "3dc", NULL
};

const char *evdw_names[evdwNR+1] = {
  "Cut-off", "Switch", "Shift", "User", "Encad-shift", NULL
};

const char *econstr_names[econtNR+1] = {
  "Lincs", "Shake", NULL
};

const char *egrp_nm[egNR+1] = { 
  "Coul-SR","LJ-SR","Buck-SR", "Coul-LR", "LJ-LR", "Buck-LR",
  "Coul-14", "LJ-14", NULL
};

const char *etcoupl_names[etcNR+1] = {
  "No", "Berendsen", "Nose-Hoover", "yes", "Andersen", "Andersen-interval", "V-rescale", NULL
}; /* yes is alias for berendsen */

const char *epcoupl_names[epcNR+1] = {
  "No", "Berendsen", "Parrinello-Rahman", "Isotropic", "MTTK", NULL
}; /* isotropic is alias for berendsen */

const char *epcoupltype_names[epctNR+1] = {
  "Isotropic", "Semiisotropic", "Anisotropic", "Surface-Tension", NULL
};

const char *erefscaling_names[erscNR+1] = {
  "No", "All", "COM", NULL
};

const char *edisre_names[edrNR+1] = {
  "No", "Simple", "Ensemble", NULL
};

const char *edisreweighting_names[edrwNR+1] = {
  "Conservative", "Equal", NULL
};

const char *enbf_names[eNBF_NR+1] = {
  "", "LJ", "Buckingham", NULL
};

const char *ecomb_names[eCOMB_NR+1] = {
  "", "Geometric", "Arithmetic", "GeomSigEps", NULL
};

const char *gtypes[egcNR+1] = {
  "T-Coupling", "Energy Mon.", "Acceleration", "Freeze",
  "User1", "User2", "VCM", "XTC", "Or. Res. Fit", "QMMM", NULL
};

const char *efep_names[efepNR+1] = {
  "no", "yes", NULL
};

const char *separate_dhdl_file_names[sepdhdlfileNR+1] = {
  "yes", "no", NULL
};

const char *dhdl_derivatives_names[dhdlderivativesNR+1] = {
  "yes", "no", NULL
};

const char *esol_names[esolNR+1] = {
  "No", "SPC", "TIP4p", NULL
};

const char *enlist_names[enlistNR+1] = {
  "Atom-Atom", "SPC-Atom", "SPC-SPC", "TIP4p-Atom", "TIP4p-TIP4p", "CG-CG", NULL
};

const char *edispc_names[edispcNR+1] = {
  "No", "EnerPres", "Ener", "AllEnerPres", "AllEner", NULL
};

const char *ecm_names[ecmNR+1] = { 
  "Linear", "Angular", "None", NULL 
};

const char *eann_names[eannNR+1] = {
  "No", "Single", "Periodic", NULL
};

const char *eis_names[eisNR+1] = {
	"No", "GBSA", NULL
};

const char *egb_names[egbNR+1] = {
  "Still", "HCT", "OBC", NULL
};

const char *esa_names[esaNR+1] = {
  "Ace-approximation", "None", "Still", NULL
};

const char *ewt_names[ewtNR+1] = {
  "9-3", "10-4", "table", "12-6", NULL
};

const char *epull_names[epullNR+1] = { 
  "no", "umbrella", "constraint", "constant_force", NULL
};

const char *epullg_names[epullgNR+1] = { 
  "distance", "direction", "cylinder", "position", "direction_periodic", NULL
};

const char *eQMmethod_names[eQMmethodNR+1] = {
  "AM1", "PM3", "RHF",
  "UHF", "M06", "B3LYP", "MP2", "CASSCF","B3LYPLAN",
  "DIRECT", NULL
};

const char *eQMbasis_names[eQMbasisNR+1] = {
  "cc-pvdz", "STO-3G", "3-21G",
  "3-21G*", "3-21+G*", "6-21G",
  "6-31G", "6-31G*", "6-31+G*","6-31+G**",
  "6-311G", NULL
};

const char *eQMMMscheme_names[eQMMMschemeNR+1] = {
  "normal", "ONIOM", NULL
};

const char *eSHmethod_names[eSHmethodNR+1] = {
  "diabatic", "Tully", "Granucci", "Ehrenfest", NULL
};

const char *eQEDrepresentation_names[eQEDrepresentationNR+1] = {
  "adiabatic", "diabatic" , "diabaticNonHerm", "hybrid", "hybridNonHerm",NULL
};

const char *eMultentOpt_names[eMultentOptNR+1] = {
  "multiple_entries", "no", "use_last", NULL
};

