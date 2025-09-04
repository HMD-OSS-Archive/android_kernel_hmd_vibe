# SPDX-License-Identifier: GPL-2.0
subdir-ccflags-y += \
			-Werror -I$(srctree)/drivers/power/supply/third_part/charger_manager/

obj-$(CONFIG_TCPC_SGM7220) += sgm7220.o
ifeq ($(CONFIG_TCPC_SGM7220),m)
$(info "SGM7220 IS BUILT AS DLKM")
BOARD_VENDOR_KERNEL_MODULES +=  $(KERNEL_MODULES_OUT)/sgm7220.ko
endif
