#define BENCHMARK "OSU MPI%s Broadcast Latency Test"
/*
 * Copyright (C) 2002-2023 the Network-Based Computing Laboratory
 * (NBCL), The Ohio State University.
 *
 * Contact: Dr. D. K. Panda (panda@cse.ohio-state.edu)
 *
 * For detailed copyright and licensing information, please refer to the
 * copyright file COPYRIGHT in the top level OMB directory.
 */
#include <osu_util_mpi.h>

int main(int argc, char *argv[])
{
    int i = 0, j, rank, size;
    int numprocs;
    double avg_time = 0.0, max_time = 0.0, min_time = 0.0;
    double latency = 0.0, t_start = 0.0, t_stop = 0.0;
    double timer = 0.0, iter_time = 0.0;
    double iter_time_percentiles[npercentiles];
    double *rank_time_list = NULL, *iter_times = NULL;
    char *buffer = NULL;
    int po_ret;
    int errors = 0, local_errors = 0;
    omb_graph_options_t omb_graph_options;
    omb_graph_data_t *omb_graph_data = NULL;
    int papi_eventset = OMB_PAPI_NULL;
    options.bench = COLLECTIVE;
    options.subtype = BCAST;
    size_t num_elements = 0;
    MPI_Datatype omb_curr_datatype = MPI_CHAR;
    size_t omb_ddt_transmit_size = 0;
    int mpi_type_itr = 0, mpi_type_size = 0, mpi_type_name_length = 0;
    char mpi_type_name_str[OMB_DATATYPE_STR_MAX_LEN];
    MPI_Datatype mpi_type_list[OMB_NUM_DATATYPES];
    MPI_Comm omb_comm = MPI_COMM_NULL;
    omb_mpi_init_data omb_init_h;
    struct omb_buffer_sizes_t omb_buffer_sizes;

    set_header(HEADER);
    set_benchmark_name("osu_bcast");
    po_ret = process_options(argc, argv);
    omb_populate_mpi_type_list(mpi_type_list);

    if (PO_OKAY == po_ret && NONE != options.accel) {
        if (init_accel()) {
            fprintf(stderr, "Error initializing device\n");
            exit(EXIT_FAILURE);
        }
    }

    omb_init_h = omb_mpi_init(&argc, &argv);
    omb_comm = omb_init_h.omb_comm;
    if (MPI_COMM_NULL == omb_comm) {
        OMB_ERROR_EXIT("Cant create communicator");
    }
    MPI_CHECK(MPI_Comm_rank(omb_comm, &rank));
    MPI_CHECK(MPI_Comm_size(omb_comm, &numprocs));

    omb_graph_options_init(&omb_graph_options);
    switch (po_ret) {
        case PO_BAD_USAGE:
            print_bad_usage_message(rank);
            omb_mpi_finalize(omb_init_h);
            exit(EXIT_FAILURE);
        case PO_HELP_MESSAGE:
            print_help_message(rank);
            omb_mpi_finalize(omb_init_h);
            exit(EXIT_SUCCESS);
        case PO_VERSION_MESSAGE:
            print_version_message(rank);
            omb_mpi_finalize(omb_init_h);
            exit(EXIT_SUCCESS);
        case PO_OKAY:
            break;
    }

    if (numprocs < 2) {
        if (rank == 0) {
            fprintf(stderr, "This test requires at least two processes\n");
        }

        omb_mpi_finalize(omb_init_h);
        exit(EXIT_FAILURE);
    }

    if (0 == rank)
    {
        rank_time_list = calloc(numprocs, sizeof(double));
        if (!rank_time_list)
        {
            fprintf(stderr, "No memory\n");
            exit(EXIT_FAILURE);
        }
    }

    iter_times = calloc(options.iterations, sizeof(double));
    if (!iter_times)
    {
        fprintf(stderr, "No memory\n");
        exit(EXIT_FAILURE);
    }

    check_mem_limit(numprocs);
    if (allocate_memory_coll((void **)&buffer, options.max_message_size,
                             options.accel)) {
        fprintf(stderr, "Could Not Allocate Memory [rank %d]\n", rank);
        MPI_CHECK(MPI_Abort(omb_comm, EXIT_FAILURE));
    }
    set_buffer(buffer, options.accel, 1, options.max_message_size);
    omb_buffer_sizes.sendbuf_size = options.max_message_size;
    omb_buffer_sizes.recvbuf_size = options.max_message_size;

    int alltoall[numprocs];
    memset(alltoall, rank, sizeof(int) * numprocs);
    /* Everyone should know each other - this helps reduce initialization delays */
    MPI_CHECK(MPI_Alltoall(MPI_IN_PLACE, 1, MPI_INT, alltoall, 1, MPI_INT, omb_comm));

    print_preamble(rank);
    omb_papi_init(&papi_eventset);
    for (mpi_type_itr = 0; mpi_type_itr < options.omb_dtype_itr;
         mpi_type_itr++) {
        MPI_CHECK(MPI_Type_size(mpi_type_list[mpi_type_itr], &mpi_type_size));
        MPI_CHECK(MPI_Type_get_name(mpi_type_list[mpi_type_itr],
                                    mpi_type_name_str, &mpi_type_name_length));
        omb_curr_datatype = mpi_type_list[mpi_type_itr];
        OMB_MPI_RUN_AT_RANK_ZERO(
            fprintf(stdout, "# Datatype: %s.\n", mpi_type_name_str));
        fflush(stdout);
        if (options.percentiles)
        {
            print_percentiles_header(rank);
        }
        else
        {
            print_only_header(rank);
        }
        for (size = options.min_message_size; size <= options.max_message_size;
             size *= 2) {
            num_elements = size / mpi_type_size;
            if (0 == num_elements) {
                continue;
            }
            if (size > LARGE_MESSAGE_SIZE) {
                options.skip = options.skip_large;
                options.iterations = options.iterations_large;
            }

            omb_graph_allocate_and_get_data_buffer(
                &omb_graph_data, &omb_graph_options, size, options.iterations);
            timer = 0.0;
            memset(iter_time_percentiles, 0, sizeof(iter_time_percentiles));
            omb_ddt_transmit_size =
                omb_ddt_assign(&omb_curr_datatype, mpi_type_list[mpi_type_itr],
                               num_elements) *
                mpi_type_size;
            num_elements = omb_ddt_get_size(num_elements);
            int root = 0;
            for (i = 0; i < options.iterations + options.skip; i++) {
                /* Force root rotation */
                options.omb_root_rank = OMB_ROOT_ROTATE_VAL;
                root = omb_get_root_rank(i, numprocs);

                if (i == options.skip) {
                    omb_papi_start(&papi_eventset);
                }
                if (options.validate) {
                    set_buffer_validation(buffer, NULL, size, options.accel, i,
                                          omb_curr_datatype, omb_buffer_sizes);
                    for (j = 0; j < options.warmup_validation; j++) {
                        MPI_CHECK(MPI_Barrier(omb_comm));
                        MPI_CHECK(MPI_Bcast(buffer, num_elements,
                                            omb_curr_datatype, root, omb_comm));
                    }
                    MPI_CHECK(MPI_Barrier(omb_comm));
                }

                MPI_CHECK(MPI_Barrier(omb_comm));
                t_start = MPI_Wtime();
                MPI_CHECK(MPI_Bcast(buffer, num_elements, omb_curr_datatype, root, omb_comm));
                t_stop = MPI_Wtime();
                MPI_CHECK(MPI_Barrier(omb_comm));

                if (options.validate && root == rank) {
                    local_errors +=
                        validate_data(buffer, size, numprocs, options.accel, i,
                                      omb_curr_datatype);
                }

                if (i >= options.skip) {
                    iter_time = t_stop - t_start;
                    iter_times[i - options.skip] = iter_time;
                    timer += iter_time;
                    if (options.graph && 0 == rank) {
                        omb_graph_data->data[i - options.skip] =
                            iter_time * 1e6;
                    }
                }
            }

            MPI_CHECK(MPI_Barrier(omb_comm));
            omb_papi_stop_and_print(&papi_eventset, size);
            latency = (timer * 1e6) / options.iterations;

            if (options.percentiles)
            {
                for (int i = 0; i < options.iterations; ++i)
                {
                    if (0 == rank)
                    {
                        MPI_CHECK(MPI_Gather(&iter_times[i], 1, MPI_DOUBLE, rank_time_list, 1, MPI_DOUBLE, 0, omb_comm));
                        qsort(rank_time_list, numprocs, sizeof(double), cmpfunc_asc_double);
                        for (size_t j = 0; j < sizeof(percentiles) / sizeof(double); ++j)
                        {
                            iter_time_percentiles[j] += CALC_PERCENTILE(rank_time_list, numprocs, percentiles[j]);
                        }
                    }
                    else
                    {
                        MPI_CHECK(MPI_Gather(&iter_times[i], 1, MPI_DOUBLE, NULL, 1, MPI_DOUBLE, 0, omb_comm));
                    }
                    MPI_Barrier(omb_comm);
                }

                for (size_t i = 0; 0 == rank && i < sizeof(percentiles) / sizeof(double); ++i)
                {
                    iter_time_percentiles[i] = (double)(iter_time_percentiles[i] * 1e6 / options.iterations);
                }
            }
            else
            {
                MPI_CHECK(MPI_Reduce(&latency, &min_time, 1, MPI_DOUBLE, MPI_MIN, 0, omb_comm));
                MPI_CHECK(MPI_Reduce(&latency, &max_time, 1, MPI_DOUBLE, MPI_MAX, 0, omb_comm));
            }

            MPI_CHECK(MPI_Reduce(&latency, &avg_time, 1, MPI_DOUBLE, MPI_SUM, 0, omb_comm));
            avg_time = avg_time / numprocs;

            if (options.validate) {
                MPI_CHECK(MPI_Allreduce(&local_errors, &errors, 1, MPI_INT,
                                        MPI_SUM, omb_comm));
            }

            if (options.validate)
            {
                print_stats_validate(rank, size, avg_time, min_time, max_time, errors);
            }
            else if (options.percentiles)
            {
                print_lat_percentiles(rank, size, iter_time_percentiles, avg_time);
            }
            else
            {
                print_stats(rank, size, avg_time, min_time, max_time);
            }

            if (options.graph && 0 == rank) {
                omb_graph_data->avg = avg_time;
            }
            omb_ddt_append_stats(omb_ddt_transmit_size);
            omb_ddt_free(&omb_curr_datatype);
            if (0 != errors) {
                break;
            }
        }
    }
    if (0 == rank && options.graph) {
        omb_graph_plot(&omb_graph_options, benchmark_name);
    }
    omb_graph_combined_plot(&omb_graph_options, benchmark_name);
    omb_graph_free_data_buffers(&omb_graph_options);
    omb_papi_free(&papi_eventset);

    if (rank_time_list)
    {
        free(rank_time_list);
    }
    if (iter_times)
    {
        free(iter_times);
    }
    free_buffer(buffer, options.accel);
    omb_mpi_finalize(omb_init_h);

    if (NONE != options.accel) {
        if (cleanup_accel()) {
            fprintf(stderr, "Error cleaning up device\n");
            exit(EXIT_FAILURE);
        }
    }

    if (0 != errors && options.validate && 0 == rank) {
        fprintf(stdout,
                "DATA VALIDATION ERROR: %s exited with status %d on"
                " message size %d.\n",
                argv[0], EXIT_FAILURE, size);
        exit(EXIT_FAILURE);
    }

    return EXIT_SUCCESS;
}

/* vi: set sw=4 sts=4 tw=80: */