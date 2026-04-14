/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Configuration for MT8196 based boards
 *
 * Copyright (C) 2026 Tobias Heider <me@tobhe.de>
 */

#ifndef __MT8196_H
#define __MT8196_H

#include <config_distro_bootcmd.h>

#define BOOT_TARGET_DEVICES(func)

#define CFG_EXTRA_ENV_SETTINGS \
	"stdout=vidconsole\0" \
	"stderr=vidconsole\0" \
	"stdin=usbkbd\0" \
	BOOTENV

#endif
