// Bibliotecas padrão do sistema  
#include <stdio.h>                  // Biblioteca padrão para entrada e saída  
#include "pico/stdlib.h"            // Biblioteca padrão da Raspberry Pi Pico  

// Bibliotecas de hardware da Pico  
#include <hardware/pio.h>           // Biblioteca para manipulação de periféricos PIO  
#include "hardware/clocks.h"        // Biblioteca para controle de relógios do hardware  
#include "hardware/gpio.h"          // Biblioteca para manipulação de GPIOs  
#include "hardware/pwm.h"           // Biblioteca para controle de PWM  
#include "hardware/i2c.h"           // Biblioteca para comunicação I2C  
#include "hardware/adc.h"           // Biblioteca para leitura de ADC (joystick simulando sensor de umidade)  
#include "hardware/timer.h"         // Biblioteca para uso de timers  

// Bibliotecas personalizadas  
#include "PassaOuRepassa.pio.h"     // Biblioteca PIO para controle de LEDs WS2818B  
#include "lib/ssd1306.h"            // Biblioteca para controle do display OLED SSD1306  
#include "lib/font.h"               // Biblioteca para manipulação de fontes no display  

// Definições de constantes  
#define BUZZER1 21              // Define o pino 21 para o Buzzer  
#define FPS 3                   // Taxa de quadros por segundo  
#define LED_COUNT 25            // Número de LEDs na matriz  
#define LED_PIN 7               // Pino GPIO conectado aos LEDs  

#define LED_R_PIN 13            // Pino do LED Vermelho  
#define LED_B_PIN 12            // Pino do LED Azul (intensidade indica velocidade da esteira)  

#define Botao_A 5               // Pino GPIO do botão A (simula sensor de detecção de latas)  
#define Botao_B 6               // Pino GPIO do botão B (controle da esteira)  
#define JOYSTICK_X_PIN 27       // Pino GPIO para leitura do eixo X do joystick  

// Variáveis globais para controle de PIO  
PIO pio = pio0;                // Instância do PIO  
uint sm;                        // Variável para state machine do PIO  

// Definições de tempo e debounce  
#define DEBOUNCE_DELAY 200      // Tempo de debounce (200ms) para evitar múltiplas leituras indesejadas  
volatile uint32_t last_interrupt_time = 0; // Armazena o tempo da última interrupção  

// Variável para verificar se o botão A foi pressionado  
volatile bool botao_A_pressionado = false;  

// Definição de pinos e configurações do hardware do display OLED  
#define I2C_PORT i2c1           // Porta I2C utilizada  
#define I2C_SDA 14              // Pino SDA para comunicação I2C  
#define I2C_SCL 15              // Pino SCL para comunicação I2C  
#define endereco 0x3C           // Endereço padrão do display OLED  

// Intervalo de amostragem para medições  
#define INTERVALO_AMOSTRAGEM 6000  // Intervalo de 6 segundos em milissegundos  

// Variáveis para medições e controle da esteira  
volatile float velocidades_L[3] = {0, 0, 0};  // Array para armazenar as últimas três medições de velocidade  
volatile bool calcular_media = false;  // Flag para indicar quando calcular a média das medições  
volatile int contador_latas = 0;  // Contador de latas detectadas  

// Estado da esteira e sistema  
bool inicio_esteira;  // Flag para o som e sinalização aconteça apenas ao ligar a esteira
char ultimo_estado = 'N'; // 'N' = Normal, 'A' = Alta velocidade, 'B' = Baixa velocidade  
char estado_atual = 'N'; // Assume que está em estado normal  
char Parada_Critica = 'N';  // Assume que não há parada crítica ('N' = Normal)  
float media;              // Média de velocidade da esteira  
float velocidade_E = 5.6; // Velocidade inicial da esteira (m/s)  
const float espacamento = 0.085; // Espaçamento entre as latas (6.5 cm diâmetro + 2 cm espaçamento = 8.5 cm = 0.085 m)  

int umidade;

// Controle de inicialização da esteira  
volatile bool iniciar_esteira = false; // Esteira inicia com botão B, e para em caso de parada crítica  

// Configuração do tempo de atualização do display  
int frame_delay = 1000 / FPS; // Intervalo entre quadros em milissegundos  

// Ordem de acionamento dos LEDs na matriz  
int ordem[] = {0, 1, 2, 3, 4, 9, 8, 7, 6, 5,  
               10, 11, 12, 13, 14, 19, 18, 17, 16, 15,  
               20, 21, 22, 23, 24};  

// Converte valores de intensidade de cores para formato RGB compatível com os LEDs.
uint32_t matrix_rgb(double b, double r, double g) {
    unsigned char R, G, B;
    R = r * 255;
    G = g * 255;
    B = b * 255;
    return (G << 24) | (R << 16) | (B << 8);
}

// Apaga todos os LEDs da matriz
void apagar_matrizLEDS(PIO pio, uint sm) {
    for (uint i = 0; i < LED_COUNT; ++i) {
        pio_sm_put_blocking(pio, sm, matrix_rgb(0.0, 0.0, 0.0)); // Envia cor preta (LEDs apagados)
    }
}

// Aviso de inicio da esteira, Ascende LEDs, da camada central até a borda
void acender_sinal_inicio() {
    uint32_t leds[LED_COUNT] = {0}; // Inicializa todos os LEDs apagados

    // Índices dos LEDs organizados por camadas concêntricas (do centro para a borda)
    int camadas[3][16] = {
        {12}, // Centro
        {7, 11, 13, 17, 6, 8, 16, 18}, // Segunda camada ao redor do centro
        {2, 3, 4, 9, 14, 19, 24, 23, 22, 21, 20, 15, 10, 5, 0, 1} // Terceira camada (borda externa)
    };

    int tamanhos[] = {1, 8, 16}; // Tamanhos de cada camada

    // Acende os LEDs de cada camada com uma pausa entre elas
    for (int camada = 0; camada < 3; camada++) {
        // Apaga todos os LEDs antes de acender a nova camada
        for (int i = 0; i < LED_COUNT; i++) {
            leds[i] = 0;
        }

        // Acende os LEDs da camada atual
        for (int i = 0; i < tamanhos[camada]; i++) {
            int index = camadas[camada][i]; // Obtém o índice correto da camada
            leds[ordem[index]] = matrix_rgb(0.0, 0.0, 0.01); // Define cor azul
        }

        // Envia os dados para os LEDs na ordem correta
        for (int i = 0; i < LED_COUNT; i++) {
            pio_sm_put_blocking(pio, sm, leds[i]);
        }

        sleep_ms(200); // Tempo de espera entre camadas
    }
    // **Apaga os LEDs após a última iteração**
    apagar_matrizLEDS(pio, sm);
}


// Controla a animação da matriz de LEDs simulando um efeito visual da passagem de latas pelo sensor
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

// Controla a animação da matriz de LEDs simulando um efeito visual de transmição de dados por LoRa.
void desenhoLora_pio(PIO pio, uint sm) {
    uint32_t leds[LED_COUNT] = {0}; // Inicializa todos os LEDs apagados

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
        }
    }
}

// Define a frequência do buzzer utilizando PWM.
void set_buzzer_tone(uint gpio, uint freq) {
    uint slice_num = pwm_gpio_to_slice_num(gpio);
    uint top = 1000000 / freq;            // Calcula o TOP para a frequência desejada
    pwm_set_wrap(slice_num, top);
    pwm_set_chan_level(slice_num, pwm_gpio_to_channel(gpio), top / 50); // 50% duty cycle
}

// Para o som do buzzer desligando o PWM.
void stop_buzzer(uint gpio) {
    uint slice_num = pwm_gpio_to_slice_num(gpio);
    pwm_set_chan_level(slice_num, pwm_gpio_to_channel(gpio), 0); // Desliga o PWM
}

// Avisa que vai se movimentar a esteira
void som_inicio_atividades() {
    int frequencias[] = {500, 700, 900};  // Sequência de tons crescentes
    int duracao = 150;  // Duração de cada tom em milissegundos
    
    for (int i = 0; i < 3; i++) {
        set_buzzer_tone(BUZZER1, frequencias[i]);  // Define a frequência do buzzer
        sleep_ms(duracao);
        stop_buzzer(BUZZER1);
        sleep_ms(50);  // Pequena pausa entre os tons
    }
    inicio_esteira = true;
}

// Ativa alarmes e LEDs em caso de parada crítica da esteira
void tratar_parada_critica(char Parada_Critica) {
    //desativa esteira (não é necessario desativar o sensor (botão A) porque v=0)
    iniciar_esteira=false;
    if(Parada_Critica == 'O'){printf("Possivel Obstrução- Continua alta a velocidade da esteira\n");};
    if(Parada_Critica == 'S'){printf("Possivel Sobrecarga no Operario- Continua baixa a velocidade da esteira\n");};
    if(Parada_Critica == 'U'){printf("Possivel Vazamento- Alta umidade na esteira\n");};
    if(Parada_Critica == 'B'){printf("Botão de parar esteira foi acionado\n");};

    gpio_put(LED_R_PIN, true);  //indica parada crítica
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
    desenhoLora_pio(pio, sm); // envia status pelo LORA para tomar medidas
}

// Função chamada periodicamente pelo temporizador para exibir a contagem de latas.
bool callback_temporizador(struct repeating_timer *t) {
    printf("\n====== Atualização do sistema ======\n");
    printf("Latas detectadas nos últimos 6 segundos: %d\n", contador_latas);
    printf("Velocidade da esteira: %.2f m/s\n", velocidade_E);
    printf("Taxa de passagem: %.2f latas/s\n", media);
    printf("Nível de umidade: %d%%\n", umidade);
    printf("Velocidade da esteira (N-Normal, A-Alta, B-Baixa): %c\n", estado_atual);
    printf("========================================================\n\n");    
    calcular_media = true;
    return true;
}

// Função de interrupções para os botões (detecção de latas e controle da esteira).
void gpio_irq_handler(uint gpio, uint32_t events) {
    uint32_t current_time = to_ms_since_boot(get_absolute_time());

    if (current_time - last_interrupt_time > DEBOUNCE_DELAY) {
        last_interrupt_time = current_time;

        if (gpio == Botao_A) {
            contador_latas++;
            botao_A_pressionado = true;
        }else if(gpio == Botao_B){
            if(!iniciar_esteira){ // se tenho que ativar a esteira entra no loop
                // Atualiza o display, Limpa matriz de leds e RGB    
                inicio_esteira = false;   
                apagar_matrizLEDS(pio, sm);
                gpio_put(LED_B_PIN, false);
                gpio_put(LED_R_PIN, false);
                calcular_media = false;
                media=0.17;
                ultimo_estado = 'N'; // N = Normal, A = Alta, B = Baixa
                velocidade_E = 5.6;  //inicia a esteira na velocidade media
                estado_atual = 'N'; // Assume que está normal
                Parada_Critica= 'N';  // Assume que está normal
                iniciar_esteira=true; //!iniciar_esteira; oi2
                contador_latas = 0;
                umidade=50;
            }else{                             
                Parada_Critica='B';
            }
            
        }
    }
}

int main() {
    stdio_init_all(); // Inicializa a comunicação serial
    char buffer[20]; // Buffer para armazenar a string formatada no display

    // Configuração do PWM para o BUZZER1
    uint slice_num = pwm_gpio_to_slice_num(BUZZER1);
    gpio_init(BUZZER1);
    gpio_set_dir(BUZZER1, GPIO_OUT);
    gpio_set_function(BUZZER1, GPIO_FUNC_PWM); // Configura o GPIO como PWM
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

    // Configuração do ADC do Joystick - sensor de umidade
    adc_init();
    adc_gpio_init(JOYSTICK_X_PIN);
    uint16_t adc_value_x;

    // Configuração dos botões
    gpio_init(Botao_B);
    gpio_set_dir(Botao_B, GPIO_IN);
    gpio_pull_up(Botao_B);
    
    gpio_init(Botao_A);
    gpio_set_dir(Botao_A, GPIO_IN);
    gpio_pull_up(Botao_A);

    // Configuração do LED RGB VERMELHO
    gpio_init(LED_R_PIN);
    gpio_set_dir(LED_R_PIN, GPIO_OUT);
    gpio_put(LED_R_PIN, false); 

    bool ok;
    ok = set_sys_clock_khz(128000, false); // Configura o clock para 128 MHz
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

    //Temporizador para cálculo de velocidade
    struct repeating_timer timer;
    add_repeating_timer_ms(INTERVALO_AMOSTRAGEM, callback_temporizador, NULL, &timer);
    
    // Configuração das interrupções nos botões
    gpio_set_irq_enabled_with_callback(Botao_B, GPIO_IRQ_EDGE_FALL, true, &gpio_irq_handler);
    gpio_set_irq_enabled_with_callback(Botao_A, GPIO_IRQ_EDGE_FALL, true, &gpio_irq_handler);

    while (true) {
        if(botao_A_pressionado == true){
            desenho_pio(pio, sm);
            botao_A_pressionado =false;
        }
        if(iniciar_esteira){
            if(Parada_Critica=='B'){
                tratar_parada_critica(Parada_Critica);
                pwm_set_chan_level(led_slice_num, led_channel, 0); // desliga servo motor
                ssd1306_draw_string(&ssd, "PARADA CRITICA", 8, 50);                  
            }else{
                    
                    // Se o som ainda não foi tocado de inicio da esteira então toca
                if (!inicio_esteira) {
                    som_inicio_atividades();
                    acender_sinal_inicio();
                    pwm_set_chan_level(led_slice_num, led_channel, 500); // PWM na velocidade media
                }
                if (calcular_media) {
                    calcular_media = false;  // Resetar flag
                    media = (float)contador_latas / (INTERVALO_AMOSTRAGEM / 1000);  // Divide pelo tempo em segundos
                    contador_latas = 0;
                    
                    if (media < 0.25) {  // se é menor que 1 lata cada 4 segundos
                        if(estado_atual=='A'){ // se já esteve aqui parada crítica
                            Parada_Critica='O';
                        }else{
                            // Aumeto suave até o top de 1000 que é velocidade da esteira 6.16
                            for (int duty = 500; duty <= 1000; duty += 5) {
                                pwm_set_chan_level(led_slice_num, led_channel, duty); // LED segue o mesmo padrão
                                sleep_ms(10);
                            }
                            velocidade_E = 6.16;
                            estado_atual = 'A'; // Alta velocidade
                        }
                    }
                    if (media > 0.5) {  // se é maior que 1 lata cada 2 segundos
                        if(estado_atual=='B'){ // se já esteve aqui parada crítica
                            Parada_Critica='S';
                        }else{
                            // Diminuição suave até o valor de 50 que é velocidade da esteira 5.04
                            for (int duty = 500; duty >= 50; duty -= 5) {
                                pwm_set_chan_level(led_slice_num, led_channel, duty); // LED segue o mesmo padrão
                                sleep_ms(10);
                            }                          
                            velocidade_E = 5.04;
                            estado_atual = 'B'; // Baixa velocidade
                        }
                    }
                    if (media >= 0.25 && media <= 0.5){ 
                        // sobe ou desce a velocidade suavemente dependendo como esteve anteirormente
                        if (estado_atual == 'B' || estado_atual == 'A') {
                            int start = (estado_atual == 'B') ? 50 : 1000;
                            int end = 500;
                            int step = (estado_atual == 'B') ? 5 : -5;

                            for (int duty = start; duty != end + step; duty += step) {
                                pwm_set_chan_level(led_slice_num, led_channel, duty);
                                sleep_ms(10);
                            }
                        } 
                        velocidade_E = 5.6;
                        estado_atual='N';
                        Parada_Critica='N';
                    }

                    // **Se o estado atual for igual ao anterior, imprime a repetição**
                    if (Parada_Critica=='O'||Parada_Critica=='S') {
                        pwm_set_chan_level(led_slice_num, led_channel, 0); // desliga servo motor
                        tratar_parada_critica(Parada_Critica);
                        ssd1306_draw_string(&ssd, "PARADA CRITICA", 8, 50);
                    }
                    // Atualiza o último estado e o tempo de ajuste
                    ultimo_estado = estado_atual;
                }

                // Atualiza o display
                ssd1306_fill(&ssd, false);            
                ssd1306_draw_string(&ssd, "Embarcatech", 20, 6);
                ssd1306_draw_string(&ssd, "Velocidade:", 8, 18);
                sprintf(buffer, "V. Lata:%.2f", media);
                ssd1306_draw_string(&ssd, buffer, 8, 18);
                
                sprintf(buffer, "V. Est.:%.2f", velocidade_E);
                ssd1306_draw_string(&ssd, buffer, 8, 28);
                if(!iniciar_esteira){ssd1306_draw_string(&ssd, "PARADA CRITICA", 8, 50);}
                ssd1306_rect(&ssd, 3, 3, 122, 60, true, false);
            
            }
            // Leitura do ADC eixo X e controle do LED Vermelho
            adc_select_input(1);
            adc_value_x = adc_read();
            umidade =adc_value_x*100/4096; // umidade maxima 100%
            sprintf(buffer, "Umid.: %d%%", umidade);
            ssd1306_draw_string(&ssd, buffer, 8, 38);
            if(umidade>=60){
                ssd1306_draw_string(&ssd, "***", 92, 38); //***=Alta
                }
            if(umidade>=80){
                Parada_Critica == 'U'; //U = umidade
                pwm_set_chan_level(led_slice_num, led_channel, 0); // desliga servo motor
                tratar_parada_critica(Parada_Critica);
                ssd1306_draw_string(&ssd, "PARADA CRITICA", 8, 50);                  
                ssd1306_draw_string(&ssd, " * ", 90, 38); //*=Critica
                }
            ssd1306_send_data(&ssd); // Desenha no display
        }
        sleep_ms(100); 
    }
    sleep_ms(10);
} 