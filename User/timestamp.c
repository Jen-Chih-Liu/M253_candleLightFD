#include "timestamp.h"

static volatile uint32_t s_u32TimerOverflowCount = 0;

void TMR0_IRQHandler(void)
{
    if (TIMER_GetIntFlag(TIMER0))
    {
        TIMER_ClearIntFlag(TIMER0);
        s_u32TimerOverflowCount++;
    }
}

void timestamp_init(void)
{
    /* Select TIMER0 clock source from PCLK0 (48MHz) */
    CLK_SetModuleClock(TMR0_MODULE, CLK_CLKSEL1_TMR0SEL_PCLK0, 0);
    CLK_EnableModuleClock(TMR0_MODULE);

    /* Open TIMER0 in continuous mode: prescaler 47 gives 1MHz (1us per tick) */
    /* 48MHz / (47 + 1) = 1MHz */
    TIMER0->CTL = TIMER_CONTINUOUS_MODE | 47UL;
    TIMER0->CMP = 0x00FFFFFFUL; /* Max 24-bit period (~16.77 seconds) */

    /* Enable Interrupt for 24-bit overflow */
    TIMER_EnableInt(TIMER0);
    NVIC_EnableIRQ(TMR0_IRQn);

    /* Start Timer */
    TIMER_Start(TIMER0);
}

uint32_t timer_get_us(void)
{
    uint32_t high1, high2, low;

    do {
        high1 = s_u32TimerOverflowCount;
        low = TIMER_GetCounter(TIMER0);
        high2 = s_u32TimerOverflowCount;
    } while (high1 != high2);

    return (high1 << 24) | (low & 0x00FFFFFFUL);
}
