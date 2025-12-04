// Servidor Periódico para Tarefas Aperiódicas
// Implementação conforme Parte 2 do Trabalho M3
// 
// Arquitetura:
// - Fila de jobs aperiódicos (thread-safe)
// - Servidor periódico com período Ts e budget Cs
// - Tarefas aperiódicas encadeiam jobs na fila
// - Servidor consome jobs respeitando o budget por período

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>
#include <sched.h>

#define TAG "SERVER"

// ====== Tipo de função para jobs ======
typedef void (*job_func_t)(void *arg);

// ====== Nó da fila de jobs ======
typedef struct job {
    job_func_t func;
    void *arg;
    int64_t arrival_ns;  // timestamp de chegada
    struct job *next;
} job_t;

// ====== Fila de requisições aperiódicas ======
static job_t *queue_head = NULL;
static job_t *queue_tail = NULL;
static pthread_mutex_t queue_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t queue_cond = PTHREAD_COND_INITIALIZER;

// ====== Estatísticas ======
typedef struct {
    uint32_t jobs_enqueued;
    uint32_t jobs_executed;
    uint32_t jobs_dropped;       // se fila estiver cheia
    uint32_t periods_executed;
    uint32_t periods_idle;       // períodos sem jobs
    int64_t total_response_ns;   // soma para calcular média
    int64_t max_response_ns;
    int64_t total_budget_used_ns;
    int64_t max_budget_used_ns;
} server_stats_t;

static server_stats_t stats = {0};
static pthread_mutex_t stats_mutex = PTHREAD_MUTEX_INITIALIZER;

// ====== Função auxiliar: tempo em nanosegundos ======
static inline int64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

// ====== Função auxiliar: adiciona ns a timespec ======
static void timespec_add_ns(struct timespec *t, long ns) {
    t->tv_nsec += ns;
    while (t->tv_nsec >= 1000000000L) {
        t->tv_nsec -= 1000000000L;
        t->tv_sec += 1;
    }
}

// ====== Enfileira um job (chamado pelas tarefas aperiódicas) ======
void enqueue_job(job_func_t f, void *arg) {
    job_t *j = malloc(sizeof(job_t));
    if (!j) {
        fprintf(stderr, "%s: malloc falhou\n", TAG);
        pthread_mutex_lock(&stats_mutex);
        stats.jobs_dropped++;
        pthread_mutex_unlock(&stats_mutex);
        return;
    }
    
    j->func = f;
    j->arg = arg;
    j->arrival_ns = now_ns();
    j->next = NULL;
    
    pthread_mutex_lock(&queue_mutex);
    if (queue_tail) {
        queue_tail->next = j;
    } else {
        queue_head = j;
    }
    queue_tail = j;
    pthread_cond_signal(&queue_cond);
    pthread_mutex_unlock(&queue_mutex);
    
    pthread_mutex_lock(&stats_mutex);
    stats.jobs_enqueued++;
    pthread_mutex_unlock(&stats_mutex);
}

// ====== Retira um job da fila (usado pelo servidor) ======
static job_t *dequeue_job(void) {
    job_t *j = queue_head;
    if (!j) return NULL;
    
    queue_head = j->next;
    if (!queue_head) {
        queue_tail = NULL;
    }
    return j;
}

// ====== Parâmetros do servidor ======
typedef struct {
    long period_ns;  // Ts (ex: 10ms = 10*10^6 ns)
    long budget_ns;  // Cs (ex: 3ms = 3*10^6 ns)
    int priority;    // Prioridade RT
} server_params_t;

static volatile bool server_running = true;

// ====== Thread Servidor Periódico ======
void *server_thread(void *arg) {
    server_params_t *params = (server_params_t *)arg;
    long Ts = params->period_ns;
    long Cs = params->budget_ns;
    
    // Define prioridade RT
    struct sched_param sp;
    sp.sched_priority = params->priority;
    if (pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp) != 0) {
        fprintf(stderr, "%s: Erro ao definir prioridade RT\n", TAG);
    }
    
    printf("%s: Iniciado (Ts=%ld ms, Cs=%ld ms, prio=%d)\n",
           TAG, Ts/1000000, Cs/1000000, params->priority);
    
    struct timespec next_release;
    clock_gettime(CLOCK_MONOTONIC, &next_release);
    
    while (server_running) {
        // Define momento da próxima ativação
        timespec_add_ns(&next_release, Ts);
        
        // Início do período
        int64_t period_start_ns = now_ns();
        int64_t consumed_ns = 0;
        bool had_jobs = false;
        
        while (consumed_ns < Cs && server_running) {
            // Pega um job, se existir
            pthread_mutex_lock(&queue_mutex);
            
            // Se não há jobs, sai do loop de serviço
            if (!queue_head) {
                pthread_mutex_unlock(&queue_mutex);
                break;
            }
            
            job_t *j = dequeue_job();
            pthread_mutex_unlock(&queue_mutex);
            
            if (!j) break;
            had_jobs = true;
            
            // Mede tempo antes/depois de executar o job
            int64_t t_before = now_ns();
            j->func(j->arg);  // Executa requisição aperiódica
            int64_t t_after = now_ns();
            
            // Calcula resposta e atualiza estatísticas
            int64_t response_ns = t_after - j->arrival_ns;
            int64_t dt = t_after - t_before;
            
            pthread_mutex_lock(&stats_mutex);
            stats.jobs_executed++;
            stats.total_response_ns += response_ns;
            if (response_ns > stats.max_response_ns) {
                stats.max_response_ns = response_ns;
            }
            pthread_mutex_unlock(&stats_mutex);
            
            free(j);
            
            // Atualiza orçamento consumido
            consumed_ns += dt;
            
            if (consumed_ns >= Cs) {
                // Orçamento esgotado neste período
                break;
            }
        }
        
        // Estatísticas do período
        pthread_mutex_lock(&stats_mutex);
        stats.periods_executed++;
        if (!had_jobs) {
            stats.periods_idle++;
        }
        stats.total_budget_used_ns += consumed_ns;
        if (consumed_ns > stats.max_budget_used_ns) {
            stats.max_budget_used_ns = consumed_ns;
        }
        pthread_mutex_unlock(&stats_mutex);
        
        // Dorme até o instante absoluto da próxima liberação
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next_release, NULL);
    }
    
    printf("%s: Finalizado\n", TAG);
    return NULL;
}

// ====== Cria e inicia o servidor ======
pthread_t start_server_thread(long period_ms, long budget_ms, int priority) {
    pthread_t th;
    pthread_attr_t attr;
    
    pthread_attr_init(&attr);
    pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
    pthread_attr_setschedpolicy(&attr, SCHED_FIFO);
    
    struct sched_param sp;
    sp.sched_priority = priority;
    pthread_attr_setschedparam(&attr, &sp);
    
    static server_params_t params;
    params.period_ns = period_ms * 1000000L;
    params.budget_ns = budget_ms * 1000000L;
    params.priority = priority;
    
    if (pthread_create(&th, &attr, server_thread, &params) != 0) {
        fprintf(stderr, "%s: Erro ao criar thread\n", TAG);
        pthread_attr_destroy(&attr);
        return 0;
    }
    
    pthread_attr_destroy(&attr);
    return th;
}

// ====== Imprime estatísticas ======
void print_server_stats(void) {
    pthread_mutex_lock(&stats_mutex);
    
    printf("\n=== Estatísticas do Servidor Periódico ===\n");
    printf("Jobs enfileirados:  %u\n", stats.jobs_enqueued);
    printf("Jobs executados:    %u\n", stats.jobs_executed);
    printf("Jobs perdidos:      %u\n", stats.jobs_dropped);
    printf("Períodos executados: %u\n", stats.periods_executed);
    printf("Períodos ociosos:   %u (%.1f%%)\n",
           stats.periods_idle,
           stats.periods_executed > 0 ? 
           (100.0 * stats.periods_idle / stats.periods_executed) : 0.0);
    
    if (stats.jobs_executed > 0) {
        int64_t avg_response_ns = stats.total_response_ns / stats.jobs_executed;
        printf("Resposta média:     %.3f ms\n", avg_response_ns / 1000000.0);
        printf("Resposta máxima:    %.3f ms\n", stats.max_response_ns / 1000000.0);
    }
    
    if (stats.periods_executed > 0) {
        int64_t avg_budget_ns = stats.total_budget_used_ns / stats.periods_executed;
        printf("Budget médio usado: %.3f ms\n", avg_budget_ns / 1000000.0);
        printf("Budget máximo usado: %.3f ms\n", stats.max_budget_used_ns / 1000000.0);
    }
    
    printf("==========================================\n\n");
    
    pthread_mutex_unlock(&stats_mutex);
}

// ====== Exemplo de job aperiódico ======
void exemplo_job_simples(void *arg) {
    int id = *(int *)arg;
    printf("  [JOB %d] Processando...\n", id);
    
    // Simula processamento (1-3 ms)
    struct timespec delay = {
        .tv_sec = 0,
        .tv_nsec = (1000000 + (rand() % 2000000))  // 1-3 ms
    };
    nanosleep(&delay, NULL);
    
    printf("  [JOB %d] Concluído\n", id);
    free(arg);
}

// ====== Exemplo de job com computação pesada ======
void exemplo_job_pesado(void *arg) {
    int id = *(int *)arg;
    printf("  [JOB PESADO %d] Iniciando...\n", id);
    
    // Simula processamento pesado (3-5 ms)
    int64_t start = now_ns();
    volatile long sum = 0;
    while ((now_ns() - start) < (3000000 + (rand() % 2000000))) {
        sum += rand();
    }
    
    printf("  [JOB PESADO %d] Finalizado (sum=%ld)\n", id, sum);
    free(arg);
}

// ====== Thread geradora de requisições aperiódicas ======
void *aperiodic_request_generator(void *arg) {
    (void)arg;
    
    printf("Gerador: Iniciando (pressione Ctrl+C para parar)\n");
    
    int job_counter = 0;
    
    while (server_running) {
        // Espera aleatória entre 50-500 ms
        int delay_ms = 50 + (rand() % 450);
        usleep(delay_ms * 1000);
        
        // 70% jobs simples, 30% jobs pesados
        bool heavy = (rand() % 100) < 30;
        
        int *id = malloc(sizeof(int));
        *id = ++job_counter;
        
        if (heavy) {
            enqueue_job(exemplo_job_pesado, id);
            printf("Gerador: Job pesado #%d enfileirado\n", *id);
        } else {
            enqueue_job(exemplo_job_simples, id);
            printf("Gerador: Job simples #%d enfileirado\n", *id);
        }
    }
    
    return NULL;
}

// ====== Main de teste ======
int main(int argc, char *argv[]) {
    printf("=== Servidor Periódico para Tarefas Aperiódicas ===\n\n");
    
    // Parâmetros padrão
    long Ts_ms = 10;  // Período: 10 ms
    long Cs_ms = 5;   // Budget: 5 ms (50% de utilização)
    int prio = 70;
    int duration_s = 30;
    
    // Parse argumentos
    if (argc >= 3) {
        Ts_ms = atol(argv[1]);
        Cs_ms = atol(argv[2]);
    }
    if (argc >= 4) {
        prio = atoi(argv[3]);
    }
    if (argc >= 5) {
        duration_s = atoi(argv[4]);
    }
    
    printf("Configuração:\n");
    printf("  Ts (período):     %ld ms\n", Ts_ms);
    printf("  Cs (budget):      %ld ms\n", Cs_ms);
    printf("  Utilização máx:   %.1f%%\n", (100.0 * Cs_ms / Ts_ms));
    printf("  Prioridade RT:    %d\n", prio);
    printf("  Duração:          %d s\n\n", duration_s);
    
    if (Cs_ms > Ts_ms) {
        fprintf(stderr, "ERRO: Budget não pode ser maior que o período!\n");
        return 1;
    }
    
    // Inicializa gerador aleatório
    srand(time(NULL));
    
    // Inicia servidor
    pthread_t server = start_server_thread(Ts_ms, Cs_ms, prio);
    if (!server) {
        fprintf(stderr, "Erro ao iniciar servidor\n");
        return 1;
    }
    
    // Inicia gerador de requisições
    pthread_t generator;
    pthread_create(&generator, NULL, aperiodic_request_generator, NULL);
    
    // Aguarda duração especificada
    for (int i = 0; i < duration_s; i++) {
        sleep(1);
        printf("\n--- %d segundos decorridos ---\n", i + 1);
        print_server_stats();
    }
    
    // Finaliza
    printf("\nFinalizando...\n");
    server_running = false;
    pthread_cond_broadcast(&queue_cond);
    
    pthread_join(generator, NULL);
    pthread_join(server, NULL);
    
    // Estatísticas finais
    print_server_stats();
    
    // Limpa fila restante
    pthread_mutex_lock(&queue_mutex);
    while (queue_head) {
        job_t *j = dequeue_job();
        if (j->arg) free(j->arg);
        free(j);
    }
    pthread_mutex_unlock(&queue_mutex);
    
    printf("Finalizado.\n");
    return 0;
}
