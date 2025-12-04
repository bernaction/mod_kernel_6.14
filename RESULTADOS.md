# Parte 2 – Resultados Experimentais no Linux RTOS

## Informações do Sistema

**Data da execução:** ___/___/2025  
**Hardware:** ________________________________  
**VM:** VirtualBox ___GB RAM / ___ CPUs  
**Kernel:** 6.14.0-rt3  
**Verificações:**
```bash
uname -r           # Versão do kernel
uname -v           # Deve conter PREEMPT_RT
cat /sys/kernel/realtime  # Deve retornar 1
```

---

## Teste 1: Esteira Industrial - Execução Normal (60s)

### Comando
```bash
cd src
make
sudo ./esteira_linux
# Deixar rodar por 60 segundos, então pressionar 'q'
```

### Resultados Coletados

#### Estado Final da Esteira
```
rpm: _______
set_rpm: _______
pos_mm: _______
```

#### Métricas - ENC_SENSE (Hard RT, 5ms)
```
releases: _______
finishes: _______
hard_miss: _______
WCRT: _______ µs
HWM99: _______ µs
Lmax: _______ µs
Cmax: _______ µs
(m,k): (___, ___)
```

#### Métricas - SPD_CTRL (Hard RT, encadeada)
```
releases: _______
finishes: _______
hard_miss: _______
WCRT: _______ µs
HWM99: _______ µs
Lmax: _______ µs
Cmax: _______ µs
(m,k): (___, ___)
blk: _______ µs
```

---

## Teste 2: Eventos Esporádicos (SORT_ACT)

### Comando
Executar e pressionar 'b' 10 vezes com intervalo de ~2s

### Resultados - SORT_ACT
```
releases: _______
finishes: _______
hard_miss: _______
WCRT: _______ µs
HWM99: _______ µs
Lmax: _______ µs
Cmax: _______ µs
(m,k): (___, ___)
```

**Observações:**
- Todos os eventos foram processados? (sim/não): _______
- Houve deadlines perdidas? (sim/não): _______

---

## Teste 3: E-STOP (SAFETY)

### Comando
Durante execução normal, pressionar 'd'

### Resultados - SAFETY
```
releases: _______
finishes: _______
hard_miss: _______
WCRT: _______ µs
HWM99: _______ µs
Lmax: _______ µs
Cmax: _______ µs
(m,k): (___, ___)
```

**Tempo para esteira parar (rpm→0):** _______ ms

---

## Teste 4: Comparação com cyclictest

### Comando
```bash
# Terminal 1
sudo ./esteira_linux

# Terminal 2
sudo cyclictest -p99 -t1 -n -m -i 5000 -D 60
```

### Resultados cyclictest (60s, 5ms de intervalo)
```
Min: _______ µs
Avg: _______ µs
Max: _______ µs
```

### Comparação

| Métrica | cyclictest | ENC_SENSE | Diferença |
|---------|------------|-----------|-----------|
| Latência Mín | ___µs | ___µs | ___µs |
| Latência Méd | ___µs | ___µs | ___µs |
| Latência Máx | ___µs | ___µs | ___µs |

**Análise:**
_______________________________________________________________
_______________________________________________________________
_______________________________________________________________

---

## Teste 5: Stress Test - Rajada de Eventos

### Comando
```bash
# Enviar 50 eventos 'b' rapidamente
perl -e 'print "b" x 50' | sudo ./esteira_linux
```

### Resultados
```
Total de eventos enviados: 50
Total processado por SORT_ACT: _______
Hard misses: _______
WCRT durante rajada: _______ µs
```

**Observações:**
_______________________________________________________________
_______________________________________________________________

---

## Análise de Previsibilidade

### (m,k)-firm Analysis

| Tarefa | k | m observado | Taxa de sucesso |
|--------|---|-------------|-----------------|
| ENC_SENSE | 10 | _______ | _______% |
| SPD_CTRL | 10 | _______ | _______% |
| SORT_ACT | 10 | _______ | _______% |
| SAFETY | 10 | _______ | _______% |

### HWM99 vs WCRT

| Tarefa | HWM99 | WCRT | Razão (WCRT/HWM99) |
|--------|-------|------|--------------------|
| ENC_SENSE | ___µs | ___µs | _______ |
| SPD_CTRL | ___µs | ___µs | _______ |
| SORT_ACT | ___µs | ___µs | _______ |
| SAFETY | ___µs | ___µs | _______ |

---

## Comparação: ESP32 vs Linux RTOS

| Aspecto | ESP32 (M2) | Linux RT (M3) |
|---------|------------|---------------|
| **Plataforma** | FreeRTOS bare-metal | PREEMPT_RT em VM |
| **CPU** | 240 MHz dual-core | Intel/AMD ___GHz |
| **ENC WCRT** | ~1.9 ms | _______ µs |
| **CTRL WCRT** | ~3.2 ms | _______ µs |
| **SORT WCRT** | ~0.7 ms | _______ µs |
| **SAFE WCRT** | ~0.9 ms | _______ µs |
| **Hard misses** | 0 | _______ |
| **(m,k) ENC** | (10,10) | (___, ___) |

---

## Problemas Encontrados

### Problema 1
**Descrição:** _______________________________________________________________
**Solução:** _______________________________________________________________

### Problema 2
**Descrição:** _______________________________________________________________
**Solução:** _______________________________________________________________

---

## Conclusões Preliminares

1. **Determinismo:**  
   _______________________________________________________________
   _______________________________________________________________

2. **Diferença VM vs Bare-metal:**  
   _______________________________________________________________
   _______________________________________________________________

3. **Adequação do PREEMPT_RT:**  
   _______________________________________________________________
   _______________________________________________________________

4. **Gargalos identificados:**  
   _______________________________________________________________
   _______________________________________________________________

---

## Screenshots / Logs

### Captura de tela - Execução normal
```
[Cole aqui print ou output do terminal]
```

### Captura de tela - cyclictest paralelo
```
[Cole aqui print ou output do terminal]
```

---

## Próximos Passos (Parte 3 do Trabalho)

- [ ] Implementar servidor periódico para tarefas aperiódicas
- [ ] Integrar fila de requisições
- [ ] Testar política de escalonamento do servidor
- [ ] Comparar overhead: servidor vs threads individuais
- [ ] Analisar utilização da CPU

---

## Referências

- [cyclictest Man Page](https://man7.org/linux/man-pages/man8/cyclictest.8.html)
- [PREEMPT_RT Documentation](https://wiki.linuxfoundation.org/realtime/documentation/start)
- Código fonte: `src/esteira_linux.c`
- Documentação: `src/README_LINUX.md`
