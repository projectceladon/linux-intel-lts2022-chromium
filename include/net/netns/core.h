/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __NETNS_CORE_H__
#define __NETNS_CORE_H__

#include <linux/types.h>

struct ctl_table_header;
struct prot_inuse;

struct netns_core {
	/* core sysctls */
	struct ctl_table_header	*sysctl_hdr;

	int	sysctl_somaxconn;
	int	sysctl_android_paranoid;
	u8	sysctl_txrehash;

#ifdef CONFIG_PROC_FS
	struct prot_inuse __percpu *prot_inuse;
#endif
};

#endif
