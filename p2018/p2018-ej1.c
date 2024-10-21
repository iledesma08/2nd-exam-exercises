#include "LPC17xx.h"
#include "lpc17xx_pinsel.h"
#include "lpc17xx_dac.h"
#include "lpc17xx_timer.h"

// Memory adresses
#define CONTROL_ADDRESS 0x10004000
#define MAX_VALUE_BITS 0x000000FF
#define HOLD_TIME_BITS 0x0000FF00
#define RISE_TIME_BITS 0x00FF0000
#define FALL_TIME_BITS 0xFF000000

// Useful macros
#define MATCH_CHANNEL_0 0

// Global variables
volatile uint8_t match_flag = 0;

// Function prototypes
void config_pins(void);
void config_dac(void);
void config_timer(void);
void delay_with_timer(uint8_t time);

int main(void) {
    SystemInit();   // Initialize system clk (default: 100MHz)

    // Required configuration
    config_pins();
    config_dac();
    config_timer();

    // I suppose the times are in [ms]
    uint32_t control_values = *((uint32_t*)CONTROL_ADDRESS);
    uint8_t max_value = (control_values & MAX_VALUE_BITS);
    uint8_t min_value = 0;
    uint8_t hold_time = (control_values & HOLD_TIME_BITS) >> 8;
    uint8_t rise_time = (control_values & RISE_TIME_BITS) >> 16;
    uint8_t fall_time = (control_values & FALL_TIME_BITS) >> 24;

    uint16_t dac_value = 0; // DAC shouldnt go past 8 bits (max value)

    while (1) {
        // Rising phase
        for (dac_value = 0; dac_value < max_value; dac_value++) {
            DAC_UpdateValue(LPC_DAC, dac_value);
            delay_with_timer(rise_time);
        }

        // Hold phase (at max)
        DAC_UpdateValue(LPC_DAC, max_value);
        delay_with_timer(hold_time);

        // Falling phase
        for (dac_value = 0; dac_value > min_value; dac_value--) {
            DAC_UpdateValue(LPC_DAC, dac_value);
            delay_with_timer(fall_time);
        }

        // Settling phase (at min)
        DAC_UpdateValue(LPC_DAC, min_value);
        delay_with_timer(hold_time);
    }

    return 0;
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

void config_dac(void) {
    DAC_CONVERTER_CFG_Type dac;
    // Configure DAC settings
    dac.CNT_ENA = RESET; // Disable DAC timeout (DAC will convert each time it receives a new value)
    dac.DMA_ENA = RESET; // We dont use DMA
    dac.DBLBUF_ENA = RESET; // We dont need double buffering
    DAC_ConfigDAConverterControl(LPC_DAC, &dac); // Set config

    DAC_Init(LPC_DAC);      // Initialize the DAC with desired config
}

void config_timer(void) {
    TIM_TIMERCFG_Type timer;
    TIM_MATCHCFG_Type match;
    
    // Timer Register Config
    timer.PrescaleOption = TIM_PRESCALE_USVAL; // Prescaler in [us]
    timer.PrescaleValue = 1000; // Every 1 [ms], Timer will rise by 1
    TIM_Init(LPC_TIM0, TIM_TIMER_MODE, &timer);

    // Match interruption Config
    match.MatchChannel = 0;
    match.IntOnMatch = ENABLE;
    // Timer is automatically stopped and reset every time it reaches match
    match.StopOnMatch = ENABLE; 
    match.ResetOnMatch = ENABLE;
    TIM_ConfigMatch(LPC_TIM0, &timer);

    // Enable interruption of Timer0
    NVIC_EnableIRQ(TIMER0_IRQn);
}
/**
* @brief uses timer to delay a signal
* @param time should be in [ms]
*/
void delay_with_timer(uint8_t time) {
    // Restart match flag
    match_flag = 0;

    // Set match value
    TIM_UpdateMatchValue(LPC_TIM0, MATCH_CHANNEL_0, time);

    // Start Timer0
    TIM_Cmd(LPC_TIM0, ENABLE);

    // Wait until Timer0 reaches match (wait for delay time)
    while (!match_flag);
}

// Verify if an interruption on MR0 of Timer0 already happened
void Timer0_IRQHandler(void) {
    if (TIM_GetIntStatus(LPC_TIM0, TIM_MR0_INT)) {
        TIM_ClearIntPending(LPC_TIM0, TIM_MR0_INT);
        match_flag = 1;
    }
}