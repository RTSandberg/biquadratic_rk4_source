
#include <algorithm> // std::copy
#include <exception> // std::exception
#include <fstream>              // std::ifstream
#include <iterator> // std::ostream_iterator
#include <math.h> // M_PI, exp, cos, sin
#include <numeric> // std::inner_product
#include <stdio.h> // printf
#include <string>
#include <string.h> // atof
#include <iostream> // cout, endl
// #include <mpi.h>
#include <omp.h>
using std::cout;
using std::endl;
#include <stdexcept> // invalid_argument exception
#include <thread> // std::thread::hardware_concurrency

extern "C" {
    #include <mpi.h>
}


// #include <nlohmann/json.hpp>
// using json = nlohmann::json;
#include "boost/property_tree/ptree.hpp"        //property_tree::ptree, property_tree::read_json
#include "boost/property_tree/json_parser.hpp"
namespace pt = boost::property_tree;

#include "Panel.hpp"
#include "AMRStructure.hpp"
#include "initial_distributions.hpp"

/*
template<class T>
T get_json_val(json& j, std::string key, T default_val) {
    T val;
    try {
        val = j.at(key);
    }
    catch (nlohmann::detail::exception& err) {
        cout << key << " not found in json file.";
        cout << " Setting to " << default_val << endl;
        val = default_val;
    }
    return val;
}
*/


int main(int argc, char** argv) {

    int rank, numProcs;
    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &numProcs);

    // if (argc < 23) {
    //     std::cout << "Not enough input arguments: had " << argc - 1 << ", but need 13" << std::endl;
    //     std::cout << "Usage: 1:sim_dir 2:xmin 3:xmax 4:vmin 5:vmax" << std::endl;
    //     std::cout << " 6:sim_type 7:normal_k 8:amp 9:vth 10:vstr" << std::endl;
    //     std::cout << " 11:initial_height 12:greens_epsilon" << std::endl;
    //     std::cout << " 13:use_treecode 14:treecode_beta" << std::endl;
    //     std::cout << " 15: mac 16: degree 17: max_source 18: max target" << std::endl;
    //     std::cout << " 19:num_steps 20:n_steps_remesh 21: n_steps_diag 22:dt" << std::endl;
    //     return 1;
    // }
    // usage : (no arguments)
    // usage : sim_dir
    // usage : sim_dir deck_name
    // usage : sim_dir deck_location deck_name
    // sim_dir : directory where output is to be saved.  Default is current working directory
    // deck_name : name of input deck.  Default is 'deck.json'
    // deck_location : directory where input deck is saved. Default is sim_dir
    std::string sim_dir, input_deck;
    if (argc > 1) {
        sim_dir = argv[1];
    } else {
        sim_dir = "";
    }
    if (argc > 3) {
        input_deck = argv[2];
        input_deck = input_deck + argv[3];
    } else {
        if (argc > 2) {
            input_deck = sim_dir + argv[2];
        } else {
            input_deck = sim_dir + "deck.json";
        }
    }

    // std::ifstream json_stream;
    // json deck;
    pt::ptree deck;
    try {
        // std::ifstream json_stream(input_deck, std::ifstream::in);
        // json_stream >> deck;
        pt::read_json(input_deck, deck);
    } catch(std::exception& err) {
        cout << "unable to open input deck" << endl;
        return 1;
    }

    std::string project_name = deck.get<std::string>("project_name", "no_name_found");
    double x_min = deck.get<double>("xmin", 0.0), x_max = deck.get<double>("xmax", 0.0);
    double v_min = deck.get<double>("vmin", -1.0), v_max = deck.get<double>("vmax",1.0);

    double Lx = x_max - x_min;
    double kx = 2.0 * M_PI / Lx * deck.get<double>("normalized_wavenumber",1.0);
    double amp = deck.get<double>("amp", 0.0);//0.5;
    double vth = deck.get<double>("vth", 1.0);//atof(argv[9]);//1.0;
    double vstr = deck.get<double>("vstr", 0.0); //atof(argv[10]);
    int sim_type = deck.get<int>("sim_type", 1);//atoi(argv[6]);

    distribution* f0;

    switch (sim_type)
    {
        case 1: // weak Landau Damping
            f0 = new F0_LD(vth, kx, amp);
            break;
        case 2: // strong Landau Damping
            f0 = new F0_LD(vth, kx, amp);
            break;
        case 3: // 'strong' two-stream
            f0 = new F0_strong_two_stream(vth, kx, amp);
            break;
        case 4: // 'colder' two-stream
            f0 = new F0_colder_two_stream(vth, vstr, kx, amp);
            break;
        default:
            f0 = new F0_LD(vth, kx, amp);
            break;
    }

    // F0_colder_two_stream f0{vth, vstr, kx, amp};
    
    int initial_height = deck.get<int>("initial_height",6);//atoi(argv[11]);//6; 
    int max_height = deck.get<int>("max_height", initial_height);
    double greens_epsilon = deck.get<double>("greens_epsilon",0.2);//atof(argv[12]);//0.2;
    int use_treecode = deck.get<int>("use_treecode", 0); //atoi(argv[13]);
    double beta = deck.get<double>("beta", -1.0); //atof(argv[14]);
    double mac = deck.get<double>("mac", -1.0); //atof(argv[15]);
    int degree = get_json_val<int>(deck, "degree", -1); //atoi(argv[16]);
    int max_source = get_json_val<int>(deck, "max_source", 200); //atoi(argv[17]);
    int max_target = get_json_val<int>(deck, "max_target", 200); //atoi(argv[18]);

    // int nxsqrt = pow(2, initial_height + 1) + 1;
    // int nx = nxsqrt * nxsqrt;

    ElectricField* calculate_e;
    if (use_treecode > 0) {
        if (0 <= beta && beta <= 1.0) {
            calculate_e = new E_MQ_Treecode(Lx, greens_epsilon, beta);
        } else {
            int verbosity = 0;
            calculate_e = new E_MQ_Treecode(Lx, greens_epsilon, 
                                            mac, degree, 
                                            max_source, max_target, 
                                            verbosity);
        }
    } else {
        calculate_e = new E_MQ_DirectSum(Lx, greens_epsilon);
    }

    int num_steps = get_json_val<int>(deck, "num_steps", 10);//atoi(argv[19]);//120;
    int n_steps_remesh = get_json_val<int>(deck, "remesh_period", 1); //atoi(argv[20]);
    int n_steps_diag = get_json_val<int>(deck, "diag_period", 1); //atoi(argv[21]);
    double dt = get_json_val<double> (deck, "dt", 0.5); //atof(argv[22]);//0.5;
    bool do_adaptively_refine = get_json_val<int> (deck, "adaptively_refine", false);//false;

    // double eps_amr = deck.at("")
    // cout << "eps_amr: " << deck.at("amr_epsilons").at(0) << endl;
    // cout << deck.at("amr_epsilons").size() << endl;

    std::vector<double> amr_epsilons; //= deck.at("amr_epsilons");
    try {
        // deck.at("amr_epsilons");
        std::vector<double> temp = deck.at("amr_epsilons");
        amr_epsilons = temp;
    } catch(nlohmann::detail::exception& err) {
        cout << "Unable to find amr refinement values.  Disabling amr." << endl;
        amr_epsilons = std::vector<double>();

        do_adaptively_refine = false;
    }

    cout << "============================" << endl;
    cout << "Running a FARRSIGHT simulation" << endl;
    cout << "sim dir: " << sim_dir << endl;
    cout << "deck found in: " << input_deck << endl;
    cout << x_min << " <= x <= " << x_max << endl;
    cout << v_min << " <= v <= " << v_max << endl;
    cout << "k=" << kx << ", amp = " << amp << ", vth = " << vth << ", vstr = " << vstr <<  endl;
    cout << "height " << initial_height << endl;
    cout << "green's epsilon = " << greens_epsilon << endl;
    cout << "Taking " << num_steps << " steps with dt = " << dt << endl;
    cout << "Remesh every " << n_steps_remesh << ", diagnostic dump every " << n_steps_diag << endl;
    cout << "use treecode flag " << use_treecode << endl;
    if (use_treecode > 0) { 
        if (0 <= beta && beta <= 1.0) {
            cout << "Using treecode with beta " << beta << endl;
        } else {
            cout << "Using treecode with mac " << mac << " and degree " << degree << endl;
        }
    } else {
        cout << "using direct sum" << endl;
    }
    if (do_adaptively_refine) {
        cout << "Adaptively refining, to height at most " << max_height << endl;
        cout << "Refinement epsilons : ";
        std::copy(amr_epsilons.begin(), amr_epsilons.end(), std::ostream_iterator<double>(cout, " "));
        cout << endl;
    } else {
        cout <<"Not adaptively refining." << endl;
    }
    cout << "============================" << endl;

    auto sim_start = high_resolution_clock::now();

    AMRStructure amr{sim_dir, f0, 
                initial_height, max_height,
                x_min, x_max, v_min, v_max, 
                calculate_e, num_steps, dt,
                do_adaptively_refine, amr_epsilons};
                
*/

// ------ problem with tc!  
/*
    cout << "Problem with treecode.  Trying to debug" << endl;

    E_MQ_DirectSum ds {Lx, greens_epsilon};
    AMRStructure amr_ds{sim_dir, f0, 
                initial_height, 
                x_min, x_max, v_min, v_max, 
                &ds, num_steps, dt, 
                do_adaptively_refine};

    amr.init_e();
    amr_ds.init_e();

    bool get_4th_e = false;
    amr.step(get_4th_e);
    amr_ds.step(get_4th_e);
    amr.remesh();
    amr_ds.remesh();

    std::vector<double> es_ds = amr_ds.get_e();
    std::vector<double> es_tc = amr.get_e();

    int nt = es_ds.size();
    double dx_t = Lx / nt;
    std::vector<double> error(nt);
    double l2norm_ds=0.0, l2norm_tc=0.0, l2norm_diff=0.0;
    for (int ii = 0; ii < nt; ++ii) {
        double dsii = es_ds[ii];
        double tcii = es_tc[ii];
        l2norm_ds += dsii * dsii * dx_t;
        l2norm_tc += tcii * tcii * dx_t;
        double errii = es_ds[ii] - es_tc[ii];
        error[ii] = errii;
        l2norm_diff += errii * errii * dx_t;
    }
    // l2norm_ds = std::inner_product(es_ds.begin(), es_ds.end(), es_ds.begin(), 0);
    // l2norm_tc = std::inner_product(es_tc.begin(), es_tc.end(), es_tc.begin(), 0);
    // l2norm_diff = std::inner_product(error.begin(), error.end(), error.begin(), 0);
    l2norm_ds = sqrt(l2norm_ds);
    l2norm_tc = sqrt(l2norm_tc);
    l2norm_diff = sqrt(l2norm_diff);

    cout << "l2 norm of es, direct sum = " << l2norm_ds << endl;
    cout << "l2 norm of es, treecode = " << l2norm_tc << endl;
    cout << "l2 norm of error = " << l2norm_diff << endl;
*/
// ----- end treecode debug section

/*

    auto start = high_resolution_clock::now();
    amr.init_e();
    auto stop = high_resolution_clock::now();
    amr.add_time(field_time, duration_cast<duration<double>>(stop - start) );
    amr.write_to_file();

    for (int ii = 0; ii < num_steps; ++ii) {
        bool get_4th_e = false;
        start = high_resolution_clock::now();
        amr.step(get_4th_e);
        stop = high_resolution_clock::now();
        amr.add_time(step_time, duration_cast<duration<double>>(stop - start) );


        start = high_resolution_clock::now();
        amr.remesh();
        stop = high_resolution_clock::now();
        amr.add_time(remesh_time, duration_cast<duration<double>>(stop - start) );

        if ((ii+1) % n_steps_diag == 0) {

            auto file_start = high_resolution_clock::now();
            amr.write_to_file();
            auto file_stop = high_resolution_clock::now();
            amr.add_time(file_time,  duration_cast<duration<double>>(file_stop - file_start) );
        }
    //     // cout << amr << endl;
    }

    // cout << amr << endl;
    auto sim_stop = high_resolution_clock::now();
    amr.add_time(sim_time,  duration_cast<duration<double>>(sim_stop - sim_start) );
    // cout << "Sim time " << sim_duration.count() << " seconds" << endl;

    amr.print_times();
    

    delete f0;
    delete calculate_e;
    */
    
    MPI_Finalize();
}