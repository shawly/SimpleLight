/*-----------------------------------------------------------------------*/
/* Low level disk I/O module SKELETON for FatFs     (C)ChaN, 2025        */
/*-----------------------------------------------------------------------*/
/* If a working storage control module is available, it should be        */
/* attached to the FatFs via a glue function rather than modifying it.   */
/* This is an example of glue functions to attach various exsisting      */
/* storage control modules to the FatFs module with a defined API.       */
/*-----------------------------------------------------------------------*/

#include "ff.h"			/* Basic definitions of FatFs */
#include "diskio.h"		/* Declarations FatFs MAI */
#include "driver/sd_card.h"  /* EZFLASH: include sd_card API */
#include "driver/rtc.h"    /* EZFLASH: include rtc API for get_fattime */

/*-----------------------------------------------------------------------*/
/* Get Drive Status                                                      */
/*-----------------------------------------------------------------------*/

DSTATUS disk_status (
	BYTE pdrv		/* Physical drive nmuber to identify the drive */
)
{
	/* EZFLASH: Removed original code */
	return RES_OK;
}



/*-----------------------------------------------------------------------*/
/* Inidialize a Drive                                                    */
/*-----------------------------------------------------------------------*/

DSTATUS disk_initialize (
	BYTE pdrv				/* Physical drive nmuber to identify the drive */
)
{
	/* EZFLASH: Removed original code */
	return RES_OK;
}



/*-----------------------------------------------------------------------*/
/* Read Sector(s)                                                        */
/*-----------------------------------------------------------------------*/

DRESULT disk_read (
	BYTE pdrv,		/* Physical drive nmuber to identify the drive */
	BYTE *buff,		/* Data buffer to store read data */
	LBA_t sector,	/* Start sector in LBA */
	UINT count		/* Number of sectors to read */
)
{
	/* EZFLASH: Directly call Read_SD_sectors */
	DRESULT res;
	res = Read_SD_sectors(sector, count, buff);
	return res;
}



/*-----------------------------------------------------------------------*/
/* Write Sector(s)                                                       */
/*-----------------------------------------------------------------------*/

#if FF_FS_READONLY == 0

DRESULT disk_write (
	BYTE pdrv,			/* Physical drive nmuber to identify the drive */
	const BYTE *buff,	/* Data to be written */
	LBA_t sector,		/* Start sector in LBA */
	UINT count			/* Number of sectors to write */
)
{
	/* EZFLASH: Directly call Write_SD_sectors */
	DRESULT res;
	res = Write_SD_sectors(sector, count, buff);
	return res;
}

#endif


/*-----------------------------------------------------------------------*/
/* Miscellaneous Functions                                               */
/*-----------------------------------------------------------------------*/

DRESULT disk_ioctl (
	BYTE pdrv,		/* Physical drive nmuber (0..) */
	BYTE cmd,		/* Control code */
	void *buff		/* Buffer to send/receive control data */
)
{
	/* EZFLASH: Removed original code */
	return RES_OK;
}

/*-----------------------------------------------------------------------*/
/* Get omega card time Functions                                               */
/*-----------------------------------------------------------------------*/
DWORD get_fattime (void)
{
		u8 datetime[7];
		rtc_enable();
		rtc_get(datetime);
		rtc_disenable();	
		return ((DWORD)(UNBCD(datetime[0])+20) << 25 | (DWORD)UNBCD(datetime[1]) << 21 | (DWORD)UNBCD(datetime[2]&0x3F) << 16 | (DWORD)UNBCD(datetime[4]&0x3F) << 11 | (DWORD)UNBCD(datetime[5]) << 5  | (DWORD)UNBCD(datetime[6]) >> 1   );
}
