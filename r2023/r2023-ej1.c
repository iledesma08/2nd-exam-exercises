#include "LPC17xx.h"
#include "lpc17xx_adc.h"
#include "lpc17xx_gpio.h"
#include "lpc17xx_pwm.h"
#include "lpc17xx_timer.h"
#include "lpc17xx_pinsel.h"

// Definir el canal ADC y el pin de salida digital
#define ADC_CHANNEL_2 2  // Canal del ADC
#define OUTPUT_PIN (1 << 20)  // Suponiendo que el pin de salida es P0.20
#define OUTPUT 1

#define N_MUESTRAS 4 // Cantidad de muestras a promediar

// Frecuencias y tiempos
#define ADC_FREQ 100000  // Frecuencia de muestreo del ADC (100 kHz)
#define PWM_PERIOD 50 // 1/20 KHz = 50 us

// Variables para almacenar las muestras y el promedio
float muestras[N_MUESTRAS] = {0};  // Arreglo para las últimas 4 muestras
uint8_t indice = 0;  // Índice para el arreglo de muestras

// Prototipos de funciones
void config_pins(void);
void init_adc(void);
void init_timer0(void);
void init_timer1(void);
float leer_promedio(void);
void procesar_salida(float promedio);
void actualizar_muestra(void);

int main(void) {
    // Inicialización
    SystemInit();
    config_pins();
    init_adc();
    init_timer0();
    init_timer1();

    while (1) {
        // El bucle principal está controlado por las interrupciones del timer
    }

    return 0;
}

void config_pins(void) {
    // Salida Digital
    PINSEL_CFG_Type pin;
    pin.Portnum = PINSEL_PORT_0;
    pin.Pinnum = PINSEL_PIN_20;
    pin.Funcnum = PINSEL_FUNC_0; // GPIO
    pin.Pinmode = PINSEL_PINMODE_PULLDOWN;
    pin.OpenDrain = PINSEL_PINMODE_NORMAL;
    PINSEL_ConfigPin(&pin);
    // Configurar el pin de salida digital
    GPIO_SetDir(PINSEL_PORT_0, OUTPUT_PIN, OUTPUT);  // P0.20 como salida

    // ADC Canal 2
    pin.Pinnum = PINSEL_PIN_25; // Canal 2
    pin.Funcnum = PINSEL_FUNC_1; // ADC
    pin.Pinmode = PINSEL_PINMODE_TRISTATE; // Para no tener imprecisiones
    PINSEL_ConfigPin(&pin);
}

// Inicializar el ADC
void init_adc(void) {
    ADC_Init(LPC_ADC, ADC_FREQ); 
    ADC_ChannelCmd(LPC_ADC, ADC_CHANNEL_2, ENABLE);  // Habilitar el canal 2 ADC
}

// Inicializar el temporizador para leer el ADC cada 30 segundos
void init_timer0(void) {
    TIM_TIMERCFG_Type timer_cfg;
    TIM_MATCHCFG_Type match_cfg;

    // Configurar el Timer0 para generar una interrupción cada 30 segundos
    timer_cfg.PrescaleOption = TIM_PRESCALE_USVAL;
    timer_cfg.PrescaleValue = 1000000;  // 1 segundo
    TIM_Init(LPC_TIM0, TIM_TIMER_MODE, &timer_cfg);

    // Configurar el Match0 para que ocurra cada 30 segundos
    match_cfg.MatchChannel = 0;
    match_cfg.IntOnMatch = ENABLE;
    match_cfg.ResetOnMatch = ENABLE;
    match_cfg.StopOnMatch = DISABLE;
    match_cfg.MatchValue = 30;  // Coincidencia a los 30 segundos
    TIM_ConfigMatch(LPC_TIM0, &match_cfg);

    // Iniciar el Timer0
    TIM_Cmd(LPC_TIM0, ENABLE);

    // Habilitar la interrupción del Timer0
    NVIC_EnableIRQ(TIMER0_IRQn);
}

// Temporizador para generar señal PWM en GPIO
void init_timer1(void) {
    TIM_TIMERCFG_Type timer_cfg;
    TIM_MATCHCFG_Type match_cfg;

    // Configurar el Timer1 para generar una interrupción cada 30 segundos
    timer_cfg.PrescaleOption = TIM_PRESCALE_USVAL;
    timer_cfg.PrescaleValue = 1;  // Cada 1 microsegundo, aumenta el Timer
    TIM_Init(LPC_TIM0, TIM_TIMER_MODE, &timer_cfg);

    // Match0 = final duty cycle
    match_cfg.MatchChannel = 0;
    match_cfg.IntOnMatch = ENABLE;
    match_cfg.ResetOnMatch = DISABLE;
    match_cfg.StopOnMatch = DISABLE;
    match_cfg.MatchValue = 25;  // Coincidencia a los 25 segundos (duty cycle 50%, que es el minimo)
    TIM_ConfigMatch(LPC_TIM1, &match_cfg);

    // Match1 = nuevo ciclo
    match_cfg.MatchChannel = 0;
    match_cfg.IntOnMatch = ENABLE;
    match_cfg.ResetOnMatch = ENABLE; // Se reinicia si se alcanza match 1
    match_cfg.StopOnMatch = DISABLE;
    match_cfg.MatchValue = PWM_PERIOD;
    TIM_ConfigMatch(LPC_TIM1, &match_cfg);

    // Habilitar la interrupción del Timer1
    NVIC_EnableIRQ(TIMER1_IRQn);
}

// Manejador de la interrupción del Timer0 (cada 30 segundos)
void TIMER0_IRQHandler(void) {
    actualizar_muestra();  // Actualizar la muestra del ADC

    // Si han pasado 2 minutos (4 muestras), calcular el promedio y procesar la salida
    if (indice == 4) {
        indice = 0;  // Reiniciar el índice
        float promedio = leer_promedio();
        procesar_salida(promedio);
    }

    // Limpiar la bandera de interrupción
    TIM_ClearIntPending(LPC_TIM0, TIM_MR0_INT);
}

// Función que se llama cada vez que el temporizador alcanza los 30 segundos
void actualizar_muestra(void) {
    // Iniciar la conversión del ADC
    ADC_StartCmd(LPC_ADC, ADC_START_NOW);

    // Esperar a que la conversión termine
    while (!ADC_ChannelGetStatus(LPC_ADC, ADC_CHANNEL_2, ADC_DATA_DONE));

    // Leer el valor del ADC y convertirlo a voltaje
    uint16_t valor_adc = ADC_ChannelGetData(LPC_ADC, ADC_CHANNEL_2);
    float voltaje = (valor_adc / 4095.0) * 3.3; // El ADC es de 12 bits (4095 valores)

    // Almacenar la nueva muestra en el arreglo de muestras
    muestras[indice] = voltaje;
    indice++;
}

// Función para obtener el promedio de las últimas 4 muestras
float leer_promedio(void) {
    float suma = 0;
    for (int i = 0; i < 4; i++) {
        suma += muestras[i];
    }
    return suma / 4;  // Devolver el promedio
}

// Función para procesar la salida según el promedio calculado
void procesar_salida(float promedio) {
    if (promedio < 1.0) {
        // Deshabilitar el Timer1
        TIM_Cmd(LPC_TIM1, DISABLE);
        // Si el promedio es menor a 1V, salida digital en 0V
        GPIO_ClearValue(PINSEL_PORT_0, OUTPUT_PIN);
    } else if (promedio >= 1.0 && promedio <= 2.0) {
        // Si el promedio está entre 1V y 2V, generar PWM con ciclo de trabajo proporcional
        uint32_t duty_cycle_time = (50 + (40 * (promedio - 1.0)))*50;  // Tiempo de ciclo de trabajo entre 50% y 90% de 50 us (1/20 KHz)
        // Configurar valor Match 0 del Timer (final del duty cycle)
        TIM_UpdateMatchValue(LPC_TIM1, 0, duty_cycle_time);
        // Reiniciar el contador del Timer1
        TIM_ResetCounter(LPC_TIM1);
        // Habilitar el Timer1
        TIM_Cmd(LPC_TIM1, ENABLE);
    } else {
        // Deshabilitar el Timer1
        TIM_Cmd(LPC_TIM1, DISABLE);
        // Si el promedio es mayor a 2V, salida digital en 1 (3.3V)
        GPIO_SetValue(PINSEL_PORT_0, OUTPUT_PIN);
    }
}

// Para modular la señal PWM
void TIMER1_IRQHandler(void) {
    // Si terminó el duty cycle
    if (TIM_GetIntStatus(LPC_TIM1, TIM_MR0_INT)) {
        // Poner salida digital en 0
        GPIO_ClearValue(PINSEL_PORT_0, OUTPUT_PIN);
        // Limpiar la bandera de interrupción
        TIM_ClearIntPending(LPC_TIM1, TIM_MR0_INT);
    }
    // Si empieza un nuevo duty cycle
    else {
        // Poner salida digital en 1
        GPIO_SetValue(PINSEL_PORT_0, OUTPUT_PIN);
        // Limpiar la bandera de interrupción
        TIM_ClearIntPending(LPC_TIM1, TIM_MR1_INT);
    }
}