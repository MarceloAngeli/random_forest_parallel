#include "argparse.h"

 void parse_args(int argc, char **argv, struct arguments *arguments) {
    // Defaults
    arguments->rows = 0;
    arguments->cols = 0;
    arguments->log_level = 1;
    arguments->random_seed = RAND_MAX;
    arguments->num_threads = 4;
    arguments->args[0] = NULL;

    for (int i = 1; i < argc; ++i) {
        //Comparara se o argumento é igual a --num_rows adiciona o número no struct arguments.
        if (strcmp(argv[i], ARG_KEY_ROWS) == 0 && i + 1 < argc) {
            arguments->rows = atol(argv[++i]);
        } 
        //Mesma coisa com o --num_cols
        else if (strcmp(argv[i], ARG_KEY_COLS) == 0 && i + 1 < argc) {
            arguments->cols = atol(argv[++i]);
        } 
        //Verifica se o argumento é --log_level=? onde ? eh um número
        //Nem sabia que podia fazer concatenação de String assim... Bizarro...
        else if (strncmp(argv[i], ARG_KEY_LOG_LEVEL "=", strlen(ARG_KEY_LOG_LEVEL) + 1) == 0) {
            arguments->log_level = atoi(argv[i] + strlen(ARG_KEY_LOG_LEVEL) + 1);
        } 
        //Verifica se o argumento é --log_level ? onde ? eh o numero
        else if (strcmp(argv[i], ARG_KEY_LOG_LEVEL) == 0 && i + 1 < argc) {
            arguments->log_level = atoi(argv[++i]);
        }
        //Verifica se o argumento é --seed ? onde ? eh o numero
        else if (strcmp(argv[i], ARG_KEY_SEED) == 0 && i + 1 < argc) {
            arguments->random_seed = atoi(argv[++i]);
        } 
        //Mesma coisa com o --num_threads
        else if (strcmp(argv[i], ARG_KEY_THREADS) == 0 && i + 1 < argc) {
            arguments->num_threads = atoi(argv[++i]);
        }
        //Se não for nenhum daqueles precisa ser o arquivo csv.
        else if (arguments->args[0] == NULL) {
            arguments->args[0] = argv[i]; // CSV file
        }
    }
}

