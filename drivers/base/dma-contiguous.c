/*
 * Contiguous Memory Allocator for DMA mapping framework
 * Copyright (c) 2010-2011 by Samsung Electronics.
 * Written by:
 *	Marek Szyprowski <m.szyprowski@xxxxxxxxxxx>
 *	Michal Nazarewicz <mina86@xxxxxxxxxx>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License or (at your optional) any later version of the license.
 */

#define pr_fmt(fmt) "cma: " fmt

#ifdef CONFIG_CMA_DEBUG
#ifndef DEBUG
#  define DEBUG
#endif
#endif

#include <asm/page.h>
#include <asm/dma-contiguous.h>

#include <linux/memblock.h>
#include <linux/err.h>
#include <linux/mm.h>
#include <linux/mutex.h>
#include <linux/page-isolation.h>
#include <linux/slab.h>
#include <linux/swap.h>
#include <linux/mm_types.h>
#include <linux/dma-contiguous.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

#ifndef SZ_1M
#define SZ_1M (1 << 20)
#endif

#ifdef phys_to_pfn
/* nothing to do */
#elif defined __phys_to_pfn
#  define phys_to_pfn __phys_to_pfn
#elif defined __va
#  define phys_to_pfn(x) page_to_pfn(virt_to_page(__va(x)))
#else
#  error phys_to_pfn implementation needed
#endif

#ifdef CONFIG_CMA_STATS

struct cma_allocation {
	struct list_head clink;
	struct device *dev;
	unsigned long pfn_start;
	unsigned long count;
	unsigned int align;
};

static void add_cma_stats(struct device *dev, struct cma *cma, unsigned long pfn,
			unsigned long count, unsigned int align, int is_alloc);

#else

#define recalculate_cma_region_stats(c)	do { } while (0)
#define add_cma_stats(d, c, p, n, a, i)	do { } while (0)

#endif
struct cma {
#ifdef CONFIG_CMA_STATS
	struct list_head clist;
	unsigned long largest_free_block;
	unsigned long total_alloc;
	unsigned long highest_alloc;
#endif
	unsigned long	base_pfn;
	unsigned long	count;
	unsigned long	*bitmap;
};

struct cma *dma_contiguous_default_area;

#ifndef CONFIG_CMA_SIZE_ABSOLUTE
#define CONFIG_CMA_SIZE_ABSOLUTE 0
#endif

#ifndef CONFIG_CMA_SIZE_PERCENTAGE
#define CONFIG_CMA_SIZE_PERCENTAGE 0
#endif

static unsigned long size_abs = CONFIG_CMA_SIZE_ABSOLUTE * SZ_1M;
static unsigned long size_percent = CONFIG_CMA_SIZE_PERCENTAGE;
static long size_cmdline = -1;
static int __init early_cma(char *p)
{
	pr_debug("%s(%s)\n", __func__, p);
	size_cmdline = memparse(p, &p);
	return 0;
}
early_param("cma", early_cma);

static unsigned long __init __cma_early_get_total_pages(void)
{
	struct memblock_region *reg;
	unsigned long total_pages = 0;

	/*
	 * We cannot use memblock_phys_mem_size() here, because
	 * memblock_analyze() has not been called yet.
	 */
	for_each_memblock(memory, reg)
		total_pages += memblock_region_memory_end_pfn(reg) -
			       memblock_region_memory_base_pfn(reg);
	return total_pages;
}

/**
 * dma_contiguous_reserve() - reserve area for contiguous memory handling
 *
 * This funtion reserves memory from early allocator. It should be
 * called by arch specific code once the early allocator (memblock or bootmem)
 * has been activated and all other subsystems have already allocated/reserved
 * memory.
 */
void __init dma_contiguous_reserve(phys_addr_t limit)
{
	unsigned long selected_size = 0;
	unsigned long total_pages;

	pr_debug("%s(limit %08lx)\n", __func__, (unsigned long)limit);

	total_pages = __cma_early_get_total_pages();
	size_percent *= (total_pages << PAGE_SHIFT) / 100;

	pr_debug("%s: total available: %ld MiB, size absolute: %ld MiB, size percentage: %ld MiB\n",
		 __func__, (total_pages << PAGE_SHIFT) / SZ_1M,
		size_abs / SZ_1M, size_percent / SZ_1M);

#ifdef CONFIG_CMA_SIZE_SEL_ABSOLUTE
	selected_size = size_abs;
#elif defined(CONFIG_CMA_SIZE_SEL_PERCENTAGE)
	selected_size = size_percent;
#elif defined(CONFIG_CMA_SIZE_SEL_MIN)
	selected_size = min(size_abs, size_percent);
#elif defined(CONFIG_CMA_SIZE_SEL_MAX)
	selected_size = max(size_abs, size_percent);
#endif

	if (size_cmdline != -1)
		selected_size = size_cmdline;

	if (!selected_size)
		return;

	pr_debug("%s: reserving %ld MiB for global area\n", __func__,
		 selected_size / SZ_1M);

	dma_declare_contiguous(NULL, selected_size, 0, limit);
};

static DEFINE_MUTEX(cma_mutex);

#ifdef CONFIG_CMA_STATS

static void recalculate_cma_region_stats(struct cma *cma)
{
	unsigned long free_pages;
	struct cma_allocation *p, *prev;

	if (!list_empty(&cma->clist)) {
		free_pages = 0UL;
		cma->total_alloc = cma->largest_free_block = 0UL;
		list_for_each_entry(p, &cma->clist, clink) {
			cma->total_alloc += p->count;
			if (p->clink.prev != &cma->clist) {
				prev = container_of(p->clink.prev, struct cma_allocation,
						clink);
				free_pages = (p->pfn_start - (prev->pfn_start + prev->count));
				if (cma->largest_free_block < free_pages)
					cma->largest_free_block = free_pages;

			}
		}

		if (cma->total_alloc > cma->highest_alloc)
			cma->highest_alloc = cma->total_alloc;
		prev = container_of(p->clink.prev, struct cma_allocation, clink);
		free_pages = (cma->base_pfn + cma->count - (prev->pfn_start + prev->count));
		if (cma->largest_free_block < free_pages)
			cma->largest_free_block = free_pages;
	}

}

/* Should be called with cma_mutex held */
static
void add_cma_stats(struct device *dev, struct cma *cma, unsigned long pfn,
			unsigned long count, unsigned int align, int is_alloc)
{
	struct cma_allocation *p, *next;
	struct cma_allocation *new;

	/* First, some range checks */
	if ((pfn < cma->base_pfn) ||
		((cma->base_pfn + cma->count) < (pfn + count))) {
		printk(KERN_ALERT"cma allocations(0x%p : 0x%08lx+0x%lx) is out of range ?!\n",
					cma, pfn, count);
		goto done;
	}

	if (is_alloc) {
		struct list_head *itr;

		if (align > CONFIG_CMA_ALIGNMENT) {
			printk(KERN_ALERT"Alignment for new allocation(%u) is bigger than configured(%u)\n",
					align, CONFIG_CMA_ALIGNMENT);
			goto done;
		}

		new = kzalloc(sizeof(*new), GFP_KERNEL);
		if (!new) {
			printk(KERN_ALERT"Failed to allocate memory for (cma_allocation)\n");
			goto done;
		}

		new->dev = dev;
		new->pfn_start = pfn;
		new->count = count;
		new->align = align;

		if (unlikely(list_empty(&cma->clist))) {
			list_add_tail(&new->clink, &cma->clist);
		} else {

			/* parse the list and insert
			 * the allocation in ascending order of pfn_start
			 */
			list_for_each(itr, &cma->clist) {
				p = list_entry(itr, struct cma_allocation, clink);
				BUG_ON(p->pfn_start == new->pfn_start);
				if (p->pfn_start > new->pfn_start) {
					__list_add(&new->clink, itr->prev, itr);
					break;
				} else if (list_is_last(itr, &cma->clist)) {
					list_add_tail(&new->clink, &cma->clist);
					break;
				}
			}
		}
	} else {

		BUG_ON(list_empty(&cma->clist));

		/* If this is a release, then just delete the node
		 * corresponding to (pfn,count)
		 */
		list_for_each_entry_safe(p, next, &cma->clist, clink) {
			if (p->pfn_start == pfn) {
				BUG_ON(p->count != count);
				list_del_init(&p->clink);
				kfree(p);
				goto done;
			}
		}
		BUG();
	}
done:
	recalculate_cma_region_stats(cma);
	return;
}

#endif /* CONFIG_CMA_STATS */

static void __cma_activate_area(unsigned long base_pfn, unsigned long count)
{
	unsigned long pfn = base_pfn;
	unsigned i = count >> pageblock_order;
	struct zone *zone;

	VM_BUG_ON(!pfn_valid(pfn));
	zone = page_zone(pfn_to_page(pfn));

	do {
		unsigned j;
		base_pfn = pfn;
		for (j = pageblock_nr_pages; j; --j, pfn++) {
			VM_BUG_ON(!pfn_valid(pfn));
			VM_BUG_ON(page_zone(pfn_to_page(pfn)) != zone);
		}
		init_cma_reserved_pageblock(pfn_to_page(base_pfn));
	} while (--i);
}

static struct cma *__cma_create_area(unsigned long base_pfn,
				     unsigned long count)
{
	int bitmap_size = BITS_TO_LONGS(count) * sizeof(long);
	struct cma *cma;

	pr_debug("%s(base %08lx, count %lx)\n", __func__, base_pfn, count);

	cma = kmalloc(sizeof *cma, GFP_KERNEL);
	if (!cma)
		return ERR_PTR(-ENOMEM);

	cma->base_pfn = base_pfn;
	cma->count = count;
	cma->bitmap = kzalloc(bitmap_size, GFP_KERNEL);

	if (!cma->bitmap)
		goto no_mem;

#ifdef CONFIG_CMA_STATS
	INIT_LIST_HEAD(&cma->clist);
	cma->total_alloc = cma->highest_alloc = 0UL;
	cma->largest_free_block = cma->count;
#endif
	__cma_activate_area(base_pfn, count);

	pr_debug("%s: returned %p\n", __func__, (void *)cma);
	return cma;

no_mem:
	kfree(cma);
	return ERR_PTR(-ENOMEM);
}

static struct cma_reserved {
	phys_addr_t start;
	unsigned long size;
	struct device *dev;
} cma_reserved[MAX_CMA_AREAS];
static unsigned cma_reserved_count;

static int __init __cma_init_reserved_areas(void)
{
	struct cma_reserved *r = cma_reserved;
	unsigned i = cma_reserved_count;

	pr_debug("%s()\n", __func__);

	for (; i; --i, ++r) {
		struct cma *cma;
		cma = __cma_create_area(phys_to_pfn(r->start),
					r->size >> PAGE_SHIFT);
		if (!IS_ERR(cma)) {
			if (r->dev) {
				pr_debug("%s: created area %p\n", __func__, cma);
				set_dev_cma_area(r->dev, cma);
			} else {
				WARN_ON(dma_contiguous_default_area);
				dma_contiguous_default_area = cma;
			}
		}
	}
	return 0;
}
core_initcall(__cma_init_reserved_areas);

/**
 * dma_declare_contiguous() - reserve area for contiguous memory handling
 *			      for particular device
 * @dev:   Pointer to device structure.
 * @size:  Size of the reserved memory.
 * @start: Start address of the reserved memory (optional, 0 for any).
 * @limit: End address of the reserved memory (optional, 0 for any).
 *
 * This funtion reserves memory for specified device. It should be
 * called by board specific code when early allocator (memblock or bootmem)
 * is still activate.
 */
int __init dma_declare_contiguous(struct device *dev, unsigned long size,
				  phys_addr_t base, phys_addr_t limit)
{
	struct cma_reserved *r = &cma_reserved[cma_reserved_count];
	unsigned long alignment;

	pr_debug("%s(size %lx, base %08lx, limit %08lx)\n", __func__,
		 (unsigned long)size, (unsigned long)base,
		 (unsigned long)limit);

	/* Sanity checks */
	if (cma_reserved_count == ARRAY_SIZE(cma_reserved))
		return -ENOSPC;

	if (!size)
		return -EINVAL;

	/* Sanitise input arguments */
	alignment = PAGE_SIZE << max(MAX_ORDER, pageblock_order);
	base = ALIGN(base, alignment);
	size = ALIGN(size, alignment);
	limit = ALIGN(limit, alignment);

	/* Reserve memory */
	if (base) {
		if (memblock_is_region_reserved(base, size) ||
		    memblock_reserve(base, size) < 0) {
			base = -EBUSY;
			goto err;
		}
	} else {
		/*
		 * Use __memblock_alloc_base() since
		 * memblock_alloc_base() panic()s.
		 */
		phys_addr_t addr = __memblock_alloc_base(size, alignment, limit);
		if (!addr) {
			base = -ENOMEM;
			goto err;
		} else if (addr + size > ~(unsigned long)0) {
			memblock_free(addr, size);
			base = -EOVERFLOW;
			goto err;
		} else {
			base = addr;
		}
	}

	/*
	 * Each reserved area must be initialised later, when more kernel
	 * subsystems (like slab allocator) are available.
	 */
	r->start = base;
	r->size = size;
	r->dev = dev;
	cma_reserved_count++;
	printk(KERN_INFO "CMA: reserved %ld MiB at %08lx\n", size / SZ_1M,
	       (unsigned long)base);

	/*
	 * Architecture specific contiguous memory fixup.
	 */
	dma_contiguous_early_fixup(base, size);
	return 0;
err:
	printk(KERN_ERR "CMA: failed to reserve %ld MiB\n", size / SZ_1M);
	return base;
}

/**
 * get_cma_area() - get start and length of cma region for device
 * @dev:   Pointer to device for which the CMA region is to be found.
 * @start: Physical start of CMA region
 * @size:  Size of device's CMA region
 *
 * This functions finds device specific CMA region (or default region
 * when there is no CMA region associated with this device), and returns
 * the start and size of that region.
 */

void get_cma_area(struct device *dev, phys_addr_t *start,
			  unsigned long *size)
{
	int i = cma_reserved_count;


	/* If we are called after __cma_init_reserved_areas()
	 * is done, then we can find a struct cma * in the device
	 * pointer itself, otherwise we will have to walk through
	 * cma_reserved array and return NULL if we can't find anything
	 * there
	 */

	if (dma_contiguous_default_area) {
		struct cma *cma = get_dev_cma_area(dev);

		*start = __pfn_to_phys(cma->base_pfn);
		*size = (cma->count << PAGE_SHIFT);

	} else if (cma_reserved_count) {
		struct cma_reserved *r = cma_reserved;
		for (; i; --i, ++r) {
			/* If there is no device associated,
			 * then this is the default region but
			 * dont stop searching yet
			 */
			if (!r->dev) {
				*start = r->start;
				*size = r->size;
				continue;
			}

			if (r->dev == dev) {
				/* Found it! */
				*start = r->start;
				*size = r->size;
				break;
			}
		}
	} else {
		printk(KERN_WARNING"%s : No CMA regions reserved yet !\n", __func__);
		*start = 0;
		*size = 0;
	}
}

/**
 * dma_alloc_from_contiguous() - allocate pages from contiguous area
 * @dev:   Pointer to device for which the allocation is performed.
 * @count: Requested number of pages.
 * @align: Requested alignment of pages (in PAGE_SIZE order).
 *
 * This funtion allocates memory buffer for specified device. It uses
 * device specific contiguous memory area if available or the default
 * global one. Requires architecture specific get_dev_cma_area() helper
 * function.
 */
struct page *dma_alloc_from_contiguous(struct device *dev, int count,
				       unsigned int align)
{
	struct cma *cma = get_dev_cma_area(dev);
	unsigned long pfn, pageno;
	unsigned long start_from = 0;
	unsigned int retries = 0;
	int ret;

	if (!cma)
		return NULL;

	if (align > CONFIG_CMA_ALIGNMENT)
		align = CONFIG_CMA_ALIGNMENT;

	pr_debug("%s(cma %p, count %d, align %d)\n", __func__, (void *)cma,
		 count, align);

	if (!count)
		return NULL;

	mutex_lock(&cma_mutex);

	do {
		pageno = bitmap_find_next_zero_area(cma->bitmap, cma->count, start_from, count,
				(1 << align) - 1);
		if (pageno >= cma->count) {
			printk(KERN_ERR"%s:%d #### CMA ALLOCATION FAILED ####\n", __func__, __LINE__);
			printk(KERN_ERR"%s : could not find %d/%d in this cma region bitmap\n", __func__, count, align);
#ifdef CONFIG_CMA_STATS
			printk(KERN_ERR"%s:%d ### Total allocation(%lukB), Largest free block(%lukB)\n",
					__func__, __LINE__, cma->total_alloc, cma->largest_free_block);
#endif
			ret = -ENOMEM;
			goto error;
		}

		pr_debug("%s: allocating (%d) pages starting from pageno (%ld)\n", __func__, count, pageno);
		bitmap_set(cma->bitmap, pageno, count);

		pfn = cma->base_pfn + pageno;
		ret = alloc_contig_range(pfn, pfn + count, 0, MIGRATE_CMA);
		if (ret) {
			printk(KERN_ERR"%s:%d #### CMA ALLOCATION FAILED #### with ret = %d for range(%08x-%08x)\n",
					__func__, __LINE__, ret, __pfn_to_phys(pfn), __pfn_to_phys(pfn + count));

			/* Always retry from a new pageblock, that wasn't used before.
			 * if we reach the end of region before running out of retries .. bad luck !
			 */
			start_from = ALIGN(pfn + count, MAX_ORDER_NR_PAGES);
			if ((start_from + count) <= (cma->base_pfn + cma->count)) {
				bitmap_clear(cma->bitmap, pageno, count);
				/* Go back to the index withing CMA region */
				start_from -= cma->base_pfn;
				retries++;
				printk(KERN_ERR"#### Retry(%u) with new start pfn/phys(%08lx/0x%08x) ####\n",
						retries, cma->base_pfn + start_from, __pfn_to_phys(cma->base_pfn + start_from));
			} else {
				printk(KERN_ERR"#### Not enough memory left in the CMA region for retries ####\n");
				goto free;
			}
		}
	} while (ret != 0 && retries < 10);

	add_cma_stats(dev, cma, pfn, count, align, 1);

	mutex_unlock(&cma_mutex);

	pr_debug("%s(): returning [0x%08lx]\n", __func__, (pfn << PAGE_SHIFT));

	return pfn_to_page(pfn);
free:
	bitmap_clear(cma->bitmap, pageno, count);
error:
	mutex_unlock(&cma_mutex);
	return NULL;
}

/**
 * dma_release_from_contiguous() - release allocated pages
 * @dev:   Pointer to device for which the pages were allocated.
 * @pages: Allocated pages.
 * @count: Number of allocated pages.
 *
 * This funtion releases memory allocated by dma_alloc_from_contiguous().
 * It return 0 when provided pages doen't belongs to contiguous area and
 * 1 on success.
 */
int dma_release_from_contiguous(struct device *dev, struct page *pages,
				int count)
{
	struct cma *cma = get_dev_cma_area(dev);
	unsigned long pfn;

	if (!cma || !pages)
		return 0;

	pr_debug("%s(page %p)\n", __func__, (void *)pages);

	pfn = page_to_pfn(pages);

	if (pfn < cma->base_pfn || pfn >= cma->base_pfn + cma->count)
		return 0;

	mutex_lock(&cma_mutex);

	bitmap_clear(cma->bitmap, pfn - cma->base_pfn, count);
	free_contig_pages(pfn, count);

	add_cma_stats(dev, cma, pfn, count, 0, 0);

	mutex_unlock(&cma_mutex);
	return 1;
}

#ifdef CONFIG_CMA_STATS
#ifdef CONFIG_PROC_FS

struct cma_alloc_by_size {
	struct list_head link;
	unsigned long size;
	unsigned int nodes;
};

static void *cma_start(struct seq_file *m, loff_t *pos)
{
	struct cma_reserved *region = (struct cma_reserved *)m->private;

	if (!cma_reserved_count || *pos >= cma_reserved_count)
		return NULL;

	return region + *pos;
}

static void *cma_next(struct seq_file *m, void *arg, loff_t *pos)
{
	struct cma_reserved *region = (struct cma_reserved *)m->private;

	*pos = *pos + 1;

	if (*pos == cma_reserved_count)
		return NULL;

	seq_putc(m, '\n');
	seq_putc(m, '\n');

	return region + *pos;
}

static void cma_stop(struct seq_file *m, void *arg)
{
}

static int cmastat_show(struct seq_file *m, void *arg)
{
	struct cma_reserved *region = (struct cma_reserved *)arg;
	struct cma *cma;
	struct cma_allocation *p;
	const char *region_name;
	const char *device_name;
	struct list_head cma_list_by_size;
	struct cma_alloc_by_size *new, *c, *next;

	INIT_LIST_HEAD(&cma_list_by_size);

	if (region->dev) {
		cma = get_dev_cma_area(region->dev);
		region_name = dev_name(region->dev);
	} else {
		cma = dma_contiguous_default_area;
		region_name = "Default Region";
	}

	seq_printf(m, "%-20s : (%08lx + %lx)", region_name, cma->base_pfn, cma->count);
	seq_putc(m, '\n');
	seq_printf(m, "%-20s :   %-20s %-15s %-20s %-10s", "Device Name", "Number of Pages", "Alignment", "Address Range", "Size");
	seq_putc(m, '\n');

	mutex_lock(&cma_mutex);
	list_for_each_entry(p, &cma->clist, clink) {
		unsigned long size_in_kb = 0UL;
		if (p->dev) {
			device_name = dev_name(p->dev);
		} else {
			device_name = "Unknown";
		}
		size_in_kb = (p->count * PAGE_SIZE)/SZ_1K;
		seq_printf(m, "%-20s :", device_name);
		seq_printf(m, "%15ld", p->count);
		seq_printf(m, "%14u       %10lx-%08lx %7ldkB",
				p->align,
				((p->pfn_start) << PAGE_SHIFT),
				((p->pfn_start + p->count) << PAGE_SHIFT),
				size_in_kb);
		seq_putc(m, '\n');

		if (unlikely(list_empty(&cma_list_by_size))) {
			new = kzalloc(sizeof(*new), GFP_KERNEL);
			if (!new) {
				printk(KERN_ALERT"Failed to allocate memory for (cma_node_by_size)\n");
			} else {
				new->nodes += 1;
				new->size = size_in_kb;
				list_add_tail(&new->link, &cma_list_by_size);
			}
		} else {
			/* Try to find existing node by size. if not, allocate
			 * new and insert in appropriate position
			 */
			struct list_head *itr;

			new = kzalloc(sizeof(*new), GFP_KERNEL);
			if (!new) {
				printk(KERN_ALERT"(%d) Failed to allocate memory for (cma_node_by_size)\n", __LINE__);
			}  else {
				new->size = size_in_kb;
				new->nodes += 1;
				list_for_each(itr, &cma_list_by_size) {
					c = list_entry(itr, struct cma_alloc_by_size, link);
					if (c->size == new->size) {
						c->nodes += 1;
						kfree(new);
						break;
					} else if (c->size > size_in_kb) {
						list_add_tail(&new->link, itr);
						break;
					} else if (list_is_last(itr, &cma_list_by_size)) {
						list_add_tail(&new->link, &cma_list_by_size);
						break;
					}
				}
			}
		}

		/* Check of this line was successfully copied by seq_printf()
		 * if not, break out and return. We well get another _start
		 * call with larger buffer
		 */
		if (m->count >= m->size)
			break;
	}

	if (m->count < m->size) {
		if (!list_empty(&cma_list_by_size)) {
			seq_putc(m, '\n');
			seq_printf(m, "CMA allocation per block size :\n");
			seq_printf(m, "Size (kB)   : No. of Blocks\n");
			list_for_each_entry_safe(c, next, &cma_list_by_size, link) {
				seq_printf(m, "%8lu kB :     %u  \n", c->size, c->nodes);
				list_del_init(&c->link);
				kfree(c);
			}
		}

		seq_putc(m, '\n');
		seq_printf(m,"Total allocation     : %lu Pages, %lukB\n", cma->total_alloc, (cma->total_alloc * PAGE_SIZE) / SZ_1K);
		seq_printf(m,"Total free           : %lu Pages, %lukB\n",
				cma->count - cma->total_alloc,
				((cma->count - cma->total_alloc) * PAGE_SIZE)/SZ_1K);
		seq_printf(m, "Largest Free Block   : %lu Pages, %lukB\n",
				cma->largest_free_block, (cma->largest_free_block * PAGE_SIZE)/SZ_1K);
		seq_printf(m, "Peak Allocation      : %lu Pages, %lukB\n",
				cma->highest_alloc, (cma->highest_alloc * PAGE_SIZE)/SZ_1K);
		seq_printf(m, "==========================================================");
		seq_putc(m,'\n');
		seq_putc(m,'\n');
	}


	mutex_unlock(&cma_mutex);

	return 0;
}

static const struct seq_operations cmastat_op = {
	.start	= cma_start,
	.next	= cma_next,
	.stop	= cma_stop,
	.show	= cmastat_show,
};

static int cmastat_open(struct inode *inode, struct file *file)
{
	int ret;
	ret = seq_open(file, &cmastat_op);
	if (!ret) {
		struct seq_file *m = file->private_data;
		m->private = &cma_reserved;
	}

	return ret;
}

static const struct file_operations cmastat_file_ops = {
	.open		= cmastat_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release,
};

static int __init cma_stats_init(void)
{
	proc_create("cmastat", S_IRUGO, NULL, &cmastat_file_ops);
	return 0;
}

late_initcall(cma_stats_init);
#endif
#endif /* CONFIG_CMA_STATS */
