#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "driver/nor_flash.h"

static int ensure_nor_open(void)
{
    return 0;
}

void Chip_Reset() {}

void Block_Erase(u32 blockAdd)
{
}

void Chip_Erase()
{
}

void FormatNor() { Chip_Erase(); }

void WriteFlash(u32 address, u8 *buffer, u32 size)
{
}

void IWRAM_CODE WriteFlash_with32word(u32 address, u8 *buffer, u32 size)
{
}

u32 Loadfile2NOR(TCHAR *filename, u32 NORaddress, u32 have_patch)
{
    return 0;
}

u32 GetFileListFromNor(void) { return 0; }
