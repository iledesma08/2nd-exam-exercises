#include "LPC17xx.h"
#include "lpc17xx_adc.h"
#include "lpc17xx_dac.h"
#include "lpc17xx_gpdma.h"
#include "lpc17xx_timer.h"
#include "lpc17xx_exti.h"

// Definir las direcciones de memoria para AHB SRAM (Bank 0)
#define BUFFER_SIZE 1024  // Tamaño del buffer circular para ADC y DAC
uint16_t adc_buffer[BUFFER_SIZE];  // Buffer circular para almacenar muestras ADC
uint16_t dac_waveform[BUFFER_SIZE];  // Buffer para almacenar la forma de onda triangular

// Variables de control
volatile uint8_t adc_mode = 1;  // 1: Capturando con ADC, 0: Reproduciendo con DAC

// Prototipos de funciones
void init_adc_dma(void);
void init_dac_dma(void);
void init_timer_for_adc(void);
void init_ext_interrupt(void);
void EINT0_IRQHandler(void);

int main(void) {
    SystemInit();

    // Generar la onda triangular en el buffer dac_waveform
    generar_onda_triangular();

    // Inicializar ADC con DMA para capturar señales
    init_adc_dma();

    // Inicializar el DAC con DMA para reproducir la señal de onda triangular
    init_dac_dma();

    // Configurar el Timer para sincronizar el ADC
    init_timer_for_adc();

    // Inicializar la interrupción externa para alternar entre modos
    init_ext_interrupt();

    while (1) {
        // El programa principal se controla a través de interrupciones
    }

    return 0;
}

// Inicializar el ADC con DMA para captura de datos
void init_adc_dma(void) {
    // Configuración del ADC
    ADC_Init(LPC_ADC, 200000);  // Configurar el ADC a 200 kHz
    ADC_ChannelCmd(LPC_ADC, ADC_CHANNEL_0, ENABLE);  // Habilitar el canal 0

    // Configuración del DMA para el ADC
    GPDMA_Init();
    GPDMA_Channel_CFG_Type dmaCfg;

    dmaCfg.ChannelNum = 0;  // Canal 0 de DMA
    dmaCfg.SrcMemAddr = 0;  // No aplicable porque el origen es el ADC
    dmaCfg.DstMemAddr = (uint32_t)adc_buffer;  // Dirección de destino (buffer circular)
    dmaCfg.TransferSize = BUFFER_SIZE;  // Tamaño de la transferencia
    dmaCfg.TransferWidth = GPDMA_WIDTH_HALFWORD;  // Transferencia de 16 bits (muestras de ADC)
    dmaCfg.TransferType = GPDMA_TRANSFERTYPE_P2M;  // Transferencia de periférico a memoria
    dmaCfg.SrcConn = GPDMA_CONN_ADC;  // Fuente es el ADC
    dmaCfg.DstConn = 0;
    dmaCfg.DMALLI = 0;  // No se utiliza la lista vinculada

    // Configurar el canal de DMA
    GPDMA_Setup(&dmaCfg);

    // Habilitar el DMA para el ADC
    GPDMA_ChannelCmd(0, ENABLE);
}

// Inicializar el DAC con DMA para reproducir la forma de onda
void init_dac_dma(void) {
    DAC_Init(LPC_DAC);

    // Configurar el DAC para utilizar DMA
    GPDMA_Channel_CFG_Type dmaCfg;

    dmaCfg.ChannelNum = 1;  // Canal 1 de DMA para DAC
    dmaCfg.SrcMemAddr = (uint32_t)dac_waveform;  // Dirección de origen (forma de onda)
    dmaCfg.DstMemAddr = (uint32_t)&(LPC_DAC->DACR);  // Dirección de destino (DAC)
    dmaCfg.TransferSize = BUFFER_SIZE;  // Tamaño de la transferencia
    dmaCfg.TransferWidth = GPDMA_WIDTH_HALFWORD;  // Transferencia de 16 bits
    dmaCfg.TransferType = GPDMA_TRANSFERTYPE_M2P;  // Transferencia de memoria a periférico
    dmaCfg.SrcConn = 0;
    dmaCfg.DstConn = GPDMA_CONN_DAC;  // Destino es el DAC
    dmaCfg.DMALLI = 0;  // No se utiliza la lista vinculada

    // Configurar el canal de DMA
    GPDMA_Setup(&dmaCfg);

    // Habilitar el DMA para el DAC
    GPDMA_ChannelCmd(1, ENABLE);
}

// Configurar el Timer para sincronizar el ADC a 16 kHz
void init_timer_for_adc(void) {
    TIM_TIMERCFG_Type timerCfg;
    TIM_MATCHCFG_Type matchCfg;

    timerCfg.PrescaleOption = TIM_PRESCALE_USVAL;
    timerCfg.PrescaleValue = 1;  // 1 us por tick

    TIM_Init(LPC_TIM0, TIM_TIMER_MODE, &timerCfg);

    // Configurar el Match para generar una interrupción cada 62.5 us (16 kHz)
    matchCfg.MatchChannel = 0;
    matchCfg.IntOnMatch = ENABLE;
    matchCfg.ResetOnMatch = ENABLE;
    matchCfg.MatchValue = 63;  // 63 us para 16 kHz
    TIM_ConfigMatch(LPC_TIM0, &matchCfg);

    // Iniciar el Timer0
    TIM_Cmd(LPC_TIM0, ENABLE);
}

// Inicializar la interrupción externa (EINT0) para alternar modos
void init_ext_interrupt(void) {
    EXTI_Init();
    EXTI_InitTypeDef extCfg;

    extCfg.EXTI_Line = EXTI_EINT0;
    extCfg.EXTI_Mode = EXTI_MODE_EDGE_SENSITIVE;
    extCfg.EXTI_polarity = EXTI_POLARITY_HIGH_ACTIVE_OR_RISING_EDGE;

    EXTI_Config(&extCfg);

    NVIC_EnableIRQ(EINT0_IRQn);
}

// Manejador de la interrupción externa (EINT0)
void EINT0_IRQHandler(void) {
    if (adc_mode == 1) {
        // Detener el ADC y comenzar a reproducir el buffer capturado con el DAC
        GPDMA_ChannelCmd(0, DISABLE);  // Detener DMA para ADC
        GPDMA_ChannelCmd(1, ENABLE);  // Habilitar DMA para DAC
        adc_mode = 0;
    } else {
        // Detener el DAC y reanudar la captura con el ADC
        GPDMA_ChannelCmd(1, DISABLE);  // Detener DMA para DAC
        GPDMA_ChannelCmd(0, ENABLE);  // Habilitar DMA para ADC
        adc_mode = 1;
    }

    // Limpiar la interrupción de EINT0
    EXTI_ClearEXTIFlag(EXTI_EINT0);
}

// Función para inicializar la forma de onda triangular en dac_waveform
void generar_onda_triangular(void) {
    uint32_t i;

    // Primera mitad: subida de la señal
    for (i = 0; i < BUFFER_SIZE / 2; i++) {
        dac_waveform[i] = (i * 1023) / (BUFFER_SIZE / 2);  // Rango de 0 a 1023
    }

    // Segunda mitad: bajada de la señal
    for (i = BUFFER_SIZE / 2; i < BUFFER_SIZE; i++) {
        dac_waveform[i] = 1023 - ((i - BUFFER_SIZE / 2) * 1023) / (BUFFER_SIZE / 2);  // Rango de 1023 a 0
    }
}
