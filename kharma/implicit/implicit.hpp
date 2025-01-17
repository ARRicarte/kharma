/* 
 *  File: implicit.hpp
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

#include "decs.hpp"

#include "emhd_sources.hpp"
#include "emhd.hpp"
#include "flux_functions.hpp"
#include "types.hpp"
#include "grmhd_functions.hpp"

#include <parthenon/parthenon.hpp>

// This class calls EMHD stuff a bunch,
// since that's the only package with specific
// implicit solver stuff
using namespace EMHD;

// Version of PLOOP for just implicit ("fluid") variables
#define FLOOP for(int ip=0; ip < nfvar; ++ip)

namespace Implicit
{

/**
 * Initialization.  Set parameters.
 */
std::shared_ptr<StateDescriptor> Initialize(ParameterInput *pin);

/**
 * @brief take the per-zone implicit portion of a semi-implicit scheme
 * 
 * @param mdi the fluid state at the beginning of the step
 * @param md0 the initial fluid state for this substep
 * @param dudt the negative flux divergence plus explicit source terms
 * @param md_solver should contain initial guess on call, contains result on return
 * @param dt the timestep (current substep)
 */
TaskStatus Step(MeshData<Real> *mdi, MeshData<Real> *md0, MeshData<Real> *dudt,
                MeshData<Real> *mc_solver, const Real& dt);

/**
 * Calculate the residual generated by the trial primitives P_test
 * 
 * "Global" here are read-only input arrays addressed var(ip, k, j, i)
 * "Local" here is anything sliced (usually Scratch) addressable var(ip)
 */
template<typename Local>
KOKKOS_INLINE_FUNCTION void calc_residual(const GRCoordinates& G, const Local& P_test,
                                          const Local& Pi, const Local& Ui, const Local& Ps,
                                          const Local& dudt_explicit, const Local& dUi, const Local& tmp, 
                                          const VarMap& m_p, const VarMap& m_u, const EMHD_parameters& emhd_params,
                                          const int& nfvar, const int& j, const int& i,
                                          const Real& gam, const double& dt,
                                          Local& residual)
{
    // These lines calculate res = (U_test - Ui)/dt - dudt_explicit - 0.5*(dU_new(ip) + dUi(ip)) - dU_time(ip) )
    // Start with conserved vars corresponding to test P, U_test
    // Note this uses the Flux:: call, it needs *all* conserved vars!
    Flux::p_to_u(G, P_test, m_p, emhd_params, gam, j, i, tmp, m_u); // U_test
    // (U_test - Ui)/dt - dudt_explicit ...
    FLOOP residual(ip) = (tmp(ip) - Ui(ip)) / dt - dudt_explicit(ip);
    // if (i == 11 && j == 11) {
    //     GReal X[GR_DIM];
    //     G.coord(0, j, i, Loci::center, X);
    //     printf("X: "); DLOOP1 printf("%g ", X[mu]); printf("\n");
    //     printf("U_test: "); PLOOP printf("%g ", tmp(ip)); printf("\n");
    //     printf("Ui:\t"); PLOOP printf("%g ", Ui(ip)); printf("\n");
    //     printf("Explicit sources: "); PLOOP printf("%g ", dudt_explicit(ip)); printf("\n");
    // }

    if (m_p.Q >= 0) {
        // Compute new implicit source terms and time derivative source terms
        Real dUq, dUdP; // Don't need full array for these
        EMHD::implicit_sources(G, P_test, m_p, gam, j, i, emhd_params, dUq, dUdP); // dU_new
        // ... - 0.5*(dU_new(ip) + dUi(ip)) ...
        residual(m_u.Q) -= 0.5*(dUq + dUi(m_u.Q));
        residual(m_u.DP) -= 0.5*(dUdP + dUi(m_u.DP));
        // if (i == 11 && j == 11) {
        //     printf("Implicit sources: "); printf("%g %g", dUq - dUi(m_u.Q), dUdP - dUi(m_u.DP)); printf("\n");
        // }
        EMHD::time_derivative_sources(G, P_test, Pi, Ps, m_p, emhd_params, gam, dt, j, i, dUq, dUdP); // dU_time
        // ... - dU_time(ip)
        residual(m_u.Q) -= dUq;
        residual(m_u.DP) -= dUdP;
        // if (i == 11 && j == 11) {
        //     printf("Time derivative sources: "); printf("%g %g", dUq, dUdP); printf("\n");
        // }
    }
}

/**
 * Evaluate the jacobian for the implicit iteration, in one zone
 * 
 * Local is anything addressable by (0:nvar-1), Local2 is the same for 2D (0:nvar-1, 0:nvar-1)
 * Usually these are Kokkos subviews
 */
template<typename Local, typename Local2>
KOKKOS_INLINE_FUNCTION void calc_jacobian(const GRCoordinates& G, const Local& P,
                                          const Local& Pi, const Local& Ui, const Local& Ps,
                                          const Local& dudt_explicit, const Local& dUi,
                                          Local& tmp1, Local& tmp2, Local& tmp3,
                                          const VarMap& m_p, const VarMap& m_u, const EMHD_parameters& emhd_params,
                                          const int& nvar, const int& nfvar, const int& j, const int& i,
                                          const Real& jac_delta, const Real& gam, const double& dt,
                                          Local2& jacobian, Local& residual)
{
    // Calculate residual of P
    calc_residual(G, P, Pi, Ui, Ps, dudt_explicit, dUi, tmp3, m_p, m_u, emhd_params, nfvar, j, i, gam, dt, residual);

    // Use one scratchpad as the incremented prims P_delta,
    // one as the new residual residual_delta
    auto& P_delta = tmp1;
    auto& residual_delta = tmp2;
    // set P_delta to P to begin with
    PLOOP P_delta(ip) = P(ip);

    // Numerically evaluate the Jacobian
    for (int col = 0; col < nfvar; col++) {
        // Compute P_delta, differently depending on whether the prims are small compared to eps
        if (abs(P(col)) < (0.5 * jac_delta)) {
            P_delta(col) = P(col) + jac_delta;
        } else {
            P_delta(col) = (1 + jac_delta) * P(col);
        }

        // Compute the residual for P_delta, residual_delta
        calc_residual(G, P_delta, Pi, Ui, Ps, dudt_explicit, dUi, tmp3, m_p, m_u, emhd_params, nfvar, j, i, gam, dt, residual_delta);

        // Compute forward derivatives of each residual vs the primitive col
        for (int row = 0; row < nfvar; row++) {
            jacobian(row, col) = (residual_delta(row) - residual(row)) / (P_delta(col) - P(col) + SMALL);
        }

        // Reset P_delta in this col
        P_delta(col) = P(col);

    }
}   

} // namespace Implicit
