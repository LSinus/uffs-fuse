/*
  This file is part of UFFS, the Ultra-low-cost Flash File System.
  
  Copyright (C) 2005-2009 Ricky Zheng <ricky_gz_zheng@yahoo.co.nz>

  UFFS is free software; you can redistribute it and/or modify it under
  the GNU Library General Public License as published by the Free Software 
  Foundation; either version 2 of the License, or (at your option) any
  later version.

  UFFS is distributed in the hope that it will be useful, but WITHOUT
  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
  or GNU Library General Public License, as applicable, for more details.
 
  You should have received a copy of the GNU General Public License
  and GNU Library General Public License along with UFFS; if not, write
  to the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
  Boston, MA  02110-1301, USA.

  As a special exception, if other files instantiate templates or use
  macros or inline functions from this file, or you compile this file
  and link it with other works to produce a work based on this file,
  this file does not by itself cause the resulting work to be covered
  by the GNU General Public License. However the source code for this
  file must still be made available in accordance with section (3) of
  the GNU General Public License v2.
 
  This exception does not invalidate any other reasons why a work based
  on this file might be covered by the GNU General Public License.
*/

/**
 * \file uffs_fileem.c
 * \brief emulate uffs file system
 * \author Ricky Zheng, created 9th May, 2005
 */
  

#include <sys/types.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "uffs_config.h"
#include "uffs/uffs_device.h"
#include "uffs_fileem.h"

#define PFX "femu: "
#define MSG(msg,...) uffs_PerrorRaw(UFFS_MSG_NORMAL, msg, ## __VA_ARGS__)
#define MSGLN(msg,...) uffs_Perror(UFFS_MSG_NORMAL, msg, ## __VA_ARGS__)

struct uffs_StorageAttrSt * femu_GetStorage()
{
    struct uffs_StorageAttrSt *femu_storage = malloc(sizeof(*femu_storage));

    if (!femu_storage) {
        fprintf(stderr, "Error allocating uffs emulator storage\n");
        exit(-1);
    }
    return femu_storage;
}

struct uffs_FileEmuSt *femu_GetPrivate()
{
    struct uffs_FileEmuSt *femu_private = malloc(sizeof(*femu_private));

    if (!femu_private) {
        fprintf(stderr, "Error allocating uffs emulator private data\n");
        exit(-1);
    }

	return femu_private;
}

URET femu_InitDevice(uffs_Device *dev)
{
	uffs_FileEmu *emu = femu_GetPrivate();
    emu->emu_filename = dev->_private;

	dev->attr->_private = (void *)emu;

	// setup flash driver operations, according to the ecc option.
    dev->ops = malloc(sizeof(uffs_FlashOps));

    if (!dev->ops) {
        fprintf(stderr, "Error allocating ops");
        exit(1);
    }

	switch(dev->attr->ecc_opt) {
		case UFFS_ECC_NONE:
		case UFFS_ECC_SOFT:
			memcpy(dev->ops, &g_femu_ops_ecc_soft, sizeof(uffs_FlashOps));
			break;
		case UFFS_ECC_HW:
			memcpy(dev->ops, &g_femu_ops_ecc_hw, sizeof(uffs_FlashOps));
			break;
		case UFFS_ECC_HW_AUTO:
			memcpy(dev->ops, &g_femu_ops_ecc_hw_auto, sizeof(uffs_FlashOps));
			break;
		default:
			break;
	}

#ifdef UFFS_FEMU_ENABLE_INJECTION
	// setup wrap functions, for inject ECC errors, etc.
	// check wrap_inited so that multiple devices can share the same driver
	if (!emu->wrap_inited) {
		femu_setup_wrapper_functions(dev);
		emu->wrap_inited = U_TRUE;
	}
#endif

	return U_SUCC;
}

/* Nothing to do here */
URET femu_ReleaseDevice(uffs_Device *dev)
{
    free(dev->attr->_private);
    free(dev->attr);
	return U_SUCC;
}


/////////////////////////////////////////////////////////////////////////////////
