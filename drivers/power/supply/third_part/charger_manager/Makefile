#
# Copyright (C) 2015 Chino-e Inc.
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License version 2 as
# published by the Free Software Foundation.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
# See http://www.gnu.org/licenses/gpl-2.0.html for more details.
#

#subdir-ccflags-y += \
#			-Werror -I$(srctree)/drivers/usb/typec/tcpc/inc/

ifeq ($(CONFIG_TCPC_CLASS),m)
subdir-ccflags-y += -DCONFIG_TCPC_CLASS
endif

obj-$(CONFIG_CHINOE_CHARGER) += charger_class.o
obj-$(CONFIG_CHINOE_CHARGER) += adapter_class.o
#obj-$(CONFIG_CHINOE_CHARGER) += cne_pd_adapter.o
obj-$(CONFIG_CHINOE_CHARGER) += chinoe-charger-manager.o
chinoe-charger-manager-y = cne_charger.o \
                           cne_pdc_intf.o cne_pdc.o \
                           cne_intf.o cne_switch_charging2.o \
                           cne_lib.o cne_hvdcp.o

obj-$(CONFIG_CHINOE_CHARGER) += cne_chg_type_det.o

obj-$(CONFIG_CHINOE_MAIN_CHARGER) += chinoe-main-charger.o
chinoe-main-charger-y = cne_main_charger.o \
						sgm4154x.o \
						upm6918_charger.o

obj-$(CONFIG_CHINOE_SLAVE_CHARGER) += chinoe-slave-charger.o
chinoe-slave-charger-y = cne_slave_charger.o \
						bq25601.o \
						hl7019d.o \
						upm6910_charger.o

ifeq ($(CONFIG_CHINOE_CHARGER),m)
$(info "chinoe-charger-manager IS BUILT AS DLKM")
BOARD_VENDOR_KERNEL_MODULES +=  $(KERNEL_MODULES_OUT)/chinoe-charger-manager.ko
#BOARD_VENDOR_KERNEL_MODULES +=  $(KERNEL_MODULES_OUT)/cne_pd_adapter.ko
BOARD_VENDOR_KERNEL_MODULES +=  $(KERNEL_MODULES_OUT)/charger_class.ko
BOARD_VENDOR_KERNEL_MODULES +=  $(KERNEL_MODULES_OUT)/adapter_class.ko
BOARD_VENDOR_KERNEL_MODULES +=  $(KERNEL_MODULES_OUT)/cne_chg_type_det.ko
endif

ifeq ($(CONFIG_CHINOE_MAIN_CHARGER),m)
$(info "SGM4154X IS BUILT AS DLKM")
BOARD_VENDOR_KERNEL_MODULES +=  $(KERNEL_MODULES_OUT)/chinoe-main-charger.ko
endif

ifeq ($(CONFIG_CHINOE_SLAVE_CHARGER),m)
$(info "BQ25601 IS BUILT AS DLKM")
BOARD_VENDOR_KERNEL_MODULES +=  $(KERNEL_MODULES_OUT)/chinoe-slave-charger.ko
endif
