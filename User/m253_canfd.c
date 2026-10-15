#include <string.h>
#include "m253_canfd.h"
#include "timestamp.h"
#include "board_config.h"
#include "debug_log.h"

/* Forward declaration for USB queueing */
extern bool usbd_gs_can_enqueue_in_frame(const struct gs_host_frame *frame);

/* Bit Timing Constraints */
const struct gs_device_bt_const g_can_bt_const = {
    .feature =
        GS_CAN_FEATURE_LISTEN_ONLY |
        GS_CAN_FEATURE_LOOP_BACK |
        GS_CAN_FEATURE_ONE_SHOT |
        GS_CAN_FEATURE_HW_TIMESTAMP |
        GS_CAN_FEATURE_IDENTIFY |
        GS_CAN_FEATURE_PAD_PKTS_TO_MAX_PKT_SIZE |
        GS_CAN_FEATURE_FD |
        GS_CAN_FEATURE_BT_CONST_EXT |
        GS_CAN_FEATURE_BERR_REPORTING |
        GS_CAN_FEATURE_GET_STATE,
    .fclk_can = CANFD_CORE_CLOCK,
    .tseg1_min = 1,
    .tseg1_max = 256,
    .tseg2_min = 1,
    .tseg2_max = 128,
    .sjw_max = 128,
    .brp_min = 1,
    .brp_max = 512,
    .brp_inc = 1,
};

const struct gs_device_bt_const_extended g_can_bt_const_ext = {
    .device_bt_const = g_can_bt_const,
    .dtseg1_min = 1,
    .dtseg1_max = 32,
    .dtseg2_min = 1,
    .dtseg2_max = 16,
    .dsjw_max = 16,
    .dbrp_min = 1,
    .dbrp_max = 32,
    .dbrp_inc = 1,
};

/* Module State */
static bool s_bCanEnabled = false;
static uint32_t s_u32ModeFlags = 0;
static struct gs_device_bittiming s_nominal_timing;
static struct gs_device_bittiming s_data_timing;

/* TX Echo Tracking (4 TX Buffers in M_CAN) */
#define NUM_TX_BUFFERS 4
#define CANFD_CORE_CLOCK 48000000UL

static struct {
    uint32_t echo_id;
    uint32_t can_id;
    uint8_t  can_dlc;
    uint8_t  flags;
} s_tx_echo_pool[NUM_TX_BUFFERS];

/* DLC <-> Byte Conversion */
static const uint8_t s_dlc_to_bytes_tbl[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 12, 16, 20, 24, 32, 48, 64};

uint32_t m253_canfd_dlc_to_bytes(uint8_t dlc)
{
    if (dlc > 15) dlc = 15;
    return s_dlc_to_bytes_tbl[dlc];
}

uint8_t m253_canfd_bytes_to_dlc(uint32_t bytes)
{
    if (bytes <= 8) return (uint8_t)bytes;
    if (bytes <= 12) return 9;
    if (bytes <= 16) return 10;
    if (bytes <= 20) return 11;
    if (bytes <= 24) return 12;
    if (bytes <= 32) return 13;
    if (bytes <= 48) return 14;
    return 15;
}

bool m253_canfd_is_enabled(void)
{
    return s_bCanEnabled;
}

void m253_canfd_init(void)
{
    /* Clear TX Echo Tracking */
    for (int i = 0; i < NUM_TX_BUFFERS; i++)
    {
        s_tx_echo_pool[i].echo_id = 0xFFFFFFFF;
    }

    /* Set default bit timings (500k nominal @ 81.25% SP / SJW=3, 2M data @ 75.0% SP) */
    s_nominal_timing.brp = 6;
    s_nominal_timing.prop_seg = 3;
    s_nominal_timing.phase_seg1 = 9;
    s_nominal_timing.phase_seg2 = 3;
    s_nominal_timing.sjw = 3;

    s_data_timing.brp = 2;
    s_data_timing.prop_seg = 2;
    s_data_timing.phase_seg1 = 6;
    s_data_timing.phase_seg2 = 3;
    s_data_timing.sjw = 2;

    /* Leave CANFD in reset until host sends GS_CAN_MODE_START */
    s_bCanEnabled = false;
}

void m253_canfd_set_bittiming(const struct gs_device_bittiming *timing)
{
    memcpy(&s_nominal_timing, timing, sizeof(s_nominal_timing));
    LOG_INFO("[CAN_CFG] Nominal Timing: BRP=%u, Prop=%u, Seg1=%u, Seg2=%u, SJW=%u\r\n",
             timing->brp, timing->prop_seg, timing->phase_seg1, timing->phase_seg2, timing->sjw);
}

void m253_canfd_set_data_bittiming(const struct gs_device_bittiming *timing)
{
    memcpy(&s_data_timing, timing, sizeof(s_data_timing));
    LOG_INFO("[CAN_CFG] Data Timing: BRP=%u, Prop=%u, Seg1=%u, Seg2=%u, SJW=%u\r\n",
             timing->brp, timing->prop_seg, timing->phase_seg1, timing->phase_seg2, timing->sjw);
}

static void calc_ram_address(CANFD_RAM_PART_T *psConfigAddr, const CANFD_ELEM_SIZE_T *psConfigSize)
{
    uint32_t u32RamAddrOffset = 0;
    psConfigAddr->u32SIDFC_FLSSA = 0;
    psConfigAddr->u32XIDFC_FLESA = 0;
    psConfigAddr->u32RXBC_RBSA = 0;
    psConfigAddr->u32RXF1C_F1SA = 0;

    /* Rx FIFO 0 */
    psConfigAddr->u32RXF0C_F0SA = u32RamAddrOffset;
    u32RamAddrOffset += psConfigSize->u32RxFifo0 * sizeof(CANFD_BUF_T);

    /* Tx Event FIFO */
    psConfigAddr->u32TXEFC_EFSA = u32RamAddrOffset;
    u32RamAddrOffset += psConfigSize->u32TxEventFifo * 8; /* Tx Event FIFO is 8 bytes per element */

    /* Tx Buffer */
    psConfigAddr->u32TXBC_TBSA = u32RamAddrOffset;
}

static void apply_hardware_config(void)
{
    CANFD_FD_T sCANFD_Config;
    memset(&sCANFD_Config, 0, sizeof(sCANFD_Config));

    /* Initialize Default CANFD Configuration */
    CANFD_GetDefaultConfig(&sCANFD_Config, CANFD_OP_CAN_FD_MODE);

    /* Elements configuration: strictly define element counts to fit within 1024B Message RAM */
    /* Total: 8*72 (RxFifo0) + 4*8 (TxEvent) + 4*72 (TxBuf) = 576 + 32 + 288 = 896B <= 1024B */
    sCANFD_Config.sElemSize.u32SIDFC = 0;
    sCANFD_Config.sElemSize.u32XIDFC = 0;
    sCANFD_Config.sElemSize.u32RxBuf = 0;
    sCANFD_Config.sElemSize.u32RxFifo1 = 0;
    sCANFD_Config.sElemSize.u32RxFifo0 = 8;
    sCANFD_Config.sElemSize.u32TxBuf   = NUM_TX_BUFFERS;
    sCANFD_Config.sElemSize.u32TxEventFifo = 4;

    /* Calculate non-overlapping Message RAM partition */
    calc_ram_address(&sCANFD_Config.sMRamStartAddr, &sCANFD_Config.sElemSize);

    /* Populate bitrates for CANFD_Open's internal calculation */
    sCANFD_Config.sBtConfig.sNormBitRate.u32BitRate = 500000;
    sCANFD_Config.sBtConfig.sDataBitRate.u32BitRate = 2000000;
    sCANFD_Config.sBtConfig.bFDEn = (s_u32ModeFlags & GS_CAN_MODE_FD) ? 1 : 0;
    sCANFD_Config.sBtConfig.bBitRateSwitch = (s_u32ModeFlags & GS_CAN_MODE_FD) ? 1 : 0;
    sCANFD_Config.sBtConfig.bEnableLoopBack = (s_u32ModeFlags & GS_CAN_MODE_LOOP_BACK) ? 1 : 0;

    /* Initialize M_CAN Hardware */
    CANFD_Open(CANFD0, &sCANFD_Config);

    /* Clock divider 1 for CANFD0 */
    CLK->CLKDIV4 = (CLK->CLKDIV4 & ~CLK_CLKDIV4_CANFD0DIV_Msk) | CLK_CLKDIV4_CANFD0(1);

    /* Explicitly configure Nominal and Data Bit Timing registers */
    CANFD0->CCCR |= (CANFD_CCCR_INIT_Msk | CANFD_CCCR_CCE_Msk);

    uint32_t tseg1 = s_nominal_timing.prop_seg + s_nominal_timing.phase_seg1;
    uint32_t dtseg1 = s_data_timing.prop_seg + s_data_timing.phase_seg1;
    uint32_t sjw = (s_nominal_timing.sjw > 0) ? (s_nominal_timing.sjw - 1) : 0;
    uint32_t brp = (s_nominal_timing.brp > 0) ? (s_nominal_timing.brp - 1) : 0;
    uint32_t ntseg1 = (tseg1 > 0) ? (tseg1 - 1) : 0;
    uint32_t ntseg2 = (s_nominal_timing.phase_seg2 > 0) ? (s_nominal_timing.phase_seg2 - 1) : 0;
    CANFD0->NBTP = ((sjw & 0x7F) << 25) | ((brp & 0x1FF) << 16) | ((ntseg1 & 0xFF) << 8) | (ntseg2 & 0x7F);

    if (s_u32ModeFlags & GS_CAN_MODE_FD)
    {
        uint32_t dsjw = (s_data_timing.sjw > 0) ? (s_data_timing.sjw - 1) : 0;
        uint32_t dbrp = (s_data_timing.brp > 0) ? (s_data_timing.brp - 1) : 0;
        uint32_t dntseg1 = (dtseg1 > 0) ? (dtseg1 - 1) : 0;
        uint32_t dntseg2 = (s_data_timing.phase_seg2 > 0) ? (s_data_timing.phase_seg2 - 1) : 0;
        uint32_t u32Dbtp = ((dbrp & 0x1F) << 16) | ((dntseg1 & 0x1F) << 8) | ((dntseg2 & 0xF) << 4) | (dsjw & 0xF);

        /* Transmitter Delay Compensation (TDC):
         * For CAN FD high-speed data phase (2Mbps, 4Mbps, etc., dbrp <= 1), physical transceiver loop delay
         * (120ns ~ 250ns) is significant compared to bit time (500ns for 2M, 250ns for 4M).
         * Without TDC, the transmitter samples its own TX data on the RX pin before it returns, causing
         * immediate Bit Errors (Form Error / Error Frame / Bus-off).
         * Per Bosch M_CAN spec and Nuvoton M253 TRM:
         * - When TDC is enabled, DBRP is limited to 0 or 1 (i.e. brp <= 2).
         * - TDCO defines the Secondary Sample Point (SSP) offset from the transmitted edge.
         *   SSP = measured transceiver delay + TDCO.
         *   TDCO should equal the data phase sample point in minimum time quanta (mtq).
         */
        if (dbrp <= 1)
        {
            uint32_t u32DataBrp = (s_data_timing.brp > 0) ? s_data_timing.brp : 1;
            uint32_t tdco = (dtseg1 + 1) * u32DataBrp;
            if (tdco > 127) tdco = 127;

            /* Set Secondary Sample Point offset (TDCF=0: filter window disabled, stop on RX falling edge) */
            CANFD0->TDCR = (tdco << CANFD_TDCR_TDCO_Pos);
            u32Dbtp |= CANFD_DBTP_TDC_Msk;

            LOG_INFO("[CAN_CFG] TDC Enabled: TDCO=%lu mtq, TDCR=0x%08X\r\n",
                     (unsigned long)tdco, (unsigned int)CANFD0->TDCR);
        }
        else
        {
            CANFD0->TDCR = 0;
        }

        CANFD0->DBTP = u32Dbtp;
    }

    /* Promiscuous Mode: Accept all non-matching frames to Rx FIFO 0 */
    CANFD_SetGFC(CANFD0, eCANFD_ACC_NON_MATCH_FRM_RX_FIFO0, eCANFD_ACC_NON_MATCH_FRM_RX_FIFO0, 0, 0);

    /* Listen-Only Mode */
    if (s_u32ModeFlags & GS_CAN_MODE_LISTEN_ONLY)
    {
        CANFD0->CCCR |= CANFD_CCCR_MON_Msk;
    }
    else
    {
        CANFD0->CCCR &= ~CANFD_CCCR_MON_Msk;
    }

    /* One-Shot Mode (Disable Automatic Retransmission) */
    if (s_u32ModeFlags & GS_CAN_MODE_ONE_SHOT)
    {
        CANFD0->CCCR |= CANFD_CCCR_DAR_Msk;
    }
    else
    {
        CANFD0->CCCR &= ~CANFD_CCCR_DAR_Msk;
    }

    /* External Loopback Mode: Per M253 TRM, LBCK=1 ignores ACK errors and feeds Tx back to Rx internally */
    if (s_u32ModeFlags & GS_CAN_MODE_LOOP_BACK)
    {
        CANFD0->CCCR |= CANFD_CCCR_TEST_Msk;
        CANFD0->TEST |= CANFD_TEST_LBCK_Msk;
    }

    /* Enable Interrupts: Rx FIFO 0 New Message, Transmission Complete, Bus-Off */
    uint32_t u32IntEn = CANFD_IE_RF0NE_Msk | CANFD_IE_TCE_Msk | CANFD_IE_BOE_Msk;
    if (s_u32ModeFlags & GS_CAN_MODE_BERR_REPORTING)
    {
        /* Enable Protocol Error (Data/Arbitration) & Error Warning/Passive Interrupts */
        u32IntEn |= (CANFD_IE_EPE_Msk | CANFD_IE_EWE_Msk | CANFD_IE_PEAE_Msk | CANFD_IE_PEDE_Msk);
    }

    /* Pass TXBTIE for all NUM_TX_BUFFERS so TC fires on buffer completion */
    uint32_t u32Txbtie = (1UL << NUM_TX_BUFFERS) - 1;
    CANFD_EnableInt(CANFD0, u32IntEn, 0, u32Txbtie, 0);

    NVIC_EnableIRQ(CANFD0_IRQ0_IRQn);

    /* Transition to Normal Operation */
    CANFD_RunToNormal(CANFD0, TRUE);

    LOG_INFO("[CAN_CFG] CANFD Started: NBTP=0x%08X, DBTP=0x%08X, TDCR=0x%08X, CCCR=0x%08X\r\n",
             CANFD0->NBTP, CANFD0->DBTP, CANFD0->TDCR, CANFD0->CCCR);
}

void m253_canfd_set_mode(uint32_t mode, uint32_t flags)
{
    s_u32ModeFlags = flags;

    if (mode == GS_CAN_MODE_START)
    {
        apply_hardware_config();
        s_bCanEnabled = true;
        LOG_INFO("[CAN_CFG] Mode -> START (FD: %u, ListenOnly: %u, Loopback: %u)\r\n",
                 (flags & GS_CAN_MODE_FD) != 0,
                 (flags & GS_CAN_MODE_LISTEN_ONLY) != 0,
                 (flags & GS_CAN_MODE_LOOP_BACK) != 0);
    }
    else if (mode == GS_CAN_MODE_RESET)
    {
        s_bCanEnabled = false;
        CANFD_RunToNormal(CANFD0, FALSE);
        NVIC_DisableIRQ(CANFD0_IRQ0_IRQn);
        LOG_INFO("[CAN_CFG] Mode -> RESET\r\n");
    }
}

void m253_canfd_get_state(struct gs_device_state *state)
{
    if (!state) return;
    memset(state, 0, sizeof(*state));

    if (!s_bCanEnabled)
    {
        state->state = GS_CAN_STATE_STOPPED;
        return;
    }

    uint32_t ecr = CANFD0->ECR;
    state->txerr = (ecr & CANFD_ECR_TEC_Msk) >> CANFD_ECR_TEC_Pos;
    state->rxerr = (ecr & CANFD_ECR_REC_Msk) >> CANFD_ECR_REC_Pos;

    uint32_t psr = CANFD0->PSR;
    if (psr & CANFD_PSR_BO_Msk)
    {
        state->state = GS_CAN_STATE_BUS_OFF;
    }
    else if (psr & CANFD_PSR_EP_Msk)
    {
        state->state = GS_CAN_STATE_ERROR_PASSIVE;
    }
    else if (psr & CANFD_PSR_EW_Msk)
    {
        state->state = GS_CAN_STATE_ERROR_WARNING;
    }
    else
    {
        state->state = GS_CAN_STATE_ERROR_ACTIVE;
    }
}

bool m253_canfd_transmit(const struct gs_host_frame *frame)
{
    if (!s_bCanEnabled) return false;

    /* Find a free TX buffer index (TXBRP bit == 0) */
    uint32_t u32PutIndex = 0xFFFFFFFFUL;
    for (uint32_t i = 0; i < NUM_TX_BUFFERS; i++)
    {
        if ((CANFD0->TXBRP & (1UL << i)) == 0)
        {
            u32PutIndex = i;
            break;
        }
    }
    if (u32PutIndex == 0xFFFFFFFFUL)
    {
        return false; /* All Tx buffers are busy */
    }

    CANFD_FD_MSG_T sTxMsg;
    memset(&sTxMsg, 0, sizeof(sTxMsg));

    /* Populate ID and Frame Type */
    if (frame->can_id & CAN_EFF_FLAG)
    {
        sTxMsg.eIdType = eCANFD_XID;
        sTxMsg.u32Id = frame->can_id & 0x1FFFFFFFUL;
    }
    else
    {
        sTxMsg.eIdType = eCANFD_SID;
        sTxMsg.u32Id = frame->can_id & 0x7FFUL;
    }

    sTxMsg.eFrmType = (frame->can_id & CAN_RTR_FLAG) ? eCANFD_REMOTE_FRM : eCANFD_DATA_FRM;
    sTxMsg.bFDFormat = (frame->flags & GS_CAN_FLAG_FD) ? 1 : 0;
    sTxMsg.bBitRateSwitch = (frame->flags & GS_CAN_FLAG_BRS) ? 1 : 0;
    sTxMsg.u32DLC = m253_canfd_dlc_to_bytes(frame->can_dlc);

    uint32_t payload_len = sTxMsg.u32DLC;
    if (payload_len > CANFD_MAX_MESSAGE_BYTES) payload_len = CANFD_MAX_MESSAGE_BYTES;
    memcpy(sTxMsg.au8Data, frame->canfd.data, payload_len);

    /* Store Echo Tracking Info */
    s_tx_echo_pool[u32PutIndex].echo_id = frame->echo_id;
    s_tx_echo_pool[u32PutIndex].can_id  = frame->can_id;
    s_tx_echo_pool[u32PutIndex].can_dlc = frame->can_dlc;
    s_tx_echo_pool[u32PutIndex].flags   = frame->flags;

    /* Submit to M_CAN Tx Buffer (non-blocking) */
    if (CANFD_TransmitDMsg(CANFD0, u32PutIndex, &sTxMsg) == 1)
    {
        g_stats.can_tx_count++;
        return true;
    }

    return false;
}

/* CAN FD Interrupt Handler */
void CANFD0_IRQ0_IRQHandler(void)
{
    uint32_t u32IntStatus = CANFD0->IR;

    /* 1. RX FIFO 0 New Message Interrupt */
    if (u32IntStatus & CANFD_IR_RF0N_Msk)
    {
        CANFD_ClearStatusFlag(CANFD0, CANFD_IR_RF0N_Msk);

        CANFD_FD_MSG_T sRxMsg;
        while ((CANFD0->RXF0S & CANFD_RXF0S_F0FL_Msk) != 0)
        {
            if (CANFD_ReadRxFifoMsg(CANFD0, 0, &sRxMsg) == eCANFD_RECEIVE_SUCCESS)
            {
                struct gs_host_frame rx_frame;
                memset(&rx_frame, 0, sizeof(rx_frame));

                rx_frame.echo_id = 0xFFFFFFFFUL;
                rx_frame.channel = 0;

                /* Format CAN ID */
                rx_frame.can_id = sRxMsg.u32Id;
                if (sRxMsg.eIdType == eCANFD_XID)
                {
                    rx_frame.can_id |= CAN_EFF_FLAG;
                }
                if (sRxMsg.eFrmType == eCANFD_REMOTE_FRM)
                {
                    rx_frame.can_id |= CAN_RTR_FLAG;
                }

                /* DLC and Flags */
                rx_frame.can_dlc = m253_canfd_bytes_to_dlc(sRxMsg.u32DLC);
                if (sRxMsg.bFDFormat)
                {
                    rx_frame.flags |= GS_CAN_FLAG_FD;
                }
                if (sRxMsg.bBitRateSwitch)
                {
                    rx_frame.flags |= GS_CAN_FLAG_BRS;
                }
                if (sRxMsg.bErrStaInd)
                {
                    rx_frame.flags |= GS_CAN_FLAG_ESI;
                }

                /* Copy payload */
                uint32_t rx_bytes = sRxMsg.u32DLC;
                if (rx_bytes > 64) rx_bytes = 64;
                memcpy(rx_frame.canfd_ts.data, sRxMsg.au8Data, rx_bytes);

                /* Timestamp */
                if (rx_frame.flags & GS_CAN_FLAG_FD)
                {
                    rx_frame.canfd_ts.timestamp_us = timer_get_us();
                }
                else
                {
                    rx_frame.classic_can_ts.timestamp_us = timer_get_us();
                }

                /* Enqueue to USB IN */
                usbd_gs_can_enqueue_in_frame(&rx_frame);
                g_stats.can_rx_count++;
            }
            else
            {
                break;
            }
        }
    }

    /* 2. Transmission Complete Interrupt (TX Echo) */
    if (u32IntStatus & CANFD_IR_TC_Msk)
    {
        uint32_t u32TxOccurred = CANFD0->TXBTO;
        CANFD_ClearStatusFlag(CANFD0, CANFD_IR_TC_Msk);

        for (uint32_t i = 0; i < NUM_TX_BUFFERS; i++)
        {
            if (u32TxOccurred & (1UL << i))
            {
                if (s_tx_echo_pool[i].echo_id != 0xFFFFFFFFUL)
                {
                    struct gs_host_frame echo_frame;
                    memset(&echo_frame, 0, sizeof(echo_frame));

                    echo_frame.echo_id = s_tx_echo_pool[i].echo_id;
                    echo_frame.can_id  = s_tx_echo_pool[i].can_id;
                    echo_frame.can_dlc = s_tx_echo_pool[i].can_dlc;
                    echo_frame.channel = 0;
                    echo_frame.flags   = s_tx_echo_pool[i].flags;
                    if (echo_frame.flags & GS_CAN_FLAG_FD)
                    {
                        echo_frame.canfd_ts.timestamp_us = timer_get_us();
                    }
                    else
                    {
                        echo_frame.classic_can_ts.timestamp_us = timer_get_us();
                    }

                    usbd_gs_can_enqueue_in_frame(&echo_frame);
                    g_stats.can_tx_echo_count++;

                    s_tx_echo_pool[i].echo_id = 0xFFFFFFFFUL;
                }
            }
        }
    }

    /* 3. Bus-Off Interrupt */
    if (u32IntStatus & CANFD_IR_BO_Msk)
    {
        CANFD_ClearStatusFlag(CANFD0, CANFD_IR_BO_Msk);
        g_stats.can_bus_err_count++;

        /* Construct Linux CAN Error Frame */
        struct gs_host_frame err_frame;
        memset(&err_frame, 0, sizeof(err_frame));

        err_frame.echo_id = 0xFFFFFFFFUL;
        err_frame.can_id = CAN_ERR_FLAG | CAN_ERR_BUSOFF;
        err_frame.can_dlc = CAN_ERR_DLC;
        err_frame.channel = 0;
        err_frame.classic_can_ts.timestamp_us = timer_get_us();

        uint32_t ecr = CANFD0->ECR;
        err_frame.classic_can.data[6] = (uint8_t)((ecr & CANFD_ECR_TEC_Msk) >> CANFD_ECR_TEC_Pos);
        err_frame.classic_can.data[7] = (uint8_t)((ecr & CANFD_ECR_REC_Msk) >> CANFD_ECR_REC_Pos);

        usbd_gs_can_enqueue_in_frame(&err_frame);
    }

    /* 4. Error Passive / Warning */
    if (u32IntStatus & (CANFD_IR_EP_Msk | CANFD_IR_EW_Msk))
    {
        CANFD_ClearStatusFlag(CANFD0, (CANFD_IR_EP_Msk | CANFD_IR_EW_Msk));
        g_stats.can_bus_err_count++;

        if (s_u32ModeFlags & GS_CAN_MODE_BERR_REPORTING)
        {
            struct gs_host_frame err_frame;
            memset(&err_frame, 0, sizeof(err_frame));

            err_frame.echo_id = 0xFFFFFFFFUL;
            err_frame.can_id = CAN_ERR_FLAG | CAN_ERR_CRTL;
            err_frame.can_dlc = CAN_ERR_DLC;
            err_frame.channel = 0;
            err_frame.classic_can_ts.timestamp_us = timer_get_us();

            uint32_t psr = CANFD0->PSR;
            if (psr & CANFD_PSR_EW_Msk)
            {
                err_frame.classic_can.data[1] |= (CAN_ERR_CRTL_RX_WARNING | CAN_ERR_CRTL_TX_WARNING);
            }
            if (psr & CANFD_PSR_EP_Msk)
            {
                err_frame.classic_can.data[1] |= (CAN_ERR_CRTL_RX_PASSIVE | CAN_ERR_CRTL_TX_PASSIVE);
            }

            uint32_t ecr = CANFD0->ECR;
            err_frame.classic_can.data[6] = (uint8_t)((ecr & CANFD_ECR_TEC_Msk) >> CANFD_ECR_TEC_Pos);
            err_frame.classic_can.data[7] = (uint8_t)((ecr & CANFD_ECR_REC_Msk) >> CANFD_ECR_REC_Pos);

            usbd_gs_can_enqueue_in_frame(&err_frame);
        }
    }

    /* 5. Protocol Error in Data / Arbitration Phase (BERR Reporting) */
    if (u32IntStatus & (CANFD_IR_PED_Msk | CANFD_IR_PEA_Msk))
    {
        CANFD_ClearStatusFlag(CANFD0, (CANFD_IR_PED_Msk | CANFD_IR_PEA_Msk));
        g_stats.can_bus_err_count++;

        if (s_u32ModeFlags & GS_CAN_MODE_BERR_REPORTING)
        {
            struct gs_host_frame err_frame;
            memset(&err_frame, 0, sizeof(err_frame));

            err_frame.echo_id = 0xFFFFFFFFUL;
            err_frame.can_id = CAN_ERR_FLAG | CAN_ERR_PROT;
            err_frame.can_dlc = CAN_ERR_DLC;
            err_frame.channel = 0;
            err_frame.classic_can_ts.timestamp_us = timer_get_us();

            uint32_t psr = CANFD0->PSR;
            uint32_t lec = (psr & CANFD_PSR_LEC_Msk) >> CANFD_PSR_LEC_Pos;
            if (lec == 7)
            {
                lec = (psr & CANFD_PSR_DLEC_Msk) >> CANFD_PSR_DLEC_Pos;
            }

            switch (lec)
            {
                case 1: /* Stuff Error */
                    err_frame.classic_can.data[2] = CAN_ERR_PROT_STUFF;
                    break;
                case 2: /* Form Error */
                    err_frame.classic_can.data[2] = CAN_ERR_PROT_FORM;
                    break;
                case 3: /* Ack Error */
                    err_frame.can_id |= CAN_ERR_ACK;
                    break;
                case 4: /* Bit1 Error */
                    err_frame.classic_can.data[2] = CAN_ERR_PROT_BIT1;
                    break;
                case 5: /* Bit0 Error */
                    err_frame.classic_can.data[2] = CAN_ERR_PROT_BIT0;
                    break;
                case 6: /* CRC Error */
                    err_frame.classic_can.data[3] = CAN_ERR_PROT_LOC_CRC_SEQ;
                    break;
                default:
                    err_frame.classic_can.data[2] = CAN_ERR_PROT_UNSPEC;
                    break;
            }

            uint32_t ecr = CANFD0->ECR;
            err_frame.classic_can.data[6] = (uint8_t)((ecr & CANFD_ECR_TEC_Msk) >> CANFD_ECR_TEC_Pos);
            err_frame.classic_can.data[7] = (uint8_t)((ecr & CANFD_ECR_REC_Msk) >> CANFD_ECR_REC_Pos);

            usbd_gs_can_enqueue_in_frame(&err_frame);
        }
    }
}
