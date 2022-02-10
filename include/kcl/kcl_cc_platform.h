/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Confidential Computing Platform Capability checks
 *
 * Copyright (C) 2021 Advanced Micro Devices, Inc.
 *
 * Author: Tom Lendacky <thomas.lendacky@amd.com>
 */
#ifndef AMDKCL_CC_PLATFORM_H
#define AMDKCL_CC_PLATFORM_H

#ifndef HAVE_LINUX_CC_PLATFORM_H
/**
 * enum cc_attr - Confidential computing attributes
 *
 * These attributes represent confidential computing features that are
 * currently active.
 */
enum cc_attr {
        /**
         * @CC_ATTR_MEM_ENCRYPT: Memory encryption is active
         *
         * The platform/OS is running with active memory encryption. This
         * includes running either as a bare-metal system or a hypervisor
         * and actively using memory encryption or as a guest/virtual machine
         * and actively using memory encryption.
         *
         * Examples include SME, SEV and SEV-ES.
         */
        CC_ATTR_MEM_ENCRYPT,

        /**
         * @CC_ATTR_HOST_MEM_ENCRYPT: Host memory encryption is active
         *
         * The platform/OS is running as a bare-metal system or a hypervisor
         * and actively using memory encryption.
         *
         * Examples include SME.
         */
        CC_ATTR_HOST_MEM_ENCRYPT,

        /**
         * @CC_ATTR_GUEST_MEM_ENCRYPT: Guest memory encryption is active
         *
         * The platform/OS is running as a guest/virtual machine and actively
         * using memory encryption.
         *
         * Examples include SEV and SEV-ES.
         */
        CC_ATTR_GUEST_MEM_ENCRYPT,

        /**
         * @CC_ATTR_GUEST_STATE_ENCRYPT: Guest state encryption is active
         *
         * The platform/OS is running as a guest/virtual machine and actively
         * using memory encryption and register state encryption.
         *
         * Examples include SEV-ES.
         */
        CC_ATTR_GUEST_STATE_ENCRYPT,
};

static inline bool cc_platform_has(enum cc_attr attr) { return false; }

#endif /* HAVE_LINUX_CC_PLATFORM_H */
#endif
