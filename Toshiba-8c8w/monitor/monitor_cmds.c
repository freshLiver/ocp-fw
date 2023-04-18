#include "debug.h"

#include "monitor/monitor.h"

void monitor_handle_admin_cmds(NVME_ADMIN_COMMAND *nvmeAdminCmd)
{
    uint32_t mode = nvmeAdminCmd->dword10; // dword10 is used to specify the monitor mode

    if (nvmeAdminCmd->OPC == ADMIN_MONITOR_BUFFER)
    {
        uint32_t lba1 = nvmeAdminCmd->dword11;
        uint32_t lba2 = nvmeAdminCmd->dword12;

        switch (mode)
        {
        case 1:
            monitor_dump_data_buffer_info(MONITOR_MODE_DUMP_DIRTY, 0, 0);
            break;
        case 2:
            monitor_dump_data_buffer_info(MONITOR_MODE_DUMP_SPECIFY, lba1, 0);
            break;
        case 3:
            monitor_dump_data_buffer_info(MONITOR_MODE_DUMP_RANGE, lba1, lba2);
            break;

        default:
            monitor_dump_data_buffer_info(MONITOR_MODE_DUMP_FULL, 0, 0);
            break;
        }
    }
    else if (nvmeAdminCmd->OPC == ADMIN_MONITOR_FLASH)
    {
        uint32_t iDie      = nvmeAdminCmd->dword11;
        uint32_t iBlk      = nvmeAdminCmd->dword12;
        uint32_t iPage     = nvmeAdminCmd->dword13;
        uint32_t len       = nvmeAdminCmd->dword14;
        uint32_t hostAddrH = nvmeAdminCmd->PRP1[1], hostAddrL = nvmeAdminCmd->PRP1[0];

        uint32_t iCh = VDIE2PCH(iDie), iWay = VDIE2PWAY(iDie);

        switch (mode)
        {
        case 1:
            monitor_dump_free_blocks(iDie);
            break;
        case 2:
            monitor_dump_phy_page(iCh, iWay, iBlk, iPage);
            break;
        case 3:
            monitor_nvme_write_slice_buffer(iDie, hostAddrH, hostAddrL, len);
            monitor_write_phy_page(iCh, iWay, iBlk, iPage);
            break;

        case 4:
            monitor_erase_phy_blk(iCh, iWay, iBlk);
            break;

        default:
            for (iDie = 0; iDie < USER_DIES; ++iDie)
                monitor_dump_free_blocks(iDie);
            break;
        }
    }
    else if (nvmeAdminCmd->OPC == ADMIN_MONITOR_MAPPING)
    {
        uint32_t src = nvmeAdminCmd->dword11;
        uint32_t dst = nvmeAdminCmd->dword12;

        switch (mode)
        {
        case 1:
            monitor_dump_lsa(src);
            break;
        case 2:
            monitor_dump_vsa(src);
            break;
        case 3:
            monitor_set_l2v(src, dst);
            break;

        default:
            monitor_dump_mapping();
            break;
        }
    }
    else
        pr_error("Monitor: Unexpected monitor opcode: %u", nvmeAdminCmd->OPC);
}