// Esteira Industrial (ESP32 + FreeRTOS) — 4 tasks + Touch (polling) + Instrumentação RT
// - ENC_SENSE (periódica 5 ms) -> notifica SPD_CTRL
// - SPD_CTRL (hard RT) -> controle PI simulado, trata HMI (soft) se solicitado
// - SORT_ACT (hard RT, evento Touch B) -> aciona "desviador"
// - SAFETY_TASK (hard RT, evento Touch D) -> E-stop
// - TOUCH (polling) e UART para injetar eventos
// - STATS imprime métricas RT: releases, hard_miss, Cmax, Lmax (evento->start), Rmax (evento->end)
// Requer ESP-IDF 5.5.x (driver touch legacy). O código compila mesmo sem habilitar trace/idle hook;
// se habilitar, imprime %CPU e runtime-stats.
//
// DICAS MENUCONFIG (opcionais p/ relatório):
//  - Component config → FreeRTOS → [x] Run FreeRTOS only on first core  (UNICORE)
//  - Component config → ESP32-specific → CPU frequency (80/160/240 MHz)
//  - (Opcional para CPU%) FreeRTOS → [x] Use Idle Hook
//  - (Opcional p/ runtime stats) FreeRTOS → [x] Enable Trace Facility
//                                  FreeRTOS → [x] Generate run time stats

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

#include "esp_timer.h"
#include "esp_log.h"
#include "esp_mac.h"

#include "driver/gpio.h"
#include "driver/touch_pad.h"   // legacy API (5.x)
#include "driver/uart.h"
#include <fcntl.h>
#include <errno.h>
#include <sys/unistd.h>
#include <sys/stat.h>
#include <sys/time.h>

// ====== Led ======
#define CONFIG_LED_PIN 2
#define ESP_INTR_FLAG_DEFAULT 0
#define CONFIG_BUTTON_PIN 0

// WIFI
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_sntp.h"
#include <time.h>
#include "lwip/sockets.h"
#include "lwip/netdb.h"

// ==== config WIFI ====
#define PC_IP   "172.20.10.12"
#define UDP_PORT 6010
#define TCP_PORT 5000

#define TAG "ESTEIRA"

// CONFIG DO WIFI
#define WIFI_SSID "berna12"
#define WIFI_PASS "12345678"

// ====== Mapeamento correto dos touch pads (ESP32 WROOM) ======
// T7 = GPIO27, T8 = GPIO33, T9 = GPIO32
#define TP_OBJ    TOUCH_PAD_NUM7   // Touch B -> detecção de objeto (GPIO27)
#define TP_HMI    TOUCH_PAD_NUM8   // Touch C -> HMI/telemetria   (GPIO33)
#define TP_ESTOP  TOUCH_PAD_NUM9   // Touch D -> E-stop           (GPIO32)
#define TP_SERVER TOUCH_PAD_NUM4   // Touch E -> Server           (GPIO13)


// ====== Periodicidade, prioridades, stack ======
#define ENC_T_MS        5
#define PRIO_ESTOP      5
#define PRIO_ENC        4
#define PRIO_CTRL       3
#define PRIO_SORT       3
#define PRIO_TOUCH      2
#define PRIO_STATS      1
#define PRIO_UDP        5
#define PRIO_TCP        5
#define STK_MAIN        4096
#define STK_AUX         4096

// ====== Deadlines (em microssegundos) ======
#define D_ENC_US    5000
#define D_CTRL_US  10000
#define D_SORT_US  10000
#define D_SAFE_US   5000

// ====== Handles/IPC ======
static TaskHandle_t hENC = NULL, hCTRL = NULL, hSORT = NULL, hSAFE = NULL;
static TaskHandle_t hTCP = NULL, hUDP = NULL; // handles for server tasks
static volatile bool tcp_should_run = false, udp_should_run = false;
static TaskHandle_t hCtrlNotify = NULL;    // ENC -> CTRL (notify)
typedef struct { int64_t t_evt_us; } sort_evt_t;
static QueueHandle_t qSort = NULL;
static SemaphoreHandle_t semEStop = NULL;  // disparado pelo Touch D
static SemaphoreHandle_t semHMI   = NULL;  // disparado pelo Touch C

// ====== Estado simulado da esteira ======
typedef struct {
    float rpm;     // medição de rpm
    float pos_mm;  // posição "andada" em mm
    float set_rpm; // referência de rpm
} belt_state_t;

static belt_state_t g_belt = { .rpm = 0.f, .pos_mm = 0.f, .set_rpm = 120.0f };

// ====== Instrumentação de tempo/métricas ======
typedef struct {
    volatile uint32_t releases, starts, finishes;
    volatile uint32_t hard_miss, soft_miss;
    volatile int64_t  last_release_us, last_start_us, last_end_us;
    volatile int64_t  worst_exec_us, worst_latency_us, worst_response_us;

    // ---- HWM(99%) / P99 ----
    #define RBUF 256
    volatile uint16_t r_count;
    volatile int32_t  r_buf[RBUF];

    // ---- (m,k)-firm (k <= 16) ----
    volatile uint8_t  k_window;     // ex.: 10
    volatile uint8_t  win_filled;   // quantos válidos na janela
    volatile uint16_t win_mask;     // últimos k bits (1=sucesso, 0=deadline miss)

    // ---- Interferência & Bloqueio ----
    volatile uint32_t preemptions;      // (opcional: preenchido via hook)
    volatile int64_t  blocked_us_total; // soma de tempos bloqueado
} rt_stats_t;

static rt_stats_t st_enc  = { .k_window = 10 };
static rt_stats_t st_ctrl = { .k_window = 10 };  // pode tratar como hard+soft (ver abaixo)
static rt_stats_t st_sort = { .k_window = 10 };
static rt_stats_t st_safe = { .k_window = 10 };


static volatile int64_t g_last_touchB_us = 0;     // release do Touch B
static volatile int64_t g_last_touchD_us = 0;     // release do Touch D
static volatile int64_t g_last_enc_release_us = 0;// release do ENC (para CTRL)

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

    int64_t lat  = s->last_start_us - s->last_release_us;
    if (lat > s->worst_latency_us) s->worst_latency_us = lat;

    // deadline miss (hard/soft conforme flag)
    if (resp > D_us) {
        if (hard) s->hard_miss++; else s->soft_miss++;
    }

    // guarda R para HWM(99%)
    if (s->r_count < RBUF) s->r_buf[s->r_count++] = (int32_t)resp;

    // (m,k)-firm (k <= 16). 1 = cumpriu deadline; 0 = perdeu
    uint8_t k = s->k_window ? s->k_window : 10;
    uint8_t hit = (resp <= D_us) ? 1 : 0;
    s->win_mask = ((s->win_mask << 1) | hit) & ((1u << k) - 1);
    if (s->win_filled < k) s->win_filled++;
}


static int32_t p99_of_buf(volatile int32_t *v, volatile uint16_t n){
    if (n == 0) return 0;
    // cópia local pra ordenar (n <= 256)
    int32_t tmp[RBUF];
    uint16_t m = n; if (m > RBUF) m = RBUF;
    for (uint16_t i=0;i<m;i++) tmp[i] = v[i];
    // insertion sort simples
    for (int i=1;i<m;i++){ int32_t key=tmp[i], j=i-1; while(j>=0 && tmp[j]>key){ tmp[j+1]=tmp[j]; j--; } tmp[j+1]=key; }
    int idx = (int)(0.99*(m-1)); if (idx<0) idx=0; if (idx>=m) idx=m-1;
    return tmp[idx];
}

static uint32_t mk_hits(const rt_stats_t *s){
    uint8_t k = s->k_window ? s->k_window : 10;
    uint16_t mask = s->win_mask & ((1u<<k)-1);
    uint32_t hits = 0; for(uint8_t i=0;i<k;i++) hits += (mask>>i) & 1u;
    return (s->win_filled < k) ? 0 : hits; // só reporta quando janela cheia
}


// [P3] epoch em micros (usa SNTP/CLOCK_REALTIME)
static inline int64_t now_us_epoch(void){
    struct timeval tv; gettimeofday(&tv, NULL);
    return (int64_t)tv.tv_sec*1000000LL + tv.tv_usec;
}

// [P3] extrai int64 de um "json" simples: procura por "key":<numero>
static bool parse_i64_from_json(const char *s, const char *key, long long *out){
    char pat[64]; snprintf(pat, sizeof(pat), "\"%s\":", key);
    const char *p = strstr(s, pat);
    if(!p) return false;
    p += strlen(pat);
    // pula espaços
    while(*p==' '||*p=='\t') p++;
    // aceita +/-
    bool neg = (*p=='-'); if(*p=='+'||*p=='-') p++;
    long long v = 0; bool ok=false;
    while(*p>='0' && *p<='9'){ v = v*10 + (*p-'0'); p++; ok=true; }
    if(!ok) return false;
    *out = neg ? -v : v;
    return true;
}



static inline void now_str(char *buf, size_t len) {
    struct timeval tv; 
    gettimeofday(&tv, NULL);          // CLOCK_REALTIME (SNTP)
    struct tm tm; 
    localtime_r(&tv.tv_sec, &tm);          // fuso local já setado
    int ms = (int)(tv.tv_usec / 1000);
    snprintf(buf, len, "%02d/%02d/%04d %02d:%02d:%02d.%03d",
             tm.tm_mday, tm.tm_mon + 1, tm.tm_year + 1900,
             tm.tm_hour, tm.tm_min, tm.tm_sec, ms);
}

// ====== Busy loop determinístico (~simula WCET) ======
static inline void cpu_tight_loop_us(uint32_t us) {
    int64_t start = esp_timer_get_time();
    while ((esp_timer_get_time() - start) < us) { __asm__ __volatile__("nop"); }
}

// ====== ms -> ticks (garante >=1 tick quando ms>0) ======
static inline TickType_t ticks_from_ms(uint32_t ms) {
    TickType_t t = pdMS_TO_TICKS(ms);
    if (ms > 0 && t == 0) return 1;
    return t;
}

// ========= Prototipação =========
static void task_enc_sense(void *arg);
static void task_spd_ctrl(void *arg);
static void task_sort_act(void *arg);
static void task_safety(void *arg);
static void task_touch_poll(void *arg);
static void task_stats(void *arg);
static void task_uart_cmd(void *arg);
static void toggle_server_tasks(void);

static void blink_led_recursive(int n, TickType_t ticks) {
    if (n <= 0) return;
    gpio_set_level(CONFIG_LED_PIN, 1);
    vTaskDelay(ticks);
    gpio_set_level(CONFIG_LED_PIN, 0);
    vTaskDelay(ticks);
    blink_led_recursive(n - 1, ticks);
}

// (Opcional) Idle hook para %CPU simples
#if configUSE_IDLE_HOOK
static volatile int64_t idle_us_acc = 0;
void vApplicationIdleHook(void) {
    static int64_t last = 0;
    int64_t now = esp_timer_get_time();
    if (last) idle_us_acc += (now - last);
    last = now;
}
#endif

// (Opcional) Runtime stats (requer Trace Facility + Run Time Stats)
#if (configUSE_TRACE_FACILITY==1) && (configGENERATE_RUN_TIME_STATS==1)
static void print_runtime_stats(void) {
    static char buf[1024];
    vTaskGetRunTimeStats(buf);
    ESP_LOGI(TAG, "\nTask               Time(us)   %%CPU\n%s", buf);
}
#endif

/* ====== ENC_SENSE (periódica 5 ms): estima velocidade/posição ====== */
static void task_enc_sense(void *arg){
    TickType_t next = xTaskGetTickCount();
    TickType_t T = ticks_from_ms(ENC_T_MS);

    for (;;) {
        int64_t t_rel = esp_timer_get_time();
        stats_on_release(&st_enc, t_rel);
        g_last_enc_release_us = t_rel;

        stats_on_start(&st_enc, esp_timer_get_time());

        // Dinâmica simulada: aproxima rpm do setpoint, integra posição
        float err = g_belt.set_rpm - g_belt.rpm;
        g_belt.rpm += 0.05f * err; // aproximação lenta para simular inércia
        g_belt.pos_mm += (g_belt.rpm / 60.0f) * (ENC_T_MS / 1000.0f) * 100.0f; // 100 mm por rev

        // Limites físicos da simulação
        if (g_belt.rpm < 0.0f)     g_belt.rpm = 0.0f;
        if (g_belt.rpm > 5000.0f)  g_belt.rpm = 5000.0f;

        // Carga determinística ~0.7 ms
        cpu_tight_loop_us(700);

        stats_on_finish(&st_enc, esp_timer_get_time(), D_ENC_US, /*hard=*/true);

        // Notifica controle (encadeamento)
        if (hCtrlNotify) xTaskNotifyGive(hCtrlNotify);

        vTaskDelayUntil(&next, T);
    }
}

/* ====== SPD_CTRL (encadeada): controle PI simulado + HMI (soft) ====== */
static void task_spd_ctrl(void *arg){
    float kp = 0.4f, ki = 0.1f, integ = 0.f;
    int64_t tb = esp_timer_get_time();
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    int64_t ta = esp_timer_get_time();
    st_ctrl.blocked_us_total += (ta - tb);

    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);   // acorda após ENC
        stats_on_release(&st_ctrl, g_last_enc_release_us);
        stats_on_start(&st_ctrl, esp_timer_get_time());

        // Controle (hard): PI
        float err = g_belt.set_rpm - g_belt.rpm;
        integ += err * (ENC_T_MS / 1000.0f);
        // anti-windup simples
        if (integ > 10000.0f) integ = 10000.0f;
        if (integ < -10000.0f) integ = -10000.0f;

        float u = kp * err + ki * integ;
        g_belt.set_rpm += 0.1f * u;

        // clamps de segurança da referência
        if (g_belt.set_rpm < 0.0f)     g_belt.set_rpm = 0.0f;
        if (g_belt.set_rpm > 5000.0f)  g_belt.set_rpm = 5000.0f;

        // Carga determinística ~1.2 ms
        cpu_tight_loop_us(1200);

        stats_on_finish(&st_ctrl, esp_timer_get_time(), D_CTRL_US, /*hard=*/true);

        // Trecho não-crítico (soft): HMI
        if (xSemaphoreTake(semHMI, 0) == pdTRUE) {
            // Ex.: se quiser medir soft-miss, poderia usar outro rt_stats_t com D=50ms
            printf("HMI: rpm=%.1f set=%.1f pos=%.1fmm\n", g_belt.rpm, g_belt.set_rpm, g_belt.pos_mm);
            cpu_tight_loop_us(400); // ~0.4 ms (soft)
        }
    }
}

/* ====== SORT_ACT (evento Touch B): aciona "desviador" ====== */
static void task_sort_act(void *arg) {
    sort_evt_t ev;
    for (;;) {
        int64_t tb = esp_timer_get_time();
        if (xQueueReceive(qSort, &ev, portMAX_DELAY) == pdTRUE) {
            int64_t ta = esp_timer_get_time();
            st_sort.blocked_us_total += (ta - tb);
            stats_on_release(&st_sort, ev.t_evt_us);
            stats_on_start(&st_sort, esp_timer_get_time());

            // Janela crítica (ex.: 0.7 ms) — aqui acionaria o atuador/suporte
            cpu_tight_loop_us(700);

            stats_on_finish(&st_sort, esp_timer_get_time(), D_SORT_US, /*hard=*/true);
        }
    }
}

/* ====== SAFETY_TASK (evento Touch D): E-stop ====== */
static void task_safety(void *arg) {
    for (;;) {
        int64_t tb = esp_timer_get_time();
        if (xSemaphoreTake(semEStop, portMAX_DELAY) == pdTRUE) {
            int64_t ta = esp_timer_get_time();
            st_safe.blocked_us_total += (ta - tb);
            stats_on_release(&st_safe, g_last_touchD_us);
            stats_on_start(&st_safe, esp_timer_get_time());

            // Ação crítica: zera "PWM", trava atuadores, sinaliza alarme (~0.9 ms)
            g_belt.set_rpm = 0.f;
            cpu_tight_loop_us(900);

            stats_on_finish(&st_safe, esp_timer_get_time(), D_SAFE_US, /*hard=*/true);
            ESP_LOGW(TAG, "E-STOP executado");

            //bool led_status = true;
            //gpio_set_level(CONFIG_LED_PIN, led_status);
        }
    }
}

/* ====== TOUCH polling (B, C, D) — voltagem, filtro, baseline e debounce ====== */
static void task_touch_poll(void *arg) {
    ESP_ERROR_CHECK(touch_pad_init());
    ESP_ERROR_CHECK(touch_pad_set_fsm_mode(TOUCH_FSM_MODE_TIMER));
    ESP_ERROR_CHECK(touch_pad_set_measurement_interval(20)); // ~2.5ms
    ESP_ERROR_CHECK(touch_pad_set_voltage(TOUCH_HVOLT_2V7, TOUCH_LVOLT_0V5, TOUCH_HVOLT_ATTEN_1V));
    ESP_ERROR_CHECK(touch_pad_filter_start(10));

    // Configura pads (threshold=0; comparamos com baseline no software)
    ESP_ERROR_CHECK(touch_pad_config(TP_OBJ,    0));
    ESP_ERROR_CHECK(touch_pad_config(TP_HMI,    0));
    ESP_ERROR_CHECK(touch_pad_config(TP_ESTOP,  0));
    ESP_ERROR_CHECK(touch_pad_config(TP_SERVER, 0));

    touch_pad_clear_status();
    vTaskDelay(ticks_from_ms(500)); // estabiliza

    uint16_t base_obj=0, base_hmi=0, base_stop=0, base_server=0;
    ESP_ERROR_CHECK(touch_pad_read_raw_data(TP_OBJ,    &base_obj));
    ESP_ERROR_CHECK(touch_pad_read_raw_data(TP_HMI,    &base_hmi));
    ESP_ERROR_CHECK(touch_pad_read_raw_data(TP_ESTOP,  &base_stop));
    ESP_ERROR_CHECK(touch_pad_read_raw_data(TP_SERVER, &base_server));
    if (base_obj  < 50) base_obj  = 2000;
    if (base_hmi  < 50) base_hmi  = 2000;
    if (base_stop < 50) base_stop = 2000;

    ESP_LOGI(TAG, "RAW baseline: OBJ=%u (T7/GPIO27)  HMI=%u (T8/GPIO33)  ESTOP=%u (T9/GPIO32)  SERVER=%u (T4/GPIO13)",
             base_obj, base_hmi, base_stop, base_server);

    uint16_t th_obj    = (uint16_t)(base_obj    * 0.70f);
    uint16_t th_hmi    = (uint16_t)(base_hmi    * 0.70f);
    uint16_t th_stop   = (uint16_t)(base_stop   * 0.70f);
    uint16_t th_server = (uint16_t)(base_server * 0.70f);

    bool prev_obj=false, prev_hmi=false, prev_stop=false, prev_server=false;
    TickType_t debounce = ticks_from_ms(30);

    while (1) {
        uint16_t raw;

        // OBJ (Touch B)
        touch_pad_read_raw_data(TP_OBJ, &raw);
        bool obj = (raw < th_obj);
        if (obj && !prev_obj) {
            g_last_touchB_us = esp_timer_get_time();         // release do evento
            sort_evt_t e = { .t_evt_us = g_last_touchB_us }; // passa release para a task
            (void)xQueueSend(qSort, &e, 0);
            char ts[32]; now_str(ts, sizeof(ts));
            ESP_LOGW(TAG, "[%s] OBJ: raw=%u (thr=%u)", ts, raw, th_obj);
            blink_led_recursive(5, ticks_from_ms(250));
        }
        prev_obj = obj;

        // HMI (Touch C)
        touch_pad_read_raw_data(TP_HMI, &raw);
        bool hmi = (raw < th_hmi);
        if (hmi && !prev_hmi) { 
            (void)xSemaphoreGive(semHMI); 
            char ts[32]; 
            now_str(ts, sizeof(ts)); 
            ESP_LOGW(TAG, "[%s] HMI: raw=%u (thr=%u)", ts, raw, th_hmi);
            blink_led_recursive(5, ticks_from_ms(150));
        }
        prev_hmi = hmi;

        // ESTOP (Touch D)
        touch_pad_read_raw_data(TP_ESTOP, &raw);
        bool stop = (raw < th_stop);
        if (stop && !prev_stop) {
            g_last_touchD_us = esp_timer_get_time();         // release do evento
            (void)xSemaphoreGive(semEStop);
            ESP_LOGW(TAG, "E-STOP: raw=%u (thr=%u)", raw, th_stop);
            blink_led_recursive(5, ticks_from_ms(100));
        }
        prev_stop = stop;

        // SERVER (Touch E) — alterna entre tarefas TCP/UDP
        touch_pad_read_raw_data(TP_SERVER, &raw);
        bool server = (raw < th_server);
        if (server && !prev_server) {
            char ts[32]; now_str(ts, sizeof(ts));
            ESP_LOGW(TAG, "[%s] SERVER touch pressed: raw=%u (thr=%u)", ts, raw, th_server);
            // toggle server tasks (graceful stop/start)
            toggle_server_tasks();
            blink_led_recursive(3, ticks_from_ms(150));
        }
        prev_server = server;

        vTaskDelay(debounce);
    }
}

/* ====== UART CMD: converte teclas do terminal em eventos (B/C/D) e lê RAWs ====== */
static void task_uart_cmd(void *arg)
{
    const uart_port_t U = UART_NUM_0;
    uart_driver_install(U, 256, 0, 0, NULL, 0);
    char ts[32]; now_str(ts, sizeof(ts));
    ESP_LOGI(TAG, "[%s] UART: b=OBJ  c=HMI  d=E-STOP  r=RAWs", ts);

    uint8_t ch;
    while (1) {
        if (uart_read_bytes(U, &ch, 1, pdMS_TO_TICKS(10)) == 1) {
            switch (ch) {
                case 'b': case 'B': {
                    g_last_touchB_us = esp_timer_get_time();
                    sort_evt_t e = { g_last_touchB_us };
                    xQueueSend(qSort,&e,0);
                    char ts[32]; now_str(ts, sizeof(ts));
                    ESP_LOGI(TAG,"[%s] [UART] OBJ", ts);
                } break;
                case 'c': case 'C': { xSemaphoreGive(semHMI); char ts[32]; now_str(ts, sizeof(ts)); ESP_LOGI(TAG,"[%s] [UART] HMI", ts); } break;
                case 'd': case 'D': {
                    g_last_touchD_us = esp_timer_get_time();
                    xSemaphoreGive(semEStop);
                    ESP_LOGW(TAG,"[UART] E-STOP");
                } break;
                case 'r': case 'R': {
                    uint16_t ro=0, rc=0, rd=0;
                    touch_pad_read_raw_data(TP_OBJ,&ro);
                    touch_pad_read_raw_data(TP_HMI,&rc);
                    touch_pad_read_raw_data(TP_ESTOP,&rd);
                    char ts[32]; now_str(ts, sizeof(ts));
                    ESP_LOGI(TAG, "[%s] RAWs -> OBJ=%u  HMI=%u  ESTOP=%u", ts, ro, rc, rd);
                } break;
                default: break;
            }
        }
        vTaskDelay(ticks_from_ms(5));
    }
}

/* ====== STATS: log 1x/s ====== */
static void task_stats(void *arg)
{
    TickType_t last = xTaskGetTickCount();
    TickType_t period = ticks_from_ms(1000);

    // Para %CPU simples com Idle Hook:
    int64_t last_sample = 0, last_idle = 0;

    for (;;) {
        vTaskDelayUntil(&last, period);

        char ts[32]; 
        now_str(ts, sizeof(ts));

        ESP_LOGI(TAG, "[%s] =====================================================================", ts);

        now_str(ts, sizeof(ts));

        ESP_LOGI(TAG, "[%s] UART: b=OBJ  c=HMI  d=E-STOP  r=RAWs", ts);

        now_str(ts, sizeof(ts));

        ESP_LOGI(TAG, "[%s] STATS: rpm=%.1f set=%.1f pos=%.1fmm", ts, g_belt.rpm, g_belt.set_rpm, g_belt.pos_mm);

        now_str(ts, sizeof(ts));
        
        int32_t p99 = p99_of_buf(st_enc.r_buf, st_enc.r_count);
        ESP_LOGI(TAG,
        "[%s] ENC: rel=%u fin=%u hard=%u WCRT=%lldus HWM99≈%dus Lmax=%lldus Cmax=%lldus (m,k)=(%u,%u) block=%lldus preempt=%u",
        ts, st_enc.releases, st_enc.finishes, st_enc.hard_miss,
        (long long)st_enc.worst_response_us, (int)p99,
        (long long)st_enc.worst_latency_us, (long long)st_enc.worst_exec_us,
        mk_hits(&st_enc), st_enc.k_window,
        (long long)st_enc.blocked_us_total, st_enc.preemptions);

        
        now_str(ts, sizeof(ts));
        
        ESP_LOGI(TAG,
            "[%s] CTRL: rel=%u fin=%u hard=%u WCRT=%lldus HWM99≈%dus Lmax=%lldus Cmax=%lldus (m,k)=(%u,%u) block=%lldus preempt=%u",
            ts,st_ctrl.releases, st_ctrl.finishes, st_ctrl.hard_miss,
            (long long)st_ctrl.worst_response_us, (int)p99,
            (long long)st_ctrl.worst_latency_us, (long long)st_ctrl.worst_exec_us,
            mk_hits(&st_ctrl), st_ctrl.k_window,
            (long long)st_ctrl.blocked_us_total, st_ctrl.preemptions);

        now_str(ts, sizeof(ts));
        
        ESP_LOGI(TAG,
            "[%s] SORT: rel=%u fin=%u hard=%u WCRT=%lldus HWM99≈%dus Lmax=%lldus Cmax=%lldus (m,k)=(%u,%u) block=%lldus preempt=%u",
            ts,st_sort.releases, st_sort.finishes, st_sort.hard_miss,
            (long long)st_sort.worst_response_us, (int)p99,
            (long long)st_sort.worst_latency_us, (long long)st_sort.worst_exec_us,
            mk_hits(&st_sort), st_sort.k_window,
            (long long)st_sort.blocked_us_total, st_sort.preemptions);

        now_str(ts, sizeof(ts));
        
        ESP_LOGI(TAG,
            "[%s] SAFE: rel=%u fin=%u hard=%u WCRT=%lldus HWM99≈%dus Lmax=%lldus Cmax=%lldus (m,k)=(%u,%u) block=%lldus preempt=%u",
            ts,st_safe.releases, st_safe.finishes, st_safe.hard_miss,
            (long long)st_safe.worst_response_us, (int)p99,
            (long long)st_safe.worst_latency_us, (long long)st_safe.worst_exec_us,
            mk_hits(&st_safe), st_safe.k_window,
            (long long)st_safe.blocked_us_total, st_safe.preemptions);

        // %CPU simples via Idle Hook (se habilitado)
        #if configUSE_IDLE_HOOK
        {
            int64_t now = esp_timer_get_time();
            if (!last_sample) { last_sample = now; last_idle = idle_us_acc; }
            else {
                int64_t dt = now - last_sample;
                int64_t didle = idle_us_acc - last_idle;
                float cpu = 100.0f * (1.0f - (float)didle/(float)dt);
                ESP_LOGI(TAG, "CPU Util (IdleHook): %.1f%% (janela %.0f ms)", cpu, dt/1000.0f);
                last_sample = now; last_idle = idle_us_acc;
            }
        }
        #endif

        // Runtime stats do FreeRTOS (se habilitado no menuconfig)
        #if (configUSE_TRACE_FACILITY==1) && (configGENERATE_RUN_TIME_STATS==1)
            print_runtime_stats();
        #endif
    }
}

//Configura WiFi da ESP32
static void wifi_init(void){
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    wifi_config_t wc = {0};
    strcpy((char*)wc.sta.ssid, WIFI_SSID);
    strcpy((char*)wc.sta.password, WIFI_PASS);
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wc));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_connect());
}

//Apenas para Log
static void time_sync_notification_cb(struct timeval *tv){
    ESP_LOGI(TAG, "Tempo sincronizado via SNTP.");
}

//Gera dados e envia para o Servidor UDP no computador
static void stop_udp(void);
static void start_udp(void);

static void udp_task(void *arg){
    udp_should_run = true;

    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock < 0) {
        ESP_LOGE(TAG, "[UDP] socket() falhou");
        vTaskDelete(NULL);
        return;
    }

    // destino: seu PC
    struct sockaddr_in dest = {0};
    dest.sin_family = AF_INET;
    dest.sin_port   = htons(UDP_PORT);
    inet_pton(AF_INET, PC_IP, &dest.sin_addr.s_addr);

    // timeout p/ não travar no recvfrom
    struct timeval tv = {.tv_sec=0, .tv_usec=400000}; // 400 ms
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    uint32_t seq = 0;
    while (udp_should_run) {
        // marca de envio no ESP (epoch µs)
        int64_t t0 = now_us_epoch();

        // payload com metadados para o PC ecoar de volta
        char tx[160];
        int n = snprintf(tx, sizeof(tx), "{\"seq\":%u,\"proto\":\"UDP\",\"t_esp_send_us\":%lld}", (unsigned int)seq, (long long)t0);

        // envia
        int s = sendto(sock, tx, n, 0, (struct sockaddr*)&dest, sizeof(dest));
        if (s < 0) {
            ESP_LOGW(TAG, "[UDP] sendto falhou (seq=%u)", seq);
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }

        // aguarda reply do PC
        char rx[256];
        struct sockaddr_in from; socklen_t flen = sizeof(from);
        int r = recvfrom(sock, rx, sizeof(rx)-1, 0, (struct sockaddr*)&from, &flen);
        int64_t t3 = now_us_epoch();

        if (r > 0) {
            rx[r] = 0;
            long long t_pc_recv_us = 0, t_pc_send_us = 0;
            bool ok_recv = parse_i64_from_json(rx, "t_pc_recv_us", &t_pc_recv_us);
            bool ok_send = parse_i64_from_json(rx, "t_pc_send_us", &t_pc_send_us);
            long long rtt_us = (long long)(t3 - t0);
            // One-way (assume relógios SNTP alinhados):
            //   ESP -> PC  ~ t_pc_recv_us - t0
            //   PC  -> ESP ~ t3 - t_pc_send_us
            long long owd_esp2pc_us = ok_recv ? (t_pc_recv_us - (long long)t0) : -1;
            long long owd_pc2esp_us = ok_send ? ((long long)t3 - t_pc_send_us) : -1;
            if (ok_recv && ok_send) {
                ESP_LOGI(TAG,
                    "[UDP] seq=%u RTT=%lldus | owd esp->pc=%lldus pc->esp=%lldus | RX=%s",
                    seq, rtt_us, owd_esp2pc_us, owd_pc2esp_us, rx);
            } else {
                ESP_LOGI(TAG,
                    "[UDP] seq=%u RTT=%lldus | (sem campos p/ owd) | RX=%s",
                    seq, rtt_us, rx);
            }
        } else {
            ESP_LOGW(TAG, "[UDP] seq=%u sem resposta (timeout)", seq);
        }
        seq++;
        vTaskDelay(pdMS_TO_TICKS(200));
    }

    close(sock);
    hUDP = NULL;
    udp_should_run = false;
    vTaskDelete(NULL);
}


//Espera pacotes enviados pelo PC usando Protocolo TCP e devolve informações
static void stop_tcp(void);
static void start_tcp(void);

static void tcp_server_task(void *arg) {
    tcp_should_run = true;
    int listen_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(TCP_PORT);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    bind(listen_fd, (struct sockaddr*)&addr, sizeof(addr));
    listen(listen_fd, 1);
    // set non-blocking to allow periodic check of tcp_should_run
    int flags = fcntl(listen_fd, F_GETFL, 0);
    fcntl(listen_fd, F_SETFL, flags | O_NONBLOCK);
    char ts[32]; now_str(ts, sizeof(ts));
    ESP_LOGE(TAG, "[%s] Servidor TCP na porta %d", ts, TCP_PORT);
    blink_led_recursive(3, ticks_from_ms(150));

    while (tcp_should_run) {
        struct sockaddr_in6 source_addr; socklen_t addr_len = sizeof(source_addr);
        int sock = accept(listen_fd, (struct sockaddr *)&source_addr, &addr_len);
        if (sock < 0) {
            int e = errno;
            // log reason for accept failure (common: EWOULDBLOCK/EAGAIN when non-blocking)
            if (e != EWOULDBLOCK && e != EAGAIN) {
                char ts2[32]; now_str(ts2, sizeof(ts2));
                ESP_LOGE(TAG, "[%s] accept() failed: errno=%d (%s)", ts2, e, strerror(e));
            }
            // no connection; sleep a bit and re-check run flag
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }
        char ts[32]; 
        now_str(ts, sizeof(ts));
        ESP_LOGW(TAG, "[%s] Cliente conectado", ts);

        // Envia mensagem de boas-vindas
        const char *hello = "ESP32: conectado!\n";
        send(sock, hello, strlen(hello), 0);

        char rx[256];
        while (tcp_should_run) {
            int len = recv(sock, rx, sizeof(rx)-1, 0);
            if (len <= 0) {
                int er = errno;
                char ts[32]; now_str(ts, sizeof(ts));
                if (len == 0) {
                    ESP_LOGW(TAG, "[%s] Cliente fechou a conexão (len=0)", ts);
                } else {
                    ESP_LOGW(TAG, "[%s] recv() <=0 (len=%d) errno=%d (%s)", ts, len, er, strerror(er));
                }
                break;
            }
            rx[len] = 0;
            int64_t t_esp_recv_us = now_us_epoch();
            unsigned seq = 0;
            long long t_pc_send_us = 0;
            {
                // pega "seq" e "t_pc_send_us" com os helpers
                // primeiro, acha começo do '{' p/ o "json" leve:
                const char *json = strchr(rx, '{');
                if (json) {
                    long long tmp = 0;
                    if (parse_i64_from_json(json, "seq", &tmp)) seq = (unsigned)tmp;
                    parse_i64_from_json(json, "t_pc_send_us", &t_pc_send_us);
                }
            }

            int64_t t_esp_send_us = now_us_epoch();

            // Monta resposta
            char tx[256];
            int tn = snprintf(tx, sizeof(tx),
                "{\"ok\":true,\"proto\":\"TCP\",\"seq\":%u,"
                "\"t_pc_send_us\":%lld,"
                "\"t_esp_recv_us\":%lld,"
                "\"t_esp_send_us\":%lld}\n",
                seq, (long long)t_pc_send_us,
                (long long)t_esp_recv_us, (long long)t_esp_send_us);

            send(sock, tx, tn, 0);
        }
        shutdown(sock, 0);
        close(sock);
    }

    // cleanup
    if (listen_fd >= 0) close(listen_fd);
    hTCP = NULL;
    tcp_should_run = false;
    vTaskDelete(NULL);
}

// ----- Start/Stop helpers for TCP/UDP tasks -----
static void start_tcp(void) {
    if (hTCP) return;
    tcp_should_run = true;
    xTaskCreatePinnedToCore(tcp_server_task, "tcp_server", STK_MAIN, NULL, PRIO_TCP, &hTCP, 0);
}

static void stop_tcp(void) {
    if (!hTCP) return;
    tcp_should_run = false; // request graceful stop
    // wait up to 2s for task to finish
    for (int i = 0; i < 20 && hTCP; ++i) vTaskDelay(pdMS_TO_TICKS(100));
    if (hTCP) { // force delete as fallback
        vTaskDelete(hTCP);
        hTCP = NULL;
    }
}

static void start_udp(void) {
    if (hUDP) return;
    udp_should_run = true;
    xTaskCreatePinnedToCore(udp_task, "udp_task", STK_MAIN, NULL, PRIO_UDP, &hUDP, 0);
    blink_led_recursive(2, ticks_from_ms(150));
    char ts[32]; now_str(ts, sizeof(ts));
    ESP_LOGE(TAG, "[%s] Servidor UDP na porta %d", ts, UDP_PORT);
}

static void stop_udp(void) {
    if (!hUDP) return;
    udp_should_run = false;
    for (int i = 0; i < 20 && hUDP; ++i) vTaskDelay(pdMS_TO_TICKS(100));
    if (hUDP) {
        vTaskDelete(hUDP);
        hUDP = NULL;
    }
}

static void toggle_server_tasks(void) {
    // If TCP running, stop it and start UDP
    if (hTCP) {
        stop_tcp();
        start_udp();
        return;
    }
    // If UDP running, stop it and start TCP
    if (hUDP) {
        stop_udp();
        start_tcp();
        return;
    }
    // If none running, start UDP by default
    start_udp();
}

/* ====== app_main ====== */
void app_main(void) {

    //WIFI SNTP
    ESP_ERROR_CHECK(nvs_flash_init());
    wifi_init();

    wifi_ap_record_t ap;
    while (esp_wifi_sta_get_ap_info(&ap) != ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    esp_netif_t *sta = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_ip_info_t ip;
    do {
        vTaskDelay(pdMS_TO_TICKS(1000));
    } while (esp_netif_get_ip_info(sta, &ip) != ESP_OK || ip.ip.addr == 0);

    // Define fuso (Brasil sem DST): BRT-3
    setenv("TZ", "<-03>3", 1);  // UTC-3 fixo
    tzset();

    // SNTP
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_set_time_sync_notification_cb(time_sync_notification_cb);
    sntp_setservername(0, "br.pool.ntp.org");
    sntp_setservername(1, "a.st1.ntp.br");
    sntp_setservername(2, "b.st1.ntp.br");
    sntp_setservername(3, "pool.ntp.org");
    sntp_init();


    while(sntp_get_sync_status() != SNTP_SYNC_STATUS_COMPLETED){
        vTaskDelay(pdMS_TO_TICKS(1000));
    };

    // IPC
    qSort    = xQueueCreate(8, sizeof(sort_evt_t));
    semEStop = xSemaphoreCreateBinary();
    semHMI   = xSemaphoreCreateBinary();

    // LED
    gpio_set_direction(CONFIG_LED_PIN, GPIO_MODE_OUTPUT);  

    // Tarefas principais (core 0)
    xTaskCreatePinnedToCore(task_safety,    "SAFETY",    STK_MAIN, NULL, PRIO_ESTOP, &hSAFE, 0);
    xTaskCreatePinnedToCore(task_enc_sense, "ENC_SENSE", STK_MAIN, NULL, PRIO_ENC,   &hENC,  0);
    xTaskCreatePinnedToCore(task_spd_ctrl,  "SPD_CTRL",  STK_MAIN, NULL, PRIO_CTRL,  &hCTRL, 0);
    xTaskCreatePinnedToCore(task_sort_act,  "SORT_ACT",  STK_MAIN, NULL, PRIO_SORT,  &hSORT, 0);

    // Encadeamento ENC -> CTRL
    hCtrlNotify = hCTRL;

    // Tarefas auxiliares
    xTaskCreatePinnedToCore(task_touch_poll, "TOUCH",    STK_AUX, NULL, PRIO_TOUCH, NULL, 0);
    xTaskCreatePinnedToCore(task_uart_cmd,   "UART_CMD", STK_AUX, NULL, PRIO_TOUCH, NULL, 0);
    xTaskCreatePinnedToCore(task_stats,      "STATS",    STK_AUX, NULL, PRIO_STATS, NULL, 0);

    //TASKS SERVER
    //xTaskCreatePinnedToCore(tcp_server_task, "tcp_server", STK_MAIN, NULL, PRIO_TCP, NULL, 0);
    //xTaskCreatePinnedToCore(udp_task,        "udp_task",   STK_MAIN, NULL, PRIO_UDP, NULL, 0);
    start_udp();
    
    ESP_LOGI(TAG, "Sistema iniciado");
}