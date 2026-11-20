# random_forest_parallel

Random Forest em C com validação cruzada, paralelizado com pthreads.

## Compilar e rodar

```
make
./random-forest wdbc.csv --seed 0
```

O número de threads é passado pelo argumento `--num_threads` (o padrão é 4):

```
./random-forest wdbc.csv --seed 0 --num_threads 8
```

O programa imprime o tempo total no `stdout` e a fração F no `stderr`:

```
./random-forest wdbc.csv --seed 0 > stdout.txt 2> stderr.txt
```

## Instrumentação

O tempo total é medido com a função `get_time()` (`utils/utils.c`, usa
`clock_gettime`) do começo ao fim do `main`. Antes ele era medido com `clock()` só em
volta do `cross_validate`, mas o `clock()` soma o tempo de CPU de todas as threads, então
não serve para o programa paralelo, e a leitura do csv e o pivot ficavam de fora da conta.

O tempo paralelizável fica na variável global `parallelizable_time` (`utils/utils.c`).
Cada trecho paralelizado marca o tempo antes de criar as threads e depois do último
`pthread_join`, e soma a diferença nela:

```c
double start = get_time();
/* pthread_create ... pthread_join */
parallelizable_time += get_time() - start;
```

Quem soma é sempre a thread principal, então a variável não precisa de mutex.

No fim o `main` escreve `F = parallelizable_time / tempo total` no `stderr` com 4 casas.

A linha que imprime a acurácia está comentada no `main.c`. Para conferir a acurácia é só
descomentar.

## Paralelização

As quatro funções seguem o mesmo esquema:

- uma struct com os argumentos da thread, incluindo o intervalo `start`/`end` que ela
  vai processar;
- uma função que a thread executa, com o laço que antes ficava na função original;
- na função original, um laço com `pthread_create` e outro com `pthread_join`.

O intervalo de cada thread `t` é calculado assim, sendo `n` o total de iterações:

```c
start = t * n / num_threads;
end = (t + 1) * n / num_threads;
```

Dessa forma os blocos ficam do mesmo tamanho (ou com diferença de 1) e funciona mesmo
quando `n` não é múltiplo do número de threads.

### pivot_data (`utils/data.c`)

1. A função copia o vetor `data` para a matriz `pivoted_data`, linha por linha.
2. Cada iteração do laço de fora escreve só na linha `i` da matriz e só lê do `data`,
   então uma iteração não depende da outra.
3. O laço foi para a função `pivot_rows`, que copia as linhas de `start` até `end`. Os
   argumentos vão na struct `pivot_args`.
4. O `pivot_data` divide as linhas entre as threads, cria todas e espera terminarem.
5. A alocação (`_2d_calloc`) continua sequencial e fica fora do tempo medido.

### parse_csv (`utils/data.c`)

1. Do jeito que estava não dava para paralelizar direto: o `fgets` lê o arquivo em
   sequência, o `strtok` guarda estado global e o `idx` era incrementado a cada valor
   lido, então cada linha dependia da anterior.
2. A função foi separada em duas partes. A primeira continua sequencial: lê o arquivo
   para um buffer só e guarda no vetor `lines` o ponteiro para o começo de cada linha.
3. A segunda parte é a conversão do texto para `double`, que foi para a função
   `parse_lines` (struct `parse_args`). Cada thread converte um bloco de linhas.
4. O `idx` deixou de ser um contador compartilhado. Como todas as linhas têm o mesmo
   número de colunas, a linha `i` começa sempre em `i * cols`, e cada thread escreve em
   um pedaço diferente do `data`.
5. O `strtok` foi trocado pelo `strtok_r`, que guarda o estado em uma variável local
   (`save`) em vez de global.
6. Só a parte da conversão entra no tempo paralelizável, a leitura do arquivo não.

### train_model (`model/forest.c`)

1. O laço treina uma árvore por iteração e guarda em `random_forest[i]`. As árvores só
   leem os dados de treino, então podem ser treinadas ao mesmo tempo.
2. Tinha duas coisas compartilhadas entre as iterações: o `rand()` e o contador
   `nodeId`.
3. `rand()`: usa um estado global e não pode ser chamado por várias threads. Antes de
   criar as threads, ainda de forma sequencial, é sorteada uma seed para cada árvore com
   `rand()`. Dentro da árvore o sorteio das features usa `rand_r(seed)`. Para isso o
   ponteiro `seed` foi adicionado como parâmetro em `train_model_tree`,
   `calculate_best_data_split` e `grow`.
4. `nodeId`: virou uma variável local, um contador por árvore. Ele só é usado para
   debug, a diferença é que agora os ids recomeçam do zero em cada árvore.
5. O laço foi para a função `train_trees` (struct `train_args`). Cada thread treina um
   bloco de árvores e escreve só nas suas posições do `random_forest`.
6. O tempo entre criar as threads e o último `join` é somado no tempo paralelizável.

Como a seed é sorteada por árvore, o resultado é o mesmo com qualquer número de threads.
Mas a sequência de números aleatórios não é mais a mesma da versão serial, então a
acurácia com `--seed 0` mudou de 95.357143 para 94.642857.

### combine_arrays (`utils/utils.c`)

1. A função junta os ponteiros de linha de `first` e `second` em um vetor só, usando um
   contador `row_index++` compartilhado pelos dois laços.
2. O contador foi removido e os dois laços viraram um só, de `0` até `n1 + n2`: se
   `i < n1` copia de `first[i]`, senão copia de `second[i - n1]`. Assim cada iteração já
   sabe onde escreve.
3. Esse laço foi para a função `combine_rows` (struct `combine_args`), e o
   `combine_arrays` divide o intervalo entre as threads.
4. A função é chamada pelo `grow`, que já roda dentro das threads do `train_model`.
   Criar mais threads ali dentro só atrapalharia, e o tempo já está contado no
   `train_model`. Por isso o `train_model` liga a variável `inside_train_model` enquanto
   as threads dele estão rodando, e nesse caso o `combine_arrays` chama o `combine_rows`
   direto, sem criar threads e sem somar o tempo de novo.

## Mudanças no Makefile

- `-pthread` para compilar e linkar com a biblioteca de threads.
- `-D_POSIX_C_SOURCE=200809L` porque o `rand_r`, o `strtok_r` e o `clock_gettime` são
  funções POSIX e não são declaradas com `-std=c99` puro.

## Tempos

Medidos com `--seed 0` em uma máquina de 8 núcleos, uma execução de cada:

| threads | tempo total (s) | F      |
|---------|-----------------|--------|
| 1       | 10.78           | 0.9997 |
| 2       | 6.04            | 0.9996 |
| 4       | 3.34            | 0.9995 |
| 8       | 3.14            | 0.9994 |

A versão serial original leva em torno de 10.6 s.
