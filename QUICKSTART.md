# 🚀 Guia Rápido - Trabalho M3

## Estrutura do Projeto

```
Lubuntu_RTOS/
├── README.md               # Guia completo de instalação do kernel RT
├── FULL_CLI.md            # Comandos consolidados (copiar/colar)
├── TEST.md                # Testes cyclictest/ptsematest executados
├── RESULTADOS.md          # Template para coletar seus resultados
├── periodic_server_example.c  # Exemplo do professor
└── src/
    ├── main.c                    # Original ESP32 (referência)
    ├── TRAB_M2.md               # Documentação do trabalho anterior
    ├── esteira_linux.c          # ✅ ADAPTAÇÃO PARA LINUX
    ├── servidor_periodico.c     # ✅ PARTE 2 DO TRABALHO
    ├── README_LINUX.md          # Documentação detalhada
    └── Makefile                 # Compilação automática
```

---

## ⚡ Quick Start

### 1. Compilar kernel PREEMPT_RT

```bash
# Copie e cole TUDO de uma vez (FULL_CLI.md - Parte 1)
sudo apt update && sudo apt upgrade -y && ...
# [~2h de compilação]

# Depois:
sudo shutdown -r now
```

### 2. Verificar kernel RT

```bash
uname -r                      # 6.14.0-rt3
cat /sys/kernel/realtime      # deve retornar 1
sudo dmesg | grep -i "preempt"
```

### 3. Compilar programas

```bash
cd ~/kernel-rt  # ou onde clonou o repo
cd src
make
```

Isso gera:
- `esteira_linux` → Simulação da esteira (Parte 1)
- `servidor_periodico` → Servidor periódico (Parte 2)

---

## 🎯 Execução

### Programa 1: Esteira Industrial

```bash
sudo ./esteira_linux
```

**Comandos interativos:**
- `b` → Simula objeto detectado (SORT_ACT)
- `d` → E-STOP de emergência
- `h` → Aumenta setpoint (+20 RPM)
- `q` → Sair

**O que observar:**
- Métricas a cada 1 segundo no terminal
- WCRT, HWM99, (m,k)-firm de cada tarefa
- Hard misses (idealmente = 0)

### Programa 2: Servidor Periódico

```bash
# Uso: sudo ./servidor_periodico [Ts_ms] [Cs_ms] [prio] [duração_s]
sudo ./servidor_periodico 10 5 70 60
```

Onde:
- `Ts_ms` = 10 → período de 10 ms
- `Cs_ms` = 5 → budget de 5 ms (50% de utilização)
- `prio` = 70 → prioridade SCHED_FIFO
- `duração_s` = 60 → executa por 60 segundos

**O que observar:**
- Jobs enfileirados vs executados
- Resposta média e máxima
- % de períodos ociosos
- Budget médio utilizado

---

## 📊 Coleta de Dados (para o relatório)

### 1. Executar esteira por 60s

```bash
sudo ./esteira_linux
# Aguardar 60s observando métricas
# Pressionar 'q'
# Copiar últimas estatísticas para RESULTADOS.md
```

### 2. Testar eventos esporádicos

```bash
sudo ./esteira_linux
# Pressionar 'b' várias vezes
# Observar SORT_ACT nas métricas
```

### 3. Comparar com cyclictest

```bash
# Terminal 1
sudo ./esteira_linux

# Terminal 2
sudo cyclictest -p99 -t1 -n -m -i 5000 -D 60
```

Comparar latências (Max) dos dois.

### 4. Servidor periódico - cenários

```bash
# Cenário 1: Alta utilização (90%)
sudo ./servidor_periodico 10 9 70 60

# Cenário 2: Baixa utilização (30%)
sudo ./servidor_periodico 10 3 70 60

# Cenário 3: Período longo
sudo ./servidor_periodico 50 10 70 60
```

Anotar jobs perdidos, resposta máxima, % idle.

---

## 📝 Documentação Completa

- **`README.md`** → Como compilar o kernel RT (Parte 1 completa)
- **`src/README_LINUX.md`** → Arquitetura detalhada dos programas
- **`RESULTADOS.md`** → Template para preencher com seus dados
- **`TEST.md`** → Resultados de referência dos testes RT

---

## 🐛 Troubleshooting

### "Operation not permitted" ao executar

```bash
# Solução 1: usar sudo
sudo ./esteira_linux

# Solução 2: dar capabilities (permanente)
sudo setcap cap_sys_nice=eip ./esteira_linux
./esteira_linux  # agora roda sem sudo
```

### Latências muito altas (> 10 ms)

```bash
# Verificar se realmente está em RT
cat /sys/kernel/realtime  # deve ser 1

# Verificar CPU governor
cat /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor
# Deve estar em "performance"
```

### Compilação falha

```bash
# Instalar dependências
sudo apt install -y build-essential

# Verificar GCC
gcc --version  # deve ter >= 11.x
```

---

## 🎓 Para o Relatório

### Parte 1: Instalação do Kernel RT
- ✅ Capturas de tela de `uname -r` e `/sys/kernel/realtime`
- ✅ Saída de `dmesg | grep PREEMPT`
- ✅ Resultados de `cyclictest` e `ptsematest` (já em TEST.md)

### Parte 2: Adaptação do Código
- ✅ Comparar métricas ESP32 (TRAB_M2.md) vs Linux
- ✅ Preencher tabelas em RESULTADOS.md
- ✅ Discutir diferenças: VM vs bare-metal

### Parte 3: Servidor Periódico
- ✅ Testar 3 configurações diferentes (Ts, Cs)
- ✅ Análise: jobs perdidos, resposta, utilização
- ✅ Comparar overhead vs threads individuais

---

## 📞 Dúvidas Frequentes

**P: Preciso instalar FreeRTOS no Linux?**  
R: Não! O código foi adaptado para usar POSIX threads nativas.

**P: Por que não compilou o servidor TCP/UDP?**  
R: Removido para focar em RT. SNTP é suficiente para timestamps.

**P: Posso executar em WSL2?**  
R: Não recomendado. WSL2 não suporta kernel RT customizado. Use VirtualBox.

**P: Quantos GBs preciso?**  
R: Mínimo 60 GB de disco, 4 GB RAM. Recomendado: 80 GB, 8 GB RAM.

---

## ✅ Checklist de Entrega

- [ ] Kernel 6.14-rt3 compilado e funcionando
- [ ] `/sys/kernel/realtime` retorna 1
- [ ] `cyclictest` executado (resultados em TEST.md ou RESULTADOS.md)
- [ ] `esteira_linux` compilado e executado por 60s
- [ ] `servidor_periodico` testado com 3 configurações
- [ ] RESULTADOS.md preenchido com métricas
- [ ] Capturas de tela coletadas
- [ ] Relatório final em PDF

---

## 🔗 Links Úteis

- [Kernel RT oficial](https://kernel.org/pub/linux/kernel/projects/rt/)
- [VirtualBox](https://www.virtualbox.org/)
- [Lubuntu 24.04](https://cdimage.ubuntu.com/lubuntu/releases/noble/release/)
- [cyclictest man page](https://man7.org/linux/man-pages/man8/cyclictest.8.html)

---

**Boa sorte! 🚀**
