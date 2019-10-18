/*
 * Copyright (c) 2003 Apple Computer, Inc. All rights reserved.
 *
 * @APPLE_LICENSE_HEADER_START@
 * 
 * This file contains Original Code and/or Modifications of Original Code
 * as defined in and that are subject to the Apple Public Source License
 * Version 2.0 (the 'License'). You may not use this file except in
 * compliance with the License. Please obtain a copy of the License at
 * http://www.opensource.apple.com/apsl/ and read it before using this
 * file.
 * 
 * The Original Code and all software distributed under the License are
 * distributed on an 'AS IS' basis, WITHOUT WARRANTY OF ANY KIND, EITHER
 * EXPRESS OR IMPLIED, AND APPLE HEREBY DISCLAIMS ALL SUCH WARRANTIES,
 * INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE, QUIET ENJOYMENT OR NON-INFRINGEMENT.
 * Please see the License for the specific language governing rights and
 * limitations under the License.
 * 
 * @APPLE_LICENSE_HEADER_END@
 */
#ifndef _STUFF_MACOSX_DEPLOYMENT_TARGET_H_
#define _STUFF_MACOSX_DEPLOYMENT_TARGET_H_

#include <mach/mach.h>

#if defined(__MWERKS__) && !defined(__private_extern__)
#define __private_extern__ __declspec(private_extern)
#elif defined(__GNUC__) && !defined(__private_extern__)
#define __private_extern__ __attribute__ ((visibility ("hidden")))
#endif

struct macosx_deployment_target {
    unsigned long major;	/* major version */
    unsigned long minor;	/* minor version (if any or zero) */
    char *name;			/* name for printing */
};

__private_extern__ void get_macosx_deployment_target(
    struct macosx_deployment_target *value);

__private_extern__ void put_macosx_deployment_target(
    char *target);

#endif /* _STUFF_MACOSX_DEPLOYMENT_TARGET_H_ */
