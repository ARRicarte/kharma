/* 
 *  File: b_cd.hpp
 *  
 *  BSD 3-Clause License
 *  
 *  Copyright (c) 2020, AFD Group at UIUC
 *  All rights reserved.
 *  
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions are met:
 *  
 *  1. Redistributions of source code must retain the above copyright notice, this
 *     list of conditions and the following disclaimer.
 *  
 *  2. Redistributions in binary form must reproduce the above copyright notice,
 *     this list of conditions and the following disclaimer in the documentation
 *     and/or other materials provided with the distribution.
 *  
 *  3. Neither the name of the copyright holder nor the names of its
 *     contributors may be used to endorse or promote products derived from
 *     this software without specific prior written permission.
 *  
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 *  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 *  DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 *  FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 *  DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 *  SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 *  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 *  OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#pragma once

#include <memory>

#include <parthenon/parthenon.hpp>

#include "b_functions.hpp"

using namespace parthenon;

/**
 * This physics package implements B field transport with Flux-CT (Toth 2000)
 *
 * This requires only the values at cell centers
 * 
 * This implementation includes conversion from "primitive" to "conserved" B and back
 */
namespace B_CD {
/**
 * Declare fields, initialize (few) parameters
 */
std::shared_ptr<StateDescriptor> Initialize(ParameterInput *pin, Packages_t packages);

/**
 * Get the primitive variables, which in Parthenon's nomenclature are "derived".
 * Also applies floors to the calculated primitives, and fixes up any inversion errors
 *
 * input: Conserved B = sqrt(-gdet) * B^i
 * output: Primitive B = B^i
 */
void UtoP(MeshBlockData<Real> *rc);

/**
 * Add the source term to dUdt, before it is applied to U
 */
TaskStatus AddSource(MeshBlockData<Real> *rc, MeshBlockData<Real> *dudt, const Real& dt);

/**
 * Take a maximum over the divB array, which is updated every step 
 */
Real MaxDivB(MeshBlockData<Real> *rc, IndexDomain domain=IndexDomain::interior);

/**
 * Diagnostics printed/computed after each step
 * Currently nothing, divB is calculated in fluxes.cpp
 */
TaskStatus PostStepDiagnostics(Mesh *pmesh, ParameterInput *pin, const SimTime& tm);

/**
 * Fill fields which are calculated only for output to file
 * Currently nothing, soon the corner-centered divB values
 */
void FillOutput(MeshBlock *pmb, ParameterInput *pin);

/**
 * Turn the primitive B, psi field into the local conserved flux
 */
KOKKOS_INLINE_FUNCTION void prim_to_flux(const GRCoordinates& G, const FourVectors D, const Real B_P[NVEC], Real& psi_p,
                                           const int& k, const int& j, const int& i, const Loci loc, const int dir,
                                           Real B_flux[NVEC], Real *psi_flux)
{
    Real gdet = G.gdet(loc, j, i);
    if (dir == 0) {  // In-place prims to cons
        VLOOP B_flux[v] = B_P[v] * gdet;
        *psi_flux = psi_p * gdet;
    } else { // Flux through a face
        // Dual of Maxwell tensor
        VLOOP B_flux[v] = (D.bcon[v+1] * D.ucon[dir] - D.bcon[dir] * D.ucon[v+1]) * gdet;
        // Psi field update as in Mosta et al (IllinoisGRMHD)
        Real alpha = 1. / sqrt(-G.gcon(Loci::center, j, i, 0, 0));
        *psi_flux = (alpha * B_P[dir - 1] - G.gcon(Loci::center, j, i, 0, dir) * alpha * alpha * psi_p) * gdet;
    }
}

KOKKOS_INLINE_FUNCTION void prim_to_u(const GRCoordinates& G, const ScratchPad2D<Real> &P, const struct varmap& m, const FourVectors D,
                                      const int& j, const int& i, const Loci loc,
                                      ScratchPad2D<Real>& flux)
{
    Real gdet = G.gdet(loc, j, i);
    VLOOP flux(m.Bu + v, i) = P(m.Bp + v, i) * gdet;
    flux(m.psiu, i) = P(m.psip, i) * gdet;
}
KOKKOS_INLINE_FUNCTION void prim_to_flux(const GRCoordinates& G, const ScratchPad2D<Real> &P, const struct varmap& m, const FourVectors D,
                                           const int& j, const int& i, const Loci loc, const int dir,
                                           ScratchPad2D<Real>& flux)
{
    Real gdet = G.gdet(loc, j, i);
    // Dual of Maxwell tensor
    VLOOP flux(m.Bu + v, i) = (v+1 == dir) ? P(m.psip, i) * gdet : (D.bcon[v+1] * D.ucon[dir] - D.bcon[dir] * D.ucon[v+1]) * gdet;
    // Psi field update as in Mosta et al (IllinoisGRMHD)
    Real alpha = 1. / sqrt(-G.gcon(Loci::center, j, i, 0, 0));
    flux(m.psiu, i) = (alpha * P(m.Bp + dir - 1, i) - G.gcon(Loci::center, j, i, 0, dir) * alpha * alpha * P(m.psip, i)) * gdet;
}

}