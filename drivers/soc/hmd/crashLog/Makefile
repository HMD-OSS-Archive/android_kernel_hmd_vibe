# SPDX-License-Identifier: GPL-2.0-or-later
#add by changxue.fang for ramdump get sw version,20201224,start
subdir-ccflags-y += -DHMD_SW_VERSION=\"$(SW_VERSION)\"
#add by changxue.fang for ramdump get sw version,20201224,end
obj-$(CONFIG_SUPPORT_CRASH_REASON)  += kernel_crash.o

# dependencies on generated files need to be listed explicitly
$(obj)/kernel_crash.o: include/generated/compile.h

# compile.h changes depending on hostname, generation number, etc,
# so we regenerate it always.
# mkcompile_h will make sure to only update the
# actual file if its content has changed.

quiet_cmd_compile.h = CHK     $@
      cmd_compile.h = \
	$(CONFIG_SHELL) $(srctree)/scripts/mkcompile_h $@	\
	"$(UTS_MACHINE)" "$(CONFIG_SMP)" "$(CONFIG_PREEMPT)"	\
	"$(CONFIG_PREEMPT_RT)" $(CONFIG_CC_VERSION_TEXT) "$(LD)"

include/generated/compile.h: FORCE
	$(call cmd,compile.h)

