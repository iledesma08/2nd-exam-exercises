#include "LPC17xx.h"
#include "lpc17xx_dac.h"
#include "lpc17xx_timer.h"
#include "lpc17xx_pinsel.h"

#define FAST_MODE 0

// Variables globales
uint16_t dac_value = 0;               // Valor actual del DAC (inicia en 0)
int8_t direction = 1;                 // Dirección del incremento (1: subiendo, -1: bajando)
const uint16_t DAC_MAX_VALUE = 1023;  // Valor máximo del DAC (10 bits)
const uint16_t DAC_MIN_VALUE = 0;     // Valor mínimo del DAC

// Prototipos de funciones
void init_timer0(void);
void TIMER0_IRQHandler(void);
void init_dac(void);

int main(void) {
    SystemInit(); // Inicializar System clk
    
    // Configurar pins
    config_pins();

    // Inicializar el DAC
    init_dac();

    // Inicializar el Timer0 para generar interrupciones periódicas
    init_timer0();

    // Bucle principal
    while (1) {
        // La generación de la señal se maneja a través de interrupciones
    }
}

void config_pins(void) {
    PINSEL_CFG_Type pin;
    
    // Configuracion del pin 0.26 como salida de DAC
    pin.Portnum = PINSEL_PORT_0;
    pin.Pinnum = PINSEL_PIN_26;
    pin.Funcnum = PINSEL_FUNC_2;
    pin.Pinmode = PINSEL_PINMODE_TRISTATE;
    pin.OpenDrain = PINSEL_PINMODE_NORMAL;
    PINSEL_ConfigPin(&pin);
}

void init_dac(void) {
    DAC_CONVERTER_CFG_Type dac;
    // Configuraciones del DAC
    dac.CNT_ENA = RESET; // Deshabilitar DAC timeout (el DAC va a convertir siempre que reciba un valor nuevo)
    dac.DMA_ENA = RESET; // No se usa dma
    dac.DBLBUF_ENA = RESET; // No usamos doble buffering
    DAC_ConfigDAConverterControl(LPC_DAC, &dac); // Cargar config
    DAC_SetBias(LPC_DAC, FAST_MODE);  // Configurar al DAC en modo de alto rendimiento (minimo periodo posible de 1 us)
    DAC_Init(LPC_DAC);  // Inicializar DAC
}

// Inicializar el Timer0 para generar una interrupción periódica
void init_timer0(void) {
    TIM_TIMERCFG_Type timerCfg;
    TIM_MATCHCFG_Type matchCfg;

    // Configuración básica del Timer0
    timerCfg.PrescaleOption = TIM_PRESCALE_TICKVAL;
    timerCfg.PrescaleValue = 100;  // Dividir el reloj a 1 MHz (100 MHz / 100)

    // Inicializar el Timer0 con la configuración anterior
    TIM_Init(LPC_TIM0, TIM_TIMER_MODE, &timerCfg);

    // Configurar el Match0 para generar una coincidencia cada 1 µs (esto da el mínimo periodo)
    matchCfg.MatchChannel = 0;
    matchCfg.IntOnMatch = ENABLE;  // Habilitar la interrupción en el match
    matchCfg.ResetOnMatch = ENABLE;  // Reiniciar el Timer cuando ocurra la coincidencia
    matchCfg.StopOnMatch = DISABLE;  // No detener el Timer en el match
    matchCfg.MatchValue = 1;  // 1 µs para el menor tiempo posible
    TIM_ConfigMatch(LPC_TIM0, &matchCfg);

    // Iniciar el Timer0
    TIM_Cmd(LPC_TIM0, ENABLE);

    // Habilitar la interrupción del Timer0
    NVIC_EnableIRQ(TIMER0_IRQn);
}

// Manejador de la interrupción del Timer0 (controla el DAC)
void TIMER0_IRQHandler(void) {
    // Actualizar el valor del DAC
    DAC_UpdateValue(LPC_DAC, dac_value);

    // Modificar el valor del DAC según la dirección
    dac_value += direction;

    // Verificar si se ha alcanzado el valor máximo o el mínimo
    if (dac_value == DAC_MAX_VALUE) {
        direction = -1;  // Cambiar la dirección a bajada
    }
    else if (dac_value == DAC_MIN_VALUE) {
        direction = 1;  // Cambiar la dirección a subida
    }

    // Limpiar la bandera de interrupción del Timer0
    TIM_ClearIntPending(LPC_TIM0, TIM_MR0_INT);
}
