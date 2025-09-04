/*
 * Copyright (C) 2017 Chino-e Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See http://www.gnu.org/licenses/gpl-2.0.html for more details.
 */

#ifndef __CNE_HVDCP_H
#define __CNE_HVDCP_H

#include "cne_charger_intf.h"

#define HVDCP_CHARGING_CURRENT_MAX_UA      6000000  // 6000mA
#define HVDCP_INPUT_CURRENT_MAX_UA         3000000  // 3000mA
#define HVDCP_CHARGING_CURRENT_DEFAULT_UA  500000   // 500mA
#define HVDCP_SINGLE_IC_CURRENT_MAX_UA     2000000  // 2000mA
#define HVDCP_VBUS_IR_DROP_THRESHOLD       600000   // 600mV
#define HVDCP_SLAVE_CHARGER_MIVR_DELTA_UV  100000   // 100mV

#define HVDCP_ERR_MAX_COUNT        5

bool hvdcp_is_check(struct charger_manager *info);
bool hvdcp_is_ready(struct charger_manager *info);
int hvdcp_device_init(struct charger_manager *info);
int cne_hvdcp_detect_setting(struct charger_manager *info);
int cne_switch_hvdcp_run(struct charger_manager *info);
int cne_switch_hvdcp_init(struct charger_manager *info);
bool cne_switch_to_hvdcp_check(struct charger_manager *info);

#endif