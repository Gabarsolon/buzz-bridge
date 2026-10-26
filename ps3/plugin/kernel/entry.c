/* SPDX-License-Identifier: MIT. Kernel-loader entry contract; intentionally inert. */
#include "native_port.h"
/* Loader's function pointer uses the LV2 TOC. A normal SPRX module_start cannot
 * be substituted here. Relocatable PPC64 raw entry assembly/linking remains to
 * be verified, so this file is not advertised as a bootable payload. */
int buzz_kernel_entry(void)
{
#ifdef BUZZ_REVIEWED_NATIVE_PORT
 return buzz_native_install_verified();
#else
 /* No syscall table writes, no allocation, no hooks on an unsupported build. */
 return -1;
#endif
}
