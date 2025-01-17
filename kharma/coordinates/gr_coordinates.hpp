/* 
 *  File: gr_coordinates.hpp
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

#include "coordinate_embedding.hpp"
#include "kharma_utils.hpp"

// Everywhere else we can just import <parthenon/parthenon.hpp>
// Here we have to be careful of circular dependencies
// General warning to be very careful with imports/namespaces in
// this file and the others in coordinates/: it's imported many
// places in parthenon and expected to be benign
#include <coordinates/uniform_cartesian.hpp>
#include <parameter_input.hpp>

// This import should always be okay, too
#include "Kokkos_Core.hpp"

// Option to ignore coordinates entirely,
// and only use flat-space SR in Cartesian coordinates
#define FAST_CARTESIAN 0
// Don't cache values of the metric, etc, just call into CoordinateEmbedding directly
#define NO_CACHE 0

/**
 * Replacement/extension coordinate class for Parthenon
 * 
 * Parthenon's UniformCartesian coordinate class keeps, for each meshblock,
 * cell locations on a global X,Y,Z grid.
 * We wish to map these "logically-Cartesian" coordinates to volumes of interest,
 * with differential geometry and lots of caching
 */
class GRCoordinates : public UniformCartesian
{
public:
    // Host-side coordinates object pointer
    // Note we keep the actual object in GRCoordinates.  This is a royal pain to implement,
    // but ensures it will get copied device-side by C++14 Lambdas, circumventing *so many* bugs
    CoordinateEmbedding coords;

    // TODO try again to get these from parent always, e.g. with the RegionSize or len()
    int n1, n2, n3;
    // And optionally some caches
#if !FAST_CARTESIAN && !NO_CACHE
    GeomTensor2 gcon_direct, gcov_direct;
    GeomScalar gdet_direct;
    GeomTensor3 conn_direct, gdet_conn_direct;
#endif

    // "Full" constructors which generate new geometry caches
    // these call Kokkos internally so we must ensure they're only called host-side
#pragma hd_warning_disable
    GRCoordinates(const RegionSize &rs, ParameterInput *pin);
#pragma hd_warning_disable
    GRCoordinates(const GRCoordinates &src, int coarsen);

    // Interim & copy constructors so that Parthenon can use us like a UniformCartesian object,
    // that is, host- & device-side indiscriminately
    KOKKOS_FUNCTION GRCoordinates(): UniformCartesian() {};
    KOKKOS_FUNCTION GRCoordinates(const GRCoordinates &src): UniformCartesian(src)
    {
        //std::cerr << "Calling copy constructor size " << src.n1 << " " << src.n2 << std::endl;
        coords = src.coords;
        n1 = src.n1;
        n2 = src.n2;
        n3 = src.n3;
    #if !FAST_CARTESIAN && !NO_CACHE
        gcon_direct = src.gcon_direct;
        gcov_direct = src.gcov_direct;
        gdet_direct = src.gdet_direct;
        conn_direct = src.conn_direct;
        gdet_conn_direct = src.gdet_conn_direct;
    #endif
    };
    KOKKOS_FUNCTION GRCoordinates operator=(const GRCoordinates& src)
    {
        //std::cerr << "Calling assignment operator size " << src.n1 << " " << src.n2 << std::endl;
        UniformCartesian::operator=(src);
        coords = src.coords;
        n1 = src.n1;
        n2 = src.n2;
        n3 = src.n3;
    #if !FAST_CARTESIAN && !NO_CACHE
        gcon_direct = src.gcon_direct;
        gcov_direct = src.gcov_direct;
        gdet_direct = src.gdet_direct;
        conn_direct = src.conn_direct;
        gdet_conn_direct = src.gdet_conn_direct;
    #endif
        return *this;
    };

    // TODO Test these vs going all-in on full-matrix versions and computing on the fly
    KOKKOS_INLINE_FUNCTION Real gcon(const Loci loc, const int& j, const int& i, const int mu, const int nu) const;
    KOKKOS_INLINE_FUNCTION Real gcov(const Loci loc, const int& j, const int& i, const int mu, const int nu) const;
    KOKKOS_INLINE_FUNCTION Real gdet(const Loci loc, const int& j, const int& i) const;
    KOKKOS_INLINE_FUNCTION Real conn(const int& j, const int& i, const int mu, const int nu, const int lam) const;
    KOKKOS_INLINE_FUNCTION Real gdet_conn(const int& j, const int& i, const int mu, const int nu, const int lam) const;

    KOKKOS_INLINE_FUNCTION void gcon(const Loci loc, const int& j, const int& i, Real gcon[GR_DIM][GR_DIM]) const;
    KOKKOS_INLINE_FUNCTION void gcov(const Loci loc, const int& j, const int& i, Real gcov[GR_DIM][GR_DIM]) const;
    KOKKOS_INLINE_FUNCTION void conn(const int& j, const int& i, Real conn[GR_DIM][GR_DIM][GR_DIM]) const;
    KOKKOS_INLINE_FUNCTION void gdet_conn(const int& j, const int& i, Real conn[GR_DIM][GR_DIM][GR_DIM]) const;

    // Coordinates of the GRCoordinates, i.e. "native"
    KOKKOS_INLINE_FUNCTION void coord(const int& k, const int& j, const int& i, const Loci& loc, GReal X[GR_DIM]) const;
    // Coordinates of the embedding system, usually r,th,phi[KS] or x1,x2,x3[Cartesian]
    KOKKOS_INLINE_FUNCTION void coord_embed(const int& k, const int& j, const int& i, const Loci& loc, GReal Xembed[GR_DIM]) const;
    // Coordinates in a specific 

    // Transformations using the cached geometry
    KOKKOS_INLINE_FUNCTION void lower(const Real vcon[GR_DIM], Real vcov[GR_DIM],
                                        const int& k, const int& j, const int& i, const Loci loc) const;
    KOKKOS_INLINE_FUNCTION void raise(const Real vcov[GR_DIM], Real vcon[GR_DIM],
                                        const int& k, const int& j, const int& i, const Loci loc) const;

    // TODO Indexing functions and named slices to make it comfy
};

/**
 * Function to return native coordinates on the GRCoordinates
 */
KOKKOS_INLINE_FUNCTION void GRCoordinates::coord(const int& k, const int& j, const int& i, const Loci& loc, Real X[GR_DIM]) const
{
    X[0] = 0;
    switch(loc)
    {
    case Loci::face1:
        X[1] = x1f(i);
        X[2] = x2v(j);
        X[3] = x3v(k);
        break;
    case Loci::face2:
        X[1] = x1v(i);
        X[2] = x2f(j);
        X[3] = x3v(k);
        break;
    case Loci::face3:
        X[1] = x1v(i);
        X[2] = x2v(j);
        X[3] = x3f(k);
        break;
    case Loci::center:
        X[1] = x1v(i);
        X[2] = x2v(j);
        X[3] = x3v(k);
        break;
    case Loci::corner:
        X[1] = x1f(i);
        X[2] = x2f(j);
        X[3] = x3f(k);
        break;
    }
}

#if FAST_CARTESIAN
KOKKOS_INLINE_FUNCTION void GRCoordinates::coord_embed(const int& k, const int& j, const int& i, const Loci& loc, GReal Xembed[GR_DIM]) const
{
    // Only supports null transform
    coord(k, j, i, loc, Xembed);
}
#else
/**
 * TODO Currently CANNOT be called from device-side code
 */
KOKKOS_INLINE_FUNCTION void GRCoordinates::coord_embed(const int& k, const int& j, const int& i, const Loci& loc, GReal Xembed[GR_DIM]) const
{
    GReal Xnative[GR_DIM];
    coord(k, j, i, loc, Xnative);
    coords.coord_to_embed(Xnative, Xembed);
}
#endif

KOKKOS_INLINE_FUNCTION void GRCoordinates::lower(const Real vcon[GR_DIM], Real vcov[GR_DIM],
                                        const int& k, const int& j, const int& i, const Loci loc) const
{
    gzero(vcov);
    DLOOP2 vcov[mu] += gcov(loc, j, i, mu, nu) * vcon[nu];
}
KOKKOS_INLINE_FUNCTION void GRCoordinates::raise(const Real vcov[GR_DIM], Real vcon[GR_DIM],
                                        const int& k, const int& j, const int& i, const Loci loc) const
{
    gzero(vcon);
    DLOOP2 vcon[mu] += gcon(loc, j, i, mu, nu) * vcov[nu];
}

// Three different implementations of the metric functions:
// FAST_CARTESIAN: Minkowski space constant values
// NO_CACHE: Re-calculate from coordinates object on every access
// NORMAL: Cache each zone center and return cached value thereafter
#if FAST_CARTESIAN
KOKKOS_INLINE_FUNCTION Real GRCoordinates::gcon(const Loci loc, const int& j, const int& i, const int mu, const int nu) const
    {return -2*(mu == 0 && nu == 0) + (mu == nu);}
KOKKOS_INLINE_FUNCTION Real GRCoordinates::gcov(const Loci loc, const int& j, const int& i, const int mu, const int nu) const
    {return -2*(mu == 0 && nu == 0) + (mu == nu);}
KOKKOS_INLINE_FUNCTION Real GRCoordinates::gdet(const Loci loc, const int& j, const int& i) const
    {return 1;}
KOKKOS_INLINE_FUNCTION Real GRCoordinates::conn(const int& j, const int& i, const int mu, const int nu, const int lam) const
    {return 0;}
KOKKOS_INLINE_FUNCTION Real GRCoordinates::gdet_conn(const int& j, const int& i, const int mu, const int nu, const int lam) const
    {return 0;}

KOKKOS_INLINE_FUNCTION void GRCoordinates::gcon(const Loci loc, const int& j, const int& i, Real gcon[GR_DIM][GR_DIM]) const
    {DLOOP2 gcon[mu][nu] = -2*(mu == 0 && nu == 0) + (mu == nu);}
KOKKOS_INLINE_FUNCTION void GRCoordinates::gcov(const Loci loc, const int& j, const int& i, Real gcov[GR_DIM][GR_DIM]) const
    {DLOOP2 gcov[mu][nu] = -2*(mu == 0 && nu == 0) + (mu == nu);}
KOKKOS_INLINE_FUNCTION void GRCoordinates::conn(const int& j, const int& i, Real conn[GR_DIM][GR_DIM][GR_DIM]) const
    {DLOOP3 conn[mu][nu][lam] = 0;}
KOKKOS_INLINE_FUNCTION void GRCoordinates::gdet_conn(const int& j, const int& i, Real gdet_conn[GR_DIM][GR_DIM][GR_DIM]) const
    {DLOOP3 gdet_conn[mu][nu][lam] = 0;}
#elif NO_CACHE
// TODO these are currently VERY SLOW.  Rework them to generate just the desired component. (TODO gdet?...)
// Except conn.  We never need conn fast.
KOKKOS_INLINE_FUNCTION Real GRCoordinates::gcon(const Loci loc, const int& j, const int& i, const int mu, const int nu) const
{
    GReal X[GR_DIM], gcon[GR_DIM][GR_DIM];
    coord(0, j, i, loc, X);
    coords.gcon_native(X, gcon);
    return gcon[mu][nu];
}
KOKKOS_INLINE_FUNCTION Real GRCoordinates::gcov(const Loci loc, const int& j, const int& i, const int mu, const int nu) const
{
    GReal X[GR_DIM], gcov[GR_DIM][GR_DIM];
    coord(0, j, i, loc, X);
    coords.gcov_native(X, gcov);
    return gcov[mu][nu];
}
KOKKOS_INLINE_FUNCTION Real GRCoordinates::gdet(const Loci loc, const int& j, const int& i) const
{
    GReal X[GR_DIM], gcon[GR_DIM][GR_DIM];
    coord(0, j, i, loc, X);
    return coords.gcon_native(X, gcon);
}
KOKKOS_INLINE_FUNCTION Real GRCoordinates::conn(const int& j, const int& i, const int mu, const int nu, const int lam) const
{
    GReal X[GR_DIM], conn[GR_DIM][GR_DIM][GR_DIM];
    coord(0, j, i, Loci::center, X);
    coords.conn_native(X, conn);
    return conn[mu][nu][lam];
}

KOKKOS_INLINE_FUNCTION void GRCoordinates::gcon(const Loci loc, const int& j, const int& i, Real gcon[GR_DIM][GR_DIM]) const
{
    GReal X[GR_DIM];
    coord(0, j, i, loc, X);
    coords.gcon_native(X, gcon);
}
KOKKOS_INLINE_FUNCTION void GRCoordinates::gcov(const Loci loc, const int& j, const int& i, Real gcov[GR_DIM][GR_DIM]) const
{
    GReal X[GR_DIM];
    coord(0, j, i, loc, X);
    coords.gcov_native(X, gcov);
}
KOKKOS_INLINE_FUNCTION void GRCoordinates::conn(const int& j, const int& i, Real conn[GR_DIM][GR_DIM][GR_DIM]) const
{
    GReal X[GR_DIM];
    coord(0, j, i, Loci::center, X);
    coords.conn_native(X, conn);
}
#else
KOKKOS_INLINE_FUNCTION Real GRCoordinates::gcon(const Loci loc, const int& j, const int& i, const int mu, const int nu) const
    {return gcon_direct(loc, j, i, mu, nu);}
KOKKOS_INLINE_FUNCTION Real GRCoordinates::gcov(const Loci loc, const int& j, const int& i, const int mu, const int nu) const
    {return gcov_direct(loc, j, i, mu, nu);}
KOKKOS_INLINE_FUNCTION Real GRCoordinates::gdet(const Loci loc, const int& j, const int& i) const
    {return gdet_direct(loc, j, i);}
KOKKOS_INLINE_FUNCTION Real GRCoordinates::conn(const int& j, const int& i, const int mu, const int nu, const int lam) const
    {return conn_direct(j, i, mu, nu, lam);}
KOKKOS_INLINE_FUNCTION Real GRCoordinates::gdet_conn(const int& j, const int& i, const int mu, const int nu, const int lam) const
    {return gdet_conn_direct(j, i, mu, nu, lam);}

KOKKOS_INLINE_FUNCTION void GRCoordinates::gcon(const Loci loc, const int& j, const int& i, Real gcon[GR_DIM][GR_DIM]) const
    {DLOOP2 gcon[mu][nu] = gcon_direct(loc, j, i, mu, nu);}
KOKKOS_INLINE_FUNCTION void GRCoordinates::gcov(const Loci loc, const int& j, const int& i, Real gcov[GR_DIM][GR_DIM]) const
    {DLOOP2 gcov[mu][nu] = gcov_direct(loc, j, i, mu, nu);}
KOKKOS_INLINE_FUNCTION void GRCoordinates::conn(const int& j, const int& i, Real conn[GR_DIM][GR_DIM][GR_DIM]) const
    {DLOOP3 conn[mu][nu][lam] = conn_direct(j, i, mu, nu, lam);}
KOKKOS_INLINE_FUNCTION void GRCoordinates::gdet_conn(const int& j, const int& i, Real gdet_conn[GR_DIM][GR_DIM][GR_DIM]) const
    {DLOOP3 gdet_conn[mu][nu][lam] = gdet_conn_direct(j, i, mu, nu, lam);}
#endif
