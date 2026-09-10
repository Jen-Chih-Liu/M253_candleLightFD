#include "system_init.h"
#include "timestamp.h"

#define TRIM_INIT_ADDR      (SYS_BASE + 0x118)
static uint32_t s_u32TrimDefault = 0;

void system_init(void)
{
    /* Unlock protected registers */
    SYS_UnlockReg();

    /* Enable Internal HIRC 48 MHz clock */
    CLK_EnableXtalRC(CLK_PWRCTL_HIRCEN_Msk);

    /* Waiting for Internal RC clock ready */
    CLK_WaitClockReady(CLK_STATUS_HIRCSTB_Msk);

    /* Switch HCLK clock source to Internal HIRC and HCLK source divide 1 */
    CLK_SetHCLK(CLK_CLKSEL0_HCLKSEL_HIRC, CLK_CLKDIV0_HCLK(1));

    /* Select CAN FD0 clock source as HCLK (48MHz), divider 1 */
    CLK_SetModuleClock(CANFD0_MODULE, CLK_CLKSEL0_CANFD0SEL_HCLK, CLK_CLKDIV4_CANFD0(1));
    CLK_EnableModuleClock(CANFD0_MODULE);

    /* Enable USBD peripheral clock */
    CLK_EnableModuleClock(USBD_MODULE);

    /* Update System Core Clock */
    SystemCoreClockUpdate();

    /* Debug UART clock setting (UART4 @ HIRC) */
    UartDebugCLK();

    /*---------------------------------------------------------------------------------------------------------*/
    /* Init I/O Multi-function                                                                                 */
    /*---------------------------------------------------------------------------------------------------------*/
    /* Debug UART MFP (PA.2 RXD, PA.3 TXD) */
    UartDebugMFP();

    /* Set PC multi-function pins for CAN FD0 RXD (PC.4) and TXD (PC.5) */
    SYS->GPC_MFPL = (SYS->GPC_MFPL & (~SYS_GPC_MFPL_PC4MFP_Msk)) | SYS_GPC_MFPL_PC4MFP_CAN0_RXD;
    SYS->GPC_MFPL = (SYS->GPC_MFPL & (~SYS_GPC_MFPL_PC5MFP_Msk)) | SYS_GPC_MFPL_PC5MFP_CAN0_TXD;

    /* Enable pull-up on CAN0 RXD (PC.4) to keep bus recessive when idle or in loopback */
    GPIO_SetPullCtl(PC, BIT4, GPIO_PUSEL_PULL_UP);

    /* Lock protected registers */
    SYS_LockReg();

    /* Initialize Debug UART (115200 bps) */
    UART_Open(DEBUG_PORT, DEBUG_UART_BAUDRATE);

    /* Backup default trim value for crystal-less USB */
    s_u32TrimDefault = M32(TRIM_INIT_ADDR);
    USBD_CLR_INT_FLAG(USBD_INTSTS_SOFIF_Msk);

    /* Initialize 1us hardware timestamp timer */
    timestamp_init();

    LOG_INFO("\r\n========================================\r\n");
    LOG_INFO("  Nuvoton M253 candleLight FD (gs_usb)  \r\n");
    LOG_INFO("  HCLK: %lu Hz, CAN Core: %lu Hz\r\n", (unsigned long)SystemCoreClock, (unsigned long)CANFD_CORE_CLOCK);
    LOG_INFO("  CAN Pins: PC.4 (RXD), PC.5 (TXD)\r\n");
    LOG_INFO("  Debug Port: UART4 (PA.2/PA.3 @ %u bps)\r\n", DEBUG_UART_BAUDRATE);
    LOG_INFO("========================================\r\n");
}

void hirc_trim_task(void)
{
    /* Start USB trim function if not yet enabled */
    if ((SYS->HIRCTRIMCTL & SYS_HIRCTRIMCTL_FREQSEL_Msk) != 0x1)
    {
        /* Start USB trim only when USB SOF arrives */
        if (USBD->INTSTS & USBD_INTSTS_SOFIF_Msk)
        {
            USBD_CLR_INT_FLAG(USBD_INTSTS_SOFIF_Msk);

            /* Enable HIRC Trimming with boundary function */
            SYS->HIRCTRIMCTL = (0x1 << SYS_HIRCTRIMCTL_REFCKSEL_Pos)
                             | (0x1 << SYS_HIRCTRIMCTL_FREQSEL_Pos)
                             | (0x0 << SYS_HIRCTRIMCTL_LOOPSEL_Pos)
                             | (0x1 << SYS_HIRCTRIMCTL_BOUNDEN_Pos)
                             | (10  << SYS_HIRCTRIMCTL_BOUNDARY_Pos);
        }
    }

    /* Disable USB Trim if any error is detected and restore default */
    if (SYS->HIRCTRIMSTS & (SYS_HIRCTRIMSTS_CLKERIF_Msk | SYS_HIRCTRIMSTS_TFAILIF_Msk))
    {
        M32(TRIM_INIT_ADDR) = s_u32TrimDefault;
        SYS->HIRCTRIMCTL = 0;
        SYS->HIRCTRIMSTS = SYS_HIRCTRIMSTS_CLKERIF_Msk | SYS_HIRCTRIMSTS_TFAILIF_Msk;
        USBD_CLR_INT_FLAG(USBD_INTSTS_SOFIF_Msk);
    }
}
