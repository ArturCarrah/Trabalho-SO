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



# Anti Bomb

Daemon de monitoramento e contenção de *fork bombs*. O sistema monitora
continuamente os processos ativos da máquina, identifica padrões anormais de
criação de processos e atua automaticamente para impedir sua propagação.

## Compilação

```bash
make
```

## Testando na VM

Terminal, utilizando o Anti Bomb:

```bash
./anti_bomb
```

Terminal, gerando um fork bomb de exemplo:

```bash
:(){ :|:& };:
```

### Nota: Outros forks bombs podem ser gerados, no trabalho, foi gerado um processo que faz um fork() em um while(true).

## Como funciona:

1. **Inicialização do Daemon:** O processo principal desvincula-se do terminal, transforma-se em um *daemon* e inicia seu loop de monitoramento contínuo em segundo plano.
2. **Varredura do Sistema:** A cada ciclo de processamento, o *daemon* acessa o diretório virtual `/proc`, que centraliza as informações de todos os processos ativos no ambiente Linux.
3. **Extração de Metadados:** Para cada diretório de processo encontrado, o sistema realiza duas ações:
   - Identifica o **PID** (ID do processo) através do nome do próprio diretório.
   - Lê o arquivo `/proc/[pid]/comm` para obter o **nome do processo** associado.
4. **Indexação em Memória:** As informações coletadas são estruturadas em um mapa de vetores (`std::map<std::string, std::vector<pid_t>>`), vinculando cada nome de processo exclusivo à sua respectiva lista de PIDs ativos.
5. **Análise de Threshold (Limite):** A cada nova inserção no mapa, o *daemon* avalia o tamanho do vetor do processo correspondente. Caso o volume ultrapasse o limite de segurança configurado (atualmente fixado em **20 PIDs** com o mesmo nome), o loop de varredura é interrompido e o nome do processo é marcado como um **alvo (ameaça)**.
6. **Contenção Imediata (Bloqueio):** Com o alvo identificado, o *daemon* eleva sua própria prioridade de execução ao nível máximo permitido pelo sistema e realiza uma nova varredura no `\proc`. Para cada instância do processo invasor localizada:
   - Envia o sinal `SIGPAUSE` (ou `SIGSTOP`) para **congelar o processo**, impedindo instantaneamente a sua autorreplicação e propagação.
   - Armazena o PID do processo paralisado em uma lista negra de contenção.
7. **Eliminação da Ameaça:** Após certificar-se de que todas as ramificações do *fork bomb* estão congeladas e seguras, o *daemon* percorre a lista negra enviando o sinal `SIGKILL` para cada PID, eliminando a ameaça do sistema de forma definitiva.
8. **Restauramento do Ciclo:** Concluída a purga, o *daemon* limpa as estruturas de dados temporárias, redefine sua prioridade de execução para o nível normal e retoma a rotina padrão de varredura no diretório `/proc`.

### Nota: Caso quem faça o fork bomb seja o bash, então uma excessão é aberta e o processo bash de menor PID não recebe o sinal `SIGKILL`, pois causaria instabilidade no sistema. 


## Fluxograma

<img width="8192" height="2792" alt="anti bomb fluxograma" src="https://github.com/user-attachments/assets/f2b5edb0-d016-42d4-aaa9-734d3e02bf6c" />


