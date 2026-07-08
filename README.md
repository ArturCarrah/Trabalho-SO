# Participantes 

Artur Carrah 

Artur Melo

Davi Maurício

André Peixoto

João Pedro Rolim Ximenes



# memmonitor

Daemon de monitoramento de page faults por processo (`monitor.c`) + gerador
de carga de teste (`stress.c`).

## Compilação

```bash
make
```

Gera dois binários: `monitor` e `stress`.

## Como funciona o monitor

1. A thread principal varre `/proc` a cada `--scan` segundos, listando os
   diretórios numéricos (PIDs).
2. Para cada PID novo, cria uma thread dedicada (destacada/`detached`) que:
   - lê `/proc/[pid]/stat` a cada `--sample` segundos;
   - extrai `minflt` (minor faults) e `majflt` (major faults) — campos 10 e
     12 do arquivo, logo após o `comm` entre parênteses;
   - calcula a variação (delta) entre duas amostras e converte para
     taxa/segundo;
   - se a taxa ultrapassa o threshold, loga `ANOMALIA` e, se `--block`
     estiver ativo, envia `SIGSTOP` ao processo (reversível com
     `kill -CONT <pid>`);
   - encerra sozinha quando detecta que o processo morreu
     (`kill(pid, 0)` retornando `ESRCH`).
3. Uma tabela protegida por mutex evita criar threads duplicadas para o
   mesmo PID.

### Parâmetros

| Flag           | Padrão | Descrição                                  |
|----------------|--------|---------------------------------------------|
| `--scan N`     | 2      | Intervalo (s) entre varreduras de `/proc`    |
| `--sample N`   | 1      | Intervalo (s) entre amostras por thread      |
| `--minflt-th N`| 20000  | Threshold de minor faults/s                  |
| `--majflt-th N`| 200    | Threshold de major faults/s                  |
| `--block`      | off    | Envia SIGSTOP ao detectar anomalia           |

## Como funciona o stress

Aloca uma região anônima com `mmap`, escreve byte a byte em cada página
(forçando o *primeiro* page fault) e depois chama `madvise(MADV_DONTNEED)`
para descartar as páginas físicas — a próxima passada gera fault de novo.
Repete isso indefinidamente, opcionalmente em várias threads.

```bash
./stress [num_threads] [tamanho_MB_por_thread]
# ex: ./stress 4 64
```

## Testando na VM

Terminal 1 (como root, ou mesmo usuário que vai rodar o stress):

```bash
sudo ./monitor --scan 1 --sample 1 --minflt-th 5000 --block
```

Terminal 2:

```bash
./stress 2 32
```

Em ~1-2s o monitor deve reportar `ANOMALIA` para o PID do `stress` e, com
`--block`, congelá-lo (confirme com `ps -o pid,stat,cmd -p <pid>`, o
estado deve mostrar `T`). Para retomar:

```bash
kill -CONT <pid>
```

## Limitações conhecidas / pontos de evolução

- **Escalabilidade do modelo thread-por-PID**: em uma máquina com muitos
  processos ativos, isso pode esbarrar no limite de threads do sistema
  (`ulimit -u`, `pid_max`). Para produção, um único loop que itera sobre
  a lista de PIDs seria mais econômico — o modelo atual foi escolhido
  porque é o que você especificou (thread dedicada por processo).
- **Falsos positivos**: processos legítimos com uso intenso de memória
  (bancos de dados, compiladores, `dd`) podem disparar o threshold. Vale
  calibrar `--minflt-th`/`--majflt-th` empiricamente no seu ambiente, ou
  evoluir para um modelo de baseline por processo (média móvel) em vez de
  threshold fixo.
- **Sinalização**: `kill()` exige mesmo UID do processo alvo, ou root.
  Rode o monitor como root para poder agir sobre qualquer processo.



# forkbomb_monitor

Daemon de monitoramento e contenção de *fork bombs*. O sistema monitora
continuamente os processos ativos da máquina, identifica padrões anormais de
criação de processos e atua automaticamente para impedir sua propagação.

## Como funciona o monitor

1. O processo principal é transformado em um daemon e inicia seu loop de
   monitoramento contínuo.

2. Durante cada ciclo, o daemon acessa o diretório virtual `/proc`, que contém
   informações sobre todos os processos em execução no sistema.

3. Para cada processo encontrado:
   - obtém seu PID através do nome do diretório dentro de `/proc`;
   - acessa o arquivo `/proc/[pid]/comm`, que contém o nome do processo;
   - associa o nome do processo ao seu respectivo PID.

4. As informações coletadas são armazenadas em uma estrutura baseada em um
   `map` de vetores, permitindo relacionar cada nome de processo a uma lista
   de PIDs:



  
- **Não distingue write faults de read faults**: `minflt`/`majflt` contam
  qualquer tipo de fault. Se quiser algo mais fino (só escritas), a
  alternativa seria instrumentar via `perf_event_open` com o evento
  `PERF_COUNT_SW_PAGE_FAULTS` — mais complexo, mas dá contadores por
  processo com mais controle.
