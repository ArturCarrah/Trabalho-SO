#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <signal.h>
#include <pthread.h>
#include <time.h>
#include <ctype.h>
#include <errno.h>
#include <stdarg.h>

/* DIMENSIONAMENTO DE BUFFERS */
#define MAX_PIDRASTREADOS     4096 
#define PATH_BUF        64
#define SAIDA_BUF        1024
#define NOME_BUF        256

/* ESTADO DE CONFIGURAÇÃO  */
static int  g_scan_intervalo  = 2;   
static int  g_sample_intervalo = 1;   
static unsigned long g_minflt_threshold = 20000; 
static unsigned long g_majflt_threshold = 200;
static int  g_block_habilitado = 0;

/*  TABELA DE PIDS MONITORADOS */
typedef struct {
    pid_t pid;
    int   active;
} entrada_mapeada;

static entrada_mapeada g_mapeamento[MAX_PIDRASTREADOS];
static pthread_mutex_t g_mapeamento_mutex = PTHREAD_MUTEX_INITIALIZER;
static pid_t g_self_pid;

/* UTILITARIOS DA TABELA */
static int rastreio_tabela(pid_t pid) {
    for (int i = 0; i < MAX_PIDRASTREADOS; i++)
        if (g_mapeamento[i].active && g_mapeamento[i].pid == pid) return 1;
    return 0;
}

static int adicao_tabela(pid_t pid) {
    for (int i = 0; i < MAX_PIDRASTREADOS; i++) {
        if (!g_mapeamento[i].active) {
            g_mapeamento[i].active = 1;
            g_mapeamento[i].pid = pid;
            return 0;
        }
    }
    return -1;  
}

static void liberacao_tabela(pid_t pid) {
    for (int i = 0; i < MAX_PIDRASTREADOS; i++) {
        if (g_mapeamento[i].active && g_mapeamento[i].pid == pid) {
            g_mapeamento[i].active = 0;
            return;
        }
    }
}

/*  LEITURA DO ESTADO EM /proc/[pid]/stat  */
typedef struct {
    char comm[NOME_BUF]; /* Mencionar na hora da apresentação para prevenção de problema de parse aqui*/
    char state;
    unsigned long minflt;
    unsigned long cminflt;
    unsigned long majflt;
    unsigned long cmajflt;
} proc_stat_t;


static int ler_proc_stat(pid_t pid, proc_stat_t *out) {
    char path[PATH_BUF];
    snprintf(path, sizeof(path), "/proc/%d/stat", pid);

    FILE *f = fopen(path, "r");
    if (!f) return -1;

    char line[SAIDA_BUF];
    if (!fgets(line, sizeof(line), f)) { fclose(f); return -1; }
    fclose(f);

    /* GAMBIARRA SINISTRA*/
    char *lparen = strchr(line, '(');
    char *rparen = strrchr(line, ')');
    if (!lparen || !rparen || rparen < lparen) return -1;

    size_t comm_len = (size_t)(rparen - lparen - 1);
    if (comm_len >= NOME_BUF) comm_len = NOME_BUF - 1;
    memcpy(out->comm, lparen + 1, comm_len);
    out->comm[comm_len] = '\0';

    int matched = sscanf(rparen + 2,
        "%c %*d %*d %*d %*d %*d %*u %lu %lu %lu %lu",
        &out->state, &out->minflt, &out->cminflt,
        &out->majflt, &out->cmajflt);

    return (matched == 5) ? 0 : -1;
}

/*  PADRÃO DE LOG  */
static void log_msg(const char *fmt, ...) {
    time_t now = time(NULL);
    struct tm tm_buf;
    localtime_r(&now, &tm_buf);
    char ts[32];
    strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", &tm_buf);

    va_list ap;
    va_start(ap, fmt);
    printf("[%s] ", ts);
    vprintf(fmt, ap);
    printf("\n");
    fflush(stdout);
    va_end(ap);
}

/* ROTINA DE MONITORAMENTO P/ CADA THREAD  */
static void *monitor_pid_thread(void *arg) {
    pid_t pid = (pid_t)(intptr_t)arg;

    proc_stat_t prev, cur;
    int tem_prev = 0;

    while (1) {
        if (kill(pid, 0) == -1 && errno == ESRCH) break;

        if (ler_proc_stat(pid, &cur) != 0) break;

        if (tem_prev) {
            long delta_min = (long)(cur.minflt - prev.minflt);
            long delta_maj = (long)(cur.majflt - prev.majflt);
            if (delta_min < 0) delta_min = 0; 
            if (delta_maj < 0) delta_maj = 0;

            double rate_min = (double)delta_min / g_sample_intervalo;
            double rate_maj = (double)delta_maj / g_sample_intervalo;

            if (rate_min > g_minflt_threshold || rate_maj > g_majflt_threshold) {
                log_msg("ANOMALIA pid=%d comm=%s minflt/s=%.0f majflt/s=%.0f "
                        "(thresholds: %lu/%lu)",
                        pid, cur.comm, rate_min, rate_maj,
                        g_minflt_threshold, g_majflt_threshold);

                if (g_block_habilitado) {
                    if (kill(pid, SIGSTOP) == 0) {
                        log_msg("AÇÃO pid=%d bloqueado com SIGSTOP "
                                "(retomar manualmente com: kill -CONT %d)", pid, pid);
                    } else {
                        log_msg("AÇÃO pid=%d falha ao enviar SIGSTOP: %s",
                                pid, strerror(errno));
                    }
                }
            }
        }

        prev = cur;
        tem_prev = 1;
        sleep(g_sample_intervalo);
    }

    pthread_mutex_lock(&g_mapeamento_mutex);
    liberacao_tabela(pid);
    pthread_mutex_unlock(&g_mapeamento_mutex);

    return NULL;
}

/* Varredura de /proc */
static void scan_proc_and_spawn(void) {
    DIR *d = opendir("/proc");
    if (!d) { perror("opendir /proc"); return; }

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (!isdigit((unsigned char)ent->d_name[0])) continue;

        pid_t pid = (pid_t)atoi(ent->d_name);
        if (pid <= 0 || pid == g_self_pid) continue;

        pthread_mutex_lock(&g_mapeamento_mutex);
        int already = rastreio_tabela(pid);
        if (!already) {
            if (adicao_tabela(pid) == 0) already = -1; /* marcado para criar thread */
        }
        pthread_mutex_unlock(&g_mapeamento_mutex);

        if (already == -1) {
            pthread_t th;
            pthread_attr_t attr;
            pthread_attr_init(&attr);
            pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
            if (pthread_create(&th, &attr, monitor_pid_thread,
                                (void *)(intptr_t)pid) != 0) {
                pthread_mutex_lock(&g_mapeamento_mutex);
                liberacao_tabela(pid);
                pthread_mutex_unlock(&g_mapeamento_mutex);
            }
            pthread_attr_destroy(&attr);
        }
    }
    closedir(d);
}

/*  CLI  */
static void parse_args(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--block") == 0) {
            g_block_habilitado = 1;
        } else if (strcmp(argv[i], "--scan") == 0 && i + 1 < argc) {
            g_scan_intervalo = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--sample") == 0 && i + 1 < argc) {
            g_sample_intervalo = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--minflt-th") == 0 && i + 1 < argc) {
            g_minflt_threshold = strtoul(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--majflt-th") == 0 && i + 1 < argc) {
            g_majflt_threshold = strtoul(argv[++i], NULL, 10);
        } else {
            fprintf(stderr, "Argumento desconhecido: %s\n", argv[i]);
            exit(1);
        }
    }
}

int main(int argc, char **argv) {
    parse_args(argc, argv);
    g_self_pid = getpid();

    log_msg("monitor iniciado (pid=%d) scan=%ds sample=%ds "
            "minflt_th=%lu/s majflt_th=%lu/s block=%s",
            g_self_pid, g_scan_intervalo, g_sample_intervalo,
            g_minflt_threshold, g_majflt_threshold,
            g_block_habilitado ? "on" : "off");

    while (1) {
        scan_proc_and_spawn();
        sleep(g_scan_intervalo);
    }

    return 0;
}