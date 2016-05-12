/*
asd - a program to quickly calculate pairwise individual allele sharing distances
    Copyright (C) 2011  Zachary A Szpiech (szpiechz@umich.edu)

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <iostream>
#include <fstream>
#include <map>
#include <vector>
#include <pthread.h>
//#include <cstdio>
#include <cstdlib>
#include <cctype>
#include "gzstream.h"
#include "param_t.h"
#include "asd-cli.h"
#include "asd-data.h"
#include "errlog.h"
#include "asd-dist.h"

using namespace std;


int main(int argc, char *argv[])
{
    param_t *params = getCLI(argc, argv);
    if (params == NULL) return 0;

    string outfile = params->getStringFlag(ARG_OUTFILE);
    LOG.init(outfile);
    LOG.log(getCommandLineString(argc, argv));
    LOG.log("Output file basename:", outfile);

    bool argerr = false;

    string filename = params->getStringFlag(ARG_FILENAME);
    string tped_filename = params->getStringFlag(ARG_TPED_FILENAME);
    string tfam_filename = params->getStringFlag(ARG_TFAM_FILENAME);
    bool STRU = (filename.compare(DEFAULT_FILENAME) != 0);
    bool TPED = (tped_filename.compare(DEFAULT_TPED_FILENAME) != 0);
    bool TFAM = (tfam_filename.compare(DEFAULT_TFAM_FILENAME) != 0);
    if (!check_file_type(STRU, TPED, TFAM)) {
        argerr = true;
        LOG.err("ERROR: Must specify either stru or tped/tfam.");
    }
    if (STRU) LOG.log("Input stru file:", filename);
    if (TPED) LOG.log("Input tped file:", tped_filename);
    if (TFAM) LOG.log("Input tfam file:", tfam_filename);

    int sort = params->getIntFlag(ARG_SORT);
    if (STRU) LOG.log("Individual ID column:", sort);

    int num_threads = params->getIntFlag(ARG_THREAD);
    if (!check_int_gt_0(num_threads)) {
        argerr = true;
        LOG.err("ERROR: Must have a positive number of threads.");
    }
    LOG.log("Number of threads:", num_threads);

    bool PRINT_PARTIAL = params->getBoolFlag(ARG_PARTIAL);
    bool PRINT_LOG = params->getBoolFlag(ARG_LOG);
    if (PRINT_PARTIAL && PRINT_LOG) {
        argerr = true;
        LOG.err("ERROR: Must choose only one of --partial, --log.");
    }
    if (!PRINT_PARTIAL) {
        LOG.log("Print allele sharing matrix:", !PRINT_PARTIAL);
        LOG.log("Log transformed:", PRINT_LOG);
    }
    else LOG.log("Print partial allele sharing matrix:", PRINT_PARTIAL);

    bool CALC_ALL_IBS = params->getBoolFlag(ARG_CALC_IBS);
    LOG.log("Output IBS matricies:", CALC_ALL_IBS);

    string STRU_MISSING = params->getStringFlag(ARG_STRU_MISSING);
    string TPED_MISSING = params->getStringFlag(ARG_TPED_MISSING);
    if (STRU) {
        LOG.log("STRU missing code:", STRU_MISSING);
    }
    else {
        LOG.log("TPED missing code:", TPED_MISSING);
    }

    if (argerr) return -1;

    int nrows, ncols;

    structure_data *data;
    if (STRU) {
        try {
            data = readData_stru2(filename, sort, nrows, ncols, STRU_MISSING);
        }
        catch (...) {
            return -1;
        }
    }
    else {
        try {
            //readData_ind_asd_tped_tfam(tped_filename, tfam_filename, data, nrows, ncols, TPED_MISSING);
        }
        catch (...) {
            return -1;
        }
    }

    int nind = nrows / 2;

    init_storage(nind, CALC_ALL_IBS);

    unsigned int *NUM_PER_THREAD = make_thread_partition(num_threads, ncols);

    work_order_t *order;
    pthread_t *peer = new pthread_t[num_threads];
    unsigned int prev_index = 0;
    for (int i = 0; i < num_threads; i++)
    {
        order = new work_order_t;
        order->first_index = prev_index;
        order->last_index = prev_index + NUM_PER_THREAD[i];
        prev_index += NUM_PER_THREAD[i];
        order->stru_data = data;
        order->CALC_ALL_IBS = CALC_ALL_IBS;
        pthread_create(&(peer[i]),
                       NULL,
                       (void *(*)(void *))calc_pw_as_dist2,
                       (void *)order);

    }

    for (int i = 0; i < num_threads; i++) pthread_join(peer[i], NULL);

    finalize_calculations(nind, ncols, CALC_ALL_IBS);

    write_dist_matrix(outfile, nind, ncols, data->ind_names, PRINT_PARTIAL, PRINT_LOG);

    if (CALC_ALL_IBS)
    {
        write_ibs_matrices(outfile, nind, ncols, data->ind_names, PRINT_PARTIAL);
    }

    delete [] NUM_PER_THREAD;
    delete [] peer;
    return 0;
}

