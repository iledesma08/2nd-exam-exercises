#include "LPC17xx.h"
#include "lpc17xx_timer.h"
#include "lpc17xx_dac.h"
#include "lpc17xx_pinsel.h"

#define LOW_POWER_MODE 1

// Tiempos registrados por el capture
volatile uint32_t t_on = 0;         // Tiempo en alto (T_on)
volatile uint32_t t_total = 0;      // Tiempo total (T)
volatile uint32_t last_rising_edge = 0;  // Guardar el último flanco de subida

// Buffer para almacenar los últimos 10 valores de ciclo de trabajo
#define NUM_VALORES 10
volatile uint32_t buffer_duty_cycle[NUM_VALORES] = {0};

// Variables globales
volatile uint8_t indice = 0;  // Índice del buffer
volatile uint8_t n_capture = 0; // 1 = primer captura (subida), 2 = segunda captura (bajada), 3 = nuevo periodo

// Prototipos de funciones
void config_pins(void);
void init_timer1_capture(void);
void init_timer0_dac(void);
void init_dac(void);
uint32_t calcular_promedio(void);

int main(void) {
    SystemInit();
    
    // Configurar pines
    config_pins();
    
    // Inicializar el Timer1 para captura de señal PWM
    init_timer1_capture();

    // Inicializar el Timer0 para actualizar el DAC
    init_timer0_dac();

    // Inicializar el DAC
    init_dac();

    while (1) {
        
    }

    return 0;
}

void config_pins(void) {
    PINSEL_CFG_Type pin;

    // Configurar pin como Canal 0 de Captura del Timer 1
    pin.Portnum = PINSEL_PORT_1;
    pin.Pinnum = PINSEL_PIN_18;
    pin.Funcnum = PINSEL_FUNC_3; // Funcion de Captura
    pin.Pinmode = PINSEL_PINMODE_PULLDOWN; // Forzamos un 0 logico siempre y cuando no haya un 1 logico a la entrada
    pin.OpenDrain = PINSEL_PINMODE_NORMAL;
    PINSEL_ConfigPin(&pin);

    // Configurar pin como DAC
    pin.Portnum = PINSEL_PORT_0;
    pin.Pinnum = PINSEL_PIN_26;
    pin.Funcnum = PINSEL_FUNC_2; // Funcion de DAC
    pin.Pinmode = PINSEL_PINMODE_TRISTATE; // Para evitar sacar una tensión imprecisa
    PINSEL_ConfigPin(&pin);
}

// Inicializar el DAC
void init_dac(void) {
    DAC_SetBias(LPC_DAC, LOW_POWER_MODE);
    DAC_Init(LPC_DAC);  // Inicializar el DAC
}

// Inicializar el Timer1 para captura de PWM en CAP1.0
void init_timer1_capture(void) {
    TIM_TIMERCFG_Type timer_cfg;
    TIM_CAPTURECFG_Type capture_cfg;

    // Configurar el Timer1
    timer_cfg.PrescaleOption = TIM_PRESCALE_TICKVAL;
    timer_cfg.PrescaleValue = 100;  // Cada tick es 1 microsegundo (100 MHz / 100)
    TIM_Init(LPC_TIM1, TIM_TIMER_MODE, &timer_cfg);

    // Configurar la captura en CAP1.0 para capturar flancos de subida y bajada
    capture_cfg.CaptureChannel = 0;
    capture_cfg.RisingEdge = ENABLE;
    capture_cfg.FallingEdge = ENABLE;
    capture_cfg.IntOnCaption = ENABLE;  // Habilitar interrupción en captura

    TIM_ConfigCapture(LPC_TIM1, &capture_cfg);

    // Habilitar interrupción para el Timer1
    NVIC_EnableIRQ(TIMER1_IRQn);
    // Capture < Update DAC
    NVIC_SetPriority(TIMER1_IRQn, 2);

    // Iniciar el Timer1
    TIM_Cmd(LPC_TIM1, ENABLE);
}

// Manejador de la interrupción de captura del Timer1
void TIMER1_IRQHandler(void) {
    uint32_t captura_actual = TIM_GetCaptureValue(LPC_TIM1, TIM_COUNTER_INCAP0);
    n_capture++;

    switch (n_capture) {
        // Flanco ascendente
        case 1:
            last_rising_edge = captura_actual;  // Actualizar el último flanco de subida
            break;
        // Flanco descendente
        case 2:
            t_on = captura_actual - last_rising_edge;  // Calcular el tiempo T_on
            break;
        // Nuevo periodo
        case 3:
            t_total = captura_actual - last_rising_edge;  // Calcular el período total (T)
            last_rising_edge = captura_actual;  // Actualizar el último flanco de subida
            n_capture = 1; // Porque el siguiente flanco va a ser descendente
            
            // Guardar ciclo de trabajo en el buffer
            save_duty();
            break;
    }

    // Limpiar la bandera de interrupción de captura
    TIM_ClearIntCapturePending(LPC_TIM1, 0);
}

// Calcular y guardar ciclo de trabajo en buffer
void save_duty(void) {
    uint32_t duty_cycle = 0;   // Ciclo de trabajo (%)
    if (t_total > 0) {
        duty_cycle = (t_on * 100) / t_total;  // Calcular el ciclo de trabajo (%)
    }

    // Guardar el ciclo de trabajo en el buffer
    buffer_duty_cycle[indice] = duty_cycle;
    indice++;
    if (indice == NUM_VALORES) {
        indice = 0;
    }
}

// Inicializar el Timer0 para actualizar DAC
void init_timer0_dac(void) {
    TIM_TIMERCFG_Type timer_cfg;
    TIM_MATCHCFG_Type match_cfg;

    // Configurar Timer0
    timer_cfg.PrescaleOption = TIM_PRESCALE_USVAL;
    timer_cfg.PrescaleValue = 1000; // El timer 0 aumenta cada 1 ms
    TIM_Init(LPC_TIM0, TIM_TIMER_MODE, &timer_cfg);
    
    match_cfg.MatchChannel = 0;
    match_cfg.IntOnMatch = ENABLE;
    match_cfg.StopOnMatch = DISABLE;
    match_cfg.ResetOnMatch = ENABLE;
    match_cfg.MatchValue = 500; // Interrupcion cada 0,5s = 500 ms
    TIM_ConfigMatch(LPC_TIM0, &match_cfg);

    // Habilitar interrupción para el Timer0
    NVIC_EnableIRQ(TIMER0_IRQn);
    // Capture < Update DAC
    NVIC_SetPriority(TIMER0_IRQn, 1);

    // Iniciar el Timer0
    TIM_Cmd(LPC_TIM0, ENABLE);
}

void TIMER0_IRQHandler(void) {
    uint32_t promedio = calcular_promedio();

    // Mapear el ciclo de trabajo a un voltaje entre 0 y 2V
    uint32_t valor_dac = ((promedio/100) * 1023);  // Mapear 0-100% a 0-1023 (DAC de 10 bits)
    DAC_UpdateValue(LPC_DAC, valor_dac);
}

// Función para calcular el promedio de los últimos 10 valores
uint32_t calcular_promedio(void) {
    uint32_t suma = 0;
    for (uint8_t i = 0; i < NUM_VALORES; i++) {
        suma += buffer_duty_cycle[i];
    }
    return suma / (NUM_VALORES);  // Devolver el promedio (%)
}
