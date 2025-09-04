# SPDX-License-Identifier: GPL-2.0-only

obj-$(CONFIG_SUPPORT_FUEL_GAUGE_SM5602) += sm5602/
obj-$(CONFIG_CHINOE_CHARGER) += charger_manager/

$(warning "Compile BUILD_SMT $(BUILD_SMT)")
ifeq ($(BUILD_SMT),true)
subdir-ccflags-y += -DSMT_VERSION
endif

$(warning "Compile BUILD_DUAL_85 $(BUILD_DUAL_85)")
ifeq ($(BUILD_DUAL_85),true)
subdir-ccflags-y += -DDUAL_85_VERSION
endif

