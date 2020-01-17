#include <AMReX_Vector.H>
#include <AMReX_BC_TYPES.H>
#include <AMReX_ParmParse.H>

#include <incflo.H>

using namespace amrex;

void incflo::init_bcs ()
{
    auto f = [this] (std::string const& bcid, Orientation ori)
    {
        m_bc_density[ori] = 1.0;
        m_bc_velocity[ori][0] = 0.0; // default
        m_bc_velocity[ori][1] = 0.0;
        m_bc_velocity[ori][2] = 0.0;
        m_bc_tracer[ori].resize(ntrac,0.0);
        if (ori.isLow()) {
            m_bc_location[ori] = geom[0].ProbLo(ori.coordDir());
        } else {
            m_bc_location[ori] = geom[0].ProbHi(ori.coordDir());
        }

        ParmParse pp(bcid);
        std::string bc_type_in = "null";
        pp.query("type", bc_type_in);
        std::string bc_type = amrex::toLower(bc_type_in);

        if (bc_type == "pressure_inflow" or bc_type == "pi")
        {
            amrex::Print() << bcid << " set to pressure inflow.\n";

            m_bc_type[ori] = BC::pressure_inflow;

            pp.get("pressure", m_bc_pressure[ori]);
        }
        else if (bc_type == "pressure_outflow" or bc_type == "po")
        {
            amrex::Print() << bcid << " set to pressure outflow.\n";

            m_bc_type[ori] = BC::pressure_inflow;

            pp.get("pressure", m_bc_pressure[ori]);
        }
        else if (bc_type == "mass_inflow" or bc_type == "mi")
        {
            amrex::Print() << bcid << " set to mass inflow.\n";

            m_bc_type[ori] = BC::mass_inflow;

            pp.query("pressure", m_bc_pressure[ori]);

            std::vector<Real> v;
            pp.getarr("velocity", v, 0, AMREX_SPACEDIM);
            m_bc_velocity[ori] = {v[0],v[1],v[2]};

            pp.query("density", m_bc_density[ori]);

            pp.queryarr("tracer", m_bc_tracer[ori], 0, ntrac);
        }
        else if (bc_type == "no_slip_wall" or bc_type == "nsw")
        {
            amrex::Print() << bcid <<" set to no-slip wall.\n";

            m_bc_type[ori] = BC::no_slip_wall;

            std::vector<Real> v;
            if (pp.queryarr("velocity", v, 0, AMREX_SPACEDIM)) {
                m_bc_velocity[ori] = {v[0],v[1],v[2]};
            }

            pp.query("density", m_bc_density[ori]);

            pp.query("location", m_bc_location[ori]);
        }
        else
        {
            m_bc_type[ori] = BC::undefined;
        }

        if (geom[0].isPeriodic(ori.coordDir())) {
            if (m_bc_type[ori] == BC::undefined) {
                m_bc_type[ori] = BC::periodic;
            } else {
                amrex::Abort("Wrong BC type for periodic boundary");
            }
        }
    };

    f("xlo", Orientation(Direction::x,Orientation::low));
    f("xhi", Orientation(Direction::x,Orientation::high));
    f("ylo", Orientation(Direction::y,Orientation::low));
    f("yhi", Orientation(Direction::y,Orientation::high));
    f("zlo", Orientation(Direction::z,Orientation::low));
    f("zhi", Orientation(Direction::z,Orientation::high));

    if (ntrac > 0) {
        Vector<Real> h_data(ntrac*AMREX_SPACEDIM*2);
        Real* hp = h_data.data();
        for (auto const& v : m_bc_tracer) {
            for (auto x : v) {
                *(hp++) = x;
            }
        }

        m_bc_tracer_raii.resize(ntrac*AMREX_SPACEDIM*2);
        Real* p = m_bc_tracer_raii.data();
#ifdef AMREX_USE_GPU
        Gpu::htod_memcpy
#else
        std::memcpy
#endif
            (p, h_data.data(), sizeof(Real)*h_data.size());

        for (int i = 0; i < AMREX_SPACEDIM*2; ++i) {
            m_bc_tracer_d[i] = p;
            p += ntrac;
        }
    }

    {
        m_bcrec_velocity.resize(AMREX_SPACEDIM);
        for (OrientationIter oit; oit; ++oit) {
            Orientation ori = oit();
            int dir = ori.coordDir();
            Orientation::Side side = ori.faceDir();
            auto const bct = m_bc_type[ori];
            if (bct == BC::pressure_inflow or
                bct == BC::pressure_outflow)
            {
                if (side == Orientation::low) {
                    m_bcrec_velocity[0].setLo(dir, BCType::foextrap);
                    m_bcrec_velocity[1].setLo(dir, BCType::foextrap);
                    m_bcrec_velocity[2].setLo(dir, BCType::foextrap);
                } else {
                    m_bcrec_velocity[0].setHi(dir, BCType::foextrap);
                    m_bcrec_velocity[1].setHi(dir, BCType::foextrap);
                    m_bcrec_velocity[2].setHi(dir, BCType::foextrap);
                }
            }
            else if (bct == BC::mass_inflow or bct == BC::no_slip_wall)
            {
                if (side == Orientation::low) {
                    m_bcrec_velocity[0].setLo(dir, BCType::ext_dir);
                    m_bcrec_velocity[1].setLo(dir, BCType::ext_dir);
                    m_bcrec_velocity[2].setLo(dir, BCType::ext_dir);
                } else {
                    m_bcrec_velocity[0].setHi(dir, BCType::ext_dir);
                    m_bcrec_velocity[1].setHi(dir, BCType::ext_dir);
                    m_bcrec_velocity[2].setHi(dir, BCType::ext_dir);
                }
            }
            else if (bct == BC::periodic)
            {
                if (side == Orientation::low) {
                    m_bcrec_velocity[0].setLo(dir, BCType::int_dir);
                    m_bcrec_velocity[1].setLo(dir, BCType::int_dir);
                    m_bcrec_velocity[2].setLo(dir, BCType::int_dir);
                } else {
                    m_bcrec_velocity[0].setHi(dir, BCType::int_dir);
                    m_bcrec_velocity[1].setHi(dir, BCType::int_dir);
                    m_bcrec_velocity[2].setHi(dir, BCType::int_dir);
                }
            }
        }
        m_bcrec_velocity_d.resize(AMREX_SPACEDIM);
#ifdef AMREX_USE_GPU
        Gpu::htod_memcpy
#else
        std::memcpy
#endif
            (m_bcrec_velocity_d.data(), m_bcrec_velocity.data(), sizeof(BCRec)*AMREX_SPACEDIM);
    }

    {
        m_bcrec_density.resize(1);
        for (OrientationIter oit; oit; ++oit) {
            Orientation ori = oit();
            int dir = ori.coordDir();
            Orientation::Side side = ori.faceDir();
            auto const bct = m_bc_type[ori];
            if (bct == BC::pressure_inflow or
                bct == BC::pressure_outflow or
                bct == BC::no_slip_wall)
            {
                if (side == Orientation::low) {
                    m_bcrec_density[0].setLo(dir, BCType::foextrap);
                } else {
                    m_bcrec_density[0].setHi(dir, BCType::foextrap);
                }
            }
            else if (bct == BC::mass_inflow)
            {
                if (side == Orientation::low) {
                    m_bcrec_density[0].setLo(dir, BCType::ext_dir);
                } else {
                    m_bcrec_density[0].setHi(dir, BCType::ext_dir);
                }
            }
            else if (bct == BC::periodic)
            {
                if (side == Orientation::low) {
                    m_bcrec_density[0].setLo(dir, BCType::int_dir);
                } else {
                    m_bcrec_density[0].setHi(dir, BCType::int_dir);
                }
            }
        }
        m_bcrec_density_d.resize(1);
#ifdef AMREX_USE_GPU
        Gpu::htod_memcpy
#else
        std::memcpy
#endif
            (m_bcrec_density_d.data(), m_bcrec_density.data(), sizeof(BCRec));
    }

    if (ntrac > 0)
    {
        m_bcrec_tracer.resize(ntrac);
        for (OrientationIter oit; oit; ++oit) {
            Orientation ori = oit();
            int dir = ori.coordDir();
            Orientation::Side side = ori.faceDir();
            auto const bct = m_bc_type[ori];
            if (bct == BC::pressure_inflow or
                bct == BC::pressure_outflow or
                bct == BC::no_slip_wall)
            {
                if (side == Orientation::low) {
                    for (auto& b : m_bcrec_tracer) b.setLo(dir, BCType::foextrap);
                } else {
                    for (auto& b : m_bcrec_tracer) b.setHi(dir, BCType::foextrap);
                }
            }
            else if (bct == BC::mass_inflow)
            {
                if (side == Orientation::low) {
                    for (auto& b : m_bcrec_tracer) b.setLo(dir, BCType::ext_dir);
                } else {
                    for (auto& b : m_bcrec_tracer) b.setHi(dir, BCType::ext_dir);
                }
            }
            else if (bct == BC::periodic)
            {
                if (side == Orientation::low) {
                    for (auto& b : m_bcrec_tracer) b.setLo(dir, BCType::int_dir);
                } else {
                    for (auto& b : m_bcrec_tracer) b.setHi(dir, BCType::int_dir);
                }
            }
        }
        m_bcrec_tracer_d.resize(ntrac);
#ifdef AMREX_USE_GPU
        Gpu::htod_memcpy
#else
        std::memcpy
#endif
            (m_bcrec_tracer_d.data(), m_bcrec_tracer.data(), sizeof(BCRec)*ntrac);
    }
}

