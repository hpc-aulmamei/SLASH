/**
 * Copyright (C) 2025 Advanced Micro Devices, Inc. All rights reserved.
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

#include "slash_driver_dmabuf.h"

#include "slash_driver.h"

#include <linux/err.h>
#include <linux/pci.h>
#include <linux/printk.h>
#include <linux/slab.h>

struct slash_bar_dmabuf_data {
    int bar_number;
    resource_size_t len;

    struct pci_dev *pdev;
};

/* We only support userspace mmaps of the BAR; importing into other devices is
 * intentionally rejected because a PCI BAR is not system memory. */
static int slash_bar_dmabuf_attach(struct dma_buf *dmabuf, struct dma_buf_attachment *attach)
{
    dev_warn(attach->dev, "%s: device attachments are not supported for BAR dmabuf", SLASH_DRIVER_NAME);
    return -EOPNOTSUPP;
}

static void slash_bar_dmabuf_detach(struct dma_buf *dmabuf, struct dma_buf_attachment *attach)
{
    dev_dbg(attach->dev, "slash: dmabuf detach (noop)\n");
}

static struct sg_table *slash_bar_dmabuf_map(struct dma_buf_attachment *attach,
                                        enum dma_data_direction dir)
{
    dev_dbg(attach->dev, "slash: dmabuf map requested -> not supported\n");
    return ERR_PTR(-EOPNOTSUPP);
}

static void slash_bar_dmabuf_unmap(struct dma_buf_attachment *attach,
                              struct sg_table *sgl, enum dma_data_direction dir)
{
    dev_dbg(attach->dev, "slash: dmabuf unmap (noop)\n");
}

static int slash_bar_dmabuf_mmap(struct dma_buf *dmabuf, struct vm_area_struct *vma)
{
    struct slash_bar_dmabuf_data *priv = dmabuf->priv;
    unsigned long pfn;
    int err;

    bool wc = !!(pci_resource_flags(priv->pdev, priv->bar_number) & IORESOURCE_PREFETCH);

    vma->vm_flags |= VM_DONTDUMP | VM_DONTEXPAND;

    wc = !!(pci_resource_flags(priv->pdev, priv->bar_number) & IORESOURCE_PREFETCH);
    vma->vm_page_prot = wc ? pgprot_writecombine(vma->vm_page_prot)
                           : pgprot_device(vma->vm_page_prot);

    /* Map within the BAR: add BAR start (in PFNs) to user-provided offset */
    pfn = (pci_resource_start(priv->pdev, priv->bar_number) >> PAGE_SHIFT) + vma->vm_pgoff;

    dev_dbg(&priv->pdev->dev, "slash: mmap BAR%d wc=%d start_pfn=0x%lx len=0x%lx\n", priv->bar_number, wc, pfn, vma->vm_end - vma->vm_start);
    err = io_remap_pfn_range(vma, vma->vm_start, pfn,
                             vma->vm_end - vma->vm_start, vma->vm_page_prot);

    if (err) {
        dev_err(&priv->pdev->dev, "slash: io_remap_pfn_range failed: %d\n", err);
        return err;
    }

    return 0;
}

static void slash_bar_dmabuf_release(struct dma_buf *dmabuf)
{
    struct slash_bar_dmabuf_data *priv = dmabuf->priv;

    dev_dbg(&priv->pdev->dev, "slash: dmabuf release (BAR%d)\n", priv->bar_number);

    pci_dev_put(priv->pdev);
    kfree(priv);
}

static const struct dma_buf_ops slash_bar_dmabuf_ops = {
    .attach        = slash_bar_dmabuf_attach,
    .detach        = slash_bar_dmabuf_detach,
    .map_dma_buf   = slash_bar_dmabuf_map,
    .unmap_dma_buf = slash_bar_dmabuf_unmap,
    .mmap          = slash_bar_dmabuf_mmap,
    .release       = slash_bar_dmabuf_release,
};

struct dma_buf *slash_bar_dmabuf_create(struct pci_dev *pdev, int bar_number)
{
    long err;
    resource_size_t len;
    DEFINE_DMA_BUF_EXPORT_INFO(exp_info);
    struct dma_buf *dmabuf;
    struct slash_bar_dmabuf_data *priv;

    if (bar_number < 0 || bar_number >= PCI_STD_NUM_BARS) {
        dev_err(&pdev->dev, "slash: invalid BAR %d\n", bar_number);
        return ERR_PTR(-EINVAL);
    }
    if (!pci_resource_start(pdev, bar_number)) {
        dev_err(&pdev->dev, "slash: BAR%d not present\n", bar_number);
        return ERR_PTR(-ENODEV);
    }
    if ((pci_resource_flags(pdev, bar_number) & IORESOURCE_MEM) == 0) {
        dev_err(&pdev->dev, "slash: BAR%d is not MMIO\n", bar_number);
        return ERR_PTR(-ENODEV);
    }

    len = pci_resource_len(pdev, bar_number);

    dev_dbg(&pdev->dev, "slash: exporting BAR%d as dma-buf (size=%pa)\n", bar_number, &len);
    
    priv = kzalloc(sizeof(*priv), GFP_KERNEL);
    if (!priv) {
        dev_err(&pdev->dev, "slash: kzalloc(priv) failed\n");
        return ERR_PTR(-ENOMEM);
    }
    
    priv->bar_number = bar_number;
    priv->len = len;
    priv->pdev = pci_dev_get(pdev);

    exp_info.ops = &slash_bar_dmabuf_ops;
    exp_info.size = len;
    exp_info.flags = O_RDWR;
    exp_info.priv = priv;
    exp_info.exp_name = SLASH_DRIVER_NAME;

    dmabuf = dma_buf_export(&exp_info);
    if (IS_ERR(dmabuf)) {
        err = PTR_ERR(dmabuf);
        dev_err(&pdev->dev, "slash: dma_buf_export failed: %ld\n", err);
        goto err_free_priv;
    }

    dev_info(&pdev->dev, "slash: BAR%d exported as dma-buf (size=%pa)\n", bar_number, &len);
    return dmabuf;

err_free_priv:
    pci_dev_put(priv->pdev);
    kfree(priv);

    return ERR_PTR(err);
}

void slash_bar_dmabuf_destroy(struct dma_buf *dmabuf)
{
    pr_debug("slash: dmabuf_destroy()\n");
    dma_buf_put(dmabuf);
}
