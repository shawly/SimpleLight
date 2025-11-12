// Fake SD driver for EMU build: serves sectors from an embedded FAT16/32 image.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "driver/sd_card.h"
#include "ff.h" // for DRESULT codes
#include "diskio.h" // for RES_OK/RES_PARERR/RES_ERROR

// The Makefile embeds disk images in diskimg/ as binary via bin2o.
#include "disk_bin.h"

#ifndef EWRAM_BSS
#define EWRAM_BSS __attribute__((section(".ewram")))
#endif

#define SECTOR_SIZE 512u

static u8 *g_img_ram EWRAM_BSS = NULL;    // mutable copy for write support during session
static u32 g_img_size EWRAM_BSS = 0;      // in bytes
static u32 g_sector_count EWRAM_BSS = 0;  // number of 512-byte sectors
static int g_sd_ready EWRAM_BSS = 0;

static int ensure_sd_open(void)
{
    if (g_sd_ready) return 1;
    // Validate embedded image symbols
    if (disk_bin == NULL || disk_bin_size <= 0) {
        return 0;
    }
    g_img_size = (u32)disk_bin_size;
    // Round down to whole sectors to be safe
    u32 usable = g_img_size - (g_img_size % SECTOR_SIZE);
    g_sector_count = usable / SECTOR_SIZE;
    // Allocate RAM buffer and copy so disk writes affect the runtime image
    g_img_ram = (u8*)malloc(usable);
    if (!g_img_ram) {
        // As a fallback, operate read-only on the const ROM buffer
        g_img_ram = NULL;
    } else {
        memcpy(g_img_ram, disk_bin, usable);
    }
    g_sd_ready = 1;
    return 1;
}

void SetSDControl(u16 control)
{
    (void)control;
}

u16 IWRAM_CODE SD_Response(void)
{
    return 0x0000;
}

// Read sectors from the embedded image into SDbuffer.
u32 Read_SD_sectors(u32 address, u16 count, u8 *SDbuffer)
{
    if (!ensure_sd_open() || !SDbuffer) return RES_ERROR;
    if (count == 0) return RES_PARERR;
    if (address >= g_sector_count) return RES_PARERR;
    if ((u32)count > g_sector_count - address) return RES_PARERR;

    const u8 *base = g_img_ram ? g_img_ram : disk_bin;
    memcpy(SDbuffer, base + (address * SECTOR_SIZE), (size_t)count * SECTOR_SIZE);
    return RES_OK;
}

// Write sectors into RAM buffer if available; otherwise pretend success but drop data.
u32 Write_SD_sectors(u32 address, u16 count, const u8 *SDbuffer)
{
    if (!ensure_sd_open() || !SDbuffer) return RES_ERROR;
    if (count == 0) return RES_PARERR;
    if (address >= g_sector_count) return RES_PARERR;
    if ((u32)count > g_sector_count - address) return RES_PARERR;

    if (g_img_ram) {
        memcpy(g_img_ram + (address * SECTOR_SIZE), SDbuffer, (size_t)count * SECTOR_SIZE);
    }
    // If no RAM buffer, we cannot persist writes, but allow the flow to continue.
    return RES_OK;
}

u16 IWRAM_CODE Read_S71NOR_ID() { return 0x2202; }

u16 Read_S98NOR_ID() { return 0x223D; }

void IWRAM_CODE SetRompage(u16 page) { (void)page; }

void IWRAM_CODE SetbufferControl(u16 control) { (void)control; }

void SetPSRampage(u16 page) { (void)page; }

void SetRampage(u16 page) { (void)page; }

void IWRAM_CODE Progress(u16 x, u16 y, u16 w, u16 h, u16 c, u8 isDrawDirect)
{
    (void)x; (void)y; (void)w; (void)h; (void)c; (void)isDrawDirect;
}

void IWRAM_CODE Send_FATbuffer(u32 *buffer, u32 mode)
{
    (void)buffer; (void)mode;
}

void IWRAM_CODE SetRompageWithHardReset(u16 page, u32 bootmode)
{
    (void)page; (void)bootmode;
}

void ReadSram(u32 address, u8 *data, u32 size) { (void)address; (void)data; (void)size; }

void WriteSram(u32 address, u8 *data, u32 size) { (void)address; (void)data; (void)size; }

void IWRAM_CODE Save_NOR_info(u16 *NOR_info_buffer, u32 buffersize)
{
    (void)NOR_info_buffer; (void)buffersize;
}

void IWRAM_CODE Save_SET_info(u16 *SET_info_buffer, u32 buffersize)
{
    (void)SET_info_buffer; (void)buffersize;
}

void IWRAM_CODE Read_NOR_info() {}

u16 IWRAM_CODE Read_SET_info(u32 offset)
{
    (void)offset; return 0;
}

void SD_Disable(void) {}

void IWRAM_CODE Set_RTC_status(u16 status) { (void)status; }

void IWRAM_CODE Check_FW_update(u16 Current_FW_ver, u16 Built_in_ver)
{
    (void)Current_FW_ver; (void)Built_in_ver;
}

void IWRAM_CODE Bank_Switching(u8 bank) { (void)bank; }

void IWRAM_CODE Set_AUTO_save(u16 mode) { (void)mode; }

u16 IWRAM_CODE Read_FPGA_ver(void) { return 0; }
