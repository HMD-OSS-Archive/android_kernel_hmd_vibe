hac_switch-y := hac-switch.o

obj-$(CONFIG_HAC_SWITCH) += hac-switch.o

ifeq ($(CONFIG_HAC_SWITCH),m)
$(info "Soc Audio for awinic aw882xxseries")
BOARD_VENDOR_KERNEL_MODULES +=  $(KERNEL_MODULES_OUT)/hac-switch.ko
endif
