/* tdma_event.c
 *
 * rtmac - real-time networking medium access control subsystem
 * Copyright (C) 2002 Marc Kleine-Budde <kleine-budde@gmx.de>
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

#include <linux/slab.h>
#include <linux/module.h>
#include <linux/list.h>
#include <linux/if_ether.h>

#include <rtai.h>

#include <rtnet.h>

#include <rtmac.h>
#include <tdma.h>
#include <tdma_event.h>

#define TIMERTICKS 1000 // 1 us // FIXME: needed?

/******************************	helper functions ********************************/
static int tdma_master_add_rt(struct rtmac_tdma *tdma, u32 ip_addr);
static int tdma_master_add_rt(struct rtmac_tdma *tdma, u32 ip_addr);
static int tdma_client_add_rt_rate(struct rtmac_tdma *tdma, u32 ip_addr, unsigned char station);
static int tdma_add_rt(struct rtmac_tdma *tdma, u32 ip_addr);
static void tdma_remove_rt(struct rtmac_tdma *tdma, u32 ip_addr);
static void tdma_expired_add_rt(struct rtmac_tdma *tdma);
static int tdma_master_request_up(struct rtmac_tdma *tdma);
static void tdma_expired_sent_conf(struct rtmac_tdma *tdma);
static void tdma_master_rcvd_test_ack(struct rtmac_tdma *tdma, struct rtskb *skb);
static void tdma_expired_master_sent_test(struct rtmac_tdma *tdma);
static void tdma_master_change_offset(struct rtmac_tdma *tdma, u32 ip_addr, unsigned int offset);

static void tdma_client_rcvd_conf(struct rtmac_tdma *tdma, struct rtskb *skb);
static void tdma_master_rcvd_ack_conf(struct rtmac_tdma *tdma, struct rtskb *skb);
static void tdma_client_rcvd_test(struct rtmac_tdma *tdma, struct rtskb *skb);
static void tdma_client_rcvd_station_list(struct rtmac_tdma *tdma, struct rtskb *skb);
static void tdma_client_rcvd_change_offset(struct rtmac_tdma *tdma, struct rtskb *skb);


static void tdma_rcvd_sof(struct rtmac_tdma *tdma, struct rtskb *skb);


/******************************	master's helper functions ************************/
static void tdma_send_conf(struct rtmac_tdma *tdma, void *hw_addr, unsigned char station);
static void tdma_make_station_list(struct rtmac_tdma *tdma, void *data);
static void tdma_send_station_list(struct rtmac_tdma *tdma, void *hw_addr, void *station_list);

/******************************	states ******************************************/
static int tdma_state_down			(struct rtmac_tdma *tdma, TDMA_EVENT event, struct tdma_info *info);

static int tdma_state_master_down		(struct rtmac_tdma *tdma, TDMA_EVENT event, struct tdma_info *info);
static int tdma_state_master_wait		(struct rtmac_tdma *tdma, TDMA_EVENT event, struct tdma_info *info);
static int tdma_state_master_sent_conf		(struct rtmac_tdma *tdma, TDMA_EVENT event, struct tdma_info *info);
static int tdma_state_master_sent_test		(struct rtmac_tdma *tdma, TDMA_EVENT event, struct tdma_info *info);

static int tdma_state_other_master		(struct rtmac_tdma *tdma, TDMA_EVENT event, struct tdma_info *info);
static int tdma_state_client_down		(struct rtmac_tdma *tdma, TDMA_EVENT event, struct tdma_info *info);
static int tdma_state_client_ack_conf		(struct rtmac_tdma *tdma, TDMA_EVENT event, struct tdma_info *info);
static int tdma_state_client_rcvd_ack		(struct rtmac_tdma *tdma, TDMA_EVENT event, struct tdma_info *info);





#ifdef CONFIG_TDMA_DEBUG
static const char *tdma_event[] = {
	"REQUEST_MASTER",
	"REQUEST_CLIENT",

	"REQUEST_UP",
	"REQUEST_DOWN",

	"REQUEST_ADD_RT",
	"REQUEST_REMOVE_RT",

	"REQUEST_ADD_NRT",
	"REQUEST_REMOVE_NRT",

	"CHANGE_MTU",
	"CHANGE_CYCLE",
	"CHANGE_OFFSET",

	"EXPIRED_ADD_RT",
	"EXPIRED_MASTER_WAIT",
	"EXPIRED_MASTER_SENT_CONF",
	"EXPIRED_MASTER_SENT_TEST",
	"EXPIRED_CLIENT_SENT_ACK",

	"NOTIFY_MASTER",
	"REQUEST_TEST",
	"ACK_TEST",

	"REQUEST_CONF",
	"ACK_CONF",
	"ACK_ACK_CONF",

	"STATION_LIST",
	"REQUEST_CHANGE_OFFSET",
	
	"START_OF_FRAME",
};
#endif // CONFIG_TDMA_DEBUG



const char *tdma_state[] = {
	"TDMA_DOWN",

	"TDMA_MASTER_DOWN",
	"TDMA_MASTER_WAIT",
	"TDMA_MASTER_SENT_CONF",
	"TDMA_MASTER_SENT_TEST",

	"TDMA_OTHER_MASTER",
	"TDMA_CLIENT_DOWN",
	"TDMA_CLIENT_ACK_CONF",
	"TDMA_CLIENT_RCVD_ACK",
};




static int (*state[]) (struct rtmac_tdma *tdma, TDMA_EVENT event, struct tdma_info *info) =
{
	tdma_state_down,

	tdma_state_master_down,
	tdma_state_master_wait,
	tdma_state_master_sent_conf,
	tdma_state_master_sent_test,

	tdma_state_other_master,
	tdma_state_client_down,
	tdma_state_client_ack_conf,
	tdma_state_client_rcvd_ack,
};



int tdma_do_event(struct rtmac_tdma *tdma, TDMA_EVENT event, struct tdma_info *info)
{
	TDMA_DEBUG(3, "RTmac: tdma: "__FUNCTION__"() event=%s, state=%s\n", tdma_event[event], tdma_state[tdma->state]);

	return (*state[tdma->state]) (tdma, event, info);
}




void tdma_next_state(struct rtmac_tdma *tdma, TDMA_STATE state)
{
	TDMA_DEBUG(4, "RTmac: tdma: next state=%s \n", tdma_state[state]);

	tdma->state = state;
}



/*
 * function tdma_state_down
 */
static int tdma_state_down(struct rtmac_tdma *tdma, TDMA_EVENT event, struct tdma_info *info)
{
	int ret = 0;

	switch(event) {
	case REQUEST_MASTER:
		MOD_INC_USE_COUNT;
		/*
		 * set cycle (us) and mtu (byte)
		 */
		tdma->cycle = info->cycle;
		tdma->mtu = info->mtu;
		
		/*
		 * change working task to notification....
		 * ...and start task
		 */
		ret = tdma_task_change(tdma, &tdma_task_notify, TDMA_NOTIFY_TASK_CYCLE * 1000*1000);
		if (ret != 0)
			return ret;

		/*
		 * start timer
		 */
		tdma_timer_start_master_wait(tdma, TDMA_MASTER_WAIT_TIMEOUT);

		tdma_next_state(tdma, TDMA_MASTER_WAIT);
		break;

	case REQUEST_CLIENT:
		MOD_INC_USE_COUNT;
		tdma_next_state(tdma, TDMA_CLIENT_DOWN);
		break;

	case REQUEST_UP:
		rt_printk("RTmac: tdma: please decide to be master or client.\n");
		return -1;
		break;
		
	case REQUEST_DOWN:
		// do nothing
		break;

	case CHANGE_MTU:		/* fallthrough */
	case CHANGE_CYCLE:
		rt_printk("RTmac: tdma: MTU and cycle can only be changed as master\n");
		return -1;
		break;

	case EXPIRED_ADD_RT:		/* fallthrough */
	case EXPIRED_MASTER_WAIT:
		rt_printk("RTmac: tdma: BUG in "__FUNCTION__"()! Unknown event %s\n", tdma_event[event]);
		return -1;
		break;

	case NOTIFY_MASTER:
		MOD_INC_USE_COUNT;
		tdma_next_state(tdma, TDMA_OTHER_MASTER);
		break;

	default:
		TDMA_DEBUG(2, "RTmac: tdma: " __FUNCTION__ "(), Unknown event %s\n", tdma_event[event]);
		break;
	}

	return ret;
}

static int tdma_state_master_wait(struct rtmac_tdma *tdma, TDMA_EVENT event, struct tdma_info *info)
{
	int ret = 0;

	switch(event) {
	case REQUEST_MASTER:
		// do nothing
		break;
		
	case REQUEST_CLIENT:
		rt_printk("RTmac: tdma: this station is confed as master, if you want to be a client shut down first!\n");
		return -1;
		break;

	case REQUEST_UP:
		rt_printk("RTmac: tdma: please be patient...\n");
		return -1;
		break;

	case REQUEST_DOWN:
		// FIXME: notify master down (???)
		tdma_cleanup_master_rt(tdma);
		tdma_next_state(tdma, TDMA_DOWN);
		break;

	case REQUEST_ADD_RT:
		ret = tdma_master_add_rt(tdma, info->ip_addr);
		break;

	case REQUEST_REMOVE_RT:
		tdma_remove_rt(tdma, info->ip_addr);
		break;

	case CHANGE_MTU:
		tdma->mtu = info->mtu;
		break;

	case CHANGE_CYCLE:
		tdma->cycle = info->cycle;
		break;

	case EXPIRED_ADD_RT:
		tdma_expired_add_rt(tdma);
		break;

	case EXPIRED_MASTER_WAIT:
		tdma_next_state(tdma, TDMA_MASTER_DOWN);
		break;

	case NOTIFY_MASTER:
		// FIXME: notify master down (???)
		rt_printk("RTmac: tdma: *** WARNING *** detected another master in subnet, going into DOWN state.\n");
		tdma_cleanup_master_rt(tdma);
		tdma_next_state(tdma, TDMA_DOWN);
		return -1;
		break;

	default:
		TDMA_DEBUG(2, "RTmac: tdma: "__FUNCTION__"(), Unknown event %s\n", tdma_event[event]);
		break;
	}

	return ret;

}

static int tdma_state_master_down(struct rtmac_tdma *tdma, TDMA_EVENT event, struct tdma_info *info)
{
	int ret = 0;

	switch(event) {
	case REQUEST_MASTER:
		// do nothing
		break;

	case REQUEST_CLIENT:
		rt_printk("RTmac: tdma: this station is confed as master, if you want to be a client shut down first!\n");
		return -1;
		break;

	case REQUEST_UP:
		ret = tdma_master_request_up(tdma);
		break;

	case REQUEST_DOWN:
		// FIXME: notify master down (???)
		tdma_cleanup_master_rt(tdma);
		tdma_next_state(tdma, TDMA_DOWN);
		break;

	case REQUEST_ADD_RT:
		ret = tdma_master_add_rt(tdma, info->ip_addr);
		break;

	case REQUEST_REMOVE_RT:
		tdma_remove_rt(tdma, info->ip_addr);
		break;

	case CHANGE_MTU:
		tdma->mtu = info->mtu;
		break;

	case CHANGE_CYCLE:
		tdma->cycle = info->cycle;
		break;

	case EXPIRED_ADD_RT:
		tdma_expired_add_rt(tdma);
		break;

	default:
		TDMA_DEBUG(2, "RTmac: tdma: "__FUNCTION__"(), Unknown event %s\n", tdma_event[event]);
		break;
	}

	return ret;
}



static int tdma_state_master_sent_conf(struct rtmac_tdma *tdma, TDMA_EVENT event, struct tdma_info *info)
{
	int ret = 0;

	switch(event) {
	case REQUEST_DOWN:
		// FIXME: notify master down (!)
		tdma_cleanup_master_rt(tdma);
		tdma_next_state(tdma, TDMA_DOWN);
		break;
	case EXPIRED_MASTER_SENT_CONF:
		tdma_expired_sent_conf(tdma);
		break;
	case ACK_TEST:
		tdma_master_rcvd_test_ack(tdma, (struct rtskb *)info);
		break;
	case ACK_CONF:
		tdma_master_rcvd_ack_conf(tdma, (struct rtskb *)info);
		break;
	default:
		TDMA_DEBUG(2, "RTmac: tdma: "__FUNCTION__"(), Unknown event %s\n", tdma_event[event]);
		break;
	}

	return ret;
}



static int tdma_state_master_sent_test(struct rtmac_tdma *tdma, TDMA_EVENT event, struct tdma_info *info)
{
	int ret = 0;

	switch(event) {
	case REQUEST_DOWN:
		// FIXME: notify master down (!)
		tdma_cleanup_master_rt(tdma);
		tdma_next_state(tdma, TDMA_DOWN);
		break;
	case CHANGE_OFFSET:
		tdma_master_change_offset(tdma, info->ip_addr, info->offset);
		break;
	case EXPIRED_MASTER_SENT_CONF:
		// do nothing
		break;
	case EXPIRED_MASTER_SENT_TEST:
		tdma_expired_master_sent_test(tdma);
		
		/*
		 * start mac
		 */
		ret = tdma_task_change(tdma, &tdma_task_master, tdma->cycle * 1000);
		if( ret != 0 )
			return ret;
		
		tdma->flags.mac_active = 1;	//reset in cleanup

		break;
	case ACK_TEST:
		// FIXME: too late, inform user, check station
		break;
	case ACK_CONF:
		// FIXME: too late, inform user
		break;
	default:
		TDMA_DEBUG(2, "RTmac: tdma: "__FUNCTION__"(), Unknown event %s\n", tdma_event[event]);
		break;
	}

	return ret;
}



static int tdma_state_other_master(struct rtmac_tdma *tdma, TDMA_EVENT event, struct tdma_info *info)
{
	int ret = 0;

	switch(event) {
	case REQUEST_MASTER:
		rt_printk("RTmac: tdma: this station can only become client, other master is or was active\n");
		return -1;
		break;

	case REQUEST_CLIENT:
		tdma_next_state(tdma, TDMA_CLIENT_DOWN);
		break;

	case REQUEST_UP:
		rt_printk("RTmac: tdma: become client first\n");
		return -1;
		break;

	case REQUEST_DOWN:
		tdma_cleanup_client_rt(tdma);
		tdma_next_state(tdma, TDMA_DOWN);
		break;

	case REQUEST_ADD_RT:		/* fallthrough */
	case REQUEST_REMOVE_RT:		/* fallthrough */
	case CHANGE_MTU:		/* fallthrough */
	case CHANGE_CYCLE:
		rt_printk("RTmac: tdma: only master can do that!\n");
		return -1;
		break;

	case EXPIRED_ADD_RT:		/* fallthrough */
	case EXPIRED_MASTER_WAIT:
		rt_printk("RTmac: tdma: BUG in "__FUNCTION__"()! Unknown event %s\n", tdma_event[event]);
		return -1;
		break;

	case NOTIFY_MASTER:
		// do nothing
		break;

	case REQUEST_CONF:
		tdma_client_rcvd_conf(tdma, (struct rtskb *)info);
		break;

	default:
		TDMA_DEBUG(2, "RTmac: tdma: "__FUNCTION__"(), Unknown event %s\n", tdma_event[event]);
		break;
	}

	return ret;
}

static int tdma_state_client_down(struct rtmac_tdma *tdma, TDMA_EVENT event, struct tdma_info *info)
{
	int ret = 0;

	switch(event) {
	case REQUEST_MASTER:
		rt_printk("RTmac: tdma: this station is confed as client, if you want to be a master shut down first!\n");
		return -1;
		break;

	case REQUEST_CLIENT:
		// do nothing
		break;

	case REQUEST_UP:
		//FIXME: do something useful
		break;

	case REQUEST_DOWN:
		tdma_cleanup_client_rt(tdma);
		tdma_next_state(tdma, TDMA_DOWN);
		break;

	case REQUEST_ADD_RT:		/* fallthrough */
	case REQUEST_REMOVE_RT:		/* fallthrough */
	case CHANGE_MTU:		/* fallthrough */
	case CHANGE_CYCLE:
		rt_printk("RTmac: tdma: only master can do that!\n");
		return -1;
		break;

	case EXPIRED_ADD_RT:		/* fallthrough */
	case EXPIRED_MASTER_WAIT:
		rt_printk("RTmac: tdma: BUG in "__FUNCTION__"()! Unknown event %s\n", tdma_event[event]);
		return -1;
		break;

	case NOTIFY_MASTER:
		// do nothing
		break;

	case REQUEST_CONF:
		tdma_client_rcvd_conf(tdma, (struct rtskb *)info);
		break;

	case START_OF_FRAME:
		//FIXME: what do now?
		break;

	default:
		TDMA_DEBUG(2, "RTmac: tdma: "__FUNCTION__"(), Unknown event %s\n", tdma_event[event]);
		break;
	}
	return ret;
}




static int tdma_state_client_ack_conf(struct rtmac_tdma *tdma, TDMA_EVENT event, struct tdma_info *info)
{
	int ret = 0;

	switch(event) {
	case REQUEST_DOWN:
		//FIXME: only for testing purpose
		tdma_cleanup_client_rt(tdma);
		tdma_next_state(tdma, TDMA_DOWN);
		break;

	case EXPIRED_CLIENT_SENT_ACK:
		//FIXME: kleineb: block sending...
		rt_printk("RTmac: tdma: master did not sent ack to our ack, doing into DOWN state\n");
		tdma_cleanup_client_rt(tdma);
		tdma_next_state(tdma, TDMA_DOWN);
		break;

	case NOTIFY_MASTER:
		// do nothing
		break;

	case ACK_ACK_CONF:
		//FIXME: kleineb: start mac
		tdma_next_state(tdma, TDMA_CLIENT_RCVD_ACK);
		break;

	default:
		TDMA_DEBUG(2, "RTmac: tdma: "__FUNCTION__"(), Unknown event %s\n", tdma_event[event]);
		break;
	}
	
	return ret;
}



static int tdma_state_client_rcvd_ack(struct rtmac_tdma *tdma, TDMA_EVENT event, struct tdma_info *info)
{
	int ret = 0;

	switch(event) {
	case REQUEST_DOWN:
		//FIXME: only for testing purpose
		tdma_cleanup_client_rt(tdma);
		tdma_next_state(tdma, TDMA_DOWN);
		break;

	case EXPIRED_CLIENT_SENT_ACK:
		// do nothing
		break;

	case NOTIFY_MASTER:
		// do nothing
		break;

	case REQUEST_TEST:
		tdma_client_rcvd_test(tdma, (struct rtskb *)info);
		break;

	case ACK_ACK_CONF:
		// do nothing
		break;

	case STATION_LIST:
		tdma_client_rcvd_station_list(tdma, (struct rtskb *)info);
		
		/*
		 * start mac
		 */
		tdma->flags.mac_active = 1;	//reset in cleanup
		tdma_task_change(tdma, &tdma_task_client, 0);
		break;
	case REQUEST_CHANGE_OFFSET:
		tdma_client_rcvd_change_offset(tdma, (struct rtskb *)info);
		break;
	case START_OF_FRAME:
		tdma_rcvd_sof(tdma, (struct rtskb *)info);
		// do nothing
		break;

	default:
		TDMA_DEBUG(2, "RTmac: tdma: "__FUNCTION__"(), Unknown event %s\n", tdma_event[event]);
		break;
	}
	
	return ret;
}





/******************************	helper functions ********************************/
static int tdma_master_add_rt(struct rtmac_tdma *tdma, u32 ip_addr)
{
	struct rtmac_device *rtmac = tdma->rtmac;
	struct rtnet_device *rtdev = rtmac->rtdev;
	struct list_head *lh;
	struct tdma_rt_entry *rt_entry;

	/*
	 * check if the users wants to add his devs ip address
	 * master is also a client...
	 */
	if (ip_addr == rtdev->local_addr) {
		rt_printk("RTmac: rtnet: you don't need to add your local IP address to realtime list\n");
		return 0;
	}

	/*
	 * check if there is the maximum count of rt stations in list
	 */
	if (list_len(&tdma->rt_list) == TDMA_MAX_RT) {
		rt_printk("RTmac: tdma: maximum count exceeded, cannot add IP %u.%u.%u.%u\n", NIPQUAD(ip_addr));
		return -1;
	}

	/*
	 * check if IP is already in realtime list
	 */
	list_for_each(lh, &tdma->rt_list) {
		rt_entry = list_entry(lh, struct tdma_rt_entry, list);

		/*
		 * already in list, then exit
		 */
		if (rt_entry->arp->ip_addr == ip_addr) {
			rt_printk("RTmac: tdma: IP %u.%u.%u.%u already in realtime list.\n", NIPQUAD(ip_addr));
			return -1;
		}
	}

	/*
	 * call real add_rt function...
	 */
	return tdma_add_rt(tdma, ip_addr);
}



static int tdma_client_add_rt_rate(struct rtmac_tdma *tdma, u32 ip_addr, unsigned char station)
{
	struct rt_arp_table_struct *arp_entry;
	struct tdma_rt_entry *rt_entry;

	/*
	 * lookup IP in ARP table...
	 */
	arp_entry = rt_arp_table_lookup(ip_addr);

	/*
	 * ...if IP is found add to realtime list
	 */
	if (arp_entry) {
		TDMA_DEBUG(4, "RTmac: tdma: "__FUNCTION__"() found IP %u.%u.%u.%u in ARP table, adding to rt-list\n", NIPQUAD(ip_addr));
		rt_entry = rt_malloc(sizeof(struct tdma_rt_entry));
		if (!rt_entry) {
			rt_printk("RTmac: tdma: out of mem!\n");
			return -1;
		}
		memset(rt_entry, 0, sizeof(struct tdma_rt_entry));
		INIT_LIST_HEAD(&rt_entry->list);
		INIT_LIST_HEAD(&rt_entry->list_rate);
			
		rt_entry->arp = arp_entry;
		rt_entry->station = station;
		list_add_tail(&rt_entry->list_rate, &tdma->rt_list_rate);

		return 0;
	}
	
	return -1;
}



static int tdma_add_rt(struct rtmac_tdma *tdma, u32 ip_addr)
{
	struct rtmac_device *rtmac = tdma->rtmac;
	struct rtnet_device *rtdev = rtmac->rtdev;
	struct tdma_rt_add_entry *rt_add_entry;
	struct tdma_rt_entry *rt_entry;
	struct rt_arp_table_struct *arp_entry;

	/*
	 * lookup IP in ARP table...
	 */
	arp_entry = rt_arp_table_lookup(ip_addr);


	/*
	 * ...if IP is found add to realtime list
	 */
	if (arp_entry) {
		TDMA_DEBUG(4, "RTmac: tdma: "__FUNCTION__"() found IP %u.%u.%u.%u in ARP table, adding to rt-list\n", NIPQUAD(ip_addr));
		rt_entry = rt_malloc(sizeof(struct tdma_rt_entry));
		memset(rt_entry, 0, sizeof(struct tdma_rt_entry));
		INIT_LIST_HEAD(&rt_entry->list);
		INIT_LIST_HEAD(&rt_entry->list_rate);
			
		rt_entry->arp = arp_entry;
		list_add_tail(&rt_entry->list, &tdma->rt_list);

		return 0;
	}

	/*
	 * ...if IP is _not_ found send ARP, make ARP request
	 */
	TDMA_DEBUG(4, "RTmac: tdma: "__FUNCTION__"() IP %d.%d.%d.%d not in ARP table, sending ARP request\n", NIPQUAD(ip_addr)),
	rt_arp_solicit(rtdev, ip_addr);

	/*
	 * alloc mem and add IP to list of possible rt stations (rt_add_list)
	 */
	rt_add_entry = rt_malloc(sizeof(struct tdma_rt_add_entry));
	memset(rt_add_entry, 0, sizeof(struct tdma_rt_add_entry));
	INIT_LIST_HEAD(&rt_add_entry->list);

	rt_add_entry->ip_addr = ip_addr;
	rt_add_entry->timeout = TDMA_RT_REQ_TIMEOUT * HZ/1000;

	list_add_tail(&rt_add_entry->list, &tdma->rt_add_list);
			
	/*
	 * start timeout
	 */
	tdma_timer_start_rt_add(tdma, rt_add_entry->timeout);

	return 0;
}


void tdma_remove_rt(struct rtmac_tdma *tdma, u32 ip_addr)
{
	struct tdma_rt_entry *rt_entry;
	struct tdma_rt_add_entry *rt_add_entry;
	struct list_head *lh, *next;

	list_for_each_safe(lh, next, &tdma->rt_add_list) {
		rt_add_entry = list_entry(lh, struct tdma_rt_add_entry, list);

		if (rt_add_entry->ip_addr == ip_addr) {
			list_del(&rt_add_entry->list);
			rt_free(rt_add_entry);
		}
	}

	list_for_each_safe(lh, next, &tdma->rt_list) {
		rt_entry = list_entry(lh, struct tdma_rt_entry, list);

		if (rt_entry->arp->ip_addr == ip_addr) {
			list_del(&rt_entry->list);
			rt_free(rt_entry);
		}
	}
}


static void tdma_expired_add_rt(struct rtmac_tdma *tdma)
{
	struct tdma_rt_add_entry *rt_add_entry;
	struct tdma_rt_entry *rt_entry;
	struct rt_arp_table_struct *arp_entry;
	struct list_head *lh, *next;

	/*
	 * iterate thrbough to-add-list
	 */
	list_for_each_safe(lh, next, &tdma->rt_add_list) {
		rt_add_entry = list_entry(lh, struct tdma_rt_add_entry, list);
		
		/*
		 * lookup IP in arp table
		 */
		arp_entry = rt_arp_table_lookup(rt_add_entry->ip_addr);
		
		if (arp_entry) {
			/*
			 * remove to-add-entry from list
			 */
			TDMA_DEBUG(4, "RTmac: tdma: timeout "__FUNCTION__"() found IP %u.%u.%u.%u in ARP table, adding to rt-list\n",
				   NIPQUAD(rt_add_entry->ip_addr));
			list_del(&rt_add_entry->list);
			rt_free(rt_add_entry);
					
			/*
			 * malloc and init rt-entry
			 *
			 * add to rt-list
			 */
			rt_entry = rt_malloc(sizeof(struct tdma_rt_entry));
			memset(rt_entry, 0, sizeof(struct tdma_rt_entry));
			INIT_LIST_HEAD(&rt_entry->list);

			rt_entry->arp = arp_entry;
					
			list_add_tail(&rt_entry->list, &tdma->rt_list);
		} else if (rt_add_entry->timeout <= jiffies) {
			/*
			 * after timeout, if no arp entry is found, remove from list
			 */
			TDMA_DEBUG(4, "RTmac: tdma: timeout "__FUNCTION__"() IP %u.%u.%u.%u not in ARP table, timeout ocurred\n",
				   NIPQUAD(rt_add_entry->ip_addr));
			list_del(&rt_add_entry->list);
			rt_free(rt_add_entry);
		}
		
	}
	return;
}




static int tdma_master_request_up(struct rtmac_tdma *tdma)
{
	struct tdma_rt_entry *rt_entry;
	struct rt_arp_table_struct *arp_entry;
	struct list_head *lh;
	unsigned char station = 1;
	int ret = 0;

	/*
	 * if there is no entry in rt list, exit
	 */
	if (list_empty(&tdma->rt_list)) {
		rt_printk("RTmac: tdma: no realtime stations in list...please add some first\n");
		return -1;
	}

	tdma_next_state(tdma, TDMA_MASTER_SENT_CONF);

	/*
	 * send config request to every station
	 */
	list_for_each(lh, &tdma->rt_list) {
		rt_entry = list_entry(lh, struct tdma_rt_entry, list);
		arp_entry = rt_entry->arp;

		//FIXME: ASSERT(rt_entry->state == RT_DOWN);
		rt_entry->state = RT_SENT_CONF;
		rt_entry->station = station;

		TDMA_DEBUG(5, "RTmac: tdma: "__FUNCTION__"() sending conf request to client %u.%u.%u.%u\n", NIPQUAD(arp_entry->ip_addr));

		tdma_send_conf(tdma, arp_entry->hw_addr, station);

		station++;
	}

	/*
	 * start timer
	 * after timeout we will look if all station acknoleged our request
	 */
	tdma_timer_start_sent_conf(tdma, TDMA_SENT_CLIENT_CONF_TIMEOUT);

	return ret;
}



static void tdma_expired_sent_conf(struct rtmac_tdma *tdma)
{
	struct rtmac_device *rtmac= tdma->rtmac;
	struct rtnet_device *rtdev = rtmac->rtdev;
	struct tdma_rt_entry *rt_entry;
	struct list_head *lh, *next;
	struct rtskb *skb;
	struct tdma_conf_msg *conf_ack_ack;
	void *data = &conf_ack_ack;
	

	/*
	 * iterate through rt list
	 */
	list_for_each_safe(lh, next, &tdma->rt_list) {
		rt_entry = list_entry(lh, struct tdma_rt_entry, list);

		if (rt_entry->state == RT_RCVD_CONF) {
			/*
			 * if station has sent ACK....
			 */
			TDMA_DEBUG(4, "RTmac: tdma: "__FUNCTION__"() station: %d, IP: %u.%u.%u.%u successful acknowledged\n",
				   rt_entry->station, NIPQUAD(rt_entry->arp->ip_addr));

			skb = tdma_make_msg(rtdev, rt_entry->arp->hw_addr, ACK_ACK_CONF, data);
			conf_ack_ack->station = rt_entry->station;

			rtdev_xmit_if(skb);
		} else { // rt_entry->state != RT_RCVD_CONF
			/*
			 * if station has _not_ sent ACK, delete it from list
			 */
			rt_printk("RTmac: tdma: client with IP %u.%u.%u.%u did _not_ send acknowledge\n", NIPQUAD(rt_entry->arp->ip_addr));

			list_del(&rt_entry->list);
			rt_free(rt_entry);
		}
	}

	if (list_empty(&tdma->rt_list)) {
		rt_printk("RTmac: tdma: no realtime stations in list...please add some first\n");
		tdma_cleanup_master_rt(tdma);
		tdma_next_state(tdma, TDMA_DOWN);

		return;
	}

	
	tdma_task_change(tdma, tdma_task_config, tdma->cycle*1000);
	return;
}



static void tdma_master_rcvd_test_ack(struct rtmac_tdma *tdma, struct rtskb *skb)
{
	struct tdma_test_msg *test_ack = (struct tdma_test_msg *)skb->data;
	struct tdma_rt_entry *rt_entry;
	struct list_head *lh;
	int max, rtt;

	max = TDMA_MASTER_MAX_TEST;
	
	TDMA_DEBUG(6, "RTmac: tdma: "__FUNCTION__"() received test ack packet\n");

	/*
	 * iterate through all rt stations...
	 */
	list_for_each(lh, &tdma->rt_list) {
		rt_entry = list_entry(lh, struct tdma_rt_entry, list);

		/*
		 * find the right station (compare MAC)
		 * the received ACK is only valid if the counter and the
		 * transmitting time match
		 */
		if (memcmp(rt_entry->arp->hw_addr, skb->mac.ethernet->h_source, RT_ARP_ADDR_LEN) == 0 &&
		    rt_entry->state == RT_SENT_TEST &&
		    rt_entry->counter == test_ack->counter &&
		    rt_entry->tx == test_ack->tx) {
			/*
			 * set state if station to sucessfull received test
			 * packet and calculate the round-trip-time
			 * note: all times are in internal count units, so we
			 * must transform then to ns....
			 */
			rt_entry->state = RT_RCVD_TEST;
			rtt = (int)count2nano(skb->rx - rt_entry->tx);
			rt_entry->rtt = MAX(rt_entry->rtt, rtt);

			TDMA_DEBUG(6, "RTMAC: tdma: "__FUNCTION__"() received test ack from %u.%u.%u.%u rtt %u ns\n",
				   NIPQUAD(rt_entry->arp->ip_addr), rtt);
			return;
		}
	}

}



static void tdma_expired_master_sent_test(struct rtmac_tdma *tdma)
{
	struct tdma_rt_entry *rt_entry, *compare_entry;
	struct list_head *lh, *next, *lh_rate;
	unsigned char station_list[TDMA_MAX_RT * sizeof(struct tdma_station_list)];
	int max;

	max = TDMA_MASTER_MAX_TEST;
	
	/*
	 * iterate through rt list
	 */
	list_for_each_safe(lh, next, &tdma->rt_list) {
		rt_entry = list_entry(lh, struct tdma_rt_entry, list);
		
		/*
		 * have we received _all_ acks for our test packes? 
		 */
		if (rt_entry->state == RT_RCVD_TEST && rt_entry->counter >= max-1) {
			rt_printk("RTmac: tdma: station %d, IP %u.%u.%u.%u, max rtt %d us\n",
				  rt_entry->station, NIPQUAD(rt_entry->arp->ip_addr), (rt_entry->rtt+500)/1000);

			/*
			 * station completed test, so set state
			 */
			rt_entry->state = RT_COMP_TEST;

			/*
			 * now we build a new list: tdma->rt_list_rate
			 * beginning with the fastest station
			 *
			 * it's simply inserting the new station into
			 * the sorted list
			 */
			if (list_empty(&tdma->rt_list_rate)) {
				list_add(&rt_entry->list_rate, &tdma->rt_list_rate);

			} else {	/* (list_empty(&tdma->rt_list_rate)) */
				list_for_each(lh_rate, &tdma->rt_list_rate) {
				        compare_entry = list_entry(lh_rate, struct tdma_rt_entry, list_rate);

					if (rt_entry->rtt <= compare_entry->rtt) {
						list_add_tail(&rt_entry->list_rate, &compare_entry->list_rate);
						goto cont;
					}
				}
				list_add_tail(&rt_entry->list_rate, &tdma->rt_list_rate);
			cont:
			}


			
		} else {	/* (rt_entry->state == RT_RCVD_TEST && rt_entry->counter == max) */
			rt_printk("RTmac: tdma: *** WARNING *** "__FUNCTION__"() received not ACK from station %d, IP %u.%u.%u.%u, going into DOWN state\n",
				  rt_entry->station, NIPQUAD(rt_entry->arp->ip_addr));
			tdma_cleanup_master_rt(tdma);
			tdma_next_state(tdma, TDMA_DOWN);
		}
	}

	tdma_make_station_list(tdma, station_list);

	list_for_each(lh, &tdma->rt_list) {
		rt_entry = list_entry(lh, struct tdma_rt_entry, list);
		
		tdma_send_station_list(tdma, rt_entry->arp->hw_addr, station_list);
	}


	return;
}



static void tdma_master_change_offset(struct rtmac_tdma *tdma, u32 ip_addr, unsigned int offset)
{
	struct rtmac_device *rtmac = tdma->rtmac;
	struct rtnet_device *rtdev = rtmac->rtdev;
	struct rtskb *skb;
	struct tdma_offset_msg *offset_msg;
	void *data = &offset_msg;
	struct tdma_rt_entry *rt_entry;
	struct list_head *lh;

	list_for_each(lh, &tdma->rt_list) {
		rt_entry = list_entry(lh, struct tdma_rt_entry, list);

		if (rt_entry->arp->ip_addr == ip_addr) {
			skb = tdma_make_msg(rtdev, rt_entry->arp->hw_addr, REQUEST_CHANGE_OFFSET, data);
			
			offset_msg->offset = offset;

			rtdev->rtmac->packet_tx(skb, rtdev);
			break;
		}
	}
}


static void tdma_client_rcvd_conf(struct rtmac_tdma *tdma, struct rtskb *skb)
{
	struct rtskb *new_skb;
	struct tdma_conf_msg *conf_req = (struct tdma_conf_msg *)skb->data;
	struct tdma_conf_msg *conf_ack;
	struct rt_arp_table_struct *arp_entry;
	void *data = &conf_ack;

	/*
	 * copy configuration from incoming packet to local config
	 */
	tdma->station = conf_req->station;			// 1 byte value (=no byte swapping needed)
	tdma->cycle = ntohl(conf_req->cycle);
	tdma->mtu = ntohs(conf_req->mtu);

	TDMA_DEBUG(5, "RTmac: tmda: "__FUNCTION__"() received conf request station %d, cycle %d, mtu %d\n",
		   tdma->station, tdma->cycle, tdma->mtu);

	/*
	 * make rarp lookup with masters mac address and save it 
	 * if master not found, do not acknowledge and change to down state...
	 */
	arp_entry = rt_rarp_table_lookup(skb->mac.ethernet->h_source);
	if (!arp_entry) {
		rt_printk("RTmac: tdma: master not found in ARP table, not good ;( ... goint into DOWN state\n");
		tdma_cleanup_client_rt(tdma);
		tdma_next_state(tdma, TDMA_DOWN);
		return;
	}
	tdma->master = arp_entry;


	/*
	 * acknowledge conf request
	 */
	TDMA_DEBUG(5, "RTmac: tdma: "__FUNCTION__"() sending conf acknowledge to master %u.%u.%u.%u\n", NIPQUAD(arp_entry->ip_addr));
	new_skb = tdma_make_msg(skb->rtdev, tdma->master->hw_addr, ACK_CONF, data);
	memcpy(conf_ack, conf_req, sizeof(struct tdma_conf_msg));
	//FIXME: crc32
	rtdev_xmit_if(new_skb);

	tdma_timer_start_sent_ack(tdma, TDMA_SENT_CLIENT_ACK_TIMEOUT);

	tdma_next_state(tdma, TDMA_CLIENT_ACK_CONF);
	return;
}



static void tdma_master_rcvd_ack_conf(struct rtmac_tdma *tdma, struct rtskb *skb)
{
	struct tdma_conf_msg *conf_ack = (struct tdma_conf_msg *)skb->data;
	struct tdma_rt_entry *rt_entry = NULL;
	struct list_head *lh;

	/*
	 * iterate through all rt stations...
	 */
	list_for_each(lh, &tdma->rt_list) {
		rt_entry = list_entry(lh, struct tdma_rt_entry, list);
		
		/*
		 * ...if the station number, the MAC address and the state matches
		 * accept the ACK and set state
		 */
		if (conf_ack->station == rt_entry->station &&
		    memcmp(rt_entry->arp->hw_addr, skb->mac.ethernet->h_source, RT_ARP_ADDR_LEN) == 0 &&
		    rt_entry->state == RT_SENT_CONF) {

			TDMA_DEBUG(4, "RTmac: tdma: "__FUNCTION__"() received config acknowledge from IP %u.%u.%u.%u\n",
				   NIPQUAD(rt_entry->arp->ip_addr));
			rt_entry->state = RT_RCVD_CONF;
			return;
		}
	}


	/*
	 * print a nice error message...
	 */
	{
		struct rt_arp_table_struct *arp_entry;

		arp_entry = rt_rarp_table_lookup(skb->mac.ethernet->h_source);
		if (arp_entry)
			rt_printk("RTmac: tdma *** WARNING *** "__FUNCTION__"() received client ack from unknown client IP %u.%u.%u.%u\n",
				  NIPQUAD(rt_entry->arp->ip_addr));
		else
			rt_printk("RTmac: tdma *** WARNING *** "__FUNCTION__"() received client ack from unknown client\n");
			
	}
}



static void tdma_client_rcvd_test(struct rtmac_tdma *tdma, struct rtskb *skb)
{
	struct rtskb *new_skb;
	struct rtnet_device *rtdev = skb->rtdev;
	struct tdma_test_msg *test_msg = (struct tdma_test_msg *)skb->data;
	struct tdma_test_msg *test_ack;
	void *data = &test_ack;

	/*
	 * Check if we receives packer from real master....
	 */
	if (memcmp(skb->mac.ethernet->h_source, tdma->master->hw_addr, RT_ARP_ADDR_LEN) != 0) {
		rt_printk("RTmac: tdma: "__FUNCTION__"() received test packet from wrong master\n");
		return;
	}
	
	TDMA_DEBUG(6, "RTmac: tdma: "__FUNCTION__"() received test packet from master\n");
	rt_printk("RTmac: tdma: received test packet from master...\n");

	/*
	 * copy data from received to transmitting buffer....
	 */
	new_skb = tdma_make_msg(rtdev, tdma->master->hw_addr, ACK_TEST, data);
	memcpy(test_ack, test_msg, sizeof(struct tdma_test_msg));

	/*
	 * ...and send it
	 * FIXME: note: transmit, even if mac is active...
	 */
	rtdev->rtmac->packet_tx(new_skb, rtdev);

	TDMA_DEBUG(6, "RTmac: tdma: "__FUNCTION__"() sending test packet back to master %u.%u.%u.%u\n", NIPQUAD(tdma->master->ip_addr));
}



static void tdma_client_rcvd_station_list(struct rtmac_tdma *tdma, struct rtskb *skb)
{
	struct tdma_station_list_hdr *station_list_hdr = (struct tdma_station_list_hdr *)skb->data;
	struct tdma_station_list *station_list_ptr;
	unsigned char no_of_stations;
	int i;

	no_of_stations = MIN(station_list_hdr->no_of_stations, TDMA_MAX_RT);
	station_list_hdr++;
	station_list_ptr = (struct tdma_station_list *)station_list_hdr;

	for (i = 1; i <= no_of_stations; i++) {
		TDMA_DEBUG(2, "RTmac: tdma: recevied station %d, IP %u.%u.%u.%u\n",
			   station_list_ptr->station, NIPQUAD(station_list_ptr->ip_addr));
		
		tdma_client_add_rt_rate(tdma, station_list_ptr->ip_addr, station_list_ptr->station);

		station_list_ptr++;
	}


	return;
}


static void tdma_client_rcvd_change_offset(struct rtmac_tdma *tdma, struct rtskb *skb)
{
	struct tdma_offset_msg *change_offset = (struct tdma_offset_msg *)skb->data;

	TDMA_DEBUG(2, "RTmac: tdma: recieved change offset %d\n", change_offset->offset);
	tdma->offset = nano2count(change_offset->offset * 1000);

	return;
}


static void tdma_rcvd_sof(struct rtmac_tdma *tdma, struct rtskb *skb)
{
	tdma->wakeup = skb->rx + tdma->offset;
	tdma->delta_t = be64_to_cpu(*(RTIME *)(skb->data)) - count2nano(skb->rx);
	
	/* rt_sem_broadcast() will wake up all tasks, which are waiting
	   for SOF, inclusive tdma_task_client. This allows application
	   softwares to do some tasks when SOF arrives.

	   NOTE: rt_sem_broadcast() does not unlock the semaphore. It just
	   wakes up all tasks that are, at the moment, waiting on the sempahore.
	   -WY-
	 */
	rt_sem_broadcast(&tdma->client_tx);
}




/******************************	master - stuff **********************************/
static void tdma_send_conf(struct rtmac_tdma *tdma, void *hw_addr, unsigned char station)
{
	struct rtmac_device *rtmac = tdma->rtmac;
	struct rtnet_device *rtdev = rtmac->rtdev;
	struct rtskb *skb;
	struct tdma_conf_msg *conf_req;
	void *data = &conf_req;
	int ret;

	/*
	 * make skb containing 'request client conf' message
	 */
	skb = tdma_make_msg(rtdev, hw_addr, REQUEST_CONF, data);
	if (skb == NULL)
		return;

	/*
	 * fill the skb with the config data
	 */
	conf_req->station = station;			// 1 byte value (=no byte swapping needed)
	conf_req->cycle = htonl(tdma->cycle);
	conf_req->mtu = htons(tdma->mtu);
	//FIXME: add crc32?

	/*
	 * transmit packet
	 */
	ret = rtdev_xmit_if(skb);
	
	return;
}



static void tdma_make_station_list(struct rtmac_tdma *tdma, void *data)
{
	struct list_head *lh_rate;
	struct tdma_rt_entry *rt_entry;
	struct tdma_station_list *station_list_ptr = (struct tdma_station_list *)data;
	
	memset(data, 0, TDMA_MAX_RT * sizeof(struct tdma_station_list));

	list_for_each(lh_rate, &tdma->rt_list_rate) {
		rt_entry = list_entry(lh_rate, struct tdma_rt_entry, list_rate);
		/*
		 * print sorted list for debugging purpose
		 */
		TDMA_DEBUG(4, "RTmac: tdma: "__FUNCTION__"() sorted: station %d, IP %u.%u.%u.%u, rtt %d us\n",
			   rt_entry->station, NIPQUAD(rt_entry->arp->ip_addr), rt_entry->rtt);

		station_list_ptr->ip_addr = rt_entry->arp->ip_addr;
		station_list_ptr->station = rt_entry->station;
		
		station_list_ptr++;
	}
}



static void tdma_send_station_list(struct rtmac_tdma *tdma, void *hw_addr, void *station_list)
{
	struct rtmac_device *rtmac = tdma->rtmac;
	struct rtnet_device *rtdev = rtmac->rtdev;
	struct rtskb *skb;
	struct tdma_station_list_hdr *station_list_hdr;
	void *data = &station_list_hdr;
	unsigned char no_of_stations = list_len(&tdma->rt_list);

	/*
	 * make skb containing 'station list'
	 */
	skb = tdma_make_msg_len(rtdev, hw_addr, STATION_LIST,
				sizeof(struct rtmac_hdr) + sizeof(struct tdma_hdr) +
				sizeof(struct tdma_station_list_hdr) + no_of_stations * sizeof(struct tdma_station_list),
				data);
	if (skb == NULL)
		return;
	

	/*
	 * copy the station list into the skb
	 */
	station_list_hdr->no_of_stations = no_of_stations;		// 1 byte value (=no byte swapping needed)
	station_list_hdr++;
	memcpy(station_list_hdr, station_list, no_of_stations * sizeof(struct tdma_station_list));
		
	/*
	 * send packet
	 */
	rtmac->packet_tx(skb, rtdev);
	
	return;
}



/******************************	state - template ********************************
static int tdma_state_down(struct rtmac_tdma *tdma, TDMA_EVENT event, struct tdma_info *info)
{
	int ret = 0;


	switch(event) {
	case :
		
		break;
	default:
		TDMA_DEBUG(2, "RTmac: tdma: "__FUNCTION__"(), Unknown event %s\n", tdma_event[event]);
		break;
	}

	return ret;
}
*/

/* *** MASTER ***
	//FIXME: tdma_task_change(tdma, &tdma_task_config);
	ret = tdma_task_change(rtdev, &tdma_task_master, tdma->cycle * 1000);
	if( ret != 0 )
		return ret;

	tdma->flags.mac_active = 1;	//reset in cleanup
*/

/* *** CLIENT ***
	tdma->flags.mac_active = 1;	//reset in cleanup
	tdma_task_change(info->rtdev, &tdma_task_client, 0);
*/
