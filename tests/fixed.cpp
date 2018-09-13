//--------------------------------------------------------------
// example of encoding / decoding higher dimensional data w/ fixed number of control points and a
// single block in a split model w/ one model containing geometry and other model science variables
//
// Tom Peterka
// Argonne National Laboratory
// tpeterka@mcs.anl.gov
//--------------------------------------------------------------

#include <mfa/mfa.hpp>

#include <vector>
#include <iostream>
#include <cmath>
#include <string>

#include <diy/master.hpp>
#include <diy/reduce-operations.hpp>
#include <diy/decomposition.hpp>
#include <diy/assigner.hpp>
#include <diy/io/block.hpp>

#include "block.hpp"
#include "opts.h"

using namespace std;

typedef  diy::RegularDecomposer<Bounds> Decomposer;

int main(int argc, char** argv)
{
    // initialize MPI
    diy::mpi::environment  env(argc, argv);     // equivalent of MPI_Init(argc, argv)/MPI_Finalize()
    diy::mpi::communicator world;               // equivalent of MPI_COMM_WORLD

    int nblocks     = 1;                        // number of local blocks
    int tot_blocks  = nblocks * world.size();   // number of global blocks
    int mem_blocks  = -1;                       // everything in core for now
    int num_threads = 1;                        // needed in order to do timing

    // default command line arguments
    int    pt_dim       = 3;                    // dimension of input points
    int    dom_dim      = 2;                    // dimension of domain (<= pt_dim)
    int    geom_degree  = 1;                    // degree for geometry (same for all dims)
    int    vars_degree  = 4;                    // degree for science variables (same for all dims)
    int    ndomp        = 100;                  // input number of domain points (same for all dims)
    int    geom_nctrl   = -1;                   // input number of control points for geometry (same for all dims)
    int    vars_nctrl   = 11;                   // input number of control points for all science variables (same for all dims)
    string input        = "sine";               // input dataset
    bool   weighted     = true;                 // solve for and use weights
    real_t rot          = 0.0;                  // rotation angle in degrees
    real_t twist        = 0.0;                  // twist (waviness) of domain (0.0-1.0)

    // get command line arguments
    opts::Options ops(argc, argv);
    ops >> opts::Option('d', "pt_dim",      pt_dim,     " dimension of points");
    ops >> opts::Option('m', "dom_dim",     dom_dim,    " dimension of domain");
    ops >> opts::Option('p', "geom_degree", geom_degree," degree in each dimension of geometry");
    ops >> opts::Option('q', "vars_degree", vars_degree," degree in each dimension of science variables");
    ops >> opts::Option('n', "ndomp",       ndomp,      " number of input points in each dimension of domain");
    ops >> opts::Option('g', "geom_nctrl",  geom_nctrl, " number of control points in each dimension of geometry");
    ops >> opts::Option('v', "vars_nctrl",  vars_nctrl, " number of control points in each dimension of all science variables");
    ops >> opts::Option('i', "input",       input,      " input dataset");
    ops >> opts::Option('w', "weights",     weighted,   " solve for and use weights");
    ops >> opts::Option('r', "rotate",      rot,        " rotation angle of domain in degrees");
    ops >> opts::Option('t', "twist",       twist,      " twist (waviness) of domain (0.0-1.0)");

    if (ops >> opts::Present('h', "help", " show help"))
    {
        if (world.rank() == 0)
            std::cout << ops;
        return 1;
    }

    // minimal number of geometry control points if not specified
    if (geom_nctrl == -1)
        geom_nctrl = geom_degree + 1;

    // echo args
    fprintf(stderr, "\n--------- Input arguments ----------\n");
    cerr <<
        "pt_dim = "         << pt_dim       << " dom_dim = "        << dom_dim      <<
        "\ngeom_degree = "  << geom_degree  << " vars_degree = "    << vars_degree  <<
        "\ninput pts = "    << ndomp        << " geom_ctrl pts = "  << geom_nctrl   <<
        "\nvars_ctrl_pts = "<< vars_nctrl   << " input = "          << input        << endl;
#ifdef CURVE_PARAMS
    cerr << "parameterization method = curve" << endl;
#else
    cerr << "parameterization method = domain" << endl;
#endif
#ifdef MFA_NO_TBB
    cerr << "TBB: off" << endl;
#else
    cerr << "TBB: on" << endl;
#endif
#ifdef MFA_NO_WEIGHTS
    cerr << "weighted = 0" << endl;
#else
    cerr << "weighted = " << weighted << endl;
#endif
    fprintf(stderr, "-------------------------------------\n\n");

    // initialize DIY
    diy::FileStorage          storage("./DIY.XXXXXX"); // used for blocks to be moved out of core
    diy::Master               master(world,
                                     num_threads,
                                     mem_blocks,
                                     &Block<real_t>::create,
                                     &Block<real_t>::destroy,
                                     &storage,
                                     &Block<real_t>::save,
                                     &Block<real_t>::load);
    diy::ContiguousAssigner   assigner(world.size(), tot_blocks);
    diy::decompose(world.rank(), assigner, master);

    DomainArgs d_args;

    // set default args for diy foreach callback functions
    d_args.pt_dim       = pt_dim;
    d_args.dom_dim      = dom_dim;
    d_args.weighted     = weighted;
    d_args.multiblock   = false;
    d_args.verbose      = 1;
    for (int i = 0; i < pt_dim - dom_dim; i++)
        d_args.f[i] = 1.0;
    for (int i = 0; i < MAX_DIM; i++)
    {
        d_args.geom_p[i]    = geom_degree;
        d_args.vars_p[i]    = vars_degree;
        d_args.ndom_pts[i]  = ndomp;
    }

    // initilize input data

    // sine function f(x) = sin(x), f(x,y) = sin(x)sin(y), ...
    if (input == "sine")
    {
        for (int i = 0; i < MAX_DIM; i++)
        {
            d_args.min[i]               = -4.0 * M_PI;
            d_args.max[i]               = 4.0  * M_PI;
            d_args.geom_nctrl_pts[i]    = geom_nctrl;
            d_args.vars_nctrl_pts[i]    = vars_nctrl;
        }
        for (int i = 0; i < pt_dim - dom_dim; i++)      // for all science variables
            d_args.s[i] = i + 1;                        // scaling factor on range
        master.foreach([&](Block<real_t>* b, const diy::Master::ProxyWithLink& cp)
                { b->generate_sine_data(cp, d_args); });
    }

    // sinc function f(x) = sin(x)/x, f(x,y) = sinc(x)sinc(y), ...
    if (input == "sinc")
    {
        for (int i = 0; i < MAX_DIM; i++)
        {
            d_args.min[i]               = -4.0 * M_PI;
            d_args.max[i]               = 4.0  * M_PI;
            d_args.geom_nctrl_pts[i]    = geom_nctrl;
            d_args.vars_nctrl_pts[i]    = vars_nctrl;
        }
        for (int i = 0; i < pt_dim - dom_dim; i++)      // for all science variables
            d_args.s[i] = 10.0 * (i + 1);                 // scaling factor on range
        d_args.r = rot * M_PI / 180.0;   // domain rotation angle in rads
        d_args.t = twist;                // twist (waviness) of domain
        master.foreach([&](Block<real_t>* b, const diy::Master::ProxyWithLink& cp)
                { b->generate_sinc_data(cp, d_args); });
    }

    // S3D dataset
    if (input == "s3d")
    {
        d_args.ndom_pts[0]          = 704;
        d_args.ndom_pts[1]          = 540;
        d_args.ndom_pts[2]          = 550;
        d_args.geom_nctrl_pts[0]    = geom_nctrl;
        d_args.geom_nctrl_pts[1]    = geom_nctrl;
        d_args.geom_nctrl_pts[2]    = geom_nctrl;
        d_args.vars_nctrl_pts[0]    = 140;
        d_args.vars_nctrl_pts[1]    = 108;
        d_args.vars_nctrl_pts[2]    = 110;
        strncpy(d_args.infile, "/Users/tpeterka/datasets/flame/6_small.xyz", sizeof(d_args.infile));
        if (dom_dim == 1)
            master.foreach([&](Block<real_t>* b, const diy::Master::ProxyWithLink& cp)
                    { b->read_1d_slice_3d_vector_data(cp, d_args); });
        else if (dom_dim == 2)
            master.foreach([&](Block<real_t>* b, const diy::Master::ProxyWithLink& cp)
                    { b->read_2d_slice_3d_vector_data(cp, d_args); });
        else if (dom_dim == 3)
            master.foreach([&](Block<real_t>* b, const diy::Master::ProxyWithLink& cp)
                    { b->read_3d_vector_data(cp, d_args); });
        else
        {
            fprintf(stderr, "S3D data only available in 2 or 3d domain\n");
            exit(0);
        }
    }

    // nek5000 dataset
    if (input == "nek")
    {
        for (int i = 0; i < 3; i++)
        {
            d_args.ndom_pts[i]          = 200;
            d_args.geom_nctrl_pts[i]    = geom_nctrl;
            d_args.vars_nctrl_pts[i]    = vars_nctrl;
        }
        strncpy(d_args.infile, "/Users/tpeterka/datasets/nek5000/200x200x200/0.xyz", sizeof(d_args.infile));
        if (dom_dim == 2)
            master.foreach([&](Block<real_t>* b, const diy::Master::ProxyWithLink& cp)
                    { b->read_2d_slice_3d_vector_data(cp, d_args); });
        else if (dom_dim == 3)
            master.foreach([&](Block<real_t>* b, const diy::Master::ProxyWithLink& cp)
                    { b->read_3d_vector_data(cp, d_args); });
        else
        {
            fprintf(stderr, "nek5000 data only available in 2 or 3d domain\n");
            exit(0);
        }
    }

    // compute the MFA

    fprintf(stderr, "\nStarting fixed encoding...\n\n");
    double encode_time = MPI_Wtime();
    master.foreach([&](Block<real_t>* b, const diy::Master::ProxyWithLink& cp)
            { b->fixed_encode_block(cp, d_args); });
    encode_time = MPI_Wtime() - encode_time;
    fprintf(stderr, "\n\nFixed encoding done.\n\n");

    // debug: compute error field for visualization and max error to verify that it is below the threshold
    fprintf(stderr, "\nFinal decoding and computing max. error...\n");
    double decode_time = MPI_Wtime();
#ifdef CURVE_PARAMS     // normal distance
    master.foreach([&](Block<real_t>* b, const diy::Master::ProxyWithLink& cp)
            { b->error(cp, 1, true); });
#else                   // range coordinate difference
    master.foreach([&](Block<real_t>* b, const diy::Master::ProxyWithLink& cp)
            { b->range_error(cp, 1, true); });
#endif
    decode_time = MPI_Wtime() - decode_time;

    // print results
    fprintf(stderr, "\n------- Final block results --------\n");
    master.foreach(&Block<real_t>::print_block);
    fprintf(stderr, "encoding time         = %.3lf s.\n", encode_time);
    fprintf(stderr, "decoding time         = %.3lf s.\n", decode_time);
    fprintf(stderr, "-------------------------------------\n\n");

    // save the results in diy format
    diy::io::write_blocks("approx.out", world, master);

    // check the results of the last (only) science variable
    Block<real_t>* b    = static_cast<Block<real_t>*>(master.block(0));
    int ndom_dims       = b->ndom_pts.size();                // domain dimensionality
    real_t range_extent = b->domain.col(ndom_dims).maxCoeff() - b->domain.col(ndom_dims).minCoeff();
    real_t err_factor   = 1.0e-3;
    // for ./fixed-test -i sinc -d 3 -m 2 -p 1 -q 5 -v 20 -w 0
    real_t expect_err   = 4.304489e-4;
    real_t our_err      = b->max_errs[0] / range_extent;    // normalized max_err
    if (fabs(expect_err - our_err) / expect_err > err_factor)
    {
        fprintf(stderr, "our error (%e) and expected error (%e) differ by more than a factor of %e\n", our_err, expect_err, err_factor);
        abort();
    }

}