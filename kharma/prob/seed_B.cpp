// Seed a torus of some type with a magnetic field according to its density

#include "decs.hpp"

#include "phys.hpp"

// Internal representation of the field initialization preference for quick switch
// Mostly for fun; the loop for vector potential is 2D
enum BSeedType{sane, ryan, r3s3, gaussian};

/**
 * Seed an axisymmetric initialization with magnetic field proportional to fluid density,
 * or density and radius, to create a SANE or MAD flow
 * Note this function expects a normalized P for which rho_max==1
 *
 * @param rin is the interior radius of the torus
 * @param min_rho_q is the minimum density at which there will be magnetic vector potential
 * @param b_field_type is one of "sane" "ryan" "r3s3" or "gaussian", described below (TODO test or remove opts)
 */
void SeedBField(MeshBlock *pmb, Grid G, GridVars P,
                Real rin, Real min_rho_q, std::string b_field_type)
{
    // Translate to an enum so we can avoid string comp inside,
    // as well as for good errors, many->one maps, etc.
    BSeedType b_field_flag = BSeedType::sane;
    if (b_field_type == "none") {
        return;
    } else if (b_field_type == "sane") {
        b_field_flag = BSeedType::sane;
    } else if (b_field_type == "ryan") {
        b_field_flag = BSeedType::ryan;
    } else if (b_field_type == "r3s3") {
        b_field_flag = BSeedType::r3s3;
    } else if (b_field_type == "gaussian") {
        b_field_flag = BSeedType::gaussian;
    } else {
        throw std::invalid_argument("Magnetic field seed type not supported: " + b_field_type);
    }

    // Find the magnetic vector potential.  In X3 symmetry only A_phi is non-zero, so we keep track of that.
    ParArrayND<Real> A("A", pmb->ncells2, pmb->ncells1);
    // TODO figure out how much of this needs to be double instead of Real.  Any?
    pmb->par_for("B_field_A", 1, pmb->ncells2-1, 1, pmb->ncells1-1,
        KOKKOS_LAMBDA_2D {
            GReal Xembed[NDIM];
            G.coord_embed(0, j, i, Loci::center, Xembed);
            GReal r = Xembed[1], th = Xembed[2];

            // Find rho (later u?) at corners by averaging from adjacent centers
            // TODO is this a thing Parthenon can do?
            Real rho_av = 0.25 * (P(prims::rho, 0, j, i) + P(prims::rho, 0, j, i - 1) +
                                  P(prims::rho, 0, j - 1, i) + P(prims::rho, 0, j - 1, i - 1));

            Real q;
            switch (b_field_flag)
            {
            case BSeedType::sane:
                q = rho_av - min_rho_q;
                break;
            case BSeedType::ryan:
                // BR's smoothed poloidal in-torus
                q = pow(sin(th), 3) * pow(r / rin, 3) * exp(-r / 400) * rho_av - min_rho_q;
                break;
            case BSeedType::r3s3:
                // Just the r^3 sin^3 th term, proposed EHT standard MAD
                // TODO split r3 here and r3s3
                q = pow(r / rin, 3) * rho_av - min_rho_q;
                break;
            case BSeedType::gaussian:
                // Pure vertical threaded field of gaussian strength with FWHM 2*rin (i.e. HM@rin)
                // centered at BH center
                Real x = (r / rin) * sin(th);
                Real sigma = 2 / sqrt(2 * log(2));
                Real u = x / fabs(sigma);
                q = (1 / (sqrt(2 * M_PI) * fabs(sigma))) * exp(-u * u / 2);
            }

            A(j, i) = max(q, 0.);
        }
    );

    // Calculate B-field
    pmb->par_for("B_field_B", 0, pmb->ncells3-2, 0, pmb->ncells2-2, 0, pmb->ncells1-2,
        KOKKOS_LAMBDA_3D {
            GReal X[NDIM], Xembed[NDIM];
            G.coord(k, j, i, Loci::center, X);
            G.coord_embed(k, j, i, Loci::center, Xembed);
            GReal r = Xembed[1], th = Xembed[2];

            // Take a flux-ct step from the corner potentials
            P(prims::B1, k, j, i) = -(A(j, i) - A(j + 1, i) + A(j, i + 1) - A(j + 1, i + 1)) /
                                (2. * pmb->pcoord->dx2v(j) * G.gdet(Loci::center, j, i));
            P(prims::B2, k, j, i) = (A(j, i) + A(j + 1, i) - A(j, i + 1) - A(j + 1, i + 1)) /
                                (2. * pmb->pcoord->dx1v(i) * G.gdet(Loci::center, j, i));
            P(prims::B3, k, j, i) = 0.;
        }
    );
}

/**
 * Get the minimum beta on the domain
 */
Real GetLocalBetaMin(MeshBlock *pmb)
{
    Container<Real> rc = pmb->real_containers.Get();
    auto is = pmb->is, js = pmb->js, ks = pmb->ks;
    auto ie = pmb->ie, je = pmb->je, ke = pmb->ke;

    // TODO *sigh*
    Grid G(pmb);
    Real gamma = pmb->packages["GRMHD"]->Param<Real>("gamma");
    EOS* eos = new GammaLaw(gamma);
    auto P = rc.Get("c.c.bulk.prims").data;

    Real beta_min;
    Kokkos::Min<Real> min_reducer(beta_min);
    Kokkos::parallel_reduce("B_field_betamin",
        Kokkos::MDRangePolicy<Kokkos::Rank<3>>({ks, js, is}, {ke+1, je+1, ie+1}),
        KOKKOS_LAMBDA_3D_REDUCE {
            FourVectors Dtmp;
            get_state(G, P, k, j, i, Loci::center, Dtmp);
            double bsq_ij = bsq_calc(Dtmp);

            Real rho = P(prims::rho, k, j, i);
            Real u = P(prims::u, k, j, i);
            Real beta_ij = (eos->p(rho, u))/(0.5*(bsq_ij + TINY_NUMBER));

            if(beta_ij < local_result) local_result = beta_ij;
        }
    , min_reducer);
    return beta_min;
}

/**
 * Normalize the magnetic field
 * 
 * LOCKSTEP: this function expects and should preserve P<->U
 */
void NormalizeBField(MeshBlock *pmb, Real factor)
{
    Container<Real> rc = pmb->real_containers.Get();
    // TODO *sigh*
    Grid G(pmb);
    Real gamma = pmb->packages["GRMHD"]->Param<Real>("gamma");
    EOS* eos = new GammaLaw(gamma);
    auto P = rc.Get("c.c.bulk.prims").data;
    auto U = rc.Get("c.c.bulk.cons").data;

    pmb->par_for("B_field_normalize", 0, pmb->ncells3-1, 0, pmb->ncells2-1, 0, pmb->ncells1-1,
        KOKKOS_LAMBDA_3D {
            P(prims::B1, k, j, i) /= factor;
            P(prims::B2, k, j, i) /= factor;
            P(prims::B3, k, j, i) /= factor;

            FourVectors Dtmp;
            get_state(G, P, k, j, i, Loci::center, Dtmp);
            prim_to_flux(G, P, Dtmp, eos, k, j, i, Loci::center, 0, U);

        }
    );
}