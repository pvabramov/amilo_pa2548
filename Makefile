##############################################################################
# Fujitsu-Siemens Computers Amilo Pa 2548 ACPI support driver
#
# ::MAKEFILE::
#
# Copyrights (c) 2008-2009 Piotr V. Abramov
##############################################################################

TARGET = amilo_pa2548

obj-m := $(TARGET).o
amilo_pa2548-objs :=

KERNEL_VER := $(shell uname -r)
KERNEL_DIR := /lib/modules/$(KERNEL_VER)/build
INSTALL_DIR := /lib/modules/$(KERNEL_VER)/kernel/drivers/misc
PWD := $(shell pwd)
DISTFILES = $(TARGET).c

all: $(DISTFILES)
	@echo "COMPILE DRIVER:"
	@echo " |00| Compiling ..."
	$(MAKE) -C $(KERNEL_DIR) SUBDIRS=$(PWD) modules
	@echo " |01| Done."

clean:
	@echo "CLEAN DEVELOP DIRECTORY:"
	@echo " |00| Removing all object files ..."
	@rm -f *.o
	@rm -f *.mod.o
	@rm -f *.mod.c
	@rm -f *.symvers
	@rm -f *.order
	@rm -f *.markers
	@rm -f .${TARGET}*
	@rm -rf .tmp*
	@echo " |01| Removing target (driver) ..."
	@rm -f $(TARGET).ko
	@echo " |02| Done."

install: all
	@echo "INSTALL DRIVER:"
	@mkdir -p $(INSTALL_DIR)
	@echo " |00| Copy driver ..."
	@install -m 644 -o 0 -g 0 $(TARGET).ko $(INSTALL_DIR)/
	@echo " |01| Do dependencies ..."
	@depmod -a
	@echo " |02| Done."

load: all install
	@echo "LOAD MODULE TO KERNEL:"
	@echo " |00| Loading driver ..."
	@modprobe $(TARGET)
	@echo " |01| Done ..."

unload:
	@echo "UNLOAD MODULE FROM KERNEL:"
	@echo " |00| Unloading driver ..."
	@modprobe -r ${TARGET}
	@echo " |01| Done ..."

uninstall:
	@echo "UNINSTALL DRIVER:"
	@echo " |00| Removing driver from the kernel space ..."
	@modprobe -r $(TARGET)
	@echo " |01| Deleting driver ..."
	@rm -f $(INSTALL_DIR)/$(TARGET).ko
	@echo " |02| Do dependencies ..."
	@depmod -a
	@echo " |03| Done."
	
