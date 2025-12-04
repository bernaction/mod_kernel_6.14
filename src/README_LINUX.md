# 🏭 Esteira Industrial - Linux RTOS

Adaptação do projeto ESP32+FreeRTOS para **Linux com PREEMPT_RT** usando **POSIX threads**.

Este programa implementa uma simulação de esteira industrial com instrumentação completa de tempo real, incluindo métricas WCRT, (m,k)-firm, HWM99, latência e bloqueio.

---

## 📋 Pré-requisitos

- **Linux com kernel PREEMPT_RT** compilado e em execução
- Verificar que `/sys/kernel/realtime` retorna `1`
- GCC instalado
- Permissões `sudo` para prioridades RT

---

## 🔧 Compilação

```bash
cd src
make
```

Isso irá:
- Compilar `esteira_linux.c`
- Gerar o executável `esteira_linux`
- Linkar com `-pthread`, `-lrt` e `-lm`

---

## ▶️ Execução

```bash
sudo ./esteira_linux
```

⚠️ **Importante:** O programa **precisa de sudo** para:
- Definir prioridades SCHED_FIFO (tempo real)
- Lock de memória com `mlockall()` (evita page faults)

---

## 🎮 Comandos Durante Execução

| Tecla | Função |
|-------|--------|
| `b` | Simula detecção de objeto → dispara `SORT_ACT` |
| `d` | Aciona E-STOP → para esteira via `SAFETY_TASK` |
| `h` | Interface HMI → aumenta setpoint em +20 RPM |
| `q` | Encerra programa gracefully |

---

## 🧱 Arquitetura do Sistema

### Tarefas em Tempo Real

| Tarefa | Tipo | Período | Prioridade | Deadline | Função |
|--------|------|---------|------------|----------|--------|
| **ENC_SENSE** | Periódica | 5 ms | 80 | 5 ms | Lê velocidade e posição simuladas |
| **SPD_CTRL** | Encadeada | — | 70 | 10 ms | Controle PI + trata HMI (soft RT) |
| **SORT_ACT** | Evento (`b`) | — | 60 | 10 ms | Aciona desviador de peças |
| **SAFETY** | Evento (`d`) | — | 90 | 5 ms | E-stop de emergência |
| **STATS** | Periódica | 1 s | 20 | — | Imprime métricas RT |

### Sincronização

- **Semáforo `semCtrlNotify`**: ENC_SENSE → SPD_CTRL (encadeamento)
- **Semáforo `semSort`**: stdin 'b' → SORT_ACT
- **Semáforo `semEStop`**: stdin 'd' → SAFETY
- **Semáforo `semHMI`**: stdin 'h' → soft RT dentro de SPD_CTRL
- **Mutex `belt_mutex`**: Protege estado compartilhado (`g_belt`)

---

## 📊 Métricas Coletadas

Para cada tarefa, o sistema instrumenta:

| Métrica | Descrição |
|---------|-----------|
| **releases** | Número de liberações (ativações) |
| **finishes** | Número de conclusões |
| **hard_miss** | Deadlines perdidas (hard RT) |
| **WCRT** | Worst-Case Response Time (µs) |
| **HWM99** | Percentil 99 dos tempos de resposta |
| **Lmax** | Latência máxima (release→start) |
| **Cmax** | Tempo de execução máximo |
| **(m,k)** | (m,k)-firm: sucessos em janela de k |
| **blk** | Tempo total bloqueado aguardando recursos |

---

## 📈 Exemplo de Saída

```
=== Esteira Industrial - Linux RTOS ===
Comandos: b=OBJ  d=E-STOP  h=HMI  q=quit

[03/12/2025 15:42:10.123] STATS: rpm=112.3 set=120.0 pos=5621.2mm
[03/12/2025 15:42:10.123] ENC: rel=200 fin=200 hard=0 WCRT=1234us HWM99≈980us Lmax=45us Cmax=890us (m,k)=(10,10)
[03/12/2025 15:42:10.125] CTRL: rel=200 fin=200 hard=0 WCRT=2456us HWM99≈1890us Lmax=123us Cmax=1567us (m,k)=(10,10) blk=12345us
[03/12/2025 15:42:10.126] SORT: rel=3 fin=3 hard=0 WCRT=891us HWM99≈850us Lmax=34us Cmax=765us (m,k)=(3,10)

[03/12/2025 15:42:11.456] SORT_ACT: Objeto desviado
[03/12/2025 15:42:15.789] ⚠️  E-STOP: Esteira parada!
```

---

## 🧪 Testes Recomendados

### 1. Teste de Periodicidade (ENC_SENSE)
Execute por 60 segundos e verifique:
- `releases` ≈ 12000 (60s / 5ms)
- `hard_miss` = 0
- WCRT < 5000 µs

### 2. Teste de Eventos
```bash
# Enviar múltiplos eventos rapidamente
echo "bbbbbbbbb" | sudo ./esteira_linux
```
Verifique que SORT_ACT processa todos sem deadline miss.

### 3. Teste de E-STOP
Durante operação normal, pressione `d`:
- `rpm` deve ir para 0 rapidamente
- SAFETY deve ter WCRT < 5000 µs

### 4. Comparação com `cyclictest`
Execute simultaneamente:
```bash
# Terminal 1
sudo ./esteira_linux

# Terminal 2
sudo cyclictest -p99 -t1 -n -m -i 5000
```
Compare latências: o programa deve ter jitter similar ao cyclictest.

---

## 🔍 Troubleshooting

### "mlockall failed"
- Execute com `sudo`
- Verifique `ulimit -l` (deve ser unlimited para root)

### "Operation not permitted" ao definir SCHED_FIFO
- Precisa de `CAP_SYS_NICE` ou executar como root
- Alternativa: `sudo setcap cap_sys_nice=eip ./esteira_linux`

### Latências altas (WCRT > 10 ms)
- Verifique se kernel é realmente PREEMPT_RT: `cat /sys/kernel/realtime`
- Desabilite serviços pesados: `systemctl stop`
- Isole CPUs: boot com `isolcpus=1,2,3`

### Programa trava no início
- Verifique se terminal está em modo raw
- Use `stty sane` para resetar terminal

---

## 🧩 Diferenças em Relação à Versão ESP32

| Aspecto | ESP32 (FreeRTOS) | Linux (POSIX) |
|---------|------------------|---------------|
| **API de threads** | `xTaskCreate()` | `pthread_create()` |
| **Semáforos** | `xSemaphoreCreateBinary()` | `sem_init()` |
| **Notificações** | `ulTaskNotifyTake()` | `sem_wait()` |
| **Prioridades** | 0-24 (maior=mais prioritário) | 1-99 SCHED_FIFO |
| **Timer** | `esp_timer_get_time()` | `clock_gettime()` |
| **Sleep periódico** | `vTaskDelayUntil()` | `clock_nanosleep(TIMER_ABSTIME)` |
| **GPIO/Touch** | Hardware ESP32 | Simulado via stdin |
| **SNTP** | `esp_sntp_*` | `gettimeofday()` (já sincronizado) |
| **Wi-Fi/UDP/TCP** | Implementado | **Removido** (foco em RT local) |
| **LED blink** | GPIO2 | **Removido** |

---

## 📚 Recursos Adicionais

- [PREEMPT_RT Wiki](https://wiki.linuxfoundation.org/realtime/start)
- [POSIX Threads Tutorial](https://www.cs.cmu.edu/afs/cs/academic/class/15492-f07/www/pthreads.html)
- [cyclictest Documentation](https://wiki.linuxfoundation.org/realtime/documentation/howto/tools/cyclictest)

---

## 🎯 Próximos Passos (Trabalho M3)

1. ✅ Compilar kernel PREEMPT_RT
2. ✅ Executar testes `cyclictest` e `ptsematest`
3. ✅ Adaptar código ESP32 para Linux
4. ⏳ **Executar esteira no Linux RT e coletar métricas**
5. ⏳ **Comparar resultados: VM vs bare-metal**
6. ⏳ **Implementar servidor periódico (parte 2 do PDF)**
7. ⏳ **Gerar relatório final**

---

## 📝 Licença

Projeto acadêmico - Sistemas em Tempo Real  
Adaptação ESP32→Linux por [seu nome]
