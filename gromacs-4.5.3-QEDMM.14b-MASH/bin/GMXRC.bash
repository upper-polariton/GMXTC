# zsh configuration file for Gromacs
# First we remove old gromacs stuff from the paths
# by selecting everything else.
# This is not 100% necessary, but very useful when we
# repeatedly switch between gmx versions in a shell.

# First remove gromacs part of ld_library_path
tmppath=""
for i in `echo $LD_LIBRARY_PATH | sed "s/:/ /g"`; do
  if test "$i" != "$GMXLDLIB"; then
    if test "${tmppath}" == ""; then
      tmppath=$i
    else
    tmppath=${tmppath}:$i
  fi
  fi
done
LD_LIBRARY_PATH=$tmppath

# remove gromacs part of PKG_CONFIG_PATH
tmppath=""
for i in `echo $PKG_CONFIG_PATH | sed "s/:/ /g"`; do
  if test "$i" != "$GMXLDLIB/pkgconfig"; then
    tmppath=${tmppath}:$i
  fi
done
PKG_CONFIG_PATH=$tmppath

# remove gromacs part of path
tmppath=""
for i in `echo $PATH | sed "s/:/ /g"`; do
  if test "$i" != "$GMXBIN"; then
    tmppath=${tmppath}:$i
  fi
done
PATH=$tmppath

# and remove the gmx part of manpath
tmppath=""
for i in `echo $MANPATH | sed "s/:/ /g"`; do
  if test "$i" != "$GMXMAN"; then
    tmppath=${tmppath}:$i
  fi
done
if test "$tmppath" == ""; then
    tmppath=":"
fi
MANPATH=$tmppath

##########################################################
# This is the real configuration part. We save the Gromacs
# things in separate vars, so we can remove them later.
# If you move gromacs, change the next four line.
##########################################################
GMXBIN=/users/rhtichau/gromacs-4.5.3-QEDMM.9/bin
GMXLDLIB=/users/rhtichau/gromacs-4.5.3-QEDMM.9/lib
GMXMAN=/users/rhtichau/gromacs-4.5.3-QEDMM.9/share/man
GMXDATA=/users/rhtichau/gromacs-4.5.3-QEDMM.9/share
	
# NB: The variables already begin with ':' now, or are empty
LD_LIBRARY_PATH=${GMXLDLIB}${LD_LIBRARY_PATH}
PKG_CONFIG_PATH=${GMXLDLIB}/pkgconfig${PKG_CONFIG_PATH}
PATH=${GMXBIN}${PATH}
MANPATH=${GMXMAN}${MANPATH}

# export should be separate, so /bin/sh understands it
export GMXBIN GMXLDLIB GMXMAN GMXDATA LD_LIBRARY_PATH PATH MANPATH PKG_CONFIG_PATH

# read bash completions if understand how to use them
# and this shell supports extended globbing
if (complete) > /dev/null 2>&1; then
  if (shopt -s extglob) > /dev/null 2>&1; then
    if [ -f $GMXBIN/completion.bash ]; then
      source $GMXBIN/completion.bash; 
    fi
  fi
fi


