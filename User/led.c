#include "led.h"
#include "timestamp.h"

static bool s_bChannelActive = false;
static bool s_bIdentifyMode = false;
static uint32_t s_u32ActivityEndTime = 0;
static uint32_t s_u32IdentifyToggleTime = 0;
static bool s_bIdentifyState = false;

static void set_led_hardware(bool on)
{
#if BOARD_HAS_LED
    bool pin_level = (on) ? (LED_ACTIVE_LEVEL == 1) : (LED_ACTIVE_LEVEL == 0);
    if (pin_level)
    {
        LED_PORT->DOUT |= LED_PIN;
    }
    else
    {
        LED_PORT->DOUT &= ~LED_PIN;
    }
#else
    (void)on;
#endif
}

void led_init(void)
{
#if BOARD_HAS_LED
    GPIO_SetMode(LED_PORT, LED_PIN, GPIO_MODE_OUTPUT);
    set_led_hardware(false);
#endif
    s_bChannelActive = false;
    s_bIdentifyMode = false;
    s_u32ActivityEndTime = 0;
}

void led_set_channel_active(bool active)
{
    s_bChannelActive = active;
}

void led_activity_trigger(void)
{
    /* Keep inverted for 30ms on traffic */
    s_u32ActivityEndTime = timer_get_us() + 30000UL;
}

void led_set_identify(bool enable)
{
    s_bIdentifyMode = enable;
    if (enable)
    {
        s_u32IdentifyToggleTime = timer_get_us() + 50000UL; /* 10Hz (50ms on / 50ms off) */
        s_bIdentifyState = true;
    }
}

void led_update(void)
{
    uint32_t now = timer_get_us();

    /* 1. Identify Mode (Highest Priority) */
    if (s_bIdentifyMode)
    {
        if (now >= s_u32IdentifyToggleTime)
        {
            s_u32IdentifyToggleTime = now + 50000UL;
            s_bIdentifyState = !s_bIdentifyState;
            set_led_hardware(s_bIdentifyState);
        }
        return;
    }

    /* 2. Channel Active State with Activity Blinking */
    if (s_bChannelActive)
    {
        if (now < s_u32ActivityEndTime)
        {
            /* Invert LED temporarily on packet activity */
            set_led_hardware(false);
        }
        else
        {
            set_led_hardware(true);
        }
    }
    else
    {
        set_led_hardware(false);
    }
}
