#include <linux/fs.h>
#include <linux/types.h>
#include <linux/proc_fs.h>
#include <linux/kernel.h>
#include <linux/module.h>

/* Module specific */
#include <linux/mmzone.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <linux/spinlock.h>
#include <linux/highmem.h>

#include <linux/debugfs.h>

#ifndef __GFP_NO_KSWAPD
#define __GFP_NO_KSWAPD (0)
#endif

#define GFP_ALLOC_LIKE_HUGETLB (GFP_HIGHUSER_MOVABLE|__GFP_REPEAT)
#define GFP_ALLOC_LIKE_THP     (GFP_HIGHUSER_MOVABLE|__GFP_NOMEMALLOC| \
				__GFP_NORETRY|__GFP_NO_KSWAPD)

#define PARAM_MSDELAY 100
#define PARAM_GFPFLAGS GFP_HIGHUSER_MOVABLE
#define PARAM_ALLOCS 100
#define PARAM_ORDER 5

static unsigned long order = PARAM_ORDER;
static unsigned long numpages = PARAM_ALLOCS;
static unsigned long msdelay = PARAM_MSDELAY;
static unsigned long gfp_flags = PARAM_GFPFLAGS;
static unsigned long run;

static void alloc_runtest(unsigned long order, unsigned long numpages, unsigned long gfp_flags)
{
	struct page **pages = NULL;	/* Pages that were allocated */
	unsigned long attempts=0, printed=0;
	unsigned long alloced=0;
	unsigned long nextjiffies = jiffies;
	unsigned long lastjiffies = jiffies;
	unsigned long success=0;
	unsigned long fail=0;
	unsigned long aborted=0;
	unsigned long page_dma=0, page_dma32=0, page_normal=0, page_highmem=0, page_easyrclm=0;
	struct zone *zone;
	char finishString[60];
	bool enabled_preempt = false;
	ktime_t start_ktime;
	ktime_t * alloc_latencies = NULL;
	bool * alloc_outcomes = NULL;

	/* Check parameters */
	if (order < 0 || order >= MAX_ORDER) {
		pr_err("Order request of %lu makes no sense\n", order);
		goto out_preempt;
	}

	if (numpages < 0) {
		pr_err("Number of pages %lu makes no sense\n", numpages);
		goto out_preempt;
	}

	pr_info("order=%lu numpages=%lu msdelay=%lu gfp_flags=0x%lx\n",
			order, numpages, msdelay, gfp_flags);

	if (in_atomic()) {
		pr_warn("WARNING: Enabling preempt behind systemtaps back\n");
		preempt_enable();
		enabled_preempt = true;
	}

	/* 
	 * Allocate memory to store pointers to pages.
	 */
	pages = __vmalloc((numpages+1) * sizeof(struct page **),
			GFP_KERNEL|__GFP_HIGHMEM,
			PAGE_KERNEL);
	if (pages == NULL) {
		pr_err("Failed to allocate space to store page pointers\n");
		goto out_preempt;
	}
	/*
	 * Allocate arrays for storing allocation outcomes and latencies
	 */
	alloc_latencies = __vmalloc((numpages+1) * sizeof(ktime_t),
			GFP_KERNEL|__GFP_HIGHMEM,
			PAGE_KERNEL);
	if (alloc_latencies == NULL) {
		pr_err("Failed to allocate space to store allocation latencies\n");
		goto out_preempt;
	}
	alloc_outcomes = __vmalloc((numpages+1) * sizeof(bool),
			GFP_KERNEL|__GFP_HIGHMEM,
			PAGE_KERNEL);
	if (alloc_outcomes == NULL) {
		pr_err("Failed to allocate space to store allocation outcomes\n");
		goto out_preempt;
	}

#if defined(OOM_DISABLE)
	/* Disable OOM Killer */
	pr_info("Disabling OOM killer for running process\n");
	oomkilladj = current->oomkilladj;
	current->oomkilladj = OOM_DISABLE;
#endif /* OOM_DISABLE */

	/*
	 * Attempt to allocate the requested number of pages
	 */
	while (attempts != numpages) {
		struct page *page;
		if (lastjiffies > jiffies)
			nextjiffies = jiffies;

		/* What the hell is this, should be a waitqueue */
		while (jiffies < nextjiffies) {
			set_current_state(TASK_RUNNING);
			schedule();
		}
		nextjiffies = jiffies + ( (HZ * msdelay)/1000);

		/* Print message if this is taking a long time */
		if (jiffies - lastjiffies > HZ) {
			printk("High order alloc test attempts: %lu (%lu)\n",
					attempts, alloced);
		}

		/* Print out a message every so often anyway */
		if (attempts > 0 && attempts % 10 == 0) {
			printk("High order alloc test attempts: %lu (%lu)\n",
					attempts, alloced);
		}

		lastjiffies = jiffies;

		start_ktime = ktime_get_real();
		page = alloc_pages(gfp_flags | __GFP_NOWARN, order);
		alloc_latencies[attempts] = ktime_sub (ktime_get_real(), start_ktime);

		if (page) {
			alloc_outcomes[attempts] = true;
			//pr_info(testinfo, HIGHALLOC_BUDDYINFO, attempts, 1);
			success++;
			pages[alloced++] = page;

			/* Count what zone this is */
			zone = page_zone(page);
			if (zone->name != NULL && !strcmp(zone->name, "Movable")) page_easyrclm++;
			if (zone->name != NULL && !strcmp(zone->name, "HighMem")) page_highmem++;
			if (zone->name != NULL && !strcmp(zone->name, "Normal")) page_normal++;
			if (zone->name != NULL && !strcmp(zone->name, "DMA32")) page_dma32++;
			if (zone->name != NULL && !strcmp(zone->name, "DMA")) page_dma++;


			/* Give up if it takes more than 60 seconds to allocate */
			if (jiffies - lastjiffies > HZ * 600) {
				printk("Took more than 600 seconds to allocate a block, giving up");
				aborted = attempts + 1;
				attempts = numpages;
				break;
			}

		} else {
			alloc_outcomes[attempts] = false;
			//printp_buddyinfo(testinfo, HIGHALLOC_BUDDYINFO, attempts, 0);
			fail++;

			/* Give up if it takes more than 30 seconds to fail */
			if (jiffies - lastjiffies > HZ * 1200) {
				printk("Took more than 1200 seconds and still failed to allocate, giving up");
				aborted = attempts + 1;
				attempts = numpages;
				break;
			}
		}
		attempts++;
	}

	/* Disable preempt now to make sure everthing is actually printed */
	if (enabled_preempt) {
		preempt_disable();
		enabled_preempt = false;
	}

	for (printed = 0; printed < attempts; printed++) 
		pr_info("%ld %s %llu\n",
			printed,
			alloc_outcomes[printed] ? "success" : "failure",
			ktime_to_ns(alloc_latencies[printed]));

	/* Re-enable OOM Killer state */
#ifdef OOM_DISABLED
	pr_info("Re-enabling OOM Killer status\n");
	current->oomkilladj = oomkilladj;
#endif

	pr_info("Test completed with %lu allocs, printing results\n", alloced);

	/* Print header */
	pr_info("Order:                 %lu\n", order);
	pr_info("GFP flags:             0x%lX\n", gfp_flags);
	pr_info("Allocation type:       %s\n", (gfp_flags & __GFP_HIGHMEM) ? "HighMem" : "Normal");
	pr_info("Attempted allocations: %lu\n", numpages);
	pr_info("Success allocs:        %lu\n", success);
	pr_info("Failed allocs:         %lu\n", fail);
	pr_info("DMA32 zone allocs:       %lu\n", page_dma32);
	pr_info("DMA zone allocs:       %lu\n", page_dma);
	pr_info("Normal zone allocs:    %lu\n", page_normal);
	pr_info("HighMem zone allocs:   %lu\n", page_highmem);
	pr_info("EasyRclm zone allocs:  %lu\n", page_easyrclm);
	pr_info("%% Success:            %lu\n", (success * 100) / (unsigned long)numpages);

	/*
	 * Free up the pages
	 */
	pr_info("Test complete, freeing %lu pages\n", alloced);
	if (alloced > 0) {
		do {
			alloced--;
			if (pages[alloced] != NULL)
				__free_pages(pages[alloced], order);
		} while (alloced != 0);
	}
	
	if (aborted == 0)
		strcpy(finishString, "Test completed successfully\n");
	else
		sprintf(finishString, "Test aborted after %lu allocations due to delays\n", aborted);
	
	pr_info("%s", finishString);

out_preempt:
	if (enabled_preempt)
		preempt_disable();

	if (alloc_latencies)
		vfree(alloc_latencies);
	if (alloc_outcomes)
		vfree(alloc_outcomes);
	if (pages)
		vfree(pages);
	
	return;
}

static int ul_get(void *data, u64 *val)
{
	*val = *(unsigned long *)data;
	return 0;
}

static int ul_set(void *data, u64 val)
{
	*(unsigned long *)data = (unsigned long)val;
	return 0;
}
DEFINE_SIMPLE_ATTRIBUTE(fops_ul, ul_get, ul_set, "%llu\n");

static int run_set(void *data, u64 val)
{
	alloc_runtest(order, numpages, gfp_flags);
	return 0;
}
DEFINE_SIMPLE_ATTRIBUTE(fops_run, NULL, run_set, "%llu\n");

static __init int create_debug_fs_entry(void)
{
	umode_t mode = S_IFREG | S_IRUSR | S_IWUSR;
	struct dentry *dir;

	dir = debugfs_create_dir("mmtests_highalloc", NULL);
	if (!dir)
		return -EINVAL;

	if (!debugfs_create_file("order", mode, dir, &order, &fops_ul))
		goto fail;
	if (!debugfs_create_file("numpages", mode, dir, &numpages, &fops_ul))
		goto fail;
	if (!debugfs_create_file("msdelay", mode, dir, &msdelay, &fops_ul))
		goto fail;
	if (!debugfs_create_file("gfp_flags", mode, dir, &gfp_flags, &fops_ul))
		goto fail;

	if (!debugfs_create_file("run", S_IFREG | S_IWUSR, dir, &run, &fops_run))
		goto fail;

	return 0;

fail:
	return -EINVAL;
}

static int __init mmtests_highalloc_init(void)
{
	return create_debug_fs_entry();
}

module_init(mmtests_highalloc_init);
