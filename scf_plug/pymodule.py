#
# @BEGIN LICENSE
#
# scf_plug by Psi4 Developer, a plugin to:
#
# Psi4: an open-source quantum chemistry software package
#
# Copyright (c) 2007-2017 The Psi4 Developers.
#
# The copyrights for code used from other parties are included in
# the corresponding files.
#
# This file is part of Psi4.
#
# Psi4 is free software; you can redistribute it and/or modify
# it under the terms of the GNU Lesser General Public License as published by
# the Free Software Foundation, version 3.
#
# Psi4 is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public License along
# with Psi4; if not, write to the Free Software Foundation, Inc.,
# 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
#
# @END LICENSE
#

import psi4
import psi4.driver.p4util as p4util
from psi4.driver.procrouting import proc_util

def run_scf_plug(name, **kwargs):
    r"""Function encoding sequence of PSI module and plugin calls so that
    scf_plug can be called via :py:func:`~driver.energy`. For post-scf plugins.

    >>> energy('scf_plug')

    """
    lowername = name.lower()
    kwargs = p4util.kwargs_lower(kwargs)

    # Your plugin's psi4 run sequence goes here
    psi4.core.set_local_option('MYPLUGIN', 'PRINT', 1)

    # Compute a SCF reference, a wavefunction is return which holds the molecule used, orbitals
    # Fock matrices, and more
    print('Attention! This SCF may be density-fitted.')
    ref_wfn = kwargs.get('ref_wfn', None)
    if ref_wfn is None:
        ref_wfn = psi4.driver.scf_helper(name, **kwargs)


    # Ensure IWL files have been written when not using DF/CD
    # proc_util.check_iwl_file_from_scf_type(psi4.core.get_option('SCF', 'SCF_TYPE'), ref_wfn)

    # analytic derivatives do not work with scf_type df/cd
    scf_type = psi4.core.get_option('SCF', 'SCF_TYPE')
    if ( scf_type == 'CD' or scf_type == 'DF' ):
        raise ValidationError("""Error: analytic gradients not implemented for scf_type %s.""" % scf_type)

    # Call the Psi4 plugin
    # Please note that setting the reference wavefunction in this way is ONLY for plugins

    scf_plug_wfn = psi4.core.plugin('scf_plug.so', ref_wfn)
    print(scf_plug_wfn)
    print(ref_wfn)

    derivobj = psi4.core.Deriv(scf_plug_wfn)
    derivobj.set_deriv_density_backtransformed(True)
    derivobj.set_ignore_reference(True)
    grad = derivobj.compute()


   

   

    scf_plug_wfn.set_gradient(grad)

    return scf_plug_wfn


# Integration with driver routines
psi4.driver.procedures['energy']['scf_plug'] = run_scf_plug


def exampleFN():
    # Your Python code goes here
    pass
