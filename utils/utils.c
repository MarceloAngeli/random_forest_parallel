#include "utils.h"
#include <time.h>
#include <pthread.h>

int log_level = 1;

int num_threads = 4;

double parallelizable_time = 0;

double get_time(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

int get_log_level()
{
    return log_level;
}

void set_log_level(int selected_log_level)
{
    if (selected_log_level < 0 || selected_log_level > 3)
    {
        printf("Error: log_level must be in range [0, 3] got: %d\n", selected_log_level);
        exit(1);
    }
    log_level = selected_log_level;
}

int contains_int(int *arr, size_t n, int val)
{
    for (size_t i = 0; i < n; ++i)
    {
        if (arr[i] == val)
            return 1;
    }
    return 0;
}

int is_row_part_of_testing_fold(size_t row, const ModelContext *ctx)
{
    //Ainda sim, isso ignora o cenário que eu tinha dito antes. 
    //Ou seja, tem dados que nunca entram no treinamento.
    size_t lower_bound = ctx->testingFoldIdx * ctx->rowsPerFold;
    size_t upper_bound = lower_bound + ctx->rowsPerFold;

    if (row >= lower_bound && row <= upper_bound)
        return 1;
    else
        return 0;
}

// Argumentos de cada thread do combine_arrays: copia as posições de start até end.
struct combine_args
{
    double **first;
    double **second;
    double **combined;
    size_t n1;
    size_t start;
    size_t end;
};

void *combine_rows(void *arg)
{
    struct combine_args *args = (struct combine_args *)arg;

    // As linhas do first ficam em [0, n1) e as do second em [n1, n1 + n2).
    for (size_t i = args->start; i < args->end; ++i)
    {
        if (i < args->n1)
            args->combined[i] = args->first[i];
        else
            args->combined[i] = args->second[i - args->n1];
    }

    return NULL;
}

double **combine_arrays(double **first, double **second, size_t n1, size_t n2, size_t cols)
{
    double **combined = (double **)malloc((n1 + n2) * sizeof(double) * cols);

    pthread_t *threads = malloc(sizeof(pthread_t) * num_threads);
    struct combine_args *args = malloc(sizeof(struct combine_args) * num_threads);

    double start = get_time();

    for (int t = 0; t < num_threads; ++t)
    {
        args[t].first = first;
        args[t].second = second;
        args[t].combined = combined;
        args[t].n1 = n1;
        args[t].start = t * (n1 + n2) / num_threads;
        args[t].end = (t + 1) * (n1 + n2) / num_threads;
        pthread_create(&threads[t], NULL, combine_rows, &args[t]);
    }
    for (int t = 0; t < num_threads; ++t)
        pthread_join(threads[t], NULL);

    parallelizable_time += get_time() - start;

    free(threads);
    free(args);
    return combined;
}

double **_2d_malloc(const size_t rows, const size_t cols)
{
    double **data;
    double *ptr;

    int len = sizeof(double *) * rows + sizeof(double) * cols * rows;
    data = (double **)malloc(len);

    ptr = (double *)(data + rows);

    for (size_t i = 0; i < rows; ++i)
        data[i] = ptr + cols * i;

    return data;
}

double **_2d_calloc(const size_t rows, const size_t cols)
{
    double **data;
    double *ptr;

    int len = sizeof(double *) * rows + sizeof(double) * cols * rows;
    data = (double **)calloc(len, sizeof(double));

    ptr = (double *)(data + rows);

    for (size_t i = 0; i < rows; ++i)
        data[i] = ptr + cols * i;

    return data;
}

double _1d_checksum(double *data, size_t size)
{
    double sum = 0;
    for (size_t i = 0; i < size; ++i)
    {
        sum += data[i];
    }
    return sum;
}

double _2d_checksum(double **data, size_t rows, size_t cols)
{
    double sum = 0;
    for (size_t i = 0; i < rows; ++i)
    {
        for (size_t j = 0; j < cols; ++j)
        {
            sum += data[i][j];
        }
    }
    return sum;
}
