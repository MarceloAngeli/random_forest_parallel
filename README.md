# random_forest_parallel

Random Forest em C com validação cruzada, paralelizado com OpenMP.

## Compilar e rodar

```
make
./random-forest wdbc.csv --seed 0
```

O número de threads é escolhido pela variável `OMP_NUM_THREADS`:

```
OMP_NUM_THREADS=4 ./random-forest wdbc.csv --seed 0
```

O programa imprime o tempo total no `stdout` e a fração F no `stderr`:

```
./random-forest wdbc.csv --seed 0 > stdout.txt 2> stderr.txt
```

## Instrumentação

O tempo total é medido com `omp_get_wtime()` do começo ao fim do `main`. Antes ele era
medido com `clock()` só em volta do `cross_validate`, mas o `clock()` soma o tempo de CPU
de todas as threads, então não serve para o programa paralelo, e a leitura do csv e o
pivot ficavam de fora da conta.

O tempo paralelizável fica na variável global `parallelizable_time` (`utils/utils.c`).
Cada trecho paralelizado marca o tempo antes e depois e soma a diferença nela:

```c
double start = omp_get_wtime();
/* trecho paralelizado */
parallelizable_time += omp_get_wtime() - start;
```

No fim o `main` escreve `F = parallelizable_time / tempo total` no `stderr` com 4 casas.

A linha que imprime a acurácia está comentada no `main.c`. Para conferir a acurácia é só
descomentar.

## Paralelização

### pivot_data (`utils/data.c`)

1. A função copia o vetor `data` para a matriz `pivoted_data`, linha por linha.
2. Cada iteração do laço de fora escreve só na linha `i` da matriz e só lê do `data`,
   então uma iteração não depende da outra.
3. Foi colocado um `#pragma omp parallel for` no laço de fora. O `j` é declarado dentro
   do laço, então já é privado de cada thread.
4. A alocação (`_2d_calloc`) continua sequencial e fica fora do tempo medido.

### parse_csv (`utils/data.c`)

1. Do jeito que estava não dava para paralelizar direto: o `fgets` lê o arquivo em
   sequência, o `strtok` guarda estado global e o `idx` era incrementado a cada valor
   lido, então cada linha dependia da anterior.
2. A função foi separada em duas partes. A primeira continua sequencial: lê o arquivo
   para um buffer só e guarda no vetor `lines` o ponteiro para o começo de cada linha.
3. A segunda parte é a conversão do texto para `double`, que é a parte paralelizada com
   `#pragma omp parallel for`, uma linha do csv por iteração.
4. O `idx` deixou de ser um contador compartilhado. Como todas as linhas têm o mesmo
   número de colunas, a linha `i` começa sempre em `i * cols`, e cada thread escreve em
   um pedaço diferente do `data`.
5. O `strtok` foi trocado pelo `strtok_r`, que guarda o estado em uma variável local
   (`save`) em vez de global.
6. Só o laço de conversão entra no tempo paralelizável, a leitura do arquivo não.

### train_model (`model/forest.c`)

1. O laço treina uma árvore por iteração e guarda em `random_forest[i]`. As árvores só
   leem os dados de treino, então podem ser treinadas ao mesmo tempo.
2. Tinha duas coisas compartilhadas entre as iterações: o `rand()` e o contador
   `nodeId`.
3. `rand()`: usa um estado global e não pode ser chamado por várias threads. Antes do
   laço, ainda de forma sequencial, é sorteada uma seed para cada árvore com `rand()`.
   Dentro da árvore o sorteio das features usa `rand_r(seed)`. Para isso o ponteiro
   `seed` foi adicionado como parâmetro em `train_model_tree`, `calculate_best_data_split`
   e `grow`.
4. `nodeId`: virou uma variável local do laço, um contador por árvore. Ele só é usado
   para debug, a diferença é que agora os ids recomeçam do zero em cada árvore.
5. Foi colocado `#pragma omp parallel for schedule(dynamic)` no laço. O `dynamic` é
   porque as árvores não demoram o mesmo tempo para treinar.
6. O tempo do laço inteiro é somado no tempo paralelizável.

Como a seed é sorteada por árvore, o resultado é o mesmo com qualquer número de threads.
Mas a sequência de números aleatórios não é mais a mesma da versão serial, então a
acurácia com `--seed 0` mudou de 95.357143 para 94.642857.

### combine_arrays (`utils/utils.c`)

1. A função junta os ponteiros de linha de `first` e `second` em um vetor só, usando um
   contador `row_index++` compartilhado pelos dois laços.
2. O contador foi removido: as linhas de `first` vão para as posições `i` e as de
   `second` para `n1 + j`. Assim cada iteração já sabe onde escreve.
3. Cada um dos dois laços recebeu um `#pragma omp parallel for`.
4. A função é chamada pelo `grow`, que roda dentro da região paralela do `train_model`.
   Nesse caso o OpenMP executa os laços com uma thread só (paralelismo aninhado vem
   desligado por padrão) e o tempo já está contado no `train_model`. Por isso o tempo só
   é somado quando `omp_in_parallel()` é falso, para não contar duas vezes.

## Mudanças no Makefile

- `-fopenmp` para habilitar o OpenMP.
- `-D_POSIX_C_SOURCE=200809L` porque o `rand_r` e o `strtok_r` são funções POSIX e não
  são declaradas com `-std=c99` puro.
