/* rtnet_chrdev.h
 *
 * RTnet - real-time networking subsystem
 * Copyright (C) 1999    Lineo, Inc
 *               1999,2002 David A. Schleef <ds@schleef.org>
 *               2002, Ulrich Marx <marx@fet.uni-hannover.de>
 *               2003, Jan Kiszka <jan.kiszka@web.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#ifndef __RTNET_CHRDEV_H_
#define __RTNET_CHRDEV_H_

#include <linux/ioctl.h>
#include <linux/list.h>
#include <linux/netdevice.h>
#include <linux/types.h>

#include <rtdev.h>


#ifdef __KERNEL__

/* new extensible interface */
struct rtnet_ioctls {
    /* internal usage only */
    struct list_head entry;
    atomic_t         ref_count;

    /* provider specification */
    const char       *service_name;
    unsigned int     ioctl_type;
    int              (*handler)(struct rtnet_device *rtdev,
                                unsigned int request, unsigned long arg);
};

extern int rtnet_register_ioctls(struct rtnet_ioctls *ioctls);
extern void rtnet_unregister_ioctls(struct rtnet_ioctls *ioctls);

extern int __init rtnet_chrdev_init(void);
extern void rtnet_chrdev_release(void);

#endif  /* __KERNEL__ */


/* user interface for /dev/rtnet */
#define RTNET_MINOR             240


struct rtnet_ioctl_head {
    char if_name[IFNAMSIZ];
};

struct rtnet_core_cfg {
    struct rtnet_ioctl_head head;

    u32 ip_addr;
    u32 ip_mask;
    u32 ip_netaddr;
    u32 ip_broadcast;
};


#define RTNET_IOC_TYPE_CORE             0
#define RTNET_IOC_TYPE_RTCFG            1
#define RTNET_IOC_TYPE_RTMAC_TDMA1      100

#define IOC_RT_IFUP                     _IOW(RTNET_IOC_TYPE_CORE, 100, \
                                             sizeof(struct rtnet_core_cfg))
#define IOC_RT_IFDOWN                   _IOW(RTNET_IOC_TYPE_CORE, 101, \
                                             sizeof(struct rtnet_core_cfg))
/*#define IOC_RT_IF                       _IOWR(RTNET_IOC_TYPE_CORE, 102, \
                                              sizeof(struct rtnet_core_cfg))*/
/*#define IOC_RT_ROUTE_ADD                _IOW(RTNET_IOC_TYPE_CORE, 103, \
                                             sizeof(struct rtnet_core_cfg))*/
#define IOC_RT_ROUTE_SOLICIT            _IOW(RTNET_IOC_TYPE_CORE, 104, \
                                             sizeof(struct rtnet_core_cfg))
#define IOC_RT_ROUTE_DELETE             _IOW(RTNET_IOC_TYPE_CORE, 105, \
                                             sizeof(struct rtnet_core_cfg))
/*#define IOC_RT_ROUTE_GET                _IOR(RTNET_IOC_TYPE_CORE, 106, \
                                             sizeof(struct rtnet_core_cfg))*/

#endif  /* __RTNET_CHRDEV_H_ */
