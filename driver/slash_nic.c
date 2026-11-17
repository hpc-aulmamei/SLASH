/**
 * Copyright (C) 2026 Advanced Micro Devices, Inc. All rights reserved.
 * This program is free software; you can redistribute it and/or modify it under the terms of the
 * GNU General Public License as published by the Free Software Foundation; version 2.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without
 * even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program; if
 * not, write to the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

/**
 * @file slash_nic.c
 *
 * Network devices for the SLASH NIC RM (nic/rtl/nic_core.sv).
 *
 * nic_core sits at PF2 BAR2 + 0x0400_0000 and has two ports: port 0 = QSFP0
 * (DCMAC0, BAR2 + 0x0200_0000), port 1 = QSFP2 (DCMAC1, BAR2 + 0x0300_0000).
 * Each port becomes one netdev, sn<card>p<port>.
 *
 * Is the NIC RM loaded? (handoff-nic/NETDEV_PLAN.md, section 2.1)
 *   1. attach (sysfs `nic` on PF2) identifies nic_core: ID "NIC1", a known
 *      VERSION, SCRATCH write/read-back, CYCLES advancing, no all-ones read;
 *      otherwise no netdev is created;
 *   2. the attach lives as long as the PF1 and PF2 bindings: every RM reload
 *      removes the functions, which detaches;
 *   3. a 1 s watchdog re-reads ID and CYCLES; on a mismatch the netdevs are
 *      detached and unregistered and the driver stops touching BAR2.
 *
 * Datapath (one ST queue each way per port, PF1's libqdma, POLL_MODE):
 *   - qpair ids 248..251 are reserved in slash_qdma at attach: H2C of port p
 *     is qid 248 + p (TX_QBASE, TX_QCNT 1), C2H of port p is qid 250 + p
 *     (RX_QID). The queues exist between ndo_open and ndo_stop.
 *   - TX: each frame is copied into one 4 KiB DMA page of a per-port ring
 *     (one page = one H2C descriptor = one frame on the wire) and submitted
 *     asynchronously; fp_done frees the slot.
 *   - RX: libqdma calls snic_rx_packet() for each completion while the NAPI
 *     poll services the C2H queue; the frame length comes from the
 *     completion entry (the RM pads the C2H data to 64 B beats).
 *   - Both queues are kept off libqdma's threads (patch 0005, uld_service);
 *     a per-port hrtimer (nic_poll_us) schedules the NAPI poll, which reaps
 *     the H2C completions and the C2H packets and rings the C2H doorbells.
 *   - ndo_open programs the RM port (queues, MAC, PCTRL); ndo_stop clears
 *     PCTRL first, drains, then stops the queues.
 *
 * Locking: card->lock (attach, detach, watchdog) is taken before the RTNL
 * (register/unregister_netdev); netdev and ethtool callbacks never take
 * card->lock. The watchdog only try-locks card->lock, so detach can cancel
 * it synchronously while holding the lock. The TX ring has one producer
 * (ndo_start_xmit) and one consumer (fp_done, always under the H2C queue's
 * libqdma lock).
 */

#include "slash_nic.h"

#include "slash.h"
#include "libqdma_export.h"

#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/hrtimer.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/u64_stats_sync.h>
#include <linux/version.h>
#include <linux/workqueue.h>

static unsigned int nic_poll_us = 50;
module_param(nic_poll_us, uint, 0644);
MODULE_PARM_DESC(nic_poll_us, "NIC netdev poll period in us (5..10000, read at ifup; default 50)");

/* ─── RM register map (nic/rtl/nic_core.sv, nic/sw/nic_raw.py) ─────────── */

#define SNIC_BAR             2
#define SNIC_CORE_OFF        0x04000000u
#define SNIC_CORE_LEN        0x00010000u
#define SNIC_BAR_MIN_LEN     (SNIC_CORE_OFF + SNIC_CORE_LEN)

#define SNIC_ID              0x000
#define SNIC_VERSION         0x004
#define SNIC_SCRATCH         0x008
#define SNIC_BUILD           0x00C
#define SNIC_CYCLES          0x010          /* 64-bit, LO here */

#define SNIC_ID_NIC1         0x4E494331u    /* "NIC1" */
#define SNIC_VERSION_MAJOR   1u
/*
 * 0x00010003: H2C store and forward. Before it, nic_core took the port from the
 * qid on a packet's first beat, but the shell gives the qid on the tlast beat
 * only: every frame longer than 64 B on a qid other than 0 was dropped
 * (H2C_NOPORT; hardware 2026-11-16, netdev b2).
 */
#define SNIC_VERSION_MIN     0x00010003u

#define SNIC_PORT(p)         (0x100u + 0x100u * (p))
#define SNIC_P_PCTRL         0x00
#define SNIC_P_RX_QID        0x04
#define SNIC_P_TX_QBASE      0x08
#define SNIC_P_TX_QCNT       0x0C
#define SNIC_P_MAC_LO        0x10           /* bytes 2..5, [31:24] byte 2 */
#define SNIC_P_MAC_HI        0x14           /* bytes 0..1, [15:8] byte 0 */
#define SNIC_P_PHY_STATUS    0x1C
#define SNIC_P_RX_DROPS      0x20           /* [11:0] bad FCS, [15:12] overflow */
#define SNIC_PCTRL_RX_EN     BIT(0)
#define SNIC_PCTRL_TX_EN     BIT(1)
#define SNIC_PCTRL_PROMISC   BIT(2)
#define SNIC_PHY_RATE_100G   BIT(9)

/* DCMAC port 0 block of each DCMAC (channel 0), read-only status used here */
static const u32 snic_dcmac_off[2] = { 0x02000000u, 0x03000000u };
#define SNIC_DCMAC_BLK       0x1000u
#define SNIC_DCMAC_BLK_LEN   0x1000u
#define SNIC_D_TX_MAC_RT     0x104          /* bit 0: TX MAC fault */
#define SNIC_D_RX_PHY_RT     0xC04          /* bit 0 status, bit 2 aligned */
#define SNIC_RD_BAD          0x0BAD0BADu    /* DCMAC register read while it reconfigures */

#define SNIC_PORTS           2
#define SNIC_BUF_LEN         4096u          /* one C2H buffer, one TX page, the RM's MAX_LEN */
#define SNIC_MTU_MAX         (SNIC_BUF_LEN - ETH_HLEN)
#define SNIC_FRAME_MIN       60u            /* without FCS */
#define SNIC_WATCH_PERIOD    HZ

#define SNIC_QID_BASE        248u           /* H2C of port p: 248 + p; C2H: 250 + p */
#define SNIC_QIDS            (2u * SNIC_PORTS)
#define SNIC_QID_TX(p)       (SNIC_QID_BASE + (p))
#define SNIC_QID_RX(p)       (SNIC_QID_BASE + SNIC_PORTS + (p))
#define SNIC_TX_SLOTS        256u           /* power of 2, below the H2C ring (2047) */
#define SNIC_TX_WAKE         (SNIC_TX_SLOTS / 4)

/* per-port 64-bit counters of nic_core (LO at off, HI at off + 4) */
static const struct {
    const char *name;
    u32 off;
} snic_hw_stats[] = {
    { "hw_tx_frames",     0x40 }, { "hw_tx_bytes",     0x48 },
    { "hw_rx_frames",     0x50 }, { "hw_rx_bytes",     0x58 },
    { "hw_rx_host",       0x60 }, { "hw_rx_filtered",  0x68 },
    { "hw_rx_toolong",    0x70 }, { "hw_hairpin",      0x78 },
    { "hw_tx_h2c",        0x80 }, { "hw_tx_drop",      0x88 },
    { "hw_c2h_drop",      0x90 }, { "hw_rx_malformed", 0x98 },
};
#define SNIC_HW_STATS        ARRAY_SIZE(snic_hw_stats)
enum { HW_TX_FRAMES, HW_TX_BYTES, HW_RX_FRAMES, HW_RX_BYTES, HW_RX_HOST, HW_RX_FILTERED,
       HW_RX_TOOLONG, HW_HAIRPIN, HW_TX_H2C, HW_TX_DROP, HW_C2H_DROP, HW_RX_MALFORMED };
static const char * const snic_sw_stats[] = {
    "rx_path_bad_fcs", "rx_path_overflow", "tx_dropped", "tx_errors", "rx_dropped",
    "rx_length_errors", "rx_queue_fault",
};
#define SNIC_SW_STATS        ARRAY_SIZE(snic_sw_stats)

/* ─── state ───────────────────────────────────────────────────────────── */

struct slash_nic_card;
struct slash_nic_port;

/* one TX frame: its own DMA-mapped page, request and one-entry SGL */
struct snic_tx_slot {
    struct qdma_request req;
    struct qdma_sw_sg sg;
    struct page *page;
    dma_addr_t dma;
    struct slash_nic_port *pt;
};

struct slash_nic_port {
    struct slash_nic_card *card;
    unsigned int idx;
    struct net_device *ndev;
    void __iomem *dcmac;                /* DCMAC port 0 block */

    spinlock_t stats_lock;              /* the cached values below */
    u64 hw[SNIC_HW_STATS];
    u32 rx_drops;
    bool rate_100g;
    u64 tx_dropped;                     /* host side, under stats_lock */

    /* datapath, between ndo_open and ndo_stop */
    unsigned long qh_tx;                /* libqdma handles, QDMA_QUEUE_IDX_INVALID if none */
    unsigned long qh_rx;
    bool dp_up;                         /* xmit and wake allowed */
    bool rx_drain;                      /* ndo_stop: the RX callback drops */
    bool rx_fault;                      /* the C2H queue reported an error: no more RX service */
    bool promisc;
    struct napi_struct napi;
    struct hrtimer timer;
    ktime_t period;
    struct sk_buff_head rxq;            /* filled by snic_rx_packet() inside a service call */
    unsigned int rx_seen;               /* completions consumed since the last doorbell */
    struct snic_tx_slot *tx;
    unsigned int tx_head;               /* written by xmit only */
    unsigned int tx_done;               /* written by fp_done only */

    struct u64_stats_sync tx_syncp;     /* xmit */
    u64 tx_packets;
    u64 tx_bytes;
    struct u64_stats_sync rx_syncp;     /* NAPI */
    u64 rx_packets;
    u64 rx_bytes;
    u64 rx_dropped;
    u64 rx_length_errors;
    atomic64_t tx_errors;               /* fp_done with an error */
};

struct slash_nic_priv {
    struct slash_nic_port *port;
};

struct slash_nic_card {
    struct list_head node;
    int domain;
    unsigned int bus;
    unsigned int slot;
    int board;

    struct pci_dev *pf1;
    struct pci_dev *pf2;
    unsigned long qdma_handle;

    struct mutex lock;                  /* everything below */
    bool attached;
    bool healthy;                       /* RM reachable; read lock-free by netdev callbacks */
    bool attr;                          /* sysfs attribute created on pf2 */
    bool qids;                          /* qpair ids reserved in slash_qdma */
    unsigned int gen;                   /* attach count, for SCRATCH patterns */
    void __iomem *core;
    u32 version;
    u32 build;
    u32 last_cycles;
    char reason[128];                   /* why the last attach failed / detach happened */

    struct delayed_work watch;
    struct slash_nic_port port[SNIC_PORTS];
};

static LIST_HEAD(snic_cards);
static DEFINE_MUTEX(snic_cards_lock);

/* ─── card registry ───────────────────────────────────────────────────── */

static bool snic_same_card(const struct slash_nic_card *c, const struct pci_dev *pdev)
{
    return c->domain == pci_domain_nr(pdev->bus) && c->bus == pdev->bus->number &&
           c->slot == PCI_SLOT(pdev->devfn);
}

static void snic_watch(struct work_struct *work);
static enum hrtimer_restart snic_timer_fn(struct hrtimer *t);

/* Find the card of @pdev, optionally creating it. Called with snic_cards_lock. */
static struct slash_nic_card *snic_card_get(struct pci_dev *pdev, bool create)
{
    struct slash_nic_card *c;
    unsigned int p;

    list_for_each_entry(c, &snic_cards, node)
        if (snic_same_card(c, pdev))
            return c;
    if (!create)
        return NULL;

    c = kzalloc(sizeof(*c), GFP_KERNEL);
    if (!c)
        return NULL;
    c->domain = pci_domain_nr(pdev->bus);
    c->bus = pdev->bus->number;
    c->slot = PCI_SLOT(pdev->devfn);
    c->board = -1;
    mutex_init(&c->lock);
    INIT_DELAYED_WORK(&c->watch, snic_watch);
    for (p = 0; p < SNIC_PORTS; p++) {
        struct slash_nic_port *pt = &c->port[p];

        pt->card = c;
        pt->idx = p;
        spin_lock_init(&pt->stats_lock);
        pt->qh_tx = QDMA_QUEUE_IDX_INVALID;
        pt->qh_rx = QDMA_QUEUE_IDX_INVALID;
        skb_queue_head_init(&pt->rxq);
        u64_stats_init(&pt->tx_syncp);
        u64_stats_init(&pt->rx_syncp);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 13, 0)
        hrtimer_setup(&pt->timer, snic_timer_fn, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
#else
        hrtimer_init(&pt->timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
        pt->timer.function = snic_timer_fn;
#endif
    }
    list_add_tail(&c->node, &snic_cards);
    return c;
}

/* Free @c once neither function is bound. Called with snic_cards_lock. */
static void snic_card_put(struct slash_nic_card *c)
{
    if (c->pf1 || c->pf2)
        return;
    /* detach already cancelled it; the watchdog only try-locks c->lock */
    cancel_delayed_work_sync(&c->watch);
    list_del(&c->node);
    mutex_destroy(&c->lock);
    kfree(c);
}

/* ─── register access ─────────────────────────────────────────────────── */

static inline u32 snic_rd(struct slash_nic_card *c, u32 off)
{
    return ioread32(c->core + off);
}

static inline void snic_wr(struct slash_nic_card *c, u32 off, u32 v)
{
    iowrite32(v, c->core + off);
}

static inline void snic_port_wr(struct slash_nic_port *pt, u32 off, u32 v)
{
    snic_wr(pt->card, SNIC_PORT(pt->idx) + off, v);
}

/* 64-bit counter, LO then HI: re-read when HI moved under us */
static u64 snic_rd64(void __iomem *base, u32 off)
{
    u32 hi, lo, hi2;

    hi = ioread32(base + off + 4);
    lo = ioread32(base + off);
    hi2 = ioread32(base + off + 4);
    if (hi2 != hi)
        lo = ioread32(base + off);
    return (u64)hi2 << 32 | lo;
}

/* ─── is the NIC RM loaded? ───────────────────────────────────────────── */

/*
 * Full identification (attach). Writes SCRATCH only after ID and VERSION
 * matched, and puts its old value back. Returns 0 or -ENODEV with the reason
 * in c->reason.
 */
static int snic_identify(struct slash_nic_card *c)
{
    u32 id, ver, build, saved, pat, cy0, cy1;
    int i;

    id = snic_rd(c, SNIC_ID);
    if (id == 0xFFFFFFFFu) {
        snprintf(c->reason, sizeof(c->reason),
                 "ID reads all ones: nothing answers at BAR2 + 0x%08x", SNIC_CORE_OFF);
        return -ENODEV;
    }
    if (id != SNIC_ID_NIC1) {
        snprintf(c->reason, sizeof(c->reason), "ID 0x%08x is not \"NIC1\" (0x%08x)", id,
                 SNIC_ID_NIC1);
        return -ENODEV;
    }
    ver = snic_rd(c, SNIC_VERSION);
    if (ver == 0xFFFFFFFFu || ver >> 16 != SNIC_VERSION_MAJOR || ver < SNIC_VERSION_MIN) {
        snprintf(c->reason, sizeof(c->reason),
                 "nic_core VERSION 0x%08x not supported (major %u, at least 0x%08x)", ver,
                 SNIC_VERSION_MAJOR, SNIC_VERSION_MIN);
        return -ENODEV;
    }
    build = snic_rd(c, SNIC_BUILD);

    saved = snic_rd(c, SNIC_SCRATCH);
    for (i = 0; i < 2; i++) {
        pat = i ? ~0xA5A55A5Au ^ (c->gen + 1) : 0xA5A55A5Au;
        snic_wr(c, SNIC_SCRATCH, pat);
        if (snic_rd(c, SNIC_SCRATCH) != pat) {
            snic_wr(c, SNIC_SCRATCH, saved);
            snprintf(c->reason, sizeof(c->reason),
                     "SCRATCH does not read back 0x%08x: not a live nic_core", pat);
            return -ENODEV;
        }
    }
    snic_wr(c, SNIC_SCRATCH, saved);

    cy0 = snic_rd(c, SNIC_CYCLES);
    udelay(20);
    cy1 = snic_rd(c, SNIC_CYCLES);
    if (cy0 == cy1 || cy1 == 0xFFFFFFFFu) {
        snprintf(c->reason, sizeof(c->reason),
                 "CYCLES does not advance (0x%08x, 0x%08x): the RM clock is stopped", cy0, cy1);
        return -ENODEV;
    }
    c->last_cycles = cy1;
    c->version = ver;
    c->build = build;
    return 0;
}

/* Watchdog check: still nic_core, still clocked. No writes. */
static int snic_alive(struct slash_nic_card *c)
{
    u32 id = snic_rd(c, SNIC_ID);
    u32 cy = snic_rd(c, SNIC_CYCLES);

    if (id != SNIC_ID_NIC1) {
        snprintf(c->reason, sizeof(c->reason), "NIC RM gone: ID reads 0x%08x", id);
        return -ENODEV;
    }
    if (cy == c->last_cycles || cy == 0xFFFFFFFFu) {
        snprintf(c->reason, sizeof(c->reason), "NIC RM gone: CYCLES stuck at 0x%08x", cy);
        return -ENODEV;
    }
    c->last_cycles = cy;
    return 0;
}

/* ─── link and counters (watchdog context) ────────────────────────────── */

static bool snic_bad_read(u32 v)
{
    return v == 0xFFFFFFFFu || v == SNIC_RD_BAD;
}

static void snic_port_poll(struct slash_nic_port *pt)
{
    struct slash_nic_card *c = pt->card;
    void __iomem *pbase = c->core + SNIC_PORT(pt->idx);
    u32 rx = ioread32(pt->dcmac + SNIC_D_RX_PHY_RT);
    u32 tx = ioread32(pt->dcmac + SNIC_D_TX_MAC_RT);
    u32 st = ioread32(pbase + SNIC_P_PHY_STATUS);
    bool up = (rx & BIT(0)) && (rx & BIT(2)) && !(tx & BIT(0));
    u64 hw[SNIC_HW_STATS];
    u32 drops = ioread32(pbase + SNIC_P_RX_DROPS);
    unsigned int i;

    /* the DCMAC answers 0x0bad0bad while nic_raw.py reconfigures it: no sample */
    if (snic_bad_read(rx) || snic_bad_read(tx) || snic_bad_read(st))
        return;

    for (i = 0; i < SNIC_HW_STATS; i++)
        hw[i] = snic_rd64(pbase, snic_hw_stats[i].off);

    spin_lock_bh(&pt->stats_lock);
    memcpy(pt->hw, hw, sizeof(hw));
    pt->rx_drops = drops;
    pt->rate_100g = !!(st & SNIC_PHY_RATE_100G);
    spin_unlock_bh(&pt->stats_lock);

    if (up && !netif_carrier_ok(pt->ndev)) {
        netif_carrier_on(pt->ndev);
        netdev_info(pt->ndev, "link up, %s\n", (st & SNIC_PHY_RATE_100G) ? "100G" : "200G");
    } else if (!up && netif_carrier_ok(pt->ndev)) {
        netif_carrier_off(pt->ndev);
        netdev_info(pt->ndev, "link down (rx phy 0x%03x, tx mac 0x%02x)\n", rx, tx);
    }
}

/* ─── attach / detach ─────────────────────────────────────────────────── */

static void snic_unmap(struct slash_nic_card *c)
{
    unsigned int p;

    for (p = 0; p < SNIC_PORTS; p++) {
        if (c->port[p].dcmac)
            pci_iounmap(c->pf2, c->port[p].dcmac);
        c->port[p].dcmac = NULL;
    }
    if (c->core)
        pci_iounmap(c->pf2, c->core);
    c->core = NULL;
}

/* No frame in or out of either port (RM reachable, c->core mapped). */
static void snic_ports_quiet(struct slash_nic_card *c)
{
    unsigned int p;

    for (p = 0; p < SNIC_PORTS; p++)
        snic_wr(c, SNIC_PORT(p) + SNIC_P_PCTRL, 0);
}

static void snic_free_netdevs(struct slash_nic_card *c)
{
    unsigned int p;

    for (p = 0; p < SNIC_PORTS; p++) {
        struct net_device *ndev = c->port[p].ndev;

        if (!ndev)
            continue;
        if (ndev->reg_state == NETREG_REGISTERED)
            unregister_netdev(ndev);    /* runs ndo_stop if it is up */
        netif_napi_del(&c->port[p].napi);
        free_netdev(ndev);
        c->port[p].ndev = NULL;
    }
}

/*
 * Tear down the netdevs and the BAR mappings. Called with c->lock held.
 * @from_watch: called by the watchdog itself, which must not cancel itself
 * (and which found the RM gone: c->healthy is already false).
 */
static void snic_detach_locked(struct slash_nic_card *c, bool from_watch, const char *why)
{
    unsigned int p;

    if (!c->attached)
        return;
    c->attached = false;
    if (!from_watch)
        cancel_delayed_work_sync(&c->watch);

    for (p = 0; p < SNIC_PORTS; p++)
        if (c->port[p].ndev)
            netif_carrier_off(c->port[p].ndev);
    /* ndo_stop of an up port clears its PCTRL while the RM is still reachable */
    snic_free_netdevs(c);
    if (READ_ONCE(c->healthy))
        snic_ports_quiet(c);
    WRITE_ONCE(c->healthy, false);
    if (c->qids)
        slash_qdma_release_qids(c->pf1, SNIC_QID_BASE, SNIC_QIDS);
    c->qids = false;
    snic_unmap(c);
    if (why)
        strscpy(c->reason, why, sizeof(c->reason));
    dev_info(&c->pf2->dev, "slash: nic: detached (%s)\n", c->reason);
}

static void snic_watch(struct work_struct *work)
{
    struct slash_nic_card *c = container_of(to_delayed_work(work), struct slash_nic_card, watch);
    unsigned int p;

    /* detach holds the lock while it cancels us: never block on it */
    if (!mutex_trylock(&c->lock)) {
        schedule_delayed_work(&c->watch, HZ / 10);
        return;
    }
    if (!c->attached)
        goto out;

    if (snic_alive(c)) {
        /* stop using the RM at once, then take the ports down */
        WRITE_ONCE(c->healthy, false);
        for (p = 0; p < SNIC_PORTS; p++) {
            if (c->port[p].ndev) {
                netif_device_detach(c->port[p].ndev);
                netif_carrier_off(c->port[p].ndev);
            }
        }
        dev_err(&c->pf2->dev, "slash: nic: %s; detaching\n", c->reason);
        snic_detach_locked(c, true, NULL);
        goto out;
    }

    for (p = 0; p < SNIC_PORTS; p++)
        snic_port_poll(&c->port[p]);
    schedule_delayed_work(&c->watch, SNIC_WATCH_PERIOD);
out:
    mutex_unlock(&c->lock);
}

static const struct net_device_ops snic_netdev_ops;
static const struct ethtool_ops snic_ethtool_ops;
static int snic_poll(struct napi_struct *napi, int budget);

static struct net_device *snic_netdev_create(struct slash_nic_port *pt)
{
    struct slash_nic_card *c = pt->card;
    struct slash_nic_priv *priv;
    struct net_device *ndev;
    char name[IFNAMSIZ];
    u8 mac[ETH_ALEN] = { 0x02, 0x0a, 0x35, 0x00, 0x00, 0x00 };

    snprintf(name, sizeof(name), "sn%dp%u", c->board, pt->idx);
    ndev = alloc_netdev_mqs(sizeof(*priv), name, NET_NAME_PREDICTABLE, ether_setup, 1, 1);
    if (!ndev)
        return NULL;
    priv = netdev_priv(ndev);
    priv->port = pt;

    SET_NETDEV_DEV(ndev, &c->pf1->dev);
    ndev->netdev_ops = &snic_netdev_ops;
    ndev->ethtool_ops = &snic_ethtool_ops;
    ndev->dev_port = pt->idx;
    ndev->min_mtu = ETH_MIN_MTU;
    ndev->max_mtu = SNIC_MTU_MAX;
    ndev->mtu = ETH_DATA_LEN;
    ndev->priv_flags |= IFF_LIVE_ADDR_CHANGE;
    /* 02:0a:35:<card>:00:<port>: locally administered, stable across reloads */
    mac[3] = (u8)c->board;
    mac[5] = (u8)pt->idx;
    eth_hw_addr_set(ndev, mac);
    netif_carrier_off(ndev);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 1, 0)
    netif_napi_add(ndev, &pt->napi, snic_poll);
#else
    netif_napi_add(ndev, &pt->napi, snic_poll, NAPI_POLL_WEIGHT);
#endif
    return ndev;
}

/* Called with c->lock held. */
static int snic_attach_locked(struct slash_nic_card *c)
{
    unsigned int p;
    int err;

    if (c->attached)
        return 0;
    if (!c->pf1 || !c->pf2) {
        snprintf(c->reason, sizeof(c->reason), "PF%d not bound to slash", c->pf1 ? 2 : 1);
        return -ENODEV;
    }
    if (pci_resource_len(c->pf2, SNIC_BAR) < SNIC_BAR_MIN_LEN) {
        snprintf(c->reason, sizeof(c->reason), "PF2 BAR2 is 0x%llx bytes, too small for nic_core",
                 (unsigned long long)pci_resource_len(c->pf2, SNIC_BAR));
        return -ENODEV;
    }

    c->core = pci_iomap_range(c->pf2, SNIC_BAR, SNIC_CORE_OFF, SNIC_CORE_LEN);
    if (!c->core) {
        snprintf(c->reason, sizeof(c->reason), "cannot map BAR2 + 0x%08x", SNIC_CORE_OFF);
        return -ENOMEM;
    }
    err = snic_identify(c);
    if (err)
        goto err_unmap;

    for (p = 0; p < SNIC_PORTS; p++) {
        struct slash_nic_port *pt = &c->port[p];

        pt->dcmac = pci_iomap_range(c->pf2, SNIC_BAR, snic_dcmac_off[p] + SNIC_DCMAC_BLK,
                                    SNIC_DCMAC_BLK_LEN);
        if (!pt->dcmac) {
            snprintf(c->reason, sizeof(c->reason), "cannot map DCMAC%u registers", p);
            err = -ENOMEM;
            goto err_unmap;
        }
        memset(pt->hw, 0, sizeof(pt->hw));
        pt->tx_dropped = 0;
        pt->tx_packets = pt->tx_bytes = 0;
        pt->rx_packets = pt->rx_bytes = pt->rx_dropped = pt->rx_length_errors = 0;
        atomic64_set(&pt->tx_errors, 0);
        pt->rx_fault = false;
        pt->promisc = false;
    }

    err = slash_qdma_reserve_qids(c->pf1, SNIC_QID_BASE, SNIC_QIDS);
    if (err) {
        snprintf(c->reason, sizeof(c->reason), "QDMA queue ids %u..%u %s (%d)", SNIC_QID_BASE,
                 SNIC_QID_BASE + SNIC_QIDS - 1,
                 err == -EBUSY ? "in use by user queue pairs" : "not available", err);
        goto err_unmap;
    }
    c->qids = true;
    /* the ports stay quiet until their netdev is up (nic_raw.py leaves rx_en, tx_en set) */
    snic_ports_quiet(c);

    for (p = 0; p < SNIC_PORTS; p++) {
        c->port[p].ndev = snic_netdev_create(&c->port[p]);
        if (!c->port[p].ndev) {
            snprintf(c->reason, sizeof(c->reason), "netdev allocation failed");
            err = -ENOMEM;
            goto err_netdevs;
        }
    }
    WRITE_ONCE(c->healthy, true);
    for (p = 0; p < SNIC_PORTS; p++) {
        err = register_netdev(c->port[p].ndev);
        if (err) {
            snprintf(c->reason, sizeof(c->reason), "register_netdev %s failed: %d",
                     c->port[p].ndev->name, err);
            goto err_netdevs;
        }
    }

    c->attached = true;
    c->gen++;
    c->reason[0] = '\0';
    schedule_delayed_work(&c->watch, 0);
    dev_info(&c->pf2->dev, "slash: nic: attached: nic_core version 0x%08x build %u, %s %s\n",
             c->version, c->build, c->port[0].ndev->name, c->port[1].ndev->name);
    return 0;

err_netdevs:
    snic_free_netdevs(c);
    WRITE_ONCE(c->healthy, false);
    slash_qdma_release_qids(c->pf1, SNIC_QID_BASE, SNIC_QIDS);
    c->qids = false;
err_unmap:
    snic_unmap(c);
    dev_err(&c->pf2->dev, "slash: nic: attach refused: %s\n", c->reason);
    return err;
}

/* ─── datapath ────────────────────────────────────────────────────────── */

static struct slash_nic_port *snic_port(struct net_device *ndev)
{
    return ((struct slash_nic_priv *)netdev_priv(ndev))->port;
}

static struct device *snic_dma_dev(struct slash_nic_port *pt)
{
    return &pt->card->pf1->dev;
}

static void snic_tx_ring_free(struct slash_nic_port *pt)
{
    unsigned int i;

    if (!pt->tx)
        return;
    for (i = 0; i < SNIC_TX_SLOTS; i++) {
        struct snic_tx_slot *s = &pt->tx[i];

        if (!s->page)
            continue;
        if (s->dma)
            dma_unmap_page(snic_dma_dev(pt), s->dma, PAGE_SIZE, DMA_TO_DEVICE);
        __free_page(s->page);
    }
    kfree(pt->tx);
    pt->tx = NULL;
}

static int snic_tx_ring_alloc(struct slash_nic_port *pt)
{
    struct device *dev = snic_dma_dev(pt);
    int node = dev_to_node(dev);
    unsigned int i;

    BUILD_BUG_ON(PAGE_SIZE < SNIC_BUF_LEN);
    BUILD_BUG_ON(SNIC_TX_SLOTS & (SNIC_TX_SLOTS - 1));

    pt->tx = kcalloc_node(SNIC_TX_SLOTS, sizeof(*pt->tx), GFP_KERNEL, node);
    if (!pt->tx)
        return -ENOMEM;
    for (i = 0; i < SNIC_TX_SLOTS; i++) {
        struct snic_tx_slot *s = &pt->tx[i];
        dma_addr_t dma;

        s->pt = pt;
        s->page = alloc_pages_node(node, GFP_KERNEL, 0);
        if (!s->page)
            goto err;
        dma = dma_map_page(dev, s->page, 0, PAGE_SIZE, DMA_TO_DEVICE);
        if (dma_mapping_error(dev, dma))
            goto err;
        s->dma = dma;
    }
    return 0;
err:
    snic_tx_ring_free(pt);
    return -ENOMEM;
}

static unsigned int snic_tx_inflight(struct slash_nic_port *pt)
{
    return pt->tx_head - smp_load_acquire(&pt->tx_done);
}

/* libqdma fp_done: under the H2C queue's lock, in xmit, NAPI or queue stop. */
static int snic_tx_done(struct qdma_request *req, unsigned int bytes_done, int err)
{
    struct snic_tx_slot *s = container_of(req, struct snic_tx_slot, req);
    struct slash_nic_port *pt = s->pt;

    if (unlikely(err))
        atomic64_inc(&pt->tx_errors);
    smp_store_release(&pt->tx_done, pt->tx_done + 1);
    smp_mb();
    if (unlikely(netif_queue_stopped(pt->ndev)) && READ_ONCE(pt->dp_up) &&
        SNIC_TX_SLOTS - snic_tx_inflight(pt) >= SNIC_TX_WAKE)
        netif_wake_queue(pt->ndev);
    return 0;
}

static void snic_tx_drop(struct slash_nic_port *pt, struct sk_buff *skb)
{
    spin_lock(&pt->stats_lock);
    pt->tx_dropped++;
    spin_unlock(&pt->stats_lock);
    dev_kfree_skb_any(skb);
}

static netdev_tx_t snic_start_xmit(struct sk_buff *skb, struct net_device *ndev)
{
    struct slash_nic_port *pt = snic_port(ndev);
    struct slash_nic_card *c = pt->card;
    unsigned int len = skb->len, head = pt->tx_head;
    struct snic_tx_slot *s;
    u8 *va;
    ssize_t rv;

    if (unlikely(!READ_ONCE(pt->dp_up) || len > SNIC_BUF_LEN || !len)) {
        snic_tx_drop(pt, skb);
        return NETDEV_TX_OK;
    }
    if (unlikely(head - smp_load_acquire(&pt->tx_done) >= SNIC_TX_SLOTS)) {
        netif_stop_queue(ndev);
        return NETDEV_TX_BUSY;
    }

    s = &pt->tx[head & (SNIC_TX_SLOTS - 1)];
    va = page_address(s->page);
    if (skb_copy_bits(skb, 0, va, len)) {
        snic_tx_drop(pt, skb);
        return NETDEV_TX_OK;
    }
    if (len < SNIC_FRAME_MIN) {
        memset(va + len, 0, SNIC_FRAME_MIN - len);
        len = SNIC_FRAME_MIN;
    }
    dma_sync_single_for_device(snic_dma_dev(pt), s->dma, len, DMA_TO_DEVICE);

    s->sg.next = NULL;
    s->sg.pg = s->page;
    s->sg.offset = 0;
    s->sg.len = len;
    s->sg.dma_addr = s->dma;
    memset(&s->req, 0, sizeof(s->req));
    s->req.sgl = &s->sg;
    s->req.sgcnt = 1;
    s->req.count = len;
    s->req.ep_addr = len;               /* ST H2C: the descriptor size, as nic_raw.py sends */
    s->req.write = 1;
    s->req.dma_mapped = 1;
    s->req.h2c_eot = 1;
    s->req.fp_done = snic_tx_done;

    /* before the submit: fp_done may run inside it, for this or earlier frames */
    pt->tx_head = head + 1;
    rv = qdma_request_submit(c->qdma_handle, pt->qh_tx, &s->req);
    if (unlikely(rv < 0)) {
        pt->tx_head = head;             /* not queued: no fp_done for it */
        atomic64_inc(&pt->tx_errors);
        dev_kfree_skb_any(skb);
        return NETDEV_TX_OK;
    }

    u64_stats_update_begin(&pt->tx_syncp);
    pt->tx_packets++;
    pt->tx_bytes += skb->len;
    u64_stats_update_end(&pt->tx_syncp);
    dev_consume_skb_any(skb);

    if (unlikely(snic_tx_inflight(pt) >= SNIC_TX_SLOTS)) {
        netif_stop_queue(ndev);
        smp_mb();
        if (snic_tx_inflight(pt) < SNIC_TX_SLOTS)
            netif_wake_queue(ndev);
    }
    return NETDEV_TX_OK;
}

/*
 * Give back the frame's buffers. libqdma takes one page reference per C2H
 * buffer it posts; with fp_descq_c2h_packet it does not drop it when the buffer
 * is consumed (flq_refill_pages), the callback owns it: without this put every
 * received frame leaked its page.
 */
static void snic_rx_put(struct qdma_sw_sg *sgl, unsigned int sgcnt)
{
    unsigned int i;

    for (i = 0; i < sgcnt && sgl; i++, sgl = sgl->next)
        put_page(sgl->pg);
}

/*
 * libqdma fp_descq_c2h_packet: one completion of the C2H queue, called by
 * qdma_queue_service() with the queue's lock held (NAPI poll or ndo_stop).
 * Copies the frame into an skb queued on pt->rxq and returns 0: the buffers
 * are consumed, their page references put here, and libqdma refills them.
 */
static int snic_rx_packet(unsigned long qhndl, unsigned long quld, unsigned int len,
                          unsigned int sgcnt, struct qdma_sw_sg *sgl, void *udd)
{
    struct slash_nic_port *pt = (struct slash_nic_port *)quld;
    struct device *dev = snic_dma_dev(pt);
    unsigned int left = len, i;
    struct qdma_sw_sg *sg;
    struct sk_buff *skb;

    pt->rx_seen++;
    if (READ_ONCE(pt->rx_drain))
        goto put;
    if (unlikely(!sgcnt || !sgl || len < ETH_HLEN || len > SNIC_BUF_LEN)) {
        u64_stats_update_begin(&pt->rx_syncp);
        pt->rx_length_errors++;
        u64_stats_update_end(&pt->rx_syncp);
        goto put;
    }
    skb = napi_alloc_skb(&pt->napi, len);
    if (unlikely(!skb)) {
        u64_stats_update_begin(&pt->rx_syncp);
        pt->rx_dropped++;
        u64_stats_update_end(&pt->rx_syncp);
        goto put;
    }
    /* stop at sgcnt: the last entry's next is not NULL */
    for (i = 0, sg = sgl; i < sgcnt && left; i++, sg = sg->next) {
        unsigned int n = min(left, sg->len);

        dma_sync_single_for_cpu(dev, sg->dma_addr, n, DMA_FROM_DEVICE);
        skb_put_data(skb, page_address(sg->pg) + sg->offset, n);
        left -= n;
    }
    if (unlikely(left)) {
        dev_kfree_skb_any(skb);
        u64_stats_update_begin(&pt->rx_syncp);
        pt->rx_length_errors++;
        u64_stats_update_end(&pt->rx_syncp);
        goto put;
    }
    skb->protocol = eth_type_trans(skb, pt->ndev);
    __skb_queue_tail(&pt->rxq, skb);
    u64_stats_update_begin(&pt->rx_syncp);
    pt->rx_packets++;
    pt->rx_bytes += len;
    u64_stats_update_end(&pt->rx_syncp);
put:
    snic_rx_put(sgl, sgcnt);
    return 0;
}

/* Service the C2H queue once (up to @budget completions, 0 = all) and ring its doorbells. */
static void snic_rx_service(struct slash_nic_port *pt, int budget)
{
    struct slash_nic_card *c = pt->card;
    int rv;

    pt->rx_seen = 0;
    if (unlikely(pt->rx_fault))
        return;
    rv = qdma_queue_service(c->qdma_handle, pt->qh_rx, budget, true);
    if (pt->rx_seen)
        qdma_queue_update_pointers(c->qdma_handle, pt->qh_rx);
    if (unlikely(rv < 0 && rv != -ENODATA)) {
        /* libqdma halts the queue on a completion error and logs on every call: stop here */
        pt->rx_fault = true;
        netdev_err(pt->ndev, "C2H queue %u error %d: no more receive until ifdown/ifup\n",
                   SNIC_QID_RX(pt->idx), rv);
    }
}

static int snic_poll(struct napi_struct *napi, int budget)
{
    struct slash_nic_port *pt = container_of(napi, struct slash_nic_port, napi);
    struct slash_nic_card *c = pt->card;
    struct sk_buff *skb;
    int work = 0;

    /* H2C completions: fp_done runs inside */
    if (READ_ONCE(pt->tx_head) != smp_load_acquire(&pt->tx_done))
        qdma_queue_service(c->qdma_handle, pt->qh_tx, 0, false);

    if (budget) {                       /* 0: netpoll, TX only */
        snic_rx_service(pt, budget);
        work = min_t(int, pt->rx_seen, budget);
        while ((skb = __skb_dequeue(&pt->rxq)))
            napi_gro_receive(napi, skb);
    }
    if (work < budget)
        napi_complete_done(napi, work);
    return work;
}

static enum hrtimer_restart snic_timer_fn(struct hrtimer *t)
{
    struct slash_nic_port *pt = container_of(t, struct slash_nic_port, timer);

    napi_schedule(&pt->napi);
    hrtimer_forward_now(t, pt->period);
    return HRTIMER_RESTART;
}

/* Add, configure and start one ST queue of @pt kept off libqdma's threads. */
static int snic_q_add(struct slash_nic_port *pt, enum queue_type_t type, u32 qid,
                      unsigned long *qh)
{
    struct slash_nic_card *c = pt->card;
    struct qdma_queue_conf q;
    unsigned long h = QDMA_QUEUE_IDX_INVALID;
    char errbuf[128] = "";
    const char *step;
    int err;

    /* the settings of slash_qdma_ioctl_qpair_add_q() for ST, as qnoc_lb.py / nic_raw.py */
    memset(&q, 0, sizeof(q));
    q.qidx = qid;
    q.q_type = type;
    q.st = 1;
    q.irq_en = 0;
    q.cmpl_en_intr = 0;
    q.cmpl_trig_mode = TRIG_MODE_DISABLE;
    q.wb_status_en = 1;
    q.cmpl_status_acc_en = 1;
    q.cmpl_status_pend_chk = 1;
    q.cmpl_stat_en = 1;
    q.mm_channel = 0;
    q.desc_rng_sz_idx = 0;
    q.uld_service = 1;                  /* patch 0005: serviced by snic_poll() only */
    if (type == Q_C2H) {
        q.cmpl_rng_sz_idx = 0;
        q.cmpl_desc_sz = CMPT_DESC_SZ_16B;
        q.cmpl_trig_mode = TRIG_MODE_ANY;
        q.pfetch_en = 1;
        q.fetch_credit = 1;
        q.fp_descq_c2h_packet = snic_rx_packet;
        q.quld = (unsigned long)pt;
    }

    step = "add";
    err = qdma_queue_add(c->qdma_handle, &q, &h, errbuf, sizeof(errbuf));
    if (!err) {
        /* replays qconf through the reconfig path (mm_channel), as slash_qdma does */
        step = "config";
        err = qdma_queue_config(c->qdma_handle, h, &q, errbuf, sizeof(errbuf));
    }
    if (!err) {
        step = "start";
        err = qdma_queue_start(c->qdma_handle, h, errbuf, sizeof(errbuf));
    }
    if (err) {
        netdev_err(pt->ndev, "%s queue %u %s failed: %d (%s)\n",
                   type == Q_C2H ? "C2H" : "H2C", qid, step, err, errbuf);
        if (h != QDMA_QUEUE_IDX_INVALID)
            qdma_queue_remove(c->qdma_handle, h, errbuf, sizeof(errbuf));
        return err;
    }
    *qh = h;
    return 0;
}

static void snic_q_del(struct slash_nic_port *pt, unsigned long *qh)
{
    struct slash_nic_card *c = pt->card;
    char errbuf[128];

    if (*qh == QDMA_QUEUE_IDX_INVALID)
        return;
    qdma_queue_stop(c->qdma_handle, *qh, errbuf, sizeof(errbuf));
    qdma_queue_remove(c->qdma_handle, *qh, errbuf, sizeof(errbuf));
    *qh = QDMA_QUEUE_IDX_INVALID;
}

static u32 snic_pctrl(struct slash_nic_port *pt)
{
    return SNIC_PCTRL_RX_EN | SNIC_PCTRL_TX_EN | (pt->promisc ? SNIC_PCTRL_PROMISC : 0);
}

static void snic_write_mac(struct slash_nic_port *pt, const u8 *a)
{
    snic_port_wr(pt, SNIC_P_MAC_LO, (u32)a[2] << 24 | (u32)a[3] << 16 | (u32)a[4] << 8 | a[5]);
    snic_port_wr(pt, SNIC_P_MAC_HI, (u32)a[0] << 8 | a[1]);
}

static int snic_open(struct net_device *ndev)
{
    struct slash_nic_port *pt = snic_port(ndev);
    unsigned int us = clamp_val(READ_ONCE(nic_poll_us), 5u, 10000u);
    int err;

    if (!READ_ONCE(pt->card->healthy))
        return -ENODEV;

    err = snic_tx_ring_alloc(pt);
    if (err)
        return err;
    err = snic_q_add(pt, Q_C2H, SNIC_QID_RX(pt->idx), &pt->qh_rx);
    if (err)
        goto err_ring;
    err = snic_q_add(pt, Q_H2C, SNIC_QID_TX(pt->idx), &pt->qh_tx);
    if (err)
        goto err_rx;

    pt->tx_head = 0;
    pt->tx_done = 0;
    pt->rx_seen = 0;
    pt->rx_fault = false;
    WRITE_ONCE(pt->rx_drain, false);
    pt->period = ns_to_ktime((u64)us * NSEC_PER_USEC);
    napi_enable(&pt->napi);

    /* RM port: queues and MAC first, then the gates */
    snic_port_wr(pt, SNIC_P_PCTRL, 0);
    snic_port_wr(pt, SNIC_P_TX_QBASE, SNIC_QID_TX(pt->idx));
    snic_port_wr(pt, SNIC_P_TX_QCNT, 1);
    snic_port_wr(pt, SNIC_P_RX_QID, SNIC_QID_RX(pt->idx));
    snic_write_mac(pt, ndev->dev_addr);
    WRITE_ONCE(pt->dp_up, true);
    snic_port_wr(pt, SNIC_P_PCTRL, snic_pctrl(pt));

    hrtimer_start(&pt->timer, pt->period, HRTIMER_MODE_REL);
    netif_tx_start_all_queues(ndev);
    netdev_info(ndev, "up: H2C queue %u, C2H queue %u, poll %u us\n", SNIC_QID_TX(pt->idx),
                SNIC_QID_RX(pt->idx), us);
    return 0;

err_rx:
    snic_q_del(pt, &pt->qh_rx);
err_ring:
    snic_tx_ring_free(pt);
    return err;
}

static int snic_stop(struct net_device *ndev)
{
    struct slash_nic_port *pt = snic_port(ndev);
    struct slash_nic_card *c = pt->card;
    int i;

    WRITE_ONCE(pt->dp_up, false);
    netif_tx_disable(ndev);
    hrtimer_cancel(&pt->timer);
    napi_disable(&pt->napi);

    /* no more frames from the RM to the queues; then drain what is in flight */
    if (READ_ONCE(c->healthy))
        snic_port_wr(pt, SNIC_P_PCTRL, 0);
    WRITE_ONCE(pt->rx_drain, true);
    for (i = 0; i < 50; i++) {
        snic_rx_service(pt, 0);
        if (pt->tx_head != smp_load_acquire(&pt->tx_done))
            qdma_queue_service(c->qdma_handle, pt->qh_tx, 0, false);
        if (i >= 2 && !pt->rx_seen && pt->tx_head == smp_load_acquire(&pt->tx_done))
            break;
        usleep_range(500, 1000);
    }
    if (pt->tx_head != smp_load_acquire(&pt->tx_done))
        netdev_warn(ndev, "%u frames still queued at ifdown\n", snic_tx_inflight(pt));
    __skb_queue_purge(&pt->rxq);

    /* a stop completes what is left with -ENXIO through fp_done */
    snic_q_del(pt, &pt->qh_tx);
    snic_q_del(pt, &pt->qh_rx);
    snic_tx_ring_free(pt);
    return 0;
}

static void snic_set_rx_mode(struct net_device *ndev)
{
    struct slash_nic_port *pt = snic_port(ndev);

    /* the RM passes every multicast / broadcast frame already: only promiscuous */
    pt->promisc = !!(ndev->flags & IFF_PROMISC);
    if (READ_ONCE(pt->dp_up) && READ_ONCE(pt->card->healthy))
        snic_port_wr(pt, SNIC_P_PCTRL, snic_pctrl(pt));
}

static int snic_set_mac_address(struct net_device *ndev, void *addr)
{
    struct slash_nic_port *pt = snic_port(ndev);
    int err = eth_mac_addr(ndev, addr);

    if (err)
        return err;
    if (READ_ONCE(pt->dp_up) && READ_ONCE(pt->card->healthy))
        snic_write_mac(pt, ndev->dev_addr);
    return 0;
}

static void snic_get_stats64(struct net_device *ndev, struct rtnl_link_stats64 *s)
{
    struct slash_nic_port *pt = snic_port(ndev);
    unsigned int start;

    do {
        start = u64_stats_fetch_begin(&pt->tx_syncp);
        s->tx_packets = pt->tx_packets;
        s->tx_bytes = pt->tx_bytes;
    } while (u64_stats_fetch_retry(&pt->tx_syncp, start));
    do {
        start = u64_stats_fetch_begin(&pt->rx_syncp);
        s->rx_packets = pt->rx_packets;
        s->rx_bytes = pt->rx_bytes;
        s->rx_dropped = pt->rx_dropped;
        s->rx_length_errors = pt->rx_length_errors;
    } while (u64_stats_fetch_retry(&pt->rx_syncp, start));
    s->tx_errors = atomic64_read(&pt->tx_errors);

    spin_lock_bh(&pt->stats_lock);
    s->tx_dropped = pt->tx_dropped;
    s->rx_length_errors += pt->hw[HW_RX_TOOLONG] + pt->hw[HW_RX_MALFORMED];
    s->rx_crc_errors = pt->rx_drops & 0xFFF;
    s->rx_fifo_errors = pt->rx_drops >> 12 & 0xF;
    s->rx_missed_errors = pt->hw[HW_C2H_DROP];    /* no C2H descriptor: host too slow */
    spin_unlock_bh(&pt->stats_lock);
    s->rx_errors = s->rx_length_errors + s->rx_crc_errors + s->rx_fifo_errors;
}

static int snic_get_phys_port_name(struct net_device *ndev, char *name, size_t len)
{
    if (snprintf(name, len, "p%u", snic_port(ndev)->idx) >= len)
        return -EINVAL;
    return 0;
}

static const struct net_device_ops snic_netdev_ops = {
    .ndo_open               = snic_open,
    .ndo_stop               = snic_stop,
    .ndo_start_xmit         = snic_start_xmit,
    .ndo_set_rx_mode        = snic_set_rx_mode,
    .ndo_get_stats64        = snic_get_stats64,
    .ndo_set_mac_address    = snic_set_mac_address,
    .ndo_validate_addr      = eth_validate_addr,
    .ndo_get_phys_port_name = snic_get_phys_port_name,
};

/* ─── ethtool ─────────────────────────────────────────────────────────── */

static void snic_get_drvinfo(struct net_device *ndev, struct ethtool_drvinfo *info)
{
    struct slash_nic_card *c = snic_port(ndev)->card;

    strscpy(info->driver, SLASH_NAME, sizeof(info->driver));
    strscpy(info->version, SLASH_VERSION_STR, sizeof(info->version));
    snprintf(info->fw_version, sizeof(info->fw_version), "nic_core 0x%08x build %u", c->version,
             c->build);
    strscpy(info->bus_info, pci_name(c->pf1), sizeof(info->bus_info));
}

static int snic_get_link_ksettings(struct net_device *ndev, struct ethtool_link_ksettings *ks)
{
    struct slash_nic_port *pt = snic_port(ndev);
    bool r100;

    spin_lock_bh(&pt->stats_lock);
    r100 = pt->rate_100g;
    spin_unlock_bh(&pt->stats_lock);

    ethtool_link_ksettings_zero_link_mode(ks, supported);
    ethtool_link_ksettings_add_link_mode(ks, supported, 100000baseCR4_Full);
    ethtool_link_ksettings_add_link_mode(ks, supported, 200000baseCR4_Full);
    ethtool_link_ksettings_add_link_mode(ks, supported, FIBRE);
    ethtool_link_ksettings_zero_link_mode(ks, advertising);
    ks->base.speed = netif_carrier_ok(ndev) ? (r100 ? SPEED_100000 : SPEED_200000) : SPEED_UNKNOWN;
    ks->base.duplex = netif_carrier_ok(ndev) ? DUPLEX_FULL : DUPLEX_UNKNOWN;
    ks->base.port = PORT_FIBRE;
    ks->base.autoneg = AUTONEG_DISABLE;
    return 0;
}

static int snic_get_sset_count(struct net_device *ndev, int sset)
{
    return sset == ETH_SS_STATS ? SNIC_HW_STATS + SNIC_SW_STATS : -EOPNOTSUPP;
}

static void snic_get_strings(struct net_device *ndev, u32 sset, u8 *data)
{
    unsigned int i;

    if (sset != ETH_SS_STATS)
        return;
    for (i = 0; i < SNIC_HW_STATS; i++, data += ETH_GSTRING_LEN)
        strscpy(data, snic_hw_stats[i].name, ETH_GSTRING_LEN);
    for (i = 0; i < SNIC_SW_STATS; i++, data += ETH_GSTRING_LEN)
        strscpy(data, snic_sw_stats[i], ETH_GSTRING_LEN);
}

static void snic_get_ethtool_stats(struct net_device *ndev, struct ethtool_stats *st, u64 *data)
{
    struct slash_nic_port *pt = snic_port(ndev);
    u64 rx_dropped, rx_len;
    unsigned int i, start;

    do {
        start = u64_stats_fetch_begin(&pt->rx_syncp);
        rx_dropped = pt->rx_dropped;
        rx_len = pt->rx_length_errors;
    } while (u64_stats_fetch_retry(&pt->rx_syncp, start));

    spin_lock_bh(&pt->stats_lock);
    for (i = 0; i < SNIC_HW_STATS; i++)
        *data++ = pt->hw[i];
    *data++ = pt->rx_drops & 0xFFF;
    *data++ = pt->rx_drops >> 12 & 0xF;
    *data++ = pt->tx_dropped;
    spin_unlock_bh(&pt->stats_lock);
    *data++ = atomic64_read(&pt->tx_errors);
    *data++ = rx_dropped;
    *data++ = rx_len;
    *data++ = READ_ONCE(pt->rx_fault);
}

static const struct ethtool_ops snic_ethtool_ops = {
    .get_drvinfo        = snic_get_drvinfo,
    .get_link           = ethtool_op_get_link,
    .get_link_ksettings = snic_get_link_ksettings,
    .get_sset_count     = snic_get_sset_count,
    .get_strings        = snic_get_strings,
    .get_ethtool_stats  = snic_get_ethtool_stats,
};

/* ─── sysfs: /sys/bus/pci/devices/<PF2>/nic ───────────────────────────── */

static struct slash_nic_card *snic_card_of(struct device *dev)
{
    struct slash_nic_card *c;

    mutex_lock(&snic_cards_lock);
    c = snic_card_get(to_pci_dev(dev), false);
    mutex_unlock(&snic_cards_lock);
    return c;
}

static ssize_t nic_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    struct slash_nic_card *c = snic_card_of(dev);
    ssize_t n;

    if (!c)
        return -ENODEV;
    mutex_lock(&c->lock);
    if (c->attached)
        n = sysfs_emit(buf, "attached %s %s nic_core 0x%08x build %u\n", c->port[0].ndev->name,
                       c->port[1].ndev->name, c->version, c->build);
    else
        n = sysfs_emit(buf, "detached%s%s%s\n", c->reason[0] ? " (" : "", c->reason,
                       c->reason[0] ? ")" : "");
    mutex_unlock(&c->lock);
    return n;
}

static ssize_t nic_store(struct device *dev, struct device_attribute *attr, const char *buf,
                         size_t count)
{
    struct slash_nic_card *c = snic_card_of(dev);
    bool on;
    int err;

    if (!c)
        return -ENODEV;
    err = kstrtobool(buf, &on);
    if (err)
        return err;
    mutex_lock(&c->lock);
    if (on)
        err = snic_attach_locked(c);
    else
        snic_detach_locked(c, false, "detached by the user");
    mutex_unlock(&c->lock);
    return err ? err : count;
}
static DEVICE_ATTR_RW(nic);

/* ─── hooks from slash_qdma.c (PF1) and slash_pcie.c (PF2) ────────────── */

void slash_nic_qdma_add(struct pci_dev *pdev, unsigned long qdma_handle, int board)
{
    struct slash_nic_card *c;

    mutex_lock(&snic_cards_lock);
    c = snic_card_get(pdev, true);
    if (c) {
        mutex_lock(&c->lock);
        c->pf1 = pdev;
        c->qdma_handle = qdma_handle;
        if (board >= 0)
            c->board = board;
        mutex_unlock(&c->lock);
    }
    mutex_unlock(&snic_cards_lock);
    if (!c)
        dev_err(&pdev->dev, "slash: nic: out of memory, no NIC on this card\n");
}

void slash_nic_qdma_remove(struct pci_dev *pdev)
{
    struct slash_nic_card *c;

    mutex_lock(&snic_cards_lock);
    c = snic_card_get(pdev, false);
    if (c) {
        mutex_lock(&c->lock);
        snic_detach_locked(c, false, "PF1 removed");
        c->pf1 = NULL;
        c->qdma_handle = 0;
        mutex_unlock(&c->lock);
        snic_card_put(c);
    }
    mutex_unlock(&snic_cards_lock);
}

void slash_nic_ctl_add(struct pci_dev *pdev, int board)
{
    struct slash_nic_card *c;
    int err;

    mutex_lock(&snic_cards_lock);
    c = snic_card_get(pdev, true);
    if (c) {
        mutex_lock(&c->lock);
        c->pf2 = pdev;
        if (board >= 0)
            c->board = board;
        mutex_unlock(&c->lock);
    }
    mutex_unlock(&snic_cards_lock);
    if (!c) {
        dev_err(&pdev->dev, "slash: nic: out of memory, no NIC on this card\n");
        return;
    }
    /* outside the locks: a store on the new file takes them */
    err = device_create_file(&pdev->dev, &dev_attr_nic);
    if (err) {
        dev_err(&pdev->dev, "slash: nic: cannot create the sysfs nic attribute: %d\n", err);
        return;
    }
    mutex_lock(&c->lock);
    c->attr = true;
    mutex_unlock(&c->lock);
}

void slash_nic_ctl_remove(struct pci_dev *pdev)
{
    struct slash_nic_card *c;
    bool attr;

    mutex_lock(&snic_cards_lock);
    c = snic_card_get(pdev, false);
    mutex_unlock(&snic_cards_lock);
    if (!c)
        return;

    mutex_lock(&c->lock);
    attr = c->attr;
    c->attr = false;
    mutex_unlock(&c->lock);
    /* waits for a running show/store, which take the locks */
    if (attr)
        device_remove_file(&pdev->dev, &dev_attr_nic);

    mutex_lock(&snic_cards_lock);
    mutex_lock(&c->lock);
    snic_detach_locked(c, false, "PF2 removed");
    c->pf2 = NULL;
    mutex_unlock(&c->lock);
    snic_card_put(c);
    mutex_unlock(&snic_cards_lock);
}
