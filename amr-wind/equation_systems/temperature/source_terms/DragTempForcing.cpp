
#include "amr-wind/equation_systems/temperature/source_terms/DragTempForcing.H"
#include "amr-wind/utilities/IOManager.H"

#include "AMReX_ParmParse.H"
#include "AMReX_Gpu.H"
#include "AMReX_Random.H"

namespace amr_wind::pde::temperature {

DragTempForcing::DragTempForcing(const CFDSim& sim)
    : m_sim(sim)
    , m_mesh(sim.mesh())
    , m_velocity(sim.repo().get_field("velocity"))
    , m_temperature(sim.repo().get_field("temperature"))
    , m_transport(sim.transport_model())
{
    amrex::ParmParse pp("DragTempForcing");
    pp.query("drag_coefficient", m_drag_coefficient);
    if (pp.contains("reference_temperature")) {
        amrex::Abort(
            "DragTempForcing.reference_temperature has been deprecated. Please "
            "replace with transport.reference_temperature.");
    }
}

DragTempForcing::~DragTempForcing() = default;

void DragTempForcing::operator()(
    const int lev,
    const amrex::MFIter& mfi,
    const amrex::Box& bx,
    const FieldState fstate,
    const amrex::Array4<amrex::Real>& src_term) const
{
    const auto& vel =
        m_velocity.state(field_impl::dof_state(fstate))(lev).const_array(mfi);
    const auto& temperature =
        m_temperature.state(field_impl::dof_state(fstate))(lev).const_array(
            mfi);
    const bool is_terrain =
        this->m_sim.repo().int_field_exists("terrain_blank");
    if (!is_terrain) {
        amrex::Abort("Need terrain blanking variable to use this source term");
    }
    auto* const m_terrain_blank =
        &this->m_sim.repo().get_int_field("terrain_blank");
    const auto& blank = (*m_terrain_blank)(lev).const_array(mfi);
    const auto& geom = m_mesh.Geom(lev);
    const auto& dx = geom.CellSizeArray();
    const amrex::Real drag_coefficient = m_drag_coefficient / dx[2];
    amrex::FArrayBox ref_theta_fab(bx, 1, amrex::The_Async_Arena());
    amrex::Array4<amrex::Real> const& ref_theta_arr = ref_theta_fab.array();
    m_transport.ref_theta_impl(lev, mfi, bx, ref_theta_arr);
    const auto tiny = std::numeric_limits<amrex::Real>::epsilon();
    const amrex::Real cd_max = 10.0;
    amrex::ParallelFor(bx, [=] AMREX_GPU_DEVICE(int i, int j, int k) noexcept {
        const amrex::Real ux1 = vel(i, j, k, 0);
        const amrex::Real uy1 = vel(i, j, k, 1);
        const amrex::Real uz1 = vel(i, j, k, 2);
        const amrex::Real m = std::sqrt(ux1 * ux1 + uy1 * uy1 + uz1 * uz1);
        const amrex::Real Cd =
            std::min(drag_coefficient / (m + tiny), cd_max / dx[2]);
        const amrex::Real T0 = ref_theta_arr(i, j, k);
        src_term(i, j, k, 0) -=
            (Cd * (temperature(i, j, k, 0) - T0) * blank(i, j, k, 0));
    });
}

} // namespace amr_wind::pde::temperature
