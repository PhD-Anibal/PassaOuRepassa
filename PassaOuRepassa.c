#include <stdio.h>
#include "pico/stdlib.h"
#include <hardware/pio.h>           // Biblioteca para manipulação de periféricos PIO
#include "hardware/clocks.h"        // Biblioteca para controle de relógios do hardware
#include "hardware/gpio.h"
#include "hardware/pwm.h"
#include "PassaOuRepassa.pio.h" // Biblioteca PIO para controle de LEDs WS2818B
#include "hardware/i2c.h"
#include "lib/ssd1306.h"
#include "lib/font.h" 

// Definições de constantes
#define BUZZER1 21              // Define o pino 21 = Buzzer
#define FPS 3                 // Taxa de quadros por segundo
#define LED_COUNT 25            // Número de LEDs na matriz
#define LED_PIN 7               // Pino GPIO conectado aos LEDs

#define LED_G_PIN 11  
#define LED_R_PIN 13  
#define LED_B_PIN 12  // a intensidade indica que tão rápido vai a esteira
#define Botao_A 5 // GPIO para botão A simula o sensor de deteção de latas passando
#define Botao_B 6 // GPIO para botão B

volatile bool iniciar_animacao = false;

PIO pio = pio0;   // Variável global para PIO
uint sm;    // Variável global para state machine

#define DEBOUNCE_DELAY 200  // Tempo de debounce para evitar múltiplas detecções de um único toque
volatile uint32_t last_interrupt_time=0; // Armazena o tempo da última interrupção
volatile uint32_t time_atual=0; 
float velocidade_L; // Velocidade Lata
float velocidades_L[3] = {0, 0, 0}; // Array circular para armazenar as últimas 3 velocidades da lata
float velocidade_E; // Velocidade Esteira
const float espacamento=0.085; // cada lata mede 6.5cm diamentro e espaçamento das latas 2cm = 8.5cm=0.085m


// Definição de pinos e configurações do hardware display
#define I2C_PORT i2c1
#define I2C_SDA 14
#define I2C_SCL 15
#define endereco 0x3C


int frame_delay = 1000 / FPS; // Intervalo em milissegundos
// Ordem da matriz de leds
int ordem[] = {0, 1, 2, 3, 4, 9, 8,7, 6, 5, 10, 11, 12, 13, 14, 19, 18, 17, 16, 15 , 20, 21, 22, 23, 24}; 

// Imprimir valor binário
void imprimir_binario(int num) {
    int i;
    for (i = 31; i >= 0; i--) {
        (num & (1 << i)) ? printf("1") : printf("0");
    }
}

// Rotina para definição da intensidade de cores do led
uint32_t matrix_rgb(double b, double r, double g) {
    unsigned char R, G, B;
    R = r * 255;
    G = g * 255;
    B = b * 255;
    return (G << 24) | (R << 16) | (B << 8);
}



void set_buzzer_tone(uint gpio, uint freq) {
    uint slice_num = pwm_gpio_to_slice_num(gpio);
    uint top = 1000000 / freq;            // Calcula o TOP para a frequência desejada
    pwm_set_wrap(slice_num, top);
    pwm_set_chan_level(slice_num, pwm_gpio_to_channel(gpio), top / 2); // 50% duty cycle
}

void stop_buzzer(uint gpio) {
    uint slice_num = pwm_gpio_to_slice_num(gpio);
    pwm_set_chan_level(slice_num, pwm_gpio_to_channel(gpio), 0); // Desliga o PWM
}

void apagar_matrizLEDS(PIO pio, uint sm) {
    for (uint i = 0; i < LED_COUNT; ++i) {
        pio_sm_put_blocking(pio, sm, matrix_rgb(0.0, 0.0, 0.0)); // Envia cor preta (LEDs apagados)
    }
}

// Rotina para acionar a matriz de leds - ws2812b
void desenho_pio(PIO pio, uint sm) {
    uint32_t leds[LED_COUNT] = {0}; // Inicializa todos os LEDs apagados

    for (int j = 4; j >= 0; j--) {
        // Primeiro, apaga todos os LEDs
        for (int i = 0; i < LED_COUNT; i++) {
            leds[i] = 0; // Apaga o LED
        }
        // Define os LEDs selecionados para azul
        for (int i = j; i < LED_COUNT; i += 5) {
            int index = ordem[i]; // Pega a posição correta da matriz
            leds[index] = matrix_rgb(0.0, 0.0, 0.01); // Define cor azul
        }
        // Envia os dados para os LEDs na ordem correta
        for (int i = 0; i < LED_COUNT; i++) {
            pio_sm_put_blocking(pio, sm, leds[i]);
        }
        sleep_ms(50); // Espera um tempo antes de próxima iteração
    }
    // **Apaga os LEDs após a última iteração**
    apagar_matrizLEDS(pio, sm);
}

// Rotina para acionar a matriz de leds - ws2812b
void desenhoLora_pio(PIO pio, uint sm) {
    uint32_t leds[LED_COUNT] = {0}; // Inicializa todos os LEDs apagados
    int ordem[] = {0, 1, 2, 3, 4, 9, 8, 7, 6, 5, 10, 11, 12, 13, 14, 19, 18, 17, 16, 15, 20, 21, 22, 23, 24};

    for (int repetir = 0; repetir < 3;repetir++){  // 3 vezes o sinal de Wi-Fi
        for (int sinal = 0; sinal < 3; sinal++) {
            // Apaga todos os LEDs antes de acender o próximo sinal
            for (int i = 0; i < LED_COUNT; i++) {
                leds[i] = 0;
            }

            // Define os LEDs de acordo com o nível do "sinal WiFi"
            if (sinal == 0) {
                leds[ordem[2]] = matrix_rgb(0, 0, 0.01);
            }
            if (sinal == 1) {
                leds[ordem[1]] = matrix_rgb(0.01, 0, 0);
                leds[ordem[3]] = matrix_rgb(0.01, 0, 0);
                leds[ordem[7]] = matrix_rgb(0.01, 0, 0);
            }
            if (sinal == 2) {
                leds[ordem[5]] = matrix_rgb(0, 0.01, 0);
                leds[ordem[9]] = matrix_rgb(0, 0.01, 0);
                leds[ordem[11]] = matrix_rgb(0, 0.01, 0);
                leds[ordem[12]] = matrix_rgb(0, 0.01, 0);
                leds[ordem[13]] = matrix_rgb(0, 0.01, 0);
            }

            // Envia os dados para os LEDs na ordem correta
            for (int i = 0; i < LED_COUNT; i++) {
                pio_sm_put_blocking(pio, sm, leds[ordem[i]]);
            }
            sleep_ms(500); // Mantém o padrão visível
            //apagar_matrizLEDS(pio, sm); // Apaga os LEDs antes do próximo ciclo
        }
    }
}


// Função de interrupção para botões
void gpio_irq_handler(uint gpio, uint32_t events) {
    uint32_t current_time = to_ms_since_boot(get_absolute_time());

    if (current_time - last_interrupt_time > DEBOUNCE_DELAY) {
        last_interrupt_time = current_time;

        if (gpio == Botao_A) {
            if (time_atual > 0) { // Evita erro na primeira medição
                float tempo_decorrido = (current_time - time_atual) / 1000.0; // Converte para segundos
                velocidade_L = espacamento / tempo_decorrido; // Agora está correto!
            } else {
                velocidade_L = 0; // Primeira medição, sem referência anterior
            }

            printf("Velocidade: %.2f m/s\n", velocidade_L);
            printf("current_time: %u ms\n", current_time);
            printf("time_atual: %u ms\n", time_atual);

            time_atual = current_time;
            iniciar_animacao = true;
        }
    }
}

int main() {
    gpio_init(BUZZER1);
    gpio_set_dir(BUZZER1, GPIO_OUT);
    
    // Configuração do PWM para o BUZZER1
    gpio_set_function(BUZZER1, GPIO_FUNC_PWM); // Configura o GPIO como PWM
    uint slice_num = pwm_gpio_to_slice_num(BUZZER1);
    pwm_set_clkdiv(slice_num, 125.0f);     // Define o divisor do clock para 1 MHz
    pwm_set_wrap(slice_num, 1000);        // Define o TOP para frequência de 1 kHz
    pwm_set_chan_level(slice_num, pwm_gpio_to_channel(BUZZER1), 0); // Razão cíclica inicial
    pwm_set_enabled(slice_num, true);     // Habilita o PWM    

    // Configuração do PWM para o LED RGB Azul
    gpio_set_function(LED_B_PIN, GPIO_FUNC_PWM);
    uint led_slice_num = pwm_gpio_to_slice_num(LED_B_PIN);
    uint led_channel = pwm_gpio_to_channel(LED_B_PIN);
    pwm_set_wrap(led_slice_num, 1000);
    pwm_set_clkdiv(led_slice_num, 125.0f);
    pwm_set_enabled(led_slice_num, true);
    
    bool ok;


    // Configura o clock para 128 MHz
    ok = set_sys_clock_khz(128000, false);

    stdio_init_all(); // Inicializa a comunicação serial


    // Configuração dos botões
    gpio_init(Botao_B);
    gpio_set_dir(Botao_B, GPIO_IN);
    gpio_pull_up(Botao_B);
    
    gpio_init(Botao_A);
    gpio_set_dir(Botao_A, GPIO_IN);
    gpio_pull_up(Botao_A);

    char buffer[20]; // Buffer para armazenar a string formatada no display

    printf("iniciando a transmissão PIO");
    if (ok) printf("clock set to %ld\n", clock_get_hz(clk_sys));

    // Configuração do PIO
    uint offset = pio_add_program(pio, &PassaOuRepassa_program);
    sm = pio_claim_unused_sm(pio, true);
    PassaOuRepassa_program_init(pio, sm, offset, LED_PIN);

    // Inicialização do I2C para o display 400Khz.
    i2c_init(I2C_PORT, 400 * 1000);
    gpio_set_function(I2C_SDA, GPIO_FUNC_I2C); // Configura o pono GPIO para I2C
    gpio_set_function(I2C_SCL, GPIO_FUNC_I2C); // Configura o pono GPIO para I2C
    gpio_pull_up(I2C_SDA);                     // Linha de dados
    gpio_pull_up(I2C_SCL);                     // Linha do clock

    ssd1306_t ssd; // Inicializa a estrutura do display
    ssd1306_init(&ssd, WIDTH, HEIGHT, false, endereco, I2C_PORT); // Inicializa o display
    ssd1306_config(&ssd); // Configura o display
    ssd1306_send_data(&ssd); // Envia os dados para o display

    // Limpa o display. O display inicia com todos os pixels apagados.
    ssd1306_fill(&ssd, false);
    ssd1306_send_data(&ssd);


    // Configuração das interrupções nos botões
    gpio_set_irq_enabled_with_callback(Botao_B, GPIO_IRQ_EDGE_FALL, true, &gpio_irq_handler);
    gpio_set_irq_enabled_with_callback(Botao_A, GPIO_IRQ_EDGE_FALL, true, &gpio_irq_handler);

uint8_t index = 0; // Índice para armazenar velocidades
uint32_t tempo_ajuste = 0; // Guarda o tempo do último ajuste da esteira
char ultimo_estado = 'N'; // N = Normal, A = Alta, B = Baixa

while (true) {
    if (iniciar_animacao) {  
        iniciar_animacao = false;  
        desenho_pio(pio, sm); 

        // Atualiza o array circular de velocidades
        velocidades_L[index] = velocidade_L; 
        index = (index + 1) % 3; 

        // Calcula a média das 3 últimas velocidades
        float soma = velocidades_L[0] + velocidades_L[1] + velocidades_L[2];
        float media = soma / 3;

        uint32_t tempo_atual = to_ms_since_boot(get_absolute_time());

        // **Verifica se já passaram 6 segundos desde o último ajuste**
        if (tempo_atual - tempo_ajuste >= 6000) {  
            char estado_atual = 'N'; // Assume que está normal
            
            if (media < 0.15) {
                velocidade_E = 6.16;
                pwm_set_chan_level(led_slice_num, led_channel, 1000);
                estado_atual = 'A'; // Alta velocidade
            } 
            else if (media > 0.19) {
                velocidade_E = 5.04;
                pwm_set_chan_level(led_slice_num, led_channel, 100);
                estado_atual = 'B'; // Baixa velocidade
            } 
            else { 
                velocidade_E = 5.6;
                pwm_set_chan_level(led_slice_num, led_channel, 500);
            }

            // **Se o estado atual for igual ao anterior, imprime a repetição**
            if (estado_atual == ultimo_estado) {
                if (estado_atual == 'A') {
                    printf("Possivel Problema de Obstrução- Continua alta a velocidade da esteira\n");
                    desenhoLora_pio(pio, sm);
                    for (int i = 0; i < 3; i++) {  // Repetir 3 vezes para maior destaque
                        set_buzzer_tone(BUZZER1, 1000); // Frequência mais alta (1 kHz)
                        sleep_ms(200);
                        stop_buzzer(BUZZER1);
                        sleep_ms(100);
                        
                        set_buzzer_tone(BUZZER1, 800); // Frequência um pouco menor
                        sleep_ms(200);
                        stop_buzzer(BUZZER1);
                        sleep_ms(100);
                    }                 
                } else if (estado_atual == 'B') {
                    printf("Possivel Problema de Sobrecarga no Operario- Continua baixa a velocidade da esteira\n");
                    desenhoLora_pio(pio, sm);
                    for (int i = 0; i < 3; i++) {  // Repetir 3 vezes para maior destaque
                        set_buzzer_tone(BUZZER1, 1000); // Frequência mais alta (1 kHz)
                        sleep_ms(200);
                        stop_buzzer(BUZZER1);
                        sleep_ms(100);
                        
                        set_buzzer_tone(BUZZER1, 800); // Frequência um pouco menor
                        sleep_ms(200);
                        stop_buzzer(BUZZER1);
                        sleep_ms(100);
                    }                  
                }
            }

            // Atualiza o último estado e o tempo de ajuste
            ultimo_estado = estado_atual;
            tempo_ajuste = tempo_atual;
        }

        // Atualiza o display
        ssd1306_fill(&ssd, false);
        ssd1306_draw_string(&ssd, "Embarcatech", 8, 6);
        ssd1306_draw_string(&ssd, "Velocidade:", 8, 18);
        
        sprintf(buffer, "VL:%.2f", velocidade_L);
        ssd1306_draw_string(&ssd, buffer, 12, 28);
        
        sprintf(buffer, "VE:%.2f", velocidade_E);
        ssd1306_draw_string(&ssd, buffer, 12, 38);

        ssd1306_rect(&ssd, 3, 3, 122, 60, true, false);
        ssd1306_send_data(&ssd);
    }

    sleep_ms(100); 
}

}
