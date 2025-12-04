// Esteira Industrial (Linux RTOS + POSIX Threads) — Instrumentação RT
// Adaptação do projeto ESP32+FreeRTOS para Linux com PREEMPT_RT
// 
// Tarefas:
// - ENC_SENSE (periódica 5 ms) -> notifica SPD_CTRL
// - SPD_CTRL (hard RT) -> controle PI simulado
// - SORT_ACT (hard RT, evento via stdin 'b') -> aciona "desviador"
// - SAFETY_TASK (hard RT, evento via stdin 'd') -> E-stop
// - STATS imprime métricas RT: releases, hard_miss, Cmax, Lmax, Rmax, (m,k)-firm
//
// Compilação: make
// Execução: sudo ./esteira_linux
// Comandos: b=OBJ  d=E-STOP  h=HMI  q=quit

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <pthread.h>
#include <semaphore.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>
#include <sys/mman.h>
#include <sched.h>
#include <errno.h>
#include <sys/time.h>
#include <fcntl.h>
#include <termios.h>
#include <sys/select.h>

#define TAG "ESTEIRA"

// ====== Periodicidade, prioridades ======
#define ENC_T_MS        5
#define PRIO_SAFE       90
#define PRIO_ENC        80
#define PRIO_CTRL       70
#define PRIO_SORT       60
#define PRIO_STATS      20

// ====== Deadlines (em microssegundos) ======
#define D_ENC_US    5000
#define D_CTRL_US  10000
#define D_SORT_US  10000
#define D_SAFE_US   5000

// ====== Handles/IPC ======
static pthread_t thENC, thCTRL, thSORT, thSAFE, thSTATS, thINPUT;
static sem_t semCtrlNotify;  // ENC -> CTRL
static sem_t semSort;        // stdin 'b' -> SORT
static sem_t semEStop;       // stdin 'd' -> SAFE
static sem_t semHMI;         // stdin 'h' -> soft RT
static volatile bool running = true;

// ====== Estado simulado da esteira ======
typedef struct {
    float rpm;
    float pos_mm;
    float set_rpm;
} belt_state_t;

static belt_state_t g_belt = { .rpm = 0.f, .pos_mm = 0.f, .set_rpm = 120.0f };
static pthread_mutex_t belt_mutex = PTHREAD_MUTEX_INITIALIZER;

// ====== Instrumentação de tempo/métricas ======
typedef struct {
    volatile uint32_t releases, starts, finishes;
    volatile uint32_t hard_miss, soft_miss;
    volatile int64_t  last_release_us, last_start_us, last_end_us;
    volatile int64_t  worst_exec_us, worst_latency_us, worst_response_us;

    #define RBUF 256
    volatile uint16_t r_count;
    volatile int32_t  r_buf[RBUF];

    volatile uint8_t  k_window;
    volatile uint8_t  win_filled;
    volatile uint16_t win_mask;

    volatile uint32_t preemptions;
    volatile int64_t  blocked_us_total;
} rt_stats_t;

static rt_stats_t st_enc  = { .k_window = 10 };
static rt_stats_t st_ctrl = { .k_window = 10 };
static rt_stats_t st_sort = { .k_window = 10 };
static rt_stats_t st_safe = { .k_window = 10 };

// ====== Função para obter tempo em microssegundos ======
static inline int64_t now_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000LL + ts.tv_nsec / 1000;
}

static inline int64_t now_us_epoch(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (int64_t)tv.tv_sec * 1000000LL + tv.tv_usec;
}

static inline void now_str(char *buf, size_t len) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    struct tm tm;
    localtime_r(&tv.tv_sec, &tm);
    int ms = (int)(tv.tv_usec / 1000);
    snprintf(buf, len, "%02d/%02d/%04d %02d:%02d:%02d.%03d",
             tm.tm_mday, tm.tm_mon + 1, tm.tm_year + 1900,
             tm.tm_hour, tm.tm_min, tm.tm_sec, ms);
}

// ====== Instrumentação ======
static inline void stats_on_release(rt_stats_t *s, int64_t t_rel) {
    s->releases++;
    s->last_release_us = t_rel;
}

static inline void stats_on_start(rt_stats_t *s, int64_t t_start) {
    s->starts++;
    s->last_start_us = t_start;
    int64_t lat = t_start - s->last_release_us;
    if (lat > s->worst_latency_us) s->worst_latency_us = lat;
}

static inline void stats_on_finish(rt_stats_t *s, int64_t t_end, int64_t D_us, bool hard) {
    s->finishes++;
    s->last_end_us = t_end;

    int64_t exec = t_end - s->last_start_us;
    if (exec > s->worst_exec_us) s->worst_exec_us = exec;

    int64_t resp = t_end - s->last_release_us;
    if (resp > s->worst_response_us) s->worst_response_us = resp;

    int64_t lat = s->last_start_us - s->last_release_us;
    if (lat > s->worst_latency_us) s->worst_latency_us = lat;

    if (resp > D_us) {
        if (hard) s->hard_miss++; else s->soft_miss++;
    }

    if (s->r_count < RBUF) s->r_buf[s->r_count++] = (int32_t)resp;

    uint8_t k = s->k_window ? s->k_window : 10;
    uint8_t hit = (resp <= D_us) ? 1 : 0;
    s->win_mask = ((s->win_mask << 1) | hit) & ((1u << k) - 1);
    if (s->win_filled < k) s->win_filled++;
}

static int32_t p99_of_buf(volatile int32_t *v, volatile uint16_t n) {
    if (n == 0) return 0;
    int32_t tmp[RBUF];
    uint16_t m = n;
    if (m > RBUF) m = RBUF;
    for (uint16_t i = 0; i < m; i++) tmp[i] = v[i];
    
    for (int i = 1; i < m; i++) {
        int32_t key = tmp[i], j = i - 1;
        while (j >= 0 && tmp[j] > key) {
            tmp[j + 1] = tmp[j];
            j--;
        }
        tmp[j + 1] = key;
    }
    int idx = (int)(0.99 * (m - 1));
    if (idx < 0) idx = 0;
    if (idx >= m) idx = m - 1;
    return tmp[idx];
}

static uint32_t mk_hits(const rt_stats_t *s) {
    uint8_t k = s->k_window ? s->k_window : 10;
    uint16_t mask = s->win_mask & ((1u << k) - 1);
    uint32_t hits = 0;
    for (uint8_t i = 0; i < k; i++) hits += (mask >> i) & 1u;
    return (s->win_filled < k) ? 0 : hits;
}

// ====== Busy loop determinístico ======
static inline void cpu_tight_loop_us(uint32_t us) {
    int64_t start = now_us();
    while ((now_us() - start) < us) {
        __asm__ __volatile__("nop");
    }
}

// ====== Sleep até próximo período (absoluto) ======
static inline void timespec_add_ns(struct timespec *t, long ns) {
    t->tv_nsec += ns;
    while (t->tv_nsec >= 1000000000L) {
        t->tv_nsec -= 1000000000L;
        t->tv_sec += 1;
    }
}

// ====== Configuração de prioridade RT ======
static int set_thread_priority(pthread_t thread, int policy, int priority) {
    struct sched_param param;
    param.sched_priority = priority;
    int ret = pthread_setschedparam(thread, policy, &param);
    if (ret != 0) {
        fprintf(stderr, "Erro ao definir prioridade: %s\n", strerror(ret));
        return -1;
    }
    return 0;
}

// ====== ENC_SENSE (periódica 5 ms): estima velocidade/posição ======
static void *task_enc_sense(void *arg) {
    (void)arg;
    set_thread_priority(pthread_self(), SCHED_FIFO, PRIO_ENC);
    
    struct timespec next;
    clock_gettime(CLOCK_MONOTONIC, &next);
    
    const long period_ns = ENC_T_MS * 1000000L;
    const float dt_s = ENC_T_MS / 1000.0f;
    
    while (running) {
        int64_t t_rel = now_us();
        stats_on_release(&st_enc, t_rel);
        
        int64_t t_start = now_us();
        stats_on_start(&st_enc, t_start);
        
        // Simula leitura de encoder
        pthread_mutex_lock(&belt_mutex);
        float delta_rpm = (g_belt.set_rpm - g_belt.rpm) * 0.3f;
        g_belt.rpm += delta_rpm;
        g_belt.pos_mm += (g_belt.rpm / 60.0f) * 100.0f * dt_s;
        pthread_mutex_unlock(&belt_mutex);
        
        cpu_tight_loop_us(200); // Simula WCET de sensor
        
        int64_t t_end = now_us();
        stats_on_finish(&st_enc, t_end, D_ENC_US, true);
        
        sem_post(&semCtrlNotify);
        
        timespec_add_ns(&next, period_ns);
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, NULL);
    }
    return NULL;
}

// ====== SPD_CTRL (encadeada): controle PI simulado ======
static void *task_spd_ctrl(void *arg) {
    (void)arg;
    set_thread_priority(pthread_self(), SCHED_FIFO, PRIO_CTRL);
    
    float kp = 0.4f, ki = 0.1f, integ = 0.f;
    
    while (running) {
        int64_t tb = now_us();
        sem_wait(&semCtrlNotify);
        if (!running) break;
        
        int64_t t_rel = st_enc.last_release_us;
        stats_on_release(&st_ctrl, t_rel);
        
        int64_t ta = now_us();
        stats_on_start(&st_ctrl, ta);
        st_ctrl.blocked_us_total += (ta - tb);
        
        pthread_mutex_lock(&belt_mutex);
        float err = g_belt.set_rpm - g_belt.rpm;
        integ += err * 0.005f;
        if (integ > 50.f) integ = 50.f;
        if (integ < -50.f) integ = -50.f;
        float out = kp * err + ki * integ;
        (void)out;
        pthread_mutex_unlock(&belt_mutex);
        
        cpu_tight_loop_us(300);
        
        // HMI (soft RT)
        struct timespec ts = {0, 1000000}; // 1ms timeout
        if (sem_timedwait(&semHMI, &ts) == 0) {
            cpu_tight_loop_us(500);
        }
        
        int64_t t_end = now_us();
        stats_on_finish(&st_ctrl, t_end, D_CTRL_US, true);
    }
    return NULL;
}

// ====== SORT_ACT (evento 'b'): aciona "desviador" ======
static void *task_sort_act(void *arg) {
    (void)arg;
    set_thread_priority(pthread_self(), SCHED_FIFO, PRIO_SORT);
    
    while (running) {
        sem_wait(&semSort);
        if (!running) break;
        
        int64_t t_rel = now_us();
        stats_on_release(&st_sort, t_rel);
        
        int64_t t_start = now_us();
        stats_on_start(&st_sort, t_start);
        
        cpu_tight_loop_us(700); // Simula atuação
        
        int64_t t_end = now_us();
        stats_on_finish(&st_sort, t_end, D_SORT_US, true);
        
        char ts[32];
        now_str(ts, sizeof(ts));
        printf("[%s] SORT_ACT: Objeto desviado\n", ts);
    }
    return NULL;
}

// ====== SAFETY_TASK (evento 'd'): E-stop ======
static void *task_safety(void *arg) {
    (void)arg;
    set_thread_priority(pthread_self(), SCHED_FIFO, PRIO_SAFE);
    
    while (running) {
        sem_wait(&semEStop);
        if (!running) break;
        
        int64_t t_rel = now_us();
        stats_on_release(&st_safe, t_rel);
        
        int64_t t_start = now_us();
        stats_on_start(&st_safe, t_start);
        
        pthread_mutex_lock(&belt_mutex);
        g_belt.set_rpm = 0.f;
        g_belt.rpm = 0.f;
        pthread_mutex_unlock(&belt_mutex);
        
        cpu_tight_loop_us(400);
        
        int64_t t_end = now_us();
        stats_on_finish(&st_safe, t_end, D_SAFE_US, true);
        
        char ts[32];
        now_str(ts, sizeof(ts));
        printf("[%s] ⚠️  E-STOP: Esteira parada!\n", ts);
    }
    return NULL;
}

// ====== STATS: log 1x/s ======
static void *task_stats(void *arg) {
    (void)arg;
    
    struct timespec next;
    clock_gettime(CLOCK_MONOTONIC, &next);
    const long period_ns = 1000000000L; // 1s
    
    while (running) {
        timespec_add_ns(&next, period_ns);
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, NULL);
        
        if (!running) break;
        
        char ts[32];
        now_str(ts, sizeof(ts));
        
        pthread_mutex_lock(&belt_mutex);
        printf("\n[%s] STATS: rpm=%.1f set=%.1f pos=%.1fmm\n",
               ts, g_belt.rpm, g_belt.set_rpm, g_belt.pos_mm);
        pthread_mutex_unlock(&belt_mutex);
        
        // ENC
        int32_t p99_enc = p99_of_buf(st_enc.r_buf, st_enc.r_count);
        uint32_t mk_enc = mk_hits(&st_enc);
        printf("[%s] ENC: rel=%u fin=%u hard=%u WCRT=%lldus HWM99≈%dus Lmax=%lldus Cmax=%lldus (m,k)=(%u,%u)\n",
               ts, st_enc.releases, st_enc.finishes, st_enc.hard_miss,
               (long long)st_enc.worst_response_us, p99_enc,
               (long long)st_enc.worst_latency_us, (long long)st_enc.worst_exec_us,
               mk_enc, st_enc.k_window);
        
        // CTRL
        int32_t p99_ctrl = p99_of_buf(st_ctrl.r_buf, st_ctrl.r_count);
        uint32_t mk_ctrl = mk_hits(&st_ctrl);
        printf("[%s] CTRL: rel=%u fin=%u hard=%u WCRT=%lldus HWM99≈%dus Lmax=%lldus Cmax=%lldus (m,k)=(%u,%u) blk=%lldus\n",
               ts, st_ctrl.releases, st_ctrl.finishes, st_ctrl.hard_miss,
               (long long)st_ctrl.worst_response_us, p99_ctrl,
               (long long)st_ctrl.worst_latency_us, (long long)st_ctrl.worst_exec_us,
               mk_ctrl, st_ctrl.k_window, (long long)st_ctrl.blocked_us_total);
        
        // SORT
        if (st_sort.releases > 0) {
            int32_t p99_sort = p99_of_buf(st_sort.r_buf, st_sort.r_count);
            uint32_t mk_sort = mk_hits(&st_sort);
            printf("[%s] SORT: rel=%u fin=%u hard=%u WCRT=%lldus HWM99≈%dus Lmax=%lldus Cmax=%lldus (m,k)=(%u,%u)\n",
                   ts, st_sort.releases, st_sort.finishes, st_sort.hard_miss,
                   (long long)st_sort.worst_response_us, p99_sort,
                   (long long)st_sort.worst_latency_us, (long long)st_sort.worst_exec_us,
                   mk_sort, st_sort.k_window);
        }
        
        // SAFE
        if (st_safe.releases > 0) {
            int32_t p99_safe = p99_of_buf(st_safe.r_buf, st_safe.r_count);
            uint32_t mk_safe = mk_hits(&st_safe);
            printf("[%s] SAFE: rel=%u fin=%u hard=%u WCRT=%lldus HWM99≈%dus Lmax=%lldus Cmax=%lldus (m,k)=(%u,%u)\n",
                   ts, st_safe.releases, st_safe.finishes, st_safe.hard_miss,
                   (long long)st_safe.worst_response_us, p99_safe,
                   (long long)st_safe.worst_latency_us, (long long)st_safe.worst_exec_us,
                   mk_safe, st_safe.k_window);
        }
    }
    return NULL;
}

// ====== INPUT: processa comandos do stdin ======
static void *task_input(void *arg) {
    (void)arg;
    
    printf("\n=== Esteira Industrial - Linux RTOS ===\n");
    printf("Comandos: b=OBJ  d=E-STOP  h=HMI  q=quit\n\n");
    
    // Configura stdin não-bloqueante
    int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);
    
    while (running) {
        // Usa select com timeout para poder sair quando running=false
        fd_set readfds;
        struct timeval tv;
        FD_ZERO(&readfds);
        FD_SET(STDIN_FILENO, &readfds);
        tv.tv_sec = 0;
        tv.tv_usec = 100000; // 100ms timeout
        
        int ret = select(STDIN_FILENO + 1, &readfds, NULL, NULL, &tv);
        if (ret <= 0) continue; // timeout ou erro, verifica running novamente
        
        char ch = getchar();
        if (ch == EOF || ch == (char)-1) continue;
        
        if (ch == 'q' || ch == 'Q') {
            running = false;
            break;
        } else if (ch == 'b' || ch == 'B') {
            char ts[64];
            time_t now = time(NULL);
            struct tm *tm_info = localtime(&now);
            struct timespec tspec;
            clock_gettime(CLOCK_REALTIME, &tspec);
            snprintf(ts, sizeof(ts), "%02d/%02d/%04d %02d:%02d:%02d.%03ld",
                     tm_info->tm_mday, tm_info->tm_mon + 1, tm_info->tm_year + 1900,
                     tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec,
                     tspec.tv_nsec / 1000000);
            printf("[%s] >>> EVENTO 'b' RECEBIDO - SORT_ACT disparado\n", ts);
            fflush(stdout);
            sem_post(&semSort);
        } else if (ch == 'd' || ch == 'D') {
            char ts[64];
            time_t now = time(NULL);
            struct tm *tm_info = localtime(&now);
            struct timespec tspec;
            clock_gettime(CLOCK_REALTIME, &tspec);
            snprintf(ts, sizeof(ts), "%02d/%02d/%04d %02d:%02d:%02d.%03ld",
                     tm_info->tm_mday, tm_info->tm_mon + 1, tm_info->tm_year + 1900,
                     tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec,
                     tspec.tv_nsec / 1000000);
            printf("[%s] >>> EVENTO 'd' RECEBIDO - E-STOP ativado!\n", ts);
            fflush(stdout);
            sem_post(&semEStop);
        } else if (ch == 'h' || ch == 'H') {
            char ts[64];
            time_t now = time(NULL);
            struct tm *tm_info = localtime(&now);
            struct timespec tspec;
            clock_gettime(CLOCK_REALTIME, &tspec);
            snprintf(ts, sizeof(ts), "%02d/%02d/%04d %02d:%02d:%02d.%03ld",
                     tm_info->tm_mday, tm_info->tm_mon + 1, tm_info->tm_year + 1900,
                     tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec,
                     tspec.tv_nsec / 1000000);
            pthread_mutex_lock(&belt_mutex);
            float old_rpm = g_belt.set_rpm;
            g_belt.set_rpm += 20.0f;
            if (g_belt.set_rpm > 500.f) g_belt.set_rpm = 120.f;
            pthread_mutex_unlock(&belt_mutex);
            printf("[%s] >>> EVENTO 'h' RECEBIDO - HMI: set_rpm %.1f -> %.1f RPM\n", ts, old_rpm, g_belt.set_rpm);
            fflush(stdout);
            sem_post(&semHMI);
            printf("HMI: set_rpm=%.1f\n", g_belt.set_rpm);
        }
    }
    
    // Restaura stdin bloqueante ao sair
    int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    fcntl(STDIN_FILENO, F_SETFL, flags & ~O_NONBLOCK);
    
    return NULL;
}

// ====== Signal handler ======
static void signal_handler(int sig) {
    (void)sig;
    printf("\n>>> Sinal recebido (Ctrl+C), finalizando...\n");
    fflush(stdout);
    running = false;
    
    // Desbloqueia todas as threads travadas em sem_wait
    sem_post(&semCtrlNotify);
    sem_post(&semSort);
    sem_post(&semEStop);
    sem_post(&semHMI);
}

// ====== main ======
int main(void) {
    // Lock memory para evitar page faults
    if (mlockall(MCL_CURRENT | MCL_FUTURE) != 0) {
        fprintf(stderr, "AVISO: mlockall falhou. Execute com sudo para RT real.\n");
    }
    
    // Signal handler
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // Inicializa semáforos
    sem_init(&semCtrlNotify, 0, 0);
    sem_init(&semSort, 0, 0);
    sem_init(&semEStop, 0, 0);
    sem_init(&semHMI, 0, 0);
    
    // Cria threads
    pthread_create(&thINPUT, NULL, task_input, NULL);
    pthread_create(&thENC, NULL, task_enc_sense, NULL);
    pthread_create(&thCTRL, NULL, task_spd_ctrl, NULL);
    pthread_create(&thSORT, NULL, task_sort_act, NULL);
    pthread_create(&thSAFE, NULL, task_safety, NULL);
    pthread_create(&thSTATS, NULL, task_stats, NULL);
    
    // Aguarda término
    pthread_join(thINPUT, NULL);
    
    // Sinaliza parada e desbloqueia threads
    running = false;
    sem_post(&semCtrlNotify);
    sem_post(&semSort);
    sem_post(&semEStop);
    sem_post(&semHMI);
    
    pthread_join(thENC, NULL);
    pthread_join(thCTRL, NULL);
    pthread_join(thSORT, NULL);
    pthread_join(thSAFE, NULL);
    pthread_join(thSTATS, NULL);
    
    // Cleanup
    sem_destroy(&semCtrlNotify);
    sem_destroy(&semSort);
    sem_destroy(&semEStop);
    sem_destroy(&semHMI);
    
    printf("\nEsteira finalizada.\n");
    return 0;
}
