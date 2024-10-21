#include "LPC17xx.h"
#include "lpc17xx_adc.h"
#include "lpc17xx_timer.h"

#define NUM_DATOS 20
#define ADC_FREQ 100000

// Variables globales
uint16_t buffer_canal_2[NUM_DATOS] = {0};  // Buffer para los datos del canal 2
uint16_t buffer_canal_4[NUM_DATOS] = {0};  // Buffer para los datos del canal 4
uint8_t indice_canal_2 = 0;  // Índice para el canal 2
uint8_t indice_canal_4 = 0;  // Índice para el canal 4

// Prototipos de funciones
void init_adc(void);
void init_timer0(void);
void guardar_datos(uint16_t valor, uint8_t canal);

int main(void) {
    SystemInit(); // Inicializar System clk
    
    // Inicializar el ADC
    init_adc();

    // Inicializar el Timer0 para generar una interrupción cada 50 microsegundos (20 kHz)
    init_timer0();

    // Bucle principal
    while (1) {
        // Las transferencias de ADC se controlan a través de interrupciones
    }
}

// Inicializar el ADC para los canales 2 y 4
void init_adc(void) {
    // Inicializar el ADC con una frecuencia de 100 kHz
    ADC_Init(LPC_ADC, ADC_FREQ);

    // Habilitar los canales 2 y 4
    ADC_ChannelCmd(LPC_ADC, ADC_CHANNEL_2, ENABLE);
    ADC_ChannelCmd(LPC_ADC, ADC_CHANNEL_4, ENABLE);
    // Deshabilitar modo ráfaga
    ADC_BurstCmd(LPC_ADC, DISABLE);
    // Configurar para que el ADC se dispare por el Timer0 cuando alcance el Match1 (MAT0.1)
    ADC_StartCmd(LPC_ADC, ADC_START_ON_MAT01);
}

// Inicializar el Timer0 para generar una interrupción cada 50 microsegundos (20 kHz)
void init_timer0(void) {
    TIM_TIMERCFG_Type timerCfg;
    TIM_MATCHCFG_Type matchCfg;

    // Configurar el Timer0
    timerCfg.PrescaleOption = TIM_PRESCALE_USVAL;
    timerCfg.PrescaleValue = 1;  // El timer aumenta en 1 cada 1 us
    TIM_Init(LPC_TIM0, TIM_TIMER_MODE, &timerCfg);

    // Configurar el Match1 para que ocurra cada 50 microsegundos (20 kHz)
    matchCfg.MatchChannel = 1; // Se configura en Match1 para el Timer0 (puede ser Match1 o 3 para este timer)
    matchCfg.IntOnMatch = ENABLE;
    matchCfg.ResetOnMatch = ENABLE;
    matchCfg.StopOnMatch = DISABLE;
    matchCfg.MatchValue = 50;  // 50 us para 20 kHz
    TIM_ConfigMatch(LPC_TIM0, &matchCfg);

    // Iniciar el Timer0
    TIM_Cmd(LPC_TIM0, ENABLE);

    // Habilitar la interrupción del Timer0
    NVIC_EnableIRQ(TIMER0_IRQn);
}

// Manejador de la interrupción del Timer0 (dispara el ADC)
void TIMER0_IRQHandler(void) {
    // Esperar a que los datos del canal 2 estén listos
    while (!ADC_ChannelGetStatus(LPC_ADC, ADC_CHANNEL_2, ADC_DATA_DONE));
    uint16_t valor_canal_2 = ADC_ChannelGetData(LPC_ADC, ADC_CHANNEL_2);

    // Esperar a que los datos del canal 4 estén listos
    while (!ADC_ChannelGetStatus(LPC_ADC, ADC_CHANNEL_4, ADC_DATA_DONE));
    uint16_t valor_canal_4 = ADC_ChannelGetData(LPC_ADC, ADC_CHANNEL_4);

    // Guardar los datos del canal 2 en su buffer
    guardar_datos(valor_canal_2, ADC_CHANNEL_2);

    // Guardar los datos del canal 4 en su buffer
    guardar_datos(valor_canal_4, ADC_CHANNEL_4);

    // Limpiar la interrupción del Timer0
    TIM_ClearIntPending(LPC_TIM0, TIM_MR0_INT);
}

// Guardar los datos en los buffers correspondientes
void guardar_datos(uint16_t valor, uint8_t canal) {
    if (canal == 2) {
        buffer_canal_2[indice_canal_2] = valor;  // Guardar en el buffer del canal 2
        indice_canal_2++; // Actualizar el índice y resetear si corresponde
        if (indice_canal_2==20) {
            indice_canal_2 = 0;
        } 
    } else if (canal == 4) {
        buffer_canal_4[indice_canal_4] = valor;  // Guardar en el buffer del canal 4
        indice_canal_4++; // Actualizar el índice y resetear si corresponde
        if (indice_canal_4==20) {
            indice_canal_4 = 0;
        } 
    }
}
