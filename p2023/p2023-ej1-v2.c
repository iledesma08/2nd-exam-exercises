#include "LPC17xx.h"
#include "lpc17xx_timer.h"
#include "lpc17xx_dac.h"
#include "lpc17xx_pinsel.h"
#include "lpc17xx_gpdma.h"

#define LOW_POWER_MODE 1
#define CLOCK_DAC_MHZ  25 // DAC clock: 25 MHz (CCLK divided by 4)
#define DESIRED_DAC_RATE 2 // 0.5 s = 2 Hz
#define DMA_CHANNEL_0 0

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
volatile uint16_t dac_value = 0; // Valor al que se debe actualizar el dac

// Prototipos de funciones
void config_pins(void);
void init_timer1_capture(void);
void init_dma(void);
void init_dac(void);
void calcular_valor_dac(void);

int main(void) {
    SystemInit();

    // Configurar pines
    config_pins();
    
    // Inicializar el Timer1 para captura de señal PWM
    init_timer1_capture();

    // Inicializar el DAC
    init_dac();

    // Inicializar DMA
    init_dma();

    // Inicializar canal 0 DMA
    GPDMA_ChannelCmd(DMA_CHANNEL_0, ENABLE);

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
    // Para definir el rate de actualizacion a través del DMA
    DAC_CONVERTER_CFG_Type dac;
    dac.CNT_ENA = ENABLE;
    dac.DMA_ENA = ENABLE;
    DAC_ConfigDAConverterControl(LPC_DAC, &dac);
    DAC_SetBias(LPC_DAC, LOW_POWER_MODE);
    uint32_t ticks_until_update = (CLOCK_DAC_MHZ * 1000000)/DESIRED_DAC_RATE; // 50M ticks at 25MHz = 0.5s
    DAC_SetDMATimeOut(LPC_DAC, ticks_until_update); // Configurar rate de actualizacion
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
    // Si es la decima muestra, guardar el valor
    if (indice == NUM_VALORES) {
        calcular_valor_dac();
        indice = 0;
    }
}

// Función para calcular el valor del DAC
void calcular_valor_dac(void) {
    uint32_t suma = 0;
    for (uint8_t i = 0; i < NUM_VALORES; i++) {
        suma += buffer_duty_cycle[i];
    }
    dac_value = (suma / (NUM_VALORES*100))*1023; // Mapear 0-100% a 0-1023 (DAC de 10 bits)
}

void init_dma(void) {
    GPDMA_Channel_CFG_Type dma;
    dma.ChannelNum = DMA_CHANNEL_0;
    dma.SrcMemAddr = (uint32_t)&dac_value; // Direccion donde se guarda el valor a cargar en el DAC
    dma.DstMemAddr = 0; // El destino es el DAC, un periferico
    dma.TransferSize = 1; // Una sola transferencia a la vez
    dma.TransferWidth = 0; // No se usa, solamente para M2M
    dma.TransferType = GPDMA_TRANSFERTYPE_M2P;
    dma.SrcConn = 0; // El origen es una direccion de memoria
    dma.DstConn = GPDMA_CONN_DAC; // Destino: DAC
    dma.DMALLI = 0; // No usamos linked list

    // Aplicar DMA config
    GPDMA_Setup(&dma);
}