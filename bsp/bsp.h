#ifndef __OPENSSD_SIM_BSP_H__
#define __OPENSSD_SIM_BSP_H__

#include "memory.h"

typedef struct
{
    void *CpuBaseAddress;
} XScuGic, *XScuGic_Config, Xil_ExceptionHandler;

#define XPAR_NVME_CTRL_0_BASEADDR      0x83C00000
#define XPAR_IODELAY_IF_0_BASEADDR     0x83C50000
#define XPAR_IODELAY_IF_1_BASEADDR     0x83C30000
#define XPAR_IODELAY_IF_0_DQS_BASEADDR 0x83C20000
#define XPAR_IODELAY_IF_1_DQS_BASEADDR 0x83C40000

#define XPAR_T4NFC_HLPER_0_BASEADDR 0x43C00000 // NSC_0_BASEADDR
#define XPAR_T4NFC_HLPER_1_BASEADDR 0x43C10000 // NSC_1_BASEADDR
#define XPAR_T4NFC_HLPER_2_BASEADDR 0x43C20000 // NSC_2_BASEADDR
#define XPAR_T4NFC_HLPER_3_BASEADDR 0x43C30000 // NSC_3_BASEADDR
#define XPAR_T4NFC_HLPER_4_BASEADDR 0x43C40000 // NSC_4_BASEADDR
#define XPAR_T4NFC_HLPER_5_BASEADDR 0x43C50000 // NSC_5_BASEADDR
#define XPAR_T4NFC_HLPER_6_BASEADDR 0x43C60000 // NSC_6_BASEADDR
#define XPAR_T4NFC_HLPER_7_BASEADDR 0x43C70000 // NSC_7_BASEADDR

#define XPAR_AXI_BRAM_CTRL_0_S_AXI_BASEADDR 0x45000000U // NSC_0_UCODEADDR
#define XPAR_AXI_BRAM_CTRL_1_S_AXI_BASEADDR 0x45100000U // NSC_1_UCODEADDR
#define XPAR_AXI_BRAM_CTRL_2_S_AXI_BASEADDR 0x45200000U // NSC_2_UCODEADDR
#define XPAR_AXI_BRAM_CTRL_3_S_AXI_BASEADDR 0x45300000U // NSC_3_UCODEADDR
#define XPAR_AXI_BRAM_CTRL_4_S_AXI_BASEADDR 0x45400000U // NSC_4_UCODEADDR
#define XPAR_AXI_BRAM_CTRL_5_S_AXI_BASEADDR 0x45500000U // NSC_5_UCODEADDR
#define XPAR_AXI_BRAM_CTRL_6_S_AXI_BASEADDR 0x45600000U // NSC_6_UCODEADDR
#define XPAR_AXI_BRAM_CTRL_7_S_AXI_BASEADDR 0x45700000U // NSC_7_UCODEADDR

#define XIL_EXCEPTION_IRQ    NULL
#define XIL_EXCEPTION_ID_INT NULL

#define Xil_ICacheEnable(...)             void_func()
#define Xil_ICacheDisable(...)            void_func()
#define Xil_DCacheEnable(...)             void_func()
#define Xil_DCacheDisable(...)            void_func()
#define Xil_EnableMMU(...)                void_func()
#define Xil_DisableMMU(...)               void_func()
#define Xil_ExceptionInit(...)            void_func()
#define Xil_ExceptionEnable(...)          void_func()
#define Xil_ExceptionEnableMask(...)      void_func()
#define Xil_ExceptionRegisterHandler(...) void_func()
#define Xil_SetTlbAttributes(...)         void_func()
#define XScuGic_Enable(...)               void_func()
#define XScuGic_Connect(...)              void_func()
#define XScuGic_LookupConfig(...)         void_func()
#define XScuGic_CfgInitialize(...)        void_func()
#define XPAR_SCUGIC_SINGLE_DEVICE_ID

extern char inbyte() __attribute__((unused));
extern void *void_func() __attribute__((unused));

#endif /*  __OPENSSD_SIM_BSP_H__ */