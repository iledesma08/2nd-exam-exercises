/*
* Falta coordinar DMA con DAC para que se genere la señal de salida.
* Falta hacer que en la 3era interrupcion se muestre lo del bloque 1 y 3
* Falta acomodar muestra de 32 bits para DAC que tiene 10 bits de datos
*/

#include "LPC17xx.h"
#include "lpc17xx_dac.h"
#include "lpc17xx_gpio.h"
#include "lpc17xx_gpdma.h"
#include "lpc17xx_timer.h"
#include "lpc17xx_exti.h"
#include "lpc17xx_pinsel.h"

// Definir las direcciones de los bloques de datos
#define DIRECCION_BLOQUE_1 0x10000000  // Dirección de memoria de bloque 0
#define DIRECCION_BLOQUE_2 0x10001000  // Dirección de memoria de bloque 1
#define DIRECCION_BLOQUE_3 0x10002000  // Dirección de memoria de bloque 2
#define FREQ_BLOQUE_1 60 // Hz
#define FREQ_BLOQUE_2 120
#define FREQ_BLOQUE_3 450

#define CLK_DAC_HZ 25000000
#define DAC_FREQ_1 407 // (CLK_DAC_HZ)/(MUESTRAS_POR_BLOQUE*FREQ_BLOQUE_0);
#define DAC_FREQ_2 203
#define DAC_FREQ_3 54
#define LOW_POWER 1

#define MUESTRAS_POR_BLOQUE 1024 // 4KB = 4096 bytes = 32768 bits / 32 bits = 1024

#define DAC_OUTPUT ((uint32_t)(1<<26)) //P0.26
#define INT_BTN ((uint32_t)(1<<0)) //P2.10

/* GPIO Direction Definitions */
#define INPUT  0
#define OUTPUT 1

// Variables para rastrear la interrupción y frecuencia de salida
volatile uint8_t current_block = 0;

void setup_dac(void) {
    DAC_CONVERTER_CFG_Type dac;

    // Configure DAC settings
    DAC_Struct.CNT_ENA = ENABLE; // Enable DAC counter mode (timeout mode)
    DAC_Struct.DMA_ENA = ENABLE; // Enable DAC DMA mode
    DAC_SetBias(LPC_DAC, LOW_POWER);  // Modo bajo consumo
    // Apply the DAC configuration
    DAC_ConfigDAConverterControl(LPC_DAC, &DAC_Struct);
    DAC_Init(LPC_DAC);        // Initialize the DAC
}

void setup_dma(void) {
    GPDMA_Channel_CFG_Type dma_cfg;

    // Configurar el canal DMA para transferir desde la memoria al DAC
    dma_cfg.ChannelNum = 0;                   // Canal DMA 0
    dma_cfg.TransferSize = MUESTRAS_POR_BLOQUE;  // Tamaño del bloque de datos
    dma_cfg.SrcMemAddr = DIRECCION_BLOQUE_0;      // Dirección del bloque de datos
    dma_cfg.DstMemAddr = (uint32_t)&LPC_DAC->DACR;  // Transferir al DAC
    dma_cfg.TransferType = GPDMA_TRANSFERTYPE_M2P;  // Memoria a periférico (DAC)
    dma_cfg.SrcConn = 0;                      // No se usa periférico de origen
    dma_cfg.DstConn = GPDMA_CONN_DAC;         // Conexión destino es el DAC
    dma_cfg.DMALLI = 0;                       // No usamos Linked List

    // Configurar el DMA
    GPDMA_Setup(&dma_cfg);

    // Iniciar el DMA
    GPDMA_ChannelCmd(0, ENABLE);
}

void change_waveform(void) {
    // Detener el DMA
    GPDMA_ChannelCmd(0, DISABLE);

    GPDMA_Channel_CFG_Type dma_cfg;

    // Configurar el canal DMA para transferir desde la memoria al DAC
    dma_cfg.ChannelNum = 0;                   // Canal DMA 0
    dma_cfg.TransferSize = MUESTRAS_POR_BLOQUE;  // Tamaño del bloque de datos
    switch (current_block) {
        case 1:
            dma_cfg.SrcMemAddr = DIRECCION_BLOQUE_0;
            DAC_SetDMATimeOut(LPC_DAC, DAC_FREQ_0);
            break;
        case 2:
            dma_cfg.SrcMemAddr = DIRECCION_BLOQUE_1;
            DAC_SetDMATimeOut(LPC_DAC, DAC_FREQ_1);
            break;
        case 3:
            dma_cfg.SrcMemAddr = DIRECCION_BLOQUE_2;
            DAC_SetDMATimeOut(LPC_DAC, DAC_FREQ_2);
            break;
    }
    dma_cfg.DstMemAddr = (uint32_t)&LPC_DAC->DACR;  // Transferir al DAC
    dma_cfg.TransferType = GPDMA_TRANSFERTYPE_M2P;  // Memoria a periférico (DAC)
    dma_cfg.SrcConn = 0;                      // No se usa periférico de origen
    dma_cfg.DstConn = GPDMA_CONN_DAC;         // Conexión destino es el DAC
    dma_cfg.DMALLI = 0;                       // No usamos Linked List

    // Configurar el DMA
    GPDMA_Setup(&dma_cfg);

    // Iniciar el DMA
    GPDMA_ChannelCmd(0, ENABLE);
}

void config_ports(void) {
    PINSEL_CFG_Type PinCfg;

    // Configurar salida del DAC

    PinCfg.Portnum = PINSEL_PORT_0;
    PinCfg.Pinnum = 26;  
    PinCfg.Funcnum = PINSEL_FUNC_2;  // DAC
    PinCfg.Pinmode = PINSEL_PINMODE_PULLUP;
    PinCfg.OpenDrain = PINSEL_PINMODE_NORMAL;
    PINSEL_ConfigPin(&PinCfg);
    
    // Configurar la interrupción externa para controlar los cambios de bloque y frecuencia
    
    PinCfg.Portnum = PINSEL_PORT_2;
    PinCfg.Pinnum = 10; 
    PinCfg.Funcnum = PINSEL_FUNC_1;  // EINT0
    PINSEL_ConfigPin(&PinCfg);

    GPIO_SetDir(PINSEL_PORT_0, DAC_OUTPUT, OUTPUT);
}

void config_exti(void) {
    // Configurar la interrupción EINT0
    EXTI_InitTypeDef exti_cfg;
    exti_cfg.EXTI_Line = EXTI_EINT0;
    exti_cfg.EXTI_Mode = EXTI_MODE_EDGE_SENSITIVE;
    exti_cfg.EXTI_polarity = EXTI_POLARITY_LOW_ACTIVE_OR_FALLING_EDGE;
    EXTI_Config(&exti_cfg);

    EXTI_ClearEXTIFlag(EXTI_EINT0);
    EXTI_Init();

    // Habilitar la interrupción en el NVIC
    NVIC_EnableIRQ(EINT0_IRQn);
}

void EINT0_IRQHandler(void) {
    // Cambiar la frecuencia y el bloque de memoria según la interrupción recibida
    current_block++;
    if (current_block == 4) {
        current_block = 1;
    }
    change_waveform();

    // Limpiar la bandera de interrupción
    EXTI_ClearEXTIFlag(EXTI_EINT0);
}

int main(void) {
    SystemInit();
    
    // Inicializar el DAC, DMA y la interrupción externa y configurar puertos
    setup_dac();
    setup_dma();
    config_exti();
    config_ports();

    while (1) {
        // El sistema estará controlado por las interrupciones
    }

    return 0;
}