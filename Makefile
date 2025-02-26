#
# Makefile
#
# Copyright (C) 2009-2011 Udo Steinberg <udo@hypervisor.org>
# Economic rights: Technische Universitaet Dresden (Germany)
#
# Copyright (C) 2012-2013 Udo Steinberg, Intel Corporation.
# Copyright (C) 2019-2024 Udo Steinberg, BedRock Systems, Inc.
#
# This file is part of the NOVA microhypervisor.
#
# NOVA is free software: you can redistribute it and/or modify it
# under the terms of the GNU General Public License version 2 as
# published by the Free Software Foundation.
#
# NOVA is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License version 2 for more details.
#

-include Makefile.conf

# Defaults
ARCH	?= x86_64
BOARD	?= acpi
COMP	?= gcc
CFP	?= none

# Tools
INSTALL	?= install -m 644
MKDIR	?= mkdir -p
ifeq ($(COMP),gcc)
HST_CC	?= g++
TGT_CC	:= $(PREFIX_$(ARCH))gcc
TGT_LD	:= $(PREFIX_$(ARCH))ld
TGT_OC	:= $(PREFIX_$(ARCH))objcopy
TGT_SZ	:= $(PREFIX_$(ARCH))size
else
$(error $(COMP) is not a valid compiler type)
endif
H2E	:= $(H2E_$(ARCH))
H2B	:= $(H2B_$(ARCH))
RUN	:= $(RUN_$(ARCH))

# In-place editing works differently between GNU/BSD sed
SEDI	:= $(shell if sed --version 2>/dev/null | grep -q GNU; then echo "sed -i"; else echo "sed -i ''"; fi)

# Directories
CMD_DIR	:= cmd
SRC_DIR	:= src/$(ARCH) src
INC_DIR	:= inc/$(ARCH) inc
BLD_DIR	?= build-$(ARCH)

# Patterns
PAT_CMD	:= $(BLD_DIR)/%
PAT_OBJ	:= $(BLD_DIR)/$(ARCH)-%.o

# Files
MFL	:= $(MAKEFILE_LIST)
SRC	:= hypervisor.ld $(sort $(notdir $(foreach dir,$(SRC_DIR),$(wildcard $(dir)/*.S)))) $(sort $(notdir $(foreach dir,$(SRC_DIR),$(wildcard $(dir)/*.cpp))))
OBJ	:= $(patsubst %.ld,$(PAT_OBJ), $(patsubst %.S,$(PAT_OBJ), $(patsubst %.cpp,$(PAT_OBJ), $(SRC))))
OBJ_DEP	:= $(OBJ:%.o=%.d)

DIG	:= $(BLD_DIR)/digest
ifeq ($(ARCH),aarch64)
HYP	:= $(BLD_DIR)/$(ARCH)-$(BOARD)-nova
else
HYP	:= $(BLD_DIR)/$(ARCH)-nova
endif
ELF	:= $(HYP).elf
BIN	:= $(HYP).bin

# Messages
ifneq ($(findstring s,$(MAKEFLAGS)),)
message = @echo $(1) $(2)
endif

# Tool check
tools = $(if $(shell command -v $($(1)) 2>/dev/null),, $(error Missing $(1)=$($(1)) *** Configure it in Makefile.conf (see Makefile.conf.example)))

# Feature check
check = $(shell if $(TGT_CC) $(1) -Werror -c -xc++ /dev/null -o /dev/null >/dev/null 2>&1; then echo "$(1)"; fi)

# Version check
gitrv = $(shell (git rev-parse HEAD 2>/dev/null || echo 0) | cut -c1-7)

# Search path
VPATH	:= $(SRC_DIR)

# Optimization options
DFLAGS	:= -MP -MMD -pipe
OFLAGS	:= -Os
ifeq ($(ARCH),aarch64)
AFLAGS	:= -march=armv8-a -mcmodel=large -mgeneral-regs-only $(call check,-mno-outline-atomics) -mstrict-align
DEFINES	+= BOARD_$(BOARD)
else ifeq ($(ARCH),x86_64)
AFLAGS	:= -Wa,--divide,--noexecstack -march=x86-64-v2 -mcmodel=kernel -mgeneral-regs-only -mno-red-zone
else
$(error $(ARCH) is not a valid architecture)
endif

# Preprocessor options
PFLAGS	:= $(addprefix -D, $(DEFINES)) $(addprefix -I, $(INC_DIR))

# Language options
FFLAGS	:= $(or $(call check,-std=gnu++26), $(call check,-std=gnu++23), $(call check,-std=gnu++20))
FFLAGS	+= -ffreestanding -fdata-sections -ffunction-sections -fno-asynchronous-unwind-tables -fno-exceptions -fno-rtti -fno-use-cxa-atexit -fomit-frame-pointer
FFLAGS	+= $(call check,-fcf-protection=$(CFP))
FFLAGS	+= $(call check,-fdiagnostics-color=auto)
FFLAGS	+= $(call check,-fno-pic)
FFLAGS	+= $(call check,-fno-stack-protector)
FFLAGS	+= $(call check,-freg-struct-return)
FFLAGS	+= $(call check,-freorder-blocks)

# Warning options
WFLAGS	:= -Wall -Wextra -Walloca -Wcast-align -Wcast-qual -Wconversion -Wctor-dtor-privacy -Wdisabled-optimization -Wduplicated-branches -Wduplicated-cond -Wenum-conversion -Wextra-semi -Wformat=2 -Wmismatched-tags -Wmissing-format-attribute -Wmissing-noreturn -Wmultichar -Wnoexcept -Wold-style-cast -Woverloaded-virtual -Wpacked -Wpointer-arith -Wredundant-decls -Wredundant-tags -Wregister -Wshadow -Wsign-promo -Wvirtual-inheritance -Wvolatile -Wwrite-strings
WFLAGS	+= $(call check,-Wbidi-chars=any)
WFLAGS	+= $(call check,-Wlogical-op)
WFLAGS	+= $(call check,-Wstrict-null-sentinel)
WFLAGS	+= $(call check,-Wstrict-overflow=5)
WFLAGS	+= $(call check,-Wsuggest-override)
WFLAGS	+= $(call check,-Wvolatile-register-var)
WFLAGS	+= $(call check,-Wzero-as-null-pointer-constant)

ifeq ($(ARCH),aarch64)
WFLAGS	+= $(call check,-Wpedantic)
endif

# Compiler flags
CFLAGS	:= $(PFLAGS) $(DFLAGS) $(AFLAGS) $(FFLAGS) $(OFLAGS) $(WFLAGS)

# Linker flags
LFLAGS	:= --defsym=GIT_VER=0x$(call gitrv) --gc-sections --warn-common -static -n -s -T

# Rules
$(HYP):			$(OBJ)
			$(call message,LNK,$@)
			$(TGT_LD) $(LFLAGS) $^ -o $@

$(ELF):			$(HYP)
			$(call message,ELF,$@)
			$(H2E) $< $@

$(BIN):			$(HYP)
			$(call message,BIN,$@)
			$(H2B) $< $@

$(PAT_OBJ):		%.ld
			$(call message,PRE,$@)
			$(TGT_CC) $(CFLAGS) -xassembler-with-cpp -E -P -MT $@ $< -o $@
			@$(SEDI) 's|$<|$(notdir $<)|' $(@:%.o=%.d)

$(PAT_OBJ):		%.S
			$(call message,ASM,$@)
			$(TGT_CC) $(CFLAGS) -c $< -o $@
			@$(SEDI) 's|$<|$(notdir $<)|' $(@:%.o=%.d)

$(PAT_OBJ):		%.cpp
			$(call message,CXX,$@)
			$(TGT_CC) $(CFLAGS) -c $< -o $@
			@$(SEDI) 's|$<|$(notdir $<)|' $(@:%.o=%.d)

$(PAT_CMD):		$(CMD_DIR)/%.cpp
			$(call message,CMD,$@)
			$(HST_CC) $< -o $@

$(BLD_DIR):
			$(call message,DIR,$@)
			@$(MKDIR) $@

Makefile.conf:
			$(call message,CFG,$@)
			@cp $@.example $@

$(DIG):			$(MFL) | $(BLD_DIR) tool_hst_cc
$(OBJ):			$(MFL) | $(BLD_DIR) tool_tgt_cc

# Zap old-fashioned suffixes
.SUFFIXES:

.PHONY:			clean install run tool_hst_cc tool_tgt_cc

clean:
			$(call message,CLN,$@)
			$(RM) $(DIG) $(OBJ) $(HYP) $(ELF) $(BIN) $(OBJ_DEP)

install:		$(HYP) | $(DIG)
			$(call message,INS,$^ =\> $(INS_DIR))
			$(INSTALL) $^ $(INS_DIR)
			@$(TGT_SZ) $<
ifeq ($(ARCH),x86_64)
			@echo Reference Integrity Measurements for $^
			@echo $(shell $(DIG) $^ | sha1sum)   "SHA1-160"
			@echo $(shell $(DIG) $^ | sha256sum) "SHA2-256"
			@echo $(shell $(DIG) $^ | sha384sum) "SHA2-384"
			@echo $(shell $(DIG) $^ | sha512sum) "SHA2-512"
endif

run:			$(ELF)
			$(RUN) $<

tool_hst_cc:
			$(call tools,HST_CC)

tool_tgt_cc:
			$(call tools,TGT_CC)

# Include Dependencies
ifneq ($(MAKECMDGOALS),clean)
-include		$(OBJ_DEP)
endif
