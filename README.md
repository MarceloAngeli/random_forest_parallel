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
O único trecho paralelo é o laço dos folds no `cross_validate` (`eval/eval.c`), que marca
o tempo antes de criar as threads e depois do último `pthread_join`, e soma a diferença
nela:

```c
double start = get_time();
/* pthread_create ... pthread_join */
parallelizable_time += get_time() - start;
```

Quem soma é a thread principal, então a variável não precisa de mutex. Os timers que a
versão serial tinha no `parse_csv`, `pivot_data`, `train_model` e `combine_arrays` foram
removidos: os dois últimos agora rodam dentro das threads e somar na mesma variável seria
uma condição de corrida, e os dois primeiros continuam sequenciais.

No fim o `main` escreve `F = parallelizable_time / tempo total` no `stderr` com 4 casas.

A linha que imprime a acurácia está comentada no `main.c`. Para conferir a acurácia é só
descomentar.

## Paralelização

Só o `cross_validate` (`eval/eval.c`) é paralelo: cada thread roda um bloco de folds. O
resto do programa (`parse_csv`, `pivot_data`, `train_model`, `combine_arrays`) é igual ao
da versão serial otimizada.

1. O laço do `cross_validate` treina e avalia um modelo por fold. Um fold só lê os dados
   e não depende do resultado dos outros, então os folds podem rodar ao mesmo tempo.
2. O corpo do laço foi para a função `run_folds`, que roda os folds de `start` até `end`.
   Os argumentos vão na struct `fold_args`.
3. O `cross_validate` divide os folds entre as threads, cria todas e espera terminarem.
   O intervalo da thread `t`, sendo `k_folds` o total de folds, é:

   ```c
   start = t * k_folds / num_threads;
   end = (t + 1) * k_folds / num_threads;
   ```

   Assim os blocos ficam do mesmo tamanho (ou com diferença de 1) e funciona mesmo quando
   `k_folds` não é múltiplo do número de threads.
4. Tinha duas coisas compartilhadas entre as iterações: o `rand()` e a soma das acurácias.
5. `rand()`: usa um estado global e não pode ser chamado por várias threads. Antes de
   criar as threads, ainda de forma sequencial, é sorteada uma seed para cada fold com
   `rand()`. Dentro do fold o sorteio das features usa `rand_r`. A seed vai no
   `ModelContext` (campo `seed`), que já era passado para todas as funções da árvore,
   então nenhuma assinatura mudou. No `calculate_best_data_split` (`model/tree.c`) a
   única linha alterada é a do sorteio: `rand()` virou `rand_r(ctx->seed)`.
6. Soma das acurácias: cada thread escreve a acurácia do fold na posição dele do vetor
   `accuracies`, e a soma é feita depois do último `join`, na ordem dos folds. Não
   precisa de mutex.
7. O `nodeId` do `train_model` já era uma variável local, então cada fold tem o seu.

Como a seed é sorteada por fold e a soma é feita em ordem, o resultado é o mesmo com
qualquer número de threads. Mas a sequência de números aleatórios não é mais a mesma da
versão serial, então a acurácia com `--seed 0` não é igual à da serial.

### Limite do speedup

São 20 folds divididos em blocos, e o tempo é o da thread com o maior bloco. O speedup
máximo é `20 / (maior bloco)`:

| threads         | 2  | 4 | 6 | 8    | 10 a 16 |
|-----------------|----|---|---|------|---------|
| maior bloco     | 10 | 5 | 4 | 3    | 2       |
| speedup máximo  | 2  | 4 | 5 | 6,67 | 10      |

De 10 a 16 threads sempre sobra uma thread com 2 folds, por isso o tempo para de cair.

## Mudanças no Makefile

- `-pthread` para compilar e linkar com a biblioteca de threads.
- `-D_POSIX_C_SOURCE=200809L` porque o `rand_r` e o `clock_gettime` são funções POSIX e
  não são declaradas com `-std=c99` puro. Por isso saiu o `#define _POSIX_C_SOURCE` que
  a versão serial tinha no topo do `utils.c` e do `data.c`.

## Tempos

Medidos no `rocks-node-1` (16 núcleos) com `--seed 0`: 33 execuções para cada N, as 2
primeiras descartadas, mediana (TR) e intervalo de confiança de 95% por bootstrap com
B = 10000. A versão serial otimizada leva 37,5546 s no mesmo nó.

| N      | 2       | 4      | 6      | 8      | 10     | 12     | 14     | 16     |
|--------|---------|--------|--------|--------|--------|--------|--------|--------|
| TR (s) | 18,4262 | 9,4854 | 7,4638 | 5,7534 | 3,8434 | 3,8332 | 3,8472 | 3,8151 |
| lb     | 18,1946 | 9,3894 | 7,3569 | 5,7056 | 3,8254 | 3,8186 | 3,8056 | 3,7826 |
| ub     | 18,6081 | 9,5559 | 7,5301 | 5,7880 | 3,8505 | 3,8517 | 3,8606 | 3,8491 |
| speedup| 2,04    | 3,96   | 5,03   | 6,53   | 9,77   | 9,80   | 9,76   | 9,84   |
