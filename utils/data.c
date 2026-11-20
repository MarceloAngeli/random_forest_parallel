#include "data.h"
#include "log.h"
#include <pthread.h>

// Set to 1 if your CSV has a header row, 0 otherwise
#define CSV_HAS_HEADER 1

// Debug macro for easier debug output
#define DEBUG_PRINT(fmt, ...) \
    do { if (DEBUG) fprintf(stderr, fmt, __VA_ARGS__); } while (0)

int DEBUG = 0;

// Parses the CSV file to determine its dimensions (rows and columns)
struct dim parse_csv_dims(const char *file_name)
{
    FILE *csv_file;
    csv_file = fopen(file_name, "r");
    fseek(csv_file, 0L, SEEK_END);
    long buf_size = ftell(csv_file);
    rewind(csv_file);

    if (csv_file == NULL)
    {
        printf("Error: can't open file: %s\n", file_name);
        exit(-1);
    }

    const char *delimiter = ",";


    char *buffer = (char *)malloc(buf_size); // BUFSIZ
    if (!buffer) {
        printf("Error: failed to allocate buffer of size %ld\n", buf_size);
        fclose(csv_file);
        exit(-1);
    }
    char *token;

    // Keeping track of how many rows and columns there are.
    int rows = 0;
    int cols = 0;

    // Reach each line of the file into the buffer.

    while (fgets(buffer, buf_size, csv_file) != NULL)
    {
        ++rows;

        // We use a second counter for columns in order to count the number of columns
        // for each row we read and will trigger an assert if there is a mismatch in the
        // size of the cols, i.e. not all rows have the same number of columns which would
        // cause undefined behavior.
        int curr_cols = 0;

        
        token = strtok(buffer, delimiter);
        while (token != NULL)
        {
            ++curr_cols;
            DEBUG_PRINT( " R: %d Col: %d %s\n", rows, curr_cols, token );

            token = strtok(NULL, delimiter);
        }
        if (cols == 0)
        {
            cols = curr_cols;
        }
        else
        {
            assert(curr_cols == cols && "Error: every row must have the same amount of columns");
        }
    }

    // Adjust for header row if present
    if (CSV_HAS_HEADER) {
        --rows;
    }


    if (fclose(csv_file) != 0) {
        printf("Warning: error closing file %s\n", file_name);
    }
    free(buffer);

    // Make sure that the dimensions are valid.

    assert(rows > 0 && "# of rows in csv must be > 0");
    assert(cols > 0 && "# of cols in csv must be > 0");


    return (struct dim){.rows = rows, .cols = cols};
}

// Argumentos de cada thread do parse_csv: converte as linhas de start até end.
struct parse_args
{
    char **lines;
    double *data;
    size_t cols;
    size_t start;
    size_t end;
};

void *parse_lines(void *arg)
{
    struct parse_args *args = (struct parse_args *)arg;
    const char *delimiter = ",";

    for (size_t i = args->start; i < args->end; ++i) {
        // strtok guarda estado global, por isso troquei por strtok_r
        char *save;
        // A linha i sempre começa em i * cols, então as threads não escrevem na mesma posição.
        size_t idx = i * args->cols;
        char *token = strtok_r(args->lines[i], delimiter, &save);
        while (token != NULL) {
            DEBUG_PRINT("Parsing row %zu col %zu: %s\n", i + 1, (idx % args->cols) + 1, token);
            //Coloca na posição idx do buffer o DOUBLE retirado do CSV
            args->data[idx] = atof(token);
            ++idx;
            //Pega a próxima STRING
            token = strtok_r(NULL, delimiter, &save);
        }
    }

    return NULL;
}

// Parses the CSV file and fills the data array
void parse_csv(const char *file_name, double **data_p, const struct dim csv_dim)
{

    FILE *csv_file;
    char *buffer;
    char **lines;
    size_t row = 0;

    csv_file = fopen(file_name, "r");
    if (csv_file == NULL) {
        printf("Error: can't open file: %s\n", file_name);
        exit(-1);
    }

    // Vai até o fim do arquivo.
    fseek(csv_file, 0L, SEEK_END); 
    // Retorna a posição (nesse caso, como está no fim retorna o número de Chars)
    long buf_size = ftell(csv_file);
    // Reseta o ponteiro para o início do arquivo.
    rewind(csv_file);

    // Agora o buffer guarda o arquivo inteiro, com um '\0' no fim de cada linha.
    buffer = (char *)malloc(buf_size + csv_dim.rows + 1);
    lines = (char **)malloc(sizeof(char *) * csv_dim.rows);
    if (!buffer || !lines) {
        printf("Error: failed to allocate buffer of size %ld\n", buf_size);
        fclose(csv_file);
        exit(-1);
    }

    // Skip header row if present
    if (CSV_HAS_HEADER && fgets(buffer, buf_size, csv_file) == NULL) {
        printf("Error: file %s is empty\n", file_name);
        exit(-1);
    }

    // A leitura do arquivo continua sequencial, aqui só guarda onde cada linha começa.
    char *line = buffer;
    while (row < csv_dim.rows && fgets(line, buf_size, csv_file) != NULL) {
        lines[row] = line;
        line += strlen(line) + 1;
        ++row;
    }

    pthread_t *threads = malloc(sizeof(pthread_t) * num_threads);
    struct parse_args *args = malloc(sizeof(struct parse_args) * num_threads);

    double start = get_time();

    // A conversão pra double é a parte paralela, cada thread pega um bloco de linhas.
    for (int t = 0; t < num_threads; ++t)
    {
        args[t].lines = lines;
        args[t].data = (*data_p);
        args[t].cols = csv_dim.cols;
        args[t].start = t * row / num_threads;
        args[t].end = (t + 1) * row / num_threads;
        pthread_create(&threads[t], NULL, parse_lines, &args[t]);
    }
    for (int t = 0; t < num_threads; ++t)
        pthread_join(threads[t], NULL);

    parallelizable_time += get_time() - start;

    log_if_level(1, "read %zu rows from file %s\n", row, file_name);

    if (fclose(csv_file) != 0) {
        printf("Warning: error closing file %s\n", file_name);
    }
    free(threads);
    free(args);
    free(lines);
    free(buffer);
}

// Argumentos de cada thread do pivot_data: copia as linhas de start até end.
struct pivot_args
{
    const double *data;
    double **pivoted_data;
    size_t cols;
    size_t start;
    size_t end;
};

void *pivot_rows(void *arg)
{
    struct pivot_args *args = (struct pivot_args *)arg;

    for (size_t i = args->start; i < args->end; ++i)
        for (size_t j = 0; j < args->cols; ++j)
            args->pivoted_data[i][j] = args->data[(i * args->cols) + j];

    return NULL;
}

// Pivots and transforms the data array into a 2D array
void pivot_data(const double *data, const struct dim csv_dim, double ***pivoted_data_p)
{
    (*pivoted_data_p) = _2d_calloc(csv_dim.rows, csv_dim.cols);

    pthread_t *threads = malloc(sizeof(pthread_t) * num_threads);
    struct pivot_args *args = malloc(sizeof(struct pivot_args) * num_threads);

    double start = get_time();

    // Divide as linhas em num_threads blocos, um pra cada thread.
    for (int t = 0; t < num_threads; ++t)
    {
        args[t].data = data;
        args[t].pivoted_data = (*pivoted_data_p);
        args[t].cols = csv_dim.cols;
        args[t].start = t * csv_dim.rows / num_threads;
        args[t].end = (t + 1) * csv_dim.rows / num_threads;
        pthread_create(&threads[t], NULL, pivot_rows, &args[t]);
    }
    for (int t = 0; t < num_threads; ++t)
        pthread_join(threads[t], NULL);

    parallelizable_time += get_time() - start;

    free(threads);
    free(args);
}
