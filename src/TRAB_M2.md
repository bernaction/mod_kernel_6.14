# Esteira Industrial — ESP32 + FreeRTOS + Instrumentação RT

Projeto acadêmico desenvolvido para a disciplina **Sistemas em Tempo Real**, implementando uma **esteira industrial simulada** controlada por um **ESP32** com **FreeRTOS**.  
O sistema executa múltiplas tarefas concorrentes (hard e soft real-time), realiza instrumentação automática e coleta estatísticas de desempenho temporal (WCRT, HWM99, (m,k)-firm, bloqueio, preempção e timestamps).

---

## 🧩 Visão Geral

A esteira possui quatro tarefas principais de controle e instrumentação:

| Tarefa | Tipo | Período / Evento | Função |
|--------|------|------------------|--------|
| **ENC_SENSE** | Hard RT | 5 ms | Leitura do sensor de encoder (velocidade e posição) |
| **SPD_CTRL** | Hard RT | 5 ms | Controle PI simulado + interface com HMI |
| **SORT_ACT** | Evento (Touch OBJ) | — | Simula o atuador de separação de peças |
| **SAFE_STOP** | Evento (Touch E-STOP) | — | Emergência: para a esteira instantaneamente |

Há também a HMI (Touch HMI) como evento **soft real-time** e um servidor UDP/TCP opcional para teste de rede e RTT.

---

## ⚙️ Funcionalidades Principais

- 🧠 **Instrumentação completa por tarefa**  
  - WCRT (Worst-Case Response Time)  
  - Cmax, Lmax, Rmax (execução, latência, resposta)  
  - (m,k)-firm deadlines  
  - Percentil 99 (HWM99)  
  - Bloqueio e preempção máxima  
  - Timestamps (`release`, `start`, `end`)  

- 🧭 **Sincronização temporal** via SNTP com timestamp UTC µs.  
- 🔌 **Comunicação UDP/TCP** com cálculo de RTT e OWD.  
- 🖐️ **Touch pad polling** (GPIOs configurados para OBJ, HMI e E-STOP).  
- 💡 **LED de status** com piscar recursivo (`blink_led_recursive()`).  
- 📊 **Coleta de métricas e logs via UART** em formato tabular, 1 Hz.

---

## 🧱 Arquitetura do Sistema
          +------------------+
          | ENC_SENSE (5 ms) |──┐
          +------------------+  │
                                ▼
          +------------------+
          | SPD_CTRL (5 ms)  |───► PI Simulado + HMI
          +------------------+
                                │
      Touch OBJ ───► SORT_ACT ──┘
      Touch STOP ──► SAFE_STOP
      Touch HMI ──► Soft RT UI handler

   
O sistema é executado sobre **FreeRTOS** com uso de **semáforos e notificações diretas**, garantindo determinismo e mínima latência de comunicação entre tarefas.

---

## 📈 Exemplo de Saída (UART)
          I (8698) ESTEIRA: STATS: rpm=353.2 set=398.3 pos=174.8mm
          I (8698) ESTEIRA: ENC: rel=102 fin=102 hard=0 WCRT=786us HWM99≈748us Lmax=13us Cmax=773us (m,k)=(10,10)
          I (8718) ESTEIRA: CTRL: rel=102 fin=102 hard=0 WCRT=2027us HWM99≈748us Lmax=805us Cmax=1231us (m,k)=(10,10)
          I (8728) ESTEIRA: SORT: rel=1 fin=1 hard=0 WCRT=741us HWM99≈1082us Lmax=28us Cmax=713us (m,k)=(0,10)
          I (8738) ESTEIRA: SAFE: rel=1 fin=1 hard=0 WCRT=945us HWM99≈1082us Lmax=32us Cmax=913us (m,k)=(0,10)


---

## 🧪 Hardware Utilizado

| Componente | Descrição |
|-------------|------------|
| **ESP32 DevKit V1** | MCU dual-core 240 MHz, Wi-Fi integrado |
| **Touch sensors (GPIO27, 33, 32, 13)** | Entradas capacitivas (OBJ, HMI, E-STOP, SERVER) |
| **LED GPIO2** | Indicação visual e debug |
| **UART0** | Log serial e interface de monitoramento |
| **Rede Wi-Fi** | Envio de pacotes UDP/TCP para medição de RTT |

---

## 🧰 Build e Execução

### Pré-requisitos
- ESP-IDF v5.5.1 ou superior
- Python 3.8+
- Git e `idf.py` no PATH

### Compilação
```bash
idf.py set-target esp32
idf.py build
idf.py -p COMx flash monitor (substitua COMx pela porta do seu ESP32)
```


## 🧩 Estrutura do Código

          📁 main/
          ├── hello_world_main.c → app_main() e tarefas FreeRTOS
          ├── stats.c / stats.h → Instrumentação RT (WCRT, HWM99, (m,k))
          ├── touch.c / touch.h → Touch pad polling e calibração
          ├── net_udp.c / net_tcp.c → Servidores UDP e TCP
          └── utils.c / utils.h → SNTP, LED, logs, formatação
 

---

## 📊 Métricas Típicas (240 MHz)

| Tarefa    | WCRT   | Cmax   | HWM99  | (m,k)   | Tipo  |
| ---------- | ------ | ------ | ------ | ------- | ----- |
| ENC_SENSE | 1.9 ms | 1.9 ms | 1.0 ms | (10,10) | Hard  |
| SPD_CTRL  | 3.2 ms | 2.0 ms | 1.0 ms | (10,10) | Hard  |
| SORT_ACT  | 0.7 ms | 0.7 ms | 1.0 ms | (0,10)  | Event |
| SAFE_STOP | 0.9 ms | 0.9 ms | 1.0 ms | (0,10)  | Event |
