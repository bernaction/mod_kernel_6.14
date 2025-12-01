
## Parte 1 – Análise de Linux Kernel RT

```bash
sudo apt install libnuma-dev
git clone git://git.kernel.org/pub/scm/utils/rt-tests/rt-tests.git
cd rt-tests
git checkout stable/v1.0
make all
make install

make cyclictest
```
### Teste padrão 1:
```bash
sudo cyclictest -p99 -t -n -m
```

### Teste padrão 2:
```bash
sudo cyclictest -a -t -p99 -n -m
```

### Teste de latência de hardware
```bash
sudo hwlatdetect --duration=30 --threshold=10
```

### Teste de latência de semáforo
```bash
sudo ptsematest -a -t -p99 -n
```





---

## Parte 2 – Evoluir o Trabalho da M2 e a Análise Temporal

### Uma fila de requisições aperiódicas e um servidor periódico:
```c
#include <pthread.h>
#include <time.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
/////////////////////// Fila de requisições aperiódicas ///////////////////////////
typedef void (*job_func_t)(void *arg);
typedef struct job {
job_func_t func;
void *arg;
struct job *next;
} job_t;
static job_t *queue_head = NULL;
static job_t *queue_tail = NULL;
static pthread_mutex_t queue_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t queue_cond = PTHREAD_COND_INITIALIZER;
/////////////////////// Enfileira um job (chamado pelas tarefas aperiódicas) ///////////////////////
void enqueue_job(job_func_t f, void *arg) {
job_t *j = malloc(sizeof(job_t));
j->func = f;
j->arg = arg;
j->next = NULL;
pthread_mutex_lock(&queue_mutex);
if (queue_tail)
queue_tail->next = j;
else
queue_head = j;
queue_tail = j;
pthread_cond_signal(&queue_cond);
pthread_mutex_unlock(&queue_mutex);
}
/////////////////////// Retira um job da fila (usado pelo servidor) ///////////////////////
job_t *dequeue_job(void) {
job_t *j = queue_head;
if (!j) return NULL;
queue_head = j->next;
if (!queue_head) queue_tail = NULL;
return j;
}
```

### Função auxiliar: dormir até o próximo período. Use tempo absoluto com clock_nanosleep (evita
drift):
```c
/////////////////////// soma nanossegundos a um timespec (periodo) ///////////////////////
static void timespec_add_ns(struct timespec *t, long ns) {
t->tv_nsec += ns;
while (t->tv_nsec >= 1000000000L) {
t->tv_nsec -= 1000000000L;
t->tv_sec += 1;
}
}
```

### Thread Servidor periódico: Aqui o servidor tem período Ts e orçamento Cs. Para simplificar, assumo que Cs é apenas um limite de tempo em ns; você mede quanto tempo gastou e, quando passar de Cs, para de atender jobs naquele período.

```c
typedef struct {
long period_ns; // Ts (por ex. 10ms = 10*10^6 ns)
long budget_ns; // Cs (pode ser <= Ts*Umax)
} server_params_t;
void *server_thread(void *arg) {
server_params_t *params = (server_params_t *)arg;
long Ts = params->period_ns;
long Cs = params->budget_ns;
struct timespec next_release;
clock_gettime(CLOCK_MONOTONIC, &next_release);
while (1) {
/////////////////////// define momento da próxima ativação
timespec_add_ns(&next_release, Ts);
/////////////////////// início do período
struct timespec start_period;
clock_gettime(CLOCK_MONOTONIC, &start_period);
long consumed_ns = 0;
while (consumed_ns < Cs) {
/////////////////////// pega um job, se existir
pthread_mutex_lock(&queue_mutex);
while (!queue_head) {
/////////////////////// se não há jobs, pode sair do loop de serviço
pthread_mutex_unlock(&queue_mutex);
goto end_of_service;
}
job_t *j = dequeue_job();
pthread_mutex_unlock(&queue_mutex);
/////////////////////// mede tempo antes/depois de executar o job
struct timespec t_before, t_after;
clock_gettime(CLOCK_MONOTONIC, &t_before);
j->func(j->arg); // executa requisição aperiódica
clock_gettime(CLOCK_MONOTONIC, &t_after);
/////////////////////// atualiza orçamento consumido
long dt = (t_after.tv_sec - t_before.tv_sec)*1000000000L +
(t_after.tv_nsec - t_before.tv_nsec);
consumed_ns += dt;
free(j);
if (consumed_ns >= Cs) {
// orçamento esgotado neste período
break;
}
}
end_of_service:
/////////////////////// Dorme até o instante absoluto da próxima liberação
clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME,
&next_release, NULL);
}
return NULL;
}
```

### Criando o servidor com prioridade de tempo real
```c
void start_server_thread() {
pthread_t th;
pthread_attr_t attr;
struct sched_param sp;
pthread_attr_init(&attr);
pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
pthread_attr_setschedpolicy(&attr, SCHED_FIFO); // ou SCHED_RR
sp.sched_priority = 60; // prioridade RT (ajuste conforme sistema)
pthread_attr_setschedparam(&attr, &sp);
static server_params_t params = {
.period_ns = 10L * 1000000L, // Ts = 10 ms
.budget_ns = 3L * 1000000L // Cs = 3 ms por período
};
pthread_create(&th, &attr, server_thread, &params);
pthread_attr_destroy(&attr);
}
```

### “Tarefas aperiódicas” (Tw, Tz, …): Em vez de criar uma thread nova para cada pedido (ou deixá-la rodar livremente)
```c
void meu_job(void *arg) {
/////////////////////// código que representa a tarefa aperiódica
printf("Processando requisição aperiódica %d\n", *(int*)arg);
/////////////////////// ...
}
void requisicao_aperiodica(int id) {
int *p = malloc(sizeof(int));
*p = id;
enqueue_job(meu_job, p);
}
```
