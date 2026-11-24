#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include "eval.h"
#include "../utils/log.h"

void hyperparameter_search(double **data, struct dim *csv_dim)
{
    // Init the options for number of trees to: 10, 100, 1000.
    size_t n = 3;

    size_t *estimators = malloc(sizeof(size_t) * n);
    estimators[0] = 10;
    estimators[1] = 50;
    estimators[2] = 100;

    // Init the options for the max depth for a tree to: 3, 7, 10.
    size_t *max_depths = malloc(sizeof(size_t) * n);
    max_depths[0] = 3;
    max_depths[1] = 7;
    max_depths[2] = 10;

    // Defaults based on SKLearn's defaults / hand picked in order to compare performance
    // with the same parameters.
    size_t max_features = 3;
    size_t min_samples_leaf = 2;

    // Number of folds for cross validation.
    size_t k_folds = 5;

    // Best params computed from running the hyperparameter search.
    size_t best_n_estimators = -1;
    double best_accuracy = -1;

    for (size_t i = 0; i < n; ++i)
    {
        size_t n_estimators = estimators[i]; /* Number of trees in the forest. */

        for (size_t j = 0; j < n; ++j)
        {
            size_t max_depth = max_depths[j];

            RandomForestParameters params = {
                .n_estimators = n_estimators,
                .max_depth = max_depth,
                .min_samples_leaf = min_samples_leaf,
                .max_features = max_features
            };

            log_if_level(0, "[hyperparameter search] testing params:\n  n_estimators: %ld\n  max_depth: %ld\n  min_samples_leaf: %ld\n  max_features: %ld\n",
                   params.n_estimators,
                   params.max_depth,
                   params.min_samples_leaf,
                   params.max_features);




            double cv_accuracy = cross_validate(data,
                                                &params,
                                                csv_dim,
                                                k_folds);

            log_if_level(0, "[hyperparameter search] cross validation accuracy: %f%% (%ld%%)\n",
                       (cv_accuracy * 100),
                       (long)(cv_accuracy * 100));

            // Update best accuracy and best parameters found so far from the hyperparameter search.
            if (cv_accuracy > best_accuracy)
            {
                best_accuracy = cv_accuracy;
                best_n_estimators = n_estimators;
            }
        }
    }

    // Free auxillary buffers.
    free(estimators);
    free(max_depths);

    printf("[hyperparameter search] run complete\n  best_accuracy: %f\n  best_n_estimators (trees): %ld\n",
           best_accuracy, best_n_estimators);
}

double eval_model(const DecisionTreeNode **random_forest,
                  double **data,
                  const RandomForestParameters *params,
                  const struct dim *csv_dim,
                  const ModelContext *ctx)
{
    // Keeping track of how many predictions have been correct. Accuracy can be
    // computed with 'num_correct' / 'rowsPerFold' (or how many predictions we make).
    long num_correct = 0;

    // Since we are evaluating the model on a single fold (to control overfitting), we start
    // iterating the rows for which we are getting predictions at an offset that can be computed
    // as 'testingFoldIdx * rowsPerFold' and make predictions for 'rowsPerFold' number of rows
    size_t row_id_offset = ctx->testingFoldIdx * ctx->rowsPerFold;
    for (size_t row_id = row_id_offset; row_id < row_id_offset + ctx->rowsPerFold; ++row_id)
    {
        int prediction = predict_model(&random_forest,
                                       params->n_estimators,
                                       data[row_id]);
        int ground_truth = (int)data[row_id][csv_dim->cols - 1];

        log_if_level(1, "majority vote:  %ld |  ground truth: %d\n",
                prediction, ground_truth);

        // if (log_level > 1)
        //     printf("majority vote: %d | %d ground truth\n", prediction, ground_truth);

        if (prediction == ground_truth)
            ++num_correct;
    }
    return (double)num_correct / (double)ctx->rowsPerFold;
}

// Argumentos de cada thread do cross_validate: roda os folds de start ate end.
struct fold_args
{
    double **data;
    const RandomForestParameters *params;
    const struct dim *csv_dim;
    size_t rowsPerFold;
    unsigned int *seeds;  // uma seed por fold, sorteadas antes de criar as threads
    double *accuracies;   // cada thread escreve so nas posicoes dos seus folds
    size_t start;
    size_t end;
};

// Corpo do laco que antes ficava no cross_validate: treina e avalia um fold por iteracao.
void *run_folds(void *arg)
{
    struct fold_args *args = (struct fold_args *)arg;
    double **data = args->data;
    size_t rows = args->csv_dim->rows;
    size_t cols = args->csv_dim->cols;
    size_t rowsPerFold = args->rowsPerFold;

    for (size_t foldIdx = args->start; foldIdx < args->end; ++foldIdx)
    {
        // Allocate training data (all rows except the test fold)
        size_t train_rows = rows - rowsPerFold;
        double **train_data = malloc(train_rows * sizeof(double*));
        size_t train_idx = 0;

        //Algo parece errado aqui. O que acontece se minha divisão não for inteira???
        //Vamos supor 17 rows com k fold = 3
        //rowsPerFold = 5
        //1º test_start= 0 ; 1ºtest_end = 5
        //2º test_start = 5, 2º test_end = 10
        //3º test_start = 10, 3º test_end = 15... Suspeito...

        size_t test_start = foldIdx * rowsPerFold;
        size_t test_end = test_start + rowsPerFold;
        for (size_t i = 0; i < rows; ++i) {
            if (i < test_start || i >= test_end) {
                train_data[train_idx++] = data[i];
            }
        }
        struct dim train_dim = {train_rows, cols};
        // O rand() tem estado global, entao cada fold usa o rand_r com a sua propria seed.
        unsigned int seed = args->seeds[foldIdx];
        const ModelContext ctx = {
            .testingFoldIdx = foldIdx,
            .rowsPerFold = rowsPerFold,
            .seed = &seed
        };
        // Train on training data only
        //Agora o bixo pega...
        const DecisionTreeNode **random_forest = train_model(
            train_data,
            args->params,
            &train_dim,
            &ctx);
        // Evaluate on the test fold (still using the full data array for eval_model, which uses ctx to select test rows)
        args->accuracies[foldIdx] = eval_model(
            random_forest,
            data,
            args->params,
            args->csv_dim,
            &ctx);
        free_random_forest(&random_forest, args->params->n_estimators);
        free(train_data);
    }

    return NULL;
}

double cross_validate(double **data,
                      const RandomForestParameters *params,
                      const struct dim *csv_dim,
                      const size_t k_folds)
{
    size_t rowsPerFold = csv_dim->rows / k_folds;

    // Sorteia aqui, ainda sequencial, uma seed para cada fold. Assim o resultado
    // e o mesmo com qualquer numero de threads.
    unsigned int *seeds = malloc(sizeof(unsigned int) * k_folds);
    for (size_t f = 0; f < k_folds; ++f)
        seeds[f] = rand();

    double *accuracies = malloc(sizeof(double) * k_folds);
    pthread_t *threads = malloc(sizeof(pthread_t) * num_threads);
    struct fold_args *args = malloc(sizeof(struct fold_args) * num_threads);

    double start = get_time();

    // Cada thread roda um bloco de folds.
    for (int t = 0; t < num_threads; ++t)
    {
        args[t].data = data;
        args[t].params = params;
        args[t].csv_dim = csv_dim;
        args[t].rowsPerFold = rowsPerFold;
        args[t].seeds = seeds;
        args[t].accuracies = accuracies;
        args[t].start = t * k_folds / num_threads;
        args[t].end = (t + 1) * k_folds / num_threads;
        pthread_create(&threads[t], NULL, run_folds, &args[t]);
    }
    for (int t = 0; t < num_threads; ++t)
        pthread_join(threads[t], NULL);

    parallelizable_time += get_time() - start;

    // Soma na ordem dos folds, depois do join, para nao precisar de mutex.
    double sumAccuracy = 0;
    for (size_t f = 0; f < k_folds; ++f)
        sumAccuracy += accuracies[f];

    free(threads);
    free(args);
    free(accuracies);
    free(seeds);
    return sumAccuracy / k_folds;
}
