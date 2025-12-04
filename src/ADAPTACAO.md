# 📋 Resumo da Adaptação ESP32 → Linux

## O que foi feito

### 1. Código Principal (`esteira_linux.c`)

**Removido (específico ESP32):**
- ❌ `esp_timer.h`, `esp_log.h`, `esp_wifi.h`, etc.
- ❌ `driver/gpio.h`, `driver/touch_pad.h`, `driver/uart.h`
- ❌ FreeRTOS headers (`freertos/task.h`, `freertos/semphr.h`)
- ❌ Wi-Fi, TCP/UDP servers
- ❌ LED GPIO, Touch pad hardware
- ❌ NVS flash, SNTP client

**Substituído por (POSIX/Linux):**
- ✅ `pthread.h` → threads POSIX
- ✅ `semaphore.h` → `sem_t` em vez de `SemaphoreHandle_t`
- ✅ `time.h` → `clock_gettime(CLOCK_MONOTONIC)` em vez de `esp_timer_get_time()`
- ✅ `sched.h` → `SCHED_FIFO` com prioridades 1-99
- ✅ `sys/mman.h` → `mlockall()` para lock de memória
- ✅ `stdin` → comandos via `getchar()` em vez de Touch pads
- ✅ `gettimeofday()` → já sincronizado pelo sistema (SNTP desnecessário)

**Mantido (lógica RT):**
- ✅ Todas as 4 tarefas RT (ENC, CTRL, SORT, SAFE)
- ✅ Instrumentação completa (WCRT, HWM99, (m,k)-firm)
- ✅ Simulação da esteira (rpm, posição, controle PI)
- ✅ Métricas de latência, bloqueio, deadline miss
- ✅ Timestamps epoch em microsegundos

---

## Comparação de APIs

| Funcionalidade | ESP32 (FreeRTOS) | Linux (POSIX) |
|----------------|------------------|---------------|
| **Criar thread** | `xTaskCreate()` | `pthread_create()` |
| **Prioridade** | `uxTaskPrioritySet()` | `pthread_setschedparam(SCHED_FIFO)` |
| **Semáforo binário** | `xSemaphoreCreateBinary()` | `sem_init(&sem, 0, 0)` |
| **Wait/Post** | `xSemaphoreTake()` / `Give()` | `sem_wait()` / `sem_post()` |
| **Notificação** | `ulTaskNotifyTake()` | `sem_wait()` (equivalente) |
| **Sleep periódico** | `vTaskDelayUntil()` | `clock_nanosleep(TIMER_ABSTIME)` |
| **Tempo (µs)** | `esp_timer_get_time()` | `clock_gettime() * 1000` |
| **Lock memória** | Automático | `mlockall(MCL_CURRENT)` |
| **Log** | `ESP_LOGI()` | `printf()` |
| **GPIO input** | `touch_pad_read()` | `getchar()` (simulado) |

---

## Arquitetura do Sistema

### ESP32 (Original)
```
┌─────────────────────────────────────┐
│         ESP32 DevKit V1             │
│   FreeRTOS 10.5 (240 MHz)           │
├─────────────────────────────────────┤
│ ENC_SENSE → SPD_CTRL (notify)       │
│ Touch B   → SORT_ACT (queue)        │
│ Touch D   → SAFETY (semaphore)      │
│ Touch C   → HMI (semaphore)         │
│ UDP/TCP servers (Wi-Fi)             │
│ LED GPIO2 (blink)                   │
└─────────────────────────────────────┘
```

### Linux RT (Adaptado)
```
┌─────────────────────────────────────┐
│      Linux 6.14 PREEMPT_RT          │
│   POSIX Threads (Intel/AMD)         │
├─────────────────────────────────────┤
│ ENC_SENSE → SPD_CTRL (sem_post)     │
│ stdin 'b' → SORT_ACT (semaphore)    │
│ stdin 'd' → SAFETY (semaphore)      │
│ stdin 'h' → HMI (semaphore)         │
│ [UDP/TCP removidos]                 │
│ [LED removido]                      │
└─────────────────────────────────────┘
```

---

## Diferenças Principais

### 1. Modelo de Escalonamento

**ESP32:**
- Escalonador preemptivo cooperativo
- Prioridades 0-24 (maior número = mais prioritário)
- Tick de 1 ms (configurável)
- Suporte a time-slicing (SCHED_RR)

**Linux RT:**
- Escalonador SCHED_FIFO (100% preemptivo)
- Prioridades 1-99 (maior número = mais prioritário)
- Tick de ~250 µs (CONFIG_HZ=4000)
- Sem time-slicing (FIFO puro)

### 2. Determinismo

**ESP32:**
- ✅ Bare-metal, latências ~10-50 µs
- ✅ Sem interferência de SO
- ❌ Limitado por hardware (240 MHz, 520 KB RAM)

**Linux RT:**
- ✅ PREEMPT_RT garante latências < 100 µs
- ❌ Overhead de syscalls, context switch
- ❌ Virtualização adiciona jitter (VirtualBox)
- ✅ Muito mais poder computacional

### 3. Sincronização

**ESP32:**
- Notificações diretas entre tasks (leve)
- Queues com timeout
- Semáforos binários/contadores

**Linux:**
- Semáforos POSIX (heavier)
- Mutexes com PI (Priority Inheritance)
- Condition variables

---

## Métricas Esperadas

### ESP32 (baseline do TRAB_M2)
- ENC WCRT: ~1.9 ms
- CTRL WCRT: ~3.2 ms
- SORT WCRT: ~0.7 ms
- SAFE WCRT: ~0.9 ms
- Hard misses: 0

### Linux RT (VM VirtualBox)
- ENC WCRT: ~2-5 ms (pior que ESP32 devido a VM)
- CTRL WCRT: ~4-8 ms
- SORT WCRT: ~1-2 ms
- SAFE WCRT: ~1-2 ms
- Hard misses: 0-2 (ocasionais devido a hypervisor)

### Linux RT (Bare-metal, ideal)
- ENC WCRT: ~0.5-1 ms (melhor que ESP32!)
- CTRL WCRT: ~1-2 ms
- SORT WCRT: ~0.3-0.5 ms
- SAFE WCRT: ~0.3-0.5 ms
- Hard misses: 0

---

## Servidor Periódico (`servidor_periodico.c`)

Implementação conforme especificação do PDF:

```c
// Estrutura
typedef struct {
    long period_ns;   // Ts (período)
    long budget_ns;   // Cs (capacidade por período)
    int priority;     // Prioridade RT
} server_params_t;

// Fila FIFO thread-safe
job_t *queue_head, *queue_tail;
pthread_mutex_t queue_mutex;
pthread_cond_t queue_cond;

// Servidor consome jobs até esgotar budget
while (consumed_ns < Cs) {
    job = dequeue_job();
    execute(job);
    consumed_ns += execution_time;
}
clock_nanosleep(TIMER_ABSTIME, &next_release);
```

**Teste:**
```bash
# Ts=10ms, Cs=5ms → 50% de utilização
sudo ./servidor_periodico 10 5 70 60

# Observar:
# - Jobs enfileirados vs executados
# - % de períodos ociosos
# - Resposta média/máxima
```

---

## Vantagens da Adaptação

1. **Portabilidade**: Roda em qualquer Linux RT
2. **Escalabilidade**: Pode usar todos os cores (ESP32 = 2)
3. **Debug**: GDB, Valgrind, perf, etc.
4. **Integração**: Fácil conectar com outros processos Linux
5. **Aprendizado**: POSIX threads é padrão da indústria

---

## Limitações

1. **Latência**: VM adiciona 1-5 ms de jitter
2. **Overhead**: Context switch mais pesado que ESP32
3. **Energia**: PC consome ~100x mais que ESP32
4. **GPIO**: Não há controle direto de hardware (apenas simulado)

---

## Próximos Passos Possíveis

### Extensões do Projeto

1. **Isolar CPUs**: `isolcpus=1,2,3` no boot
2. **Tracing**: usar `trace-cmd` para visualizar preempções
3. **Benchmark**: comparar com Xenomai, RT_PREEMPT vs LOWLATENCY
4. **GPIO real**: usar `/sys/class/gpio` ou libgpiod
5. **CAN bus**: simular comunicação industrial (SocketCAN)

### Análise Avançada

1. **Jitter histogram**: `cyclictest -h 100` e plotar
2. **CPU affinity**: pinar tasks em cores específicos
3. **NUMA**: testar em sistemas multi-socket
4. **Deadline scheduler**: testar SCHED_DEADLINE vs SCHED_FIFO

---

## Conclusão

✅ **Código adaptado com sucesso!**

- Todas as funcionalidades RT mantidas
- Instrumentação completa preservada
- Adição de servidor periódico (Parte 2)
- Pronto para testes no kernel PREEMPT_RT

📊 **Próximos passos:**
1. Compilar kernel RT (README.md)
2. Compilar programas (`make`)
3. Executar e coletar métricas (RESULTADOS.md)
4. Comparar com ESP32 (TRAB_M2.md)
5. Gerar relatório final
