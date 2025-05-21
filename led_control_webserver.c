// -----------------------------------------------------------------------------
// Projeto: GreenLife Integrado - Webserver + Funcionalidades Físicas
// Autor: Levi Silva Freitas
// Data: 2025
// Descrição: Sistema unificado com monitoramento web e feedback visual/físico
// -----------------------------------------------------------------------------

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "pico/stdlib.h"
#include "hardware/adc.h"
#include "hardware/pwm.h"
#include "hardware/i2c.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"

#include "pico/cyw43_arch.h"
#include "lwip/tcp.h"
#include "lwip/pbuf.h"
#include "lwip/netif.h"

#include "lib/ssd1306.h"
#include "lib/font.h"
#include "final.pio.h"  // Matriz de LEDs via PIO

// ----------------------------- Definições Gerais ------------------------------
#define WIFI_SSID       "Casa1"
#define WIFI_PASSWORD   "40302010"

#define LED_WIFI        CYW43_WL_GPIO_LED_PIN

// Entradas analógicas simuladas via joystick
#define ADC_UMIDADE_PIN     26  // ADC0
#define ADC_TEMPERATURA_PIN 27  // ADC1

// GPIO para botão de modo web/manual
#define BOTAO_MODO      5


// LEDs RGB
#define LED_VERMELHO 13
#define LED_VERDE 11
#define LED_AZUL 12

// Buzzer
#define BUZZER 21

// Matriz de LED
#define LED_MATRIX      7
#define NUM_LEDS        25

// Display OLED via I2C
#define I2C_SDA         14
#define I2C_SCL         15
#define I2C_PORT        i2c1
#define I2C_ENDERECO        0x3C

// ------------------------ Variáveis Globais e Estados -------------------------
volatile bool modo_manual = false;
volatile uint64_t ultima_troca_modo = 0;

static volatile uint64_t debounce_antes = 0;
static volatile bool alarme_ativo = false;
static volatile bool exibir_temp = false;
static volatile bool exibir_umidade = false;

ssd1306_t display;
PIO pio = pio0;
uint sm;
uint32_t valor_led;

// Planta representada em matriz 5x5
uint8_t planta_matriz[NUM_LEDS] = {
    2, 2, 2, 2, 2,
    0, 0, 1, 0, 0,
    0, 0, 1, 1, 0,
    1, 1, 1, 1, 0,
    1, 1, 1, 0, 0
};

// -------------------------- Protótipos das Funções ----------------------------
// Inicializações
bool inicializar_perifericos();
void configurar_pwm(uint gpio);

// Botões
void configurar_botoes();
static void irq_botoes(uint gpio, uint32_t eventos);
void botao_modo_callback(uint gpio, uint32_t eventos);

// Leitura de sensores
float ler_temperatura_interna();
float ler_temperatura_joystick();
float ler_umidade_joystick();

// Display
void atualizar_display_dados(float temperatura, float umidade);

// Feedback visual
void atualizar_feedback_leds(float temperatura, float umidade);
void animacao_matriz_leds(float temperatura);

// Alarme
void acionar_alarme();
bool alarme_periodico_callback(repeating_timer_t *t);

// HTTP/TCP
static err_t aceitar_conexao_tcp(void *arg, struct tcp_pcb *novo_pcb, err_t err);
static err_t receber_dados_tcp(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err);
void interpretar_estado_planta(float temp, float umid, char *msg, size_t tamanho);

// Utilitário
uint32_t matrix_rgb(double b, double r, double g);

// -------------------------- Função Principal ----------------------------

int main() {
    stdio_init_all();

    // Inicializa periféricos (ADC, I2C, PWM, Display, Botões, PIO)
    if (!inicializar_perifericos()) {
        printf("Erro na inicialização dos periféricos.\n");
        return 1;
    }

    // Configura botão de troca de modo (monitoramento web)
    gpio_init(BOTAO_MODO);
    gpio_set_dir(BOTAO_MODO, GPIO_IN);
    gpio_pull_up(BOTAO_MODO);
    gpio_set_irq_enabled_with_callback(BOTAO_MODO, GPIO_IRQ_EDGE_FALL, true, botao_modo_callback);

    // Inicializa o módulo Wi-Fi
    while (cyw43_arch_init()) {
        printf("Falha ao iniciar Wi-Fi\n");
        sleep_ms(100);
        return -1;
    }

    cyw43_arch_gpio_put(LED_WIFI, 0); // Apaga LED Wi-Fi inicialmente
    cyw43_arch_enable_sta_mode();

    printf("Conectando à rede Wi-Fi...\n");
    while (cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASSWORD, CYW43_AUTH_WPA2_AES_PSK, 20000)) {
        printf("Falha na conexão Wi-Fi\n");
        sleep_ms(100);
        return -1;
    }
    printf("Conectado com IP: %s\n", ipaddr_ntoa(&netif_default->ip_addr));

    // Cria o servidor TCP
    struct tcp_pcb *server = tcp_new();
    if (!server || tcp_bind(server, IP_ADDR_ANY, 80) != ERR_OK) {
        printf("Erro ao criar servidor TCP\n");
        return -1;
    }

    server = tcp_listen(server);
    tcp_accept(server, aceitar_conexao_tcp);
    printf("Servidor HTTP ativo na porta 80\n");

    // Inicia alarme periódico no terminal
    static repeating_timer_t timer;
    add_repeating_timer_ms(5000, alarme_periodico_callback, NULL, &timer);

    // Loop principal
    while (true) {
        float temperatura = modo_manual ? ler_temperatura_joystick() : ler_temperatura_interna();
        float umidade = ler_umidade_joystick();

        atualizar_feedback_leds(temperatura, umidade);
        animacao_matriz_leds(temperatura);

        atualizar_display_dados(temperatura, umidade);

        cyw43_arch_poll(); // Mantém conexão Wi-Fi ativa
        sleep_ms(200);

        // Controle do buzzer em caso de alarme
        const uint16_t frequencias[] = {1000, 1500};
        const uint16_t duracao = 300;
        uint8_t i = 0;

        while (alarme_ativo) {
            pwm_set_clkdiv(pwm_gpio_to_slice_num(BUZZER), 125.0f);
            pwm_set_wrap(pwm_gpio_to_slice_num(BUZZER), 125000 / frequencias[i]);
            pwm_set_chan_level(pwm_gpio_to_slice_num(BUZZER), pwm_gpio_to_channel(BUZZER), 125000 / frequencias[i] / 2);
            sleep_ms(duracao);
            i = !i;
        }
        pwm_set_chan_level(pwm_gpio_to_slice_num(BUZZER), pwm_gpio_to_channel(BUZZER), 0);
    }

    cyw43_arch_deinit();
    return 0;
}

// -------------------------- Implementação das Funções ----------------------------
// Inicializa os periféricos do sistema
bool inicializar_perifericos() {
    // --- Inicializa ADCs ---
    adc_init();
    adc_gpio_init(ADC_UMIDADE_PIN);
    adc_gpio_init(ADC_TEMPERATURA_PIN);
    adc_set_temp_sensor_enabled(true); // Habilita sensor interno

    // --- Inicializa I2C e Display OLED ---
    i2c_init(I2C_PORT, 400 * 1000);
    gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA);
    gpio_pull_up(I2C_SCL);

    ssd1306_init(&display, WIDTH, HEIGHT, false, I2C_ENDERECO, I2C_PORT);
    ssd1306_config(&display);
    ssd1306_fill(&display, false);
    ssd1306_send_data(&display);

    // --- Inicializa PWM dos LEDs RGB e Buzzer ---
    configurar_pwm(LED_VERDE);
    configurar_pwm(LED_AZUL);
    configurar_pwm(LED_VERMELHO);
    configurar_pwm(BUZZER);

    // --- Inicializa matriz de LEDs via PIO ---
    uint offset = pio_add_program(pio, &final_program);
    sm = pio_claim_unused_sm(pio, true);
    final_program_init(pio, sm, offset, LED_MATRIX);

    return true;
}

// Função genérica para configurar um GPIO como saída PWM
void configurar_pwm(uint gpio) {
    gpio_set_function(gpio, GPIO_FUNC_PWM);
    uint slice = pwm_gpio_to_slice_num(gpio);
    pwm_set_wrap(slice, 4095);
    pwm_set_chan_level(slice, pwm_gpio_to_channel(gpio), 0);
    pwm_set_enabled(slice, true);
}

// ---------------- Botões físicos (TEMP / UMID) e Callback ----------------
// Configura botões físicos para troca de exibição
static void irq_botoes(uint gpio, uint32_t eventos) {
    uint64_t agora = to_us_since_boot(get_absolute_time());
    if ((agora - debounce_antes) < 200000)
        return;
    debounce_antes = agora;

    if (!alarme_ativo) {
        if (gpio == BOTAO_MODO) {
            exibir_temp = !exibir_temp;
            exibir_umidade = false;
        } else if (gpio == BOTAO_MODO) {
            exibir_umidade = !exibir_umidade;
            exibir_temp = false;
        }
    } else {
        acionar_alarme();  // desativa alarme
    }
}

// Callback para troca de modo (manual/web)
void botao_modo_callback(uint gpio, uint32_t eventos) {
    uint64_t agora = to_us_since_boot(get_absolute_time());
    if ((agora - ultima_troca_modo) > 300000) { // debounce: 300 ms
        if (alarme_ativo) {
            acionar_alarme(); // Desativa o alarme se estiver ativo
        } else {
            modo_manual = !modo_manual;
            printf("Modo %s ativado\n", modo_manual ? "MANUAL" : "AUTOMÁTICO");
        }
        ultima_troca_modo = agora;
    }
}


// ---------------- Leitura de dados ----------------
// Lê temperatura e umidade do joystick (simulação)
float ler_temperatura_joystick() {
    adc_select_input(1); // ADC1 = GPIO27
    uint16_t raw = adc_read();
    return (raw / 4095.0f) * 60.0f; // Simula temperatura de 0 a 60 ºC
}

float ler_umidade_joystick() {
    adc_select_input(0); // ADC0 = GPIO26
    uint16_t raw = adc_read();
    return (raw / 4095.0f) * 100.0f; // Simula umidade de 0 a 100%
}

// Lê temperatura interna do sensor
float ler_temperatura_interna() {
    adc_select_input(4); // Sensor interno
    uint16_t raw = adc_read();
    const float fator_conv = 3.3f / (1 << 12);
    return 27.0f - ((raw * fator_conv) - 0.706f) / 0.001721f;
}

// ---------------- Animação na matriz de LEDs RGB ----------------
void animacao_matriz_leds(float temperatura) {
    double r = 0.0, g = 1.0, b = 0.0;

    if (temperatura > 50.0) {
        r = 1.0; g = 0.0; b = 0.0; // quente
    } else if (temperatura < 10.0) {
        r = 0.0; g = 0.0; b = 1.0; // frio
    }

    for (int i = 0; i < NUM_LEDS; i++) {
        if (planta_matriz[i] == 1) {
            valor_led = matrix_rgb(b, r, g);  // planta
        } else if (planta_matriz[i] == 2) {
            valor_led = matrix_rgb(0.0, 1.0, 1.0);  // vaso (amarelo)
        } else {
            valor_led = matrix_rgb(0.0, 0.0, 0.0);  // fundo
        }
        pio_sm_put_blocking(pio, sm, valor_led);
    }
}

uint32_t matrix_rgb(double b, double r, double g) {
    unsigned char R = r * 255;
    unsigned char G = g * 255;
    unsigned char B = b * 255;
    return (G << 24) | (R << 16) | (B << 8);
}

// ---------------- Alarme sonoro e visual ----------------
void acionar_alarme() {
    alarme_ativo = !alarme_ativo;

    if (alarme_ativo) {
        pwm_set_gpio_level(LED_VERDE, 0);
        pwm_set_gpio_level(LED_AZUL, 0);
        pwm_set_gpio_level(LED_VERMELHO, 4095);
        printf("Alarme ativado!\n");
    } else {
        pwm_set_gpio_level(BUZZER, 0);
        printf("Alarme desativado!\n");
    }
}

// ---------------- Feedback visual (LEDs e display de aviso) ----------------
void atualizar_feedback_visual(float temperatura, float umidade) {
    static uint64_t tempo_inicio_alerta = 0;
    uint64_t agora = to_us_since_boot(get_absolute_time());

    // Limites padronizados
    bool temp_critica = temperatura < 20.0f || temperatura > 40.0f;
    bool umid_critica = umidade < 20.0f || umidade > 80.0f;
    bool cond_critica = temp_critica && umid_critica;

    if (cond_critica) {
        if (tempo_inicio_alerta == 0) tempo_inicio_alerta = agora;
        if ((agora - tempo_inicio_alerta) > 5000000) {
            printf("Condição crítica detectada.\n");
            acionar_alarme();
        }
        pwm_set_gpio_level(LED_VERDE, 0);
        pwm_set_gpio_level(LED_AZUL, 0);
        pwm_set_gpio_level(LED_VERMELHO, 4095);
    } else {
        tempo_inicio_alerta = 0;
        if (temp_critica || umid_critica) {
            pwm_set_gpio_level(LED_VERDE, 4095);
            pwm_set_gpio_level(LED_AZUL, 0);
            pwm_set_gpio_level(LED_VERMELHO, 4095);  // laranja
        } else {
            pwm_set_gpio_level(LED_VERDE, 4095);
            pwm_set_gpio_level(LED_AZUL, 0);
            pwm_set_gpio_level(LED_VERMELHO, 0); // verde
        }
    }
}

void atualizar_feedback_leds(float t, float u) {
    atualizar_feedback_visual(t, u);
}


// ---------------- Display gráfico (representação como ponto) ----------------
// ---------------- Display com valores numéricos ----------------
void atualizar_display_dados(float temperatura, float umidade) {
    char temp_str[20];
    char umidade_str[20];
    char modo_str[20];

    snprintf(temp_str, sizeof(temp_str), "Temp: %.1f C", temperatura);
    snprintf(umidade_str, sizeof(umidade_str), "Umid: %.1f %%", umidade);
    snprintf(modo_str, sizeof(modo_str), "Modo: %s", modo_manual ? "Manual" : "Auto");

    ssd1306_fill(&display, false);

    if (!alarme_ativo) {
        ssd1306_draw_string(&display, temp_str, 3, 10);
        ssd1306_draw_string(&display, umidade_str, 3, 25);
        ssd1306_draw_string(&display, modo_str, 3, 40);
    } else {
        ssd1306_draw_string(&display, "ALARME", 10, 10);
        ssd1306_draw_string(&display, "DISPARADO", 10, 20);
        ssd1306_draw_string(&display, "Aperte o", 10, 30);
        ssd1306_draw_string(&display, "botao A para/", 10, 40);
        ssd1306_draw_string(&display, "desativar", 10, 50);
    }

    ssd1306_send_data(&display);
}

// ---------------- Timer periódico: relatório no terminal ----------------
bool alarme_periodico_callback(repeating_timer_t *t) {
    float temperatura = modo_manual ? ler_temperatura_joystick() : ler_temperatura_interna();
    float umidade = ler_umidade_joystick();

    printf("[Relatório] Temp: %.2f °C | Umid: %.2f %%\n", temperatura, umidade);

    if (alarme_ativo) {
        printf("Alarme ligado, aperte o botao A na placa para desativar\n");
        return true;
    }

    bool temp_ok = temperatura >= 20.0f && temperatura <= 40.0f;
    bool umid_ok = umidade >= 20.0f && umidade <= 80.0f;

    if (temp_ok && umid_ok) {
        printf("Sua planta esta feliz!\n");
    } else if (!temp_ok && !umid_ok) {
        printf("Sua planta esta em perigo!\n");
    } else if (!temp_ok) {
        if (temperatura < 20.0f)
            printf("Sua planta esta com frio!\n");
        else
            printf("Sua planta esta com calor!\n");
    } else if (!umid_ok) {
        if (umidade < 20.0f)
            printf("Sua planta esta com sede!\n");
        else
            printf("Excesso de agua detectado!\n");
    }

    return true;
}

// ----------------------------- HTTP e TCP -----------------------------------------
static err_t aceitar_conexao_tcp(void *arg, struct tcp_pcb *novo_pcb, err_t err) {
    printf("Conexão TCP aceita\n");
    tcp_recv(novo_pcb, receber_dados_tcp);   // callback para montar a página html
    return ERR_OK;
}

static err_t receber_dados_tcp(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err) {
    if (!p) {
        tcp_close(tpcb);
        tcp_recv(tpcb, NULL);
        return ERR_OK;
    }
    

    char *request = (char *)malloc(p->len + 1);
    memcpy(request, p->payload, p->len);
    request[p->len] = '\0';

    // Verifica se é um GET para /toggle_modo
    if (strncmp(request, "GET /toggle_modo", 16) == 0) {
        modo_manual = !modo_manual;

        // Redireciona para a página principal após alternar o modo
        const char *redirect =
            "HTTP/1.1 303 See Other\r\n"
            "Location: /\r\n"
            "Connection: close\r\n\r\n";
        tcp_write(tpcb, redirect, strlen(redirect), TCP_WRITE_FLAG_COPY);
        tcp_output(tpcb);

        free(request);
        pbuf_free(p);
        return ERR_OK;
    }

    float temperatura = modo_manual ? ler_temperatura_joystick() : ler_temperatura_interna();
    float umidade = ler_umidade_joystick();

    char status_msg[64];
    interpretar_estado_planta(temperatura, umidade, status_msg, sizeof(status_msg));
    const char *modo_str = modo_manual ? "Manual" : "Autom&aacute;tico";

    char html[2048];
    snprintf(html, sizeof(html),
    "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nConnection: close\r\n\r\n"
    "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta http-equiv='refresh' content='5'>"
    "<title>Green Life</title><style>body{background:#e6ffe6;font-family:Arial;text-align:center;padding:20px;}"
    "h1{font-size:48px;color:#228B22;}.sensor{font-size:28px;margin-top:20px;}"
    ".status{font-size:32px;color:#333;margin-top:30px;font-weight:bold;}"
    "img{width:200px;margin-top:20px;border-radius:10px;}</style>"
    "<script>setTimeout(function(){ location.reload(); }, 5000);</script>"
    "</head><body>"
    "<h1>Green Life</h1><p class='sensor'><strong>Modo de monitoramento:</strong> %s</p>"
    "<img src='https://cdn-icons-png.flaticon.com/512/628/628324.png' alt='Planta'>"
    "<p class='sensor'>Temperatura atual: %.1f &deg;C</p><p class='sensor'>Umidade atual: %.1f%%</p>"
    "<p class='status'>%s</p>"
    "<form method='GET' action='/toggle_modo'>"
    "<button type='submit' style='font-size:22px;margin-top:25px;padding:10px 30px;'>Alternar modo</button>"
    "</form>"
    "</body></html>", modo_str, temperatura, umidade, status_msg);

    tcp_write(tpcb, html, strlen(html), TCP_WRITE_FLAG_COPY);
    tcp_output(tpcb);

    free(request);
    pbuf_free(p);

    return ERR_OK;
}

// ----------------------------- Logica de Avaliacao --------------------------------
void interpretar_estado_planta(float temp, float umid, char *msg, size_t tamanho) {
    if (alarme_ativo) {
        strncpy(msg, "Alarme ligado, aperte o botao A na placa para desativar", tamanho);
        msg[tamanho - 1] = '\0';
        return;
    }

    bool temp_ok = temp >= 20.0 && temp <= 40.0;
    bool umid_ok = umid >= 20.0 && umid <= 80.0;

    if (temp_ok && umid_ok)
        strncpy(msg, "Sua planta est&aacute; feliz!", tamanho);
    else if (!temp_ok && !umid_ok)
        strncpy(msg, "Sua planta est&aacute; em perigo!", tamanho);
    else if (!temp_ok)
        strncpy(msg, temp < 20.0 ? "Sua planta est&aacute; com frio!" : "Sua planta est&aacute; com calor!", tamanho);
    else if (!umid_ok)
        strncpy(msg, umid < 20.0 ? "Sua planta est&aacute; com sede!" : "Excesso de &aacute;gua detectado!", tamanho);

    msg[tamanho - 1] = '\0';
}
