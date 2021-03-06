/*
 * @BEGIN LICENSE
 *
 * scf_plug by Psi4 Developer, a plugin to:
 *
 * Psi4: an open-source quantum chemistry software package
 *
 * Copyright (c) 2007-2017 The Psi4 Developers.
 *
 * The copyrights for code used from other parties are included in
 * the corresponding files.
 *
 * This file is part of Psi4.
 *
 * Psi4 is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, version 3.
 *
 * Psi4 is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License along
 * with Psi4; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * @END LICENSE
 */

#include "psi4/psi4-dec.h"
#include "psi4/libpsi4util/PsiOutStream.h"
#include "psi4/libpsi4util/process.h"
#include "psi4/liboptions/liboptions.h"
#include "psi4/libmints/wavefunction.h"
#include "psi4/libmints/molecule.h"
#include "psi4/libmints/factory.h"
#include "psi4/libmints/mintshelper.h"
#include "psi4/libmints/basisset.h"
#include "psi4/libmints/integral.h"
#include "psi4/libmints/dipole.h"
#include "psi4/libpsio/psio.hpp"
#include <math.h>
#include <iomanip>
#include <vector>

double e=2.718281828;


namespace psi{ namespace scf_plug {

extern "C" PSI_API
int read_options(std::string name, Options& options)
{
    if (name == "SCF_PLUG"|| options.read_globals()) {
        options.add_int("PRINT", 1);
        options.add_double("CVG", 1);
        options.add_double("PERT", 1);
        options.add_double("S", 1);
    }

    return true;
}




void FormDensityMatrix(SharedMatrix D, SharedMatrix C, int nmo, int occ){

    D->zero();

    for(int i = 0; i < nmo; ++i){

		for(int j = 0; j < nmo; ++j){

			for(int k = 0; k < occ; ++k){

				D->add(0, i, j, C->get(0, i, k) * C->get(0, j, k));

			}
		}
	}
}

void FormNewFockMatrix(SharedMatrix F, SharedMatrix H, SharedMatrix D, SharedMatrix eri, int nmo){

    for(int p = 0; p < nmo; ++p){

    	for(int q = 0; q < nmo; ++q){

    		F->set(0, p, q, H->get(0, p, q));

    		for(int r = 0; r < nmo; ++r){

    			for(int s = 0; s < nmo; ++s){

    				F->add(0, p, q, D->get(0, r, s) * (2.0*eri->get(0, p * nmo + q, r * nmo + s) - eri->get(0, p * nmo + r, q * nmo + s)));

    			}
    		}
    	}
    }
}

double ElecEnergy(double Elec, SharedMatrix D, SharedMatrix H, SharedMatrix F, int nmo){

    Elec = 0;

    for(int p = 0; p < nmo; ++p){

    	for(int q = 0; q < nmo; ++q){

    		Elec += D->get(0, p, q)*(H->get(0, p, q)+F->get(0, p, q));

    	}
    }

    return Elec;
}

void AO2MO_TwoElecInts(SharedMatrix eri, SharedMatrix eri_mo, SharedMatrix C, int nmo ){

    for(int i = 0; i < nmo; ++i){

        for(int j = 0; j < nmo; ++j){

            for(int k = 0; k < nmo; ++k){

                for(int l = 0; l < nmo; ++l){

                    double ints_temp = 0.0;

                    for(int p = 0; p < nmo; ++p){

                        for(int q = 0; q < nmo; ++q){

                            for(int r = 0; r < nmo; ++r){

                                for(int s = 0; s < nmo; ++s){

                                    ints_temp +=  eri->get(0, p * nmo + q, r * nmo + s) * C->get(0, s, l) * C->get(0, r, k) * C->get(0, q, j) * C->get(0, p, i);

                                }
                            }
                        }
                    }

                    eri_mo->set(0, i * nmo + j, k * nmo + l, ints_temp);

                }
            }
        }
    }
}

void AO2MO_FockMatrix(SharedMatrix F, SharedMatrix F_MO, SharedMatrix C, int nmo){

    for(int i = 0; i < nmo; ++i){

        for(int j = 0; j < nmo; ++j){

            double ints_temp = 0.0;

            for(int p = 0; p < nmo; ++p){

                for(int q = 0; q < nmo; ++q){

                    ints_temp += F->get(0, p, q) * C->get(0, p, i) * C->get(0, q, j);

                }
            }

            F_MO->set(0, i, j, ints_temp);

        }
    }
}



double MP2_Energy_SO(SharedMatrix eri_mo, SharedMatrix F_MO, int nso, int doccpi, std::vector<double> so_ints, std::vector<double> epsilon_ijab){

    double Emp2 = 0.0;
    int idx;

    for(int i = 0; i < 2 * doccpi; ++i){

        for(int j = 0; j < 2 * doccpi; ++j){

            for(int a = 2 * doccpi; a < nso; ++a){

                for(int b = 2 * doccpi; b < nso; ++b){

                    idx = i * nso * nso * nso + j * nso * nso + a * nso + b;

                    Emp2 += 0.25 * so_ints[idx] * so_ints[idx] / epsilon_ijab[idx];
                }
            }
        }
    }
    return(Emp2);
}


double DSRG_PT2_Energy_SO(SharedMatrix eri_mo, SharedMatrix F_MO, int nso, int doccpi, std::vector<double> so_ints, std::vector<double> epsilon_ijab, double S){

    double Edsrgpt2 = 0.0;
    int idx;

    for(int i = 0; i < 2 * doccpi; ++i){

        for(int j = 0; j < 2 * doccpi; ++j){

            for(int a = 2 * doccpi; a < nso; ++a){

                for(int b = 2 * doccpi; b < nso; ++b){

                    idx = i * nso * nso * nso + j * nso * nso + a * nso + b;

                    Edsrgpt2 += 0.25 * so_ints[idx] * so_ints[idx] / epsilon_ijab[idx] * (1.0 - pow(e, -2.0 * S * epsilon_ijab[idx] * epsilon_ijab[idx] ));
                }
            }
        }
    }
    return(Edsrgpt2);
}


void build_AOdipole_ints(SharedWavefunction wfn, SharedMatrix Dp) {

    std::shared_ptr<BasisSet> basisset = wfn->basisset();
    std::shared_ptr<IntegralFactory> ints_fac = std::make_shared<IntegralFactory>(basisset);
    int nbf = basisset->nbf();

    std::vector<SharedMatrix> AOdipole_ints_;
    // AOdipole_ints_.clear();
    for (const std::string& direction : {"X", "Y", "Z"}) {
        std::string name = "AO Dipole " + direction;
        AOdipole_ints_.push_back(SharedMatrix(new Matrix(name, nbf, nbf)));
    }
    std::shared_ptr<OneBodyAOInt> aodOBI(ints_fac->ao_dipole());
    aodOBI->compute(AOdipole_ints_);
    Dp->copy(AOdipole_ints_[2]);
}


extern "C" PSI_API
SharedWavefunction scf_plug(SharedWavefunction ref_wfn, Options& options)
{

//Parameters Declaration

    std::shared_ptr<Molecule> molecule = Process::environment.molecule();
    molecule->update_geometry();
    molecule->print();

    std::shared_ptr<BasisSet> ao_basisset=ref_wfn->basisset();
    MintsHelper mints(ref_wfn);

    int      print = options.get_int("PRINT");
    int      dims[] = {ao_basisset->nbf()};
    double   CVG = options.get_double("CVG");
    double   pert = options.get_double("PERT");
    double   S_const = options.get_double("S");
    int      iternum = 1;
    double   energy_pre;
    int      doccpi = 0;
    double   Enuc = molecule->nuclear_repulsion_energy(ref_wfn->get_dipole_field_strength());
    double   Elec, Etot, Emp2, Edsrg_pt2;
    int      irrep_num = ref_wfn->nirrep();

    int      nmo = dims[0];
    size_t   nso = 2 * dims[0];



    std::shared_ptr<MatrixFactory> factory(new MatrixFactory);
    factory->init_with(1,dims,dims);
  
    SharedMatrix overlap = mints.ao_overlap();
    SharedMatrix kinetic = mints.ao_kinetic();
    SharedMatrix potential = mints.ao_potential();
	SharedMatrix Omega = factory->create_shared_matrix("Omega");
    SharedMatrix F (new Matrix("Fock matrix", 1, dims, dims, 0));
    SharedMatrix F_uptp (new Matrix("Unperturbed Fock matrix", 1, dims, dims, 0));
    SharedMatrix F_MO (new Matrix("Fock_MO matrix", 1, dims, dims, 0));
    SharedMatrix F_MO_uptp (new Matrix("unperturbed Fock_MO matrix", 1, dims, dims, 0));
    SharedMatrix C (new Matrix("C matrix", 1, dims, dims, 0));
    SharedMatrix C_uptp (new Matrix("Unperturbed C matrix", 1, dims, dims, 0));
    SharedMatrix D (new Matrix("Density matrix", 1, dims, dims, 0));
    SharedMatrix D_uptp (new Matrix("Unperturbed Density matrix", 1, dims, dims, 0));
    SharedMatrix S (new Matrix("S matrix", 1, dims, dims, 0));
    SharedMatrix H = factory->create_shared_matrix("H");
    SharedMatrix H_uptb = factory->create_shared_matrix("Unperturbed H");
    SharedMatrix eri = mints.ao_eri();
    SharedMatrix eri_mo = eri->clone();
                 eri_mo->zero();                 
    SharedMatrix Dp (new Matrix("Dipole correction matrix", 1, dims, dims, 0));
    SharedMatrix Dp_temp (new Matrix("Dipole correction matrix Copy", 1, dims, dims, 0));
    SharedMatrix evecs (new Matrix("evecs", 1, dims, dims, 0));
    SharedVector evals (new Vector("evals", 1, dims));
    SharedVector ndip = DipoleInt::nuclear_contribution(Process::environment.molecule(), Vector3(0.0, 0.0, 0.0));
        
    Enuc += pert * ndip->get(0,2);

    Dimension doccpi_add = ref_wfn->doccpi();

    for(int i = 0; i < irrep_num; ++i){
    	doccpi += doccpi_add[i];
    }

    std::cout<<std::endl;


/************************ index ************************/

    auto four_idx = [&](size_t p, size_t q, size_t r, size_t s, size_t dim) -> size_t {
        size_t dim2 = dim * dim;
        size_t dim3 = dim2 * dim;
        return (p * dim3 + q * dim2 + r * dim + s);
    };

    auto two_idx = [&](size_t p, size_t q, size_t dim) -> size_t {
        return (p * dim + q);
    };

/************************ SCF ************************/

//Create H matrix

    H->copy(kinetic);
    H->add(potential);

    H_uptb->copy(H);

    build_AOdipole_ints(ref_wfn, Dp);
    Dp_temp->copy(Dp);


    Dp->Matrix::scale(pert);
    H->add(Dp);
    Dp->copy(Dp_temp);


//Create S^(-1/2) Matrix

    Omega->zero();
    overlap->diagonalize(evecs, evals);

    for(int i=0; i < nmo; ++i){
    	Omega->set(0, i, i, 1.0/sqrt(evals->get(0,i)));
    }
     
    S = Matrix::triplet(evecs, Omega, evecs, false, false, true);

//Create original fock matrix using transformation on H

	F = Matrix::triplet(S, H, S, true, false, false);

    F_uptp = Matrix::triplet(S, H_uptb, S, true, false, false);

//Create C matrix

    F->diagonalize(evecs, evals);
    C = Matrix::doublet(S, evecs, false, false);

    F_uptp->diagonalize(evecs, evals);
    C_uptp = Matrix::doublet(S, evecs, false, false);    

//Create Density matrix

    FormDensityMatrix(D, C, nmo, doccpi);

    FormDensityMatrix(D_uptp, C_uptp, nmo, doccpi);

//Create new Fock matrix

	FormNewFockMatrix(F, H, D, eri, nmo);

    FormNewFockMatrix(F_uptp, H_uptb, D_uptp, eri, nmo);

//Calculate the energy

	Elec = ElecEnergy(Elec, D_uptp, H_uptb, F_uptp, nmo);/*!!!!! TEST !!!! unperturbed*/
    Etot = Elec + Enuc;
    energy_pre = Etot; 
    Etot += CVG + 1.0;

//SCF iteration

while( fabs(energy_pre - Etot) > CVG){

	energy_pre = Etot;

    F = Matrix::triplet(S, F, S, true, false, false);
    F->diagonalize(evecs, evals);
    C = Matrix::doublet(S, evecs, false, false);

    FormDensityMatrix(D, C, nmo, doccpi);
	FormNewFockMatrix(F, H, D, eri, nmo);

    iternum++;

   /*********** unperturbed C *********/

    F_uptp = Matrix::triplet(S, F_uptp, S, true, false, false);
    F_uptp->diagonalize(evecs, evals);
    C_uptp = Matrix::doublet(S, evecs, false, false);

    FormDensityMatrix(D_uptp, C_uptp, nmo, doccpi);
    FormNewFockMatrix(F_uptp, H_uptb, D_uptp, eri, nmo);
    Elec = ElecEnergy(Elec, D_uptp, H_uptb, F_uptp, nmo);
    Etot = Elec + Enuc;

}

    double Escf = Etot;



/************************ MP2 & DSRG-PT2 (Orbital irrelevant) ver1.0 ************************/
   


    SharedMatrix Dp_d (new Matrix("dipole diagonal matrix", 1, dims, dims, 0));
    Dp_d->zero();

    SharedMatrix Dp_mo = Dp->clone();    

    AO2MO_FockMatrix(Dp, Dp_mo, C_uptp, nmo);


    Dp_d->copy(Dp_mo);

    Dp_d->Matrix::scale(pert);



    AO2MO_FockMatrix(F_uptp, F_MO, C_uptp, nmo);

    F_MO_uptp->copy(F_MO);

    F_MO->add(Dp_d);


/************************ MP2 & DSRG-PT2 (Orbital irrelevant) ver3.0 O-O block ************************/



    F_MO->diagonalize(evecs, evals);


    SharedMatrix F_MO_1 (new Matrix("rotated Fock matrix", 1, dims, dims, 0));
    F_MO_1->zero();

    for(int i = 0; i < nmo; ++i){
        F_MO_1->set(0, i, i, evals->get(0, i));
    }


    SharedMatrix C_pert (new Matrix("C_pert matrix", 1, dims, dims, 0));
    SharedMatrix U_pert (new Matrix("transformation matrix", 1, dims, dims, 0));



    U_pert->copy(evecs);

    C_pert = Matrix::doublet(C_uptp, U_pert, false, false);


/********** !!!!!This is the test area, remember to remove this section to rewind !!!!! *************/


// F_MO->copy(F_MO_uptp);



C_uptp->copy(C);
AO2MO_FockMatrix(F, F_MO, C, nmo);

/************************ MP2 & DSRG-PT2 (Orbital irrelevant) ver2.0 ************************/

    AO2MO_TwoElecInts(eri, eri_mo, C_uptp, nmo);

    // define the order of spin orbitals and store it as a vector of pairs (orbital index,spin)
    std::vector<std::pair<size_t, int>> so_labels(nso);
    for (size_t n = 0; n < nmo; n++) {
        so_labels[2 * n] = std::make_pair(n, 0);     // 0 = alpha
        so_labels[2 * n + 1] = std::make_pair(n, 1); // 1 = beta
    }

    // allocate the vector that will store the spin orbital integrals
    size_t nso2 = nso * nso;
    size_t nso4 = nso2 * nso2;
    std::vector<double> so_ints(nso4, 0.0);
    std::vector<double> amp_t(nso4, 0.0);
    std::vector<double> amp_t_dsrg(nso4, 0.0);
    std::vector<double> epsilon(nso, 0.0);
    std::vector<double> epsilon_ijab(nso4, 0.0);

    for (size_t p = 0; p < nso; ++p){
        epsilon[p] = F_MO->get(0, p/2, p/2);
    }

    for (size_t i = 0; i < 2 * doccpi; ++i){
        for (size_t j = 0; j < 2 * doccpi; ++j){
            for (size_t a = 2 * doccpi; a < nso; ++a){
                for (size_t b = 2 * doccpi; b < nso; ++b){

                    epsilon_ijab[four_idx(i, j, a, b, nso)] = epsilon[i] + epsilon[j] - epsilon[a] - epsilon[b];

                }
            }
        }
    }

    //form the integrals <pq||rs> = <pq|rs> - <pq|sr> = (pr|qs) - (ps|qr)
    for (size_t p = 0; p < nso; p++) {
        size_t p_orb = so_labels[p].first;
        int p_spin = so_labels[p].second;
        for (size_t q = 0; q < nso; q++) {
            size_t q_orb = so_labels[q].first;
            int q_spin = so_labels[q].second;
            for (size_t r = 0; r < nso; r++) {
                size_t r_orb = so_labels[r].first;
                int r_spin = so_labels[r].second;
                for (size_t s = 0; s < nso; s++) {
                    size_t s_orb = so_labels[s].first;
                    int s_spin = so_labels[s].second;

                    double integral = 0.0;
                    if ((p_spin == r_spin) and (q_spin == s_spin)) {
                        integral += eri_mo->get(0, p/2 * nmo + r/2, q/2 * nmo + s/2);
                    }
                    if ((p_spin == s_spin) and (q_spin == r_spin)) {
                        integral -= eri_mo->get(0, p/2 * nmo + s/2, q/2 * nmo + r/2);
                    }
                    so_ints[four_idx(p, q, r, s, nso)] = integral;

                    if(p < 2 * doccpi && q < 2 * doccpi && r >= 2 * doccpi && s >= 2 * doccpi){

                        amp_t[four_idx(p, q, r, s, nso)] = integral / epsilon_ijab[four_idx(p, q, r, s, nso)];
                        amp_t_dsrg[four_idx(p, q, r, s, nso)] = integral / epsilon_ijab[four_idx(p, q, r, s, nso)] * (1.0-pow(e,-S_const*epsilon_ijab[four_idx(p, q, r, s, nso)]*epsilon_ijab[four_idx(p, q, r, s, nso)]));

                    }

                }
            }
        }
    }


    int dims_nso2[] = {0};
    dims_nso2[0] = nso;


    SharedMatrix D_MP2 (new Matrix("MP2 Dipole Density matrix", 1, dims_nso2, dims_nso2, 0));
    SharedMatrix Z_MP2 (new Matrix("Z MP2 matrix", 1, dims_nso2, dims_nso2, 0));
    SharedMatrix Z_temp (new Matrix("Z matrix temporal", 1, dims_nso2, dims_nso2, 0));

    D_MP2->zero();
    Z_MP2->zero();



    for(int i = 0; i < 2 * doccpi; ++i){
        for(int j = 0; j < 2 * doccpi; ++j){
            for(int a = 2 * doccpi; a < nso; ++a){
                for(int b = 2 * doccpi; b < nso; ++b){

                    double t2,t3;

                    // t2 = -2.0*S_const*so_ints[four_idx(i, j, a, b, nso)]*amp_t_dsrg[four_idx(i, j, a, b, nso)]*epsilon_ijab[four_idx(i, j, a, b, nso)]*pow(e,-S_const*epsilon_ijab[four_idx(i, j, a, b, nso)]*epsilon_ijab[four_idx(i, j, a, b, nso)])-0.5*amp_t_dsrg[four_idx(i, j, a, b, nso)]*amp_t_dsrg[four_idx(i, j, a, b, nso)]*(1.0+pow(e,-S_const*epsilon_ijab[four_idx(i, j, a, b, nso)]*epsilon_ijab[four_idx(i, j, a, b, nso)]))/(1.0-pow(e,-S_const*epsilon_ijab[four_idx(i, j, a, b, nso)]*epsilon_ijab[four_idx(i, j, a, b, nso)]))+2.0*S_const*epsilon_ijab[four_idx(i, j, a, b, nso)]*epsilon_ijab[four_idx(i, j, a, b, nso)]*amp_t_dsrg[four_idx(i, j, a, b, nso)]*amp_t_dsrg[four_idx(i, j, a, b, nso)]*pow(e,-S_const*epsilon_ijab[four_idx(i, j, a, b, nso)]*epsilon_ijab[four_idx(i, j, a, b, nso)])/(1.0-pow(e,-S_const*epsilon_ijab[four_idx(i, j, a, b, nso)]*epsilon_ijab[four_idx(i, j, a, b, nso)]))/(1.0-pow(e,-S_const*epsilon_ijab[four_idx(i, j, a, b, nso)]*epsilon_ijab[four_idx(i, j, a, b, nso)]));
                    // t3 = -t2;

                    t2 = -0.5*amp_t_dsrg[four_idx(i, j, a, b, nso)]*amp_t_dsrg[four_idx(i, j, a, b, nso)]*(1.0+pow(e,-S_const*epsilon_ijab[four_idx(i, j, a, b, nso)]*epsilon_ijab[four_idx(i, j, a, b, nso)]))/(1.0-pow(e,-S_const*epsilon_ijab[four_idx(i, j, a, b, nso)]*epsilon_ijab[four_idx(i, j, a, b, nso)]))+2.0*S_const*so_ints[four_idx(i, j, a, b, nso)]*so_ints[four_idx(i, j, a, b, nso)]*pow(e,-2.0*S_const*epsilon_ijab[four_idx(i, j, a, b, nso)]*epsilon_ijab[four_idx(i, j, a, b, nso)]);
                    t3 = -t2;

                    D_MP2->add(0, i, i, t2);
                    D_MP2->add(0, a, a, t3);

                }
            }
        }
    }


    for(int m = 0; m < 2 * doccpi; ++m){
        for(int n = 0; n < 2 * doccpi; ++n){
            for(int j = 0; j < 2 * doccpi; ++j){
                for(int a = 2 * doccpi; a < nso; ++a){
                    for(int b = 2 * doccpi; b < nso; ++b){

                        if(m!=n){

                        if(fabs(epsilon[m]-epsilon[n]) > 1e-6){

                            double t3;

                            t3 = 1.0/(epsilon[n] - epsilon[m]) * so_ints[four_idx(m, j, a, b, nso)] * so_ints[four_idx(a, b, n, j, nso)] * ((1.0-pow(e, -2.0*S_const*epsilon_ijab[four_idx(n, j, a, b, nso)]*epsilon_ijab[four_idx(n, j, a, b, nso)]))/epsilon_ijab[four_idx(n, j, a, b, nso)] - (1.0-pow(e, -2.0*S_const*epsilon_ijab[four_idx(m, j, a, b, nso)]*epsilon_ijab[four_idx(m, j, a, b, nso)]))/epsilon_ijab[four_idx(m, j, a, b, nso)]);

                            Z_MP2->add(0, n, m, t3);}

                        else{


                            double t3;

                            t3 = so_ints[four_idx(m, j, a, b, nso)] * so_ints[four_idx(a, b, n, j, nso)] * (4.0*S_const*pow(e,-2.0*S_const*epsilon_ijab[four_idx(m, j, a, b, nso)]*epsilon_ijab[four_idx(m, j, a, b, nso)])-(1.0-pow(e,-2.0*S_const*epsilon_ijab[four_idx(m, j, a, b, nso)]*epsilon_ijab[four_idx(m, j, a, b, nso)]))/epsilon_ijab[four_idx(m, j, a, b, nso)]/epsilon_ijab[four_idx(m, j, a, b, nso)]);

                            Z_MP2->add(0, n, m, t3);

                        }
                        }
                    }
                }
            }
        }
    }   

    for(int c = 2 * doccpi; c < nso; ++c){
        for(int d = 2 * doccpi; d < nso; ++d){
            for(int a = 2 * doccpi; a < nso; ++a){
                for(int i = 0; i < 2 * doccpi; ++i){
                    for(int j = 0; j < 2 * doccpi; ++j){

                        if(c!=d){

                        if(fabs(epsilon[c]-epsilon[d]) > 1e-6){

                            double t4;

                            t4 = 1.0/(epsilon[d]-epsilon[c])*amp_t_dsrg[four_idx(i, j, a, c, nso)]*amp_t_dsrg[four_idx(i, j, a, d, nso)]*(epsilon_ijab[four_idx(i, j, a, c, nso)]*(1.0+pow(e, -S_const*epsilon_ijab[four_idx(i, j, a, d, nso)]*epsilon_ijab[four_idx(i, j, a, d, nso)]))/(1.0-pow(e, -S_const*epsilon_ijab[four_idx(i, j, a, c, nso)]*epsilon_ijab[four_idx(i, j, a, c, nso)])) - epsilon_ijab[four_idx(i, j, a, d, nso)]*(1.0+pow(e, -S_const*epsilon_ijab[four_idx(i, j, a, c, nso)]*epsilon_ijab[four_idx(i, j, a, c, nso)]))/(1.0-pow(e, -S_const*epsilon_ijab[four_idx(i, j, a, d, nso)]*epsilon_ijab[four_idx(i, j, a, d, nso)])));

                            Z_MP2->add(0, d, c, t4);}

                        else{

                            double t4;
                            t4 = so_ints[four_idx(i, j, a, c, nso)]*so_ints[four_idx(i, j, a, d, nso)]*(-4.0*S_const*pow(e,-2.0*S_const*epsilon_ijab[four_idx(i, j, a, c, nso)]*epsilon_ijab[four_idx(i, j, a, c, nso)])+(1.0-pow(e,-2.0*S_const*epsilon_ijab[four_idx(i, j, a, c, nso)]*epsilon_ijab[four_idx(i, j, a, c, nso)]))/epsilon_ijab[four_idx(i, j, a, c, nso)]/epsilon_ijab[four_idx(i, j, a, c, nso)]);

                            Z_MP2->add(0, d, c, t4);

                        }
                        }
                    }
                }
            }
        }
    }    



Z_temp->copy(Z_MP2);





for ( int times = 0; times < 100; ++times){

    for(int c = 2 * doccpi; c < nso; ++c){
        for(int n = 0; n < 2 *doccpi; ++n){

            double T_1 = 0.0; 
            double T_2 = 0.0; 
            double T_3 = 0.0; 
            double T_4 = 0.0; 
            double T_5 = 0.0; 

            for(int j = 0; j < 2 * doccpi; ++j){
                for(int a = 2 * doccpi; a < nso; a++){
                    for(int b = 2 * doccpi; b < nso; ++b){

                        T_1 += so_ints[four_idx(c, j, a, b, nso)] * amp_t_dsrg[four_idx(n, j, a, b, nso)]*(1.0+pow(e, -S_const*epsilon_ijab[four_idx(n, j, a, b, nso)]*epsilon_ijab[four_idx(n, j, a, b, nso)])); 
                    }
                }
            }


            for(int i = 0; i < 2 * doccpi; ++i){
                for(int j = 0; j < 2 * doccpi; ++j){
                    for(int a = 2 * doccpi; a < nso; ++a){

                        T_2 -= so_ints[four_idx(i, j, a, n, nso)] * amp_t_dsrg[four_idx(i, j, a, c, nso)]*(1.0+pow(e, -S_const*epsilon_ijab[four_idx(i, j, a, c, nso)]*epsilon_ijab[four_idx(i, j, a, c, nso)])); 
                    }
                }
            }


            for(int i = 0; i < 2 * doccpi; ++i){
                for(int j = 0; j < 2 * doccpi; ++j){
                    for(int a = 2 * doccpi; a < nso; ++a){
                        for(int b = 2 * doccpi; b < nso; ++b){



                            T_4 -= (so_ints[four_idx(i, c, i, n, nso)]-so_ints[four_idx(a, c, a, n, nso)])*amp_t_dsrg[four_idx(i, j, a, b, nso)]*amp_t_dsrg[four_idx(i, j, a, b, nso)]*(1.0+pow(e, -S_const*epsilon_ijab[four_idx(i, j, a, b, nso)]*epsilon_ijab[four_idx(i, j, a, b, nso)]))/(1.0-pow(e, -S_const*epsilon_ijab[four_idx(i, j, a, b, nso)]*epsilon_ijab[four_idx(i, j, a, b, nso)]));


                        }
                    }
                }
            }


            for(int i = 0; i < 2 * doccpi; ++i){
                for(int j = 0; j < 2 * doccpi; ++j){
                    for(int a = 2 * doccpi; a < nso; ++a){
                        for(int b = 2 * doccpi; b < nso; ++b){



                            T_5 += 4.0*S_const*(so_ints[four_idx(i, c, i, n, nso)]-so_ints[four_idx(a, c, a, n, nso)])*so_ints[four_idx(i, j, a, b, nso)]*so_ints[four_idx(i, j, a, b, nso)]*pow(e, -2.0*S_const*epsilon_ijab[four_idx(i, j, a, b, nso)]*epsilon_ijab[four_idx(i, j, a, b, nso)]);


                        }
                    }
                }
            }


            for(int p = 0; p < nso; ++p){
                for(int q = 0; q < nso; ++q){

                    if (p != q){
                        T_3 += so_ints[four_idx(p, n, q, c, nso)] * Z_MP2->get(0, q, p);
                    }

                }
            }


            Z_temp->set(0, n, c, (T_1 + T_2 + T_3 + T_4 + T_5) / (epsilon[n] - epsilon[c]));
            Z_temp->set(0, c, n, Z_temp->get(0, n, c));

        }
    }
    Z_MP2->copy(Z_temp);
}


    for(int p = 0; p < nso; ++p){
        for(int q = 0; q < nso; ++q){

            if(p!=q) {D_MP2->set(0,p,q,0.5*Z_MP2->get(0,p,q));}

        }
    }


    D_MP2->print();


    double dipole_MP2 = 0.0;

    for(int p = 0; p < nso; ++p){
        for(int q = 0; q < nso; ++q){

            dipole_MP2 += D_MP2->get(0, p, q) * Dp_mo->get(0, p/2, q/2);

        }
    }


    Emp2 = MP2_Energy_SO(eri_mo, F_MO, nso, doccpi, so_ints, epsilon_ijab );
    Edsrg_pt2 = DSRG_PT2_Energy_SO(eri_mo, F_MO, nso, doccpi, so_ints, epsilon_ijab, S_const);


/****** test ********/


// Dp->print();
// F->print();
// F_uptp->print();


/****** test ********/    



//Output
	std::cout<<"Energy precision(SCF Iter):   "<< std::setprecision(15)<< CVG<< std::endl<< std::endl;
	std::cout<<"Iteration times:              "<< iternum << std::endl;
    std::cout<<"Nuclear repulsion energy:     "<< std::setprecision(15)<< Enuc<< std::endl;
    std::cout<<"Electronic energy:            "<< std::setprecision(15)<< Elec<< std::endl;
    std::cout<<"SCF energy:                   "<< std::setprecision(15)<< Escf<< std::endl;
    std::cout<<"MP2 energy:                   "<< std::setprecision(15)<< Emp2<< std::endl;
    std::cout<<"Total energy(MP2):            "<< std::setprecision(15)<< Escf + Emp2 << std::endl;
    std::cout<<"DSRG-PT2 energy:              "<< std::setprecision(15)<< Edsrg_pt2 << std::endl;
    std::cout<<"Total energy(DSRG-PT2):       "<< std::setprecision(15)<< Escf + Edsrg_pt2 << std::endl;
    std::cout<<"MP2 dipole:                   "<< std::setprecision(15)<< dipole_MP2 << std::endl << std::endl;


    return ref_wfn;
}
}} // End namespaces

