#include <stdio.h>
#include <string.h>
#include <time.h>
#include <omp.h>
#include "eval/eval.h"
#include "utils/argparse.h"
#include "utils/data.h"
#include "utils/utils.h"
#include "utils/log.h"

int main(int argc, char **argv)
{
    // Tempo total agora conta desde o início, pra incluir a leitura do csv e o pivot.
    double begin_time = omp_get_wtime();

    struct arguments arguments;
    parse_args(argc, argv, &arguments);


    set_log_level(arguments.log_level);

    // Optionally set the random seed if a specific random seed was provided via an argument.

    unsigned int seed;
    if (arguments.random_seed!=RAND_MAX) seed=arguments.random_seed;
    else seed=(unsigned int)time(NULL);
    srand(seed);

    // Read the csv file from args which must be parsed now.

    const char *file_name = arguments.args[0];
    if (!file_name) {
        printf("Usage: %s <CSV_FILE> [--num_rows N] [--num_cols N] [--log_level N] [--seed N]\n", argv[0]);
        return 1;
    }

    // If the values for rows and cols were provided as arguments, then use them for the
    // 'dim' struct, otherwise call 'parse_csv_dims()' to parse the csv file provided to
    // compute the size of the csv file.

    // Struct definida no Data.h
    struct dim csv_dim;

    if (arguments.rows && arguments.cols)
        csv_dim = (struct dim){.rows = arguments.rows, .cols = arguments.cols};
    else
        csv_dim = parse_csv_dims(file_name);

/*
    log_if_level(0, "using:\n  seed: %d\n  verbose log level: %d\n  rows: %ld, cols: %ld\nreading from csv file:\n  \"%s\"\n",
               seed,
               arguments.log_level,
               csv_dim.rows,
               csv_dim.cols,
               file_name);
*/

    // Allocate memory for the data coming from the .csv and read in the data.
    double *data = malloc(sizeof(double) * csv_dim.rows * csv_dim.cols);

    // Enche o buffer com os dados do CSV convertidos para DOUBLE.
    // Da pra PARALELIZAR, mas pode dar um trabalinho...
    parse_csv(file_name, &data, csv_dim);

    // Compute a checksum of the data to verify that loaded correctly.
    //log_if_level(1, "data checksum = %f\n", _1d_checksum(data, csv_dim.rows * csv_dim.cols));

    const int k_folds = 20 ;

    //log_if_level(0, "using:\n  k_folds: %d\n", k_folds);

    // Example configuration for a random forest model.
    const RandomForestParameters params = {
        .n_estimators = 20 /* Number of trees in the random forest model. */,
        .max_depth = 7 /* Maximum depth of a tree in the model. */,
        .min_samples_leaf = 3,
        .max_features = 10
    };

    // Print random forest parameters.

/*
    if (log_level > 0)
        print_params(&params);
*/

    // Pivot the csv file data into a two dimensional array.

    // Certamente da pra PARALELIZAR isso, precisa nem ler.
    // Fácil de fazer.
    double **pivoted_data;
    pivot_data(data, csv_dim, &pivoted_data);

    //log_if_level(1, "checksum of pivoted 2d array: %f\n", _2d_checksum(pivoted_data, csv_dim.rows, csv_dim.cols));
    
    // Inside eval
    double cv_accuracy = cross_validate(pivoted_data, &params, &csv_dim, k_folds);
    // Acurácia já conferida, comentada pra sobrar só o tempo total e o F na saída.
    //printf("%f\n",
    //       (cv_accuracy * 100));
    (void)cv_accuracy;

    // Record and output the time taken to run.
    double total_time = omp_get_wtime() - begin_time;
    printf("%f\n", total_time);

    // F = tempo paralelizável / tempo total
    fprintf(stderr, "%.4f\n", parallelizable_time / total_time);

    // Free loaded csv file data.
    free(data);
    free(pivoted_data);
}
