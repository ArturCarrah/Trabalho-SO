#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <pthread.h>

#define PAGE_SIZE 4096
#define MAX_THREADS 64

static size_t g_region_size = 64UL * 1024 * 1024; /* 64 MB por thread */

static void *bomb(void *arg) {
    (void)arg;

    void *addr = mmap(NULL, g_region_size, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (addr == MAP_FAILED) {
        perror("mmap");
        return NULL;
    }

    volatile char *mem = (volatile char *)addr;

    while (1) {
        for (size_t off = 0; off < g_region_size; off += PAGE_SIZE) {
            mem[off] = (char)(off & 0xFF); /* força o page fault na 1a escrita */
        }
        /* descarta as páginas físicas: a próxima escrita gera fault de novo */
        madvise(addr, g_region_size, MADV_DONTNEED);
    }

    return NULL; /* nunca alcançado */
}

int main(int argc, char **argv) {
    int nthreads = (argc > 1) ? atoi(argv[1]) : 4;
    if (nthreads < 1) nthreads = 1;
    if (nthreads > MAX_THREADS) nthreads = MAX_THREADS;

    if (argc > 2) {
        long mb = atol(argv[2]);
        if (mb > 0) g_region_size = (size_t)mb * 1024 * 1024;
    }

    printf("stress: pid=%d threads=%d regiao_por_thread=%zuMB\n",
           getpid(), nthreads, g_region_size / (1024 * 1024));
    printf("Pressione Ctrl+C para encerrar.\n");
    fflush(stdout);

    pthread_t threads[MAX_THREADS];
    for (int i = 0; i < nthreads; i++) {
        if (pthread_create(&threads[i], NULL, bomb, NULL) != 0) {
            fprintf(stderr, "falha ao criar thread %d\n", i);
        }
    }
    for (int i = 0; i < nthreads; i++) {
        pthread_join(threads[i], NULL);
    }

    return 0;
}
