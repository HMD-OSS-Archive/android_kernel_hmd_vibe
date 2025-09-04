# SPDX-License-Identifier: GPL-2.0
subdir-ccflags-y += \
			-Werror -I$(srctree)/drivers/power/supply/third_part/charger_manager/

obj-$(CONFIG_TCPC_WUSB3801X) += wusb3801x.o

ifeq ($(CONFIG_TCPC_WUSB3801X),m)
$(info "WUSB3801X IS BUILT AS DLKM")
BOARD_VENDOR_KERNEL_MODULES +=  $(KERNEL_MODULES_OUT)/wusb3801x.ko
endif
