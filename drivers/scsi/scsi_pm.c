/*
 *	scsi_pm.c	Copyright (C) 2010 Alan Stern
 *
 *	SCSI dynamic Power Management
 *		Initial version: Alan Stern <stern@rowland.harvard.edu>
 */

#include <linux/pm_runtime.h>
#include <linux/export.h>
#include <linux/async.h>

#include <scsi/scsi.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_driver.h>
#include <scsi/scsi_host.h>

#include "scsi_priv.h"

static int sdev_suspend_common(struct device *dev, int (*cb)(struct device *))
{
	struct scsi_device *sdev = to_scsi_device(dev);
	int err = 0;

	err = blk_pre_runtime_suspend(sdev->request_queue);
	if (err)
		return err;
	if (cb)
		err = cb(dev);
	blk_post_runtime_suspend(sdev->request_queue, err);

	return err;
}

static int sdev_suspend(struct device *dev, int (*cb)(struct device *))
{
	struct scsi_device *sdev = to_scsi_device(dev);

	while (sdev->request_queue->nr_pending)
		scsi_run_queue(sdev->request_queue);

	return sdev_suspend_common(dev, cb);
}

static int sdev_resume(struct device *dev, int (*cb)(struct device *))
{
	struct scsi_device *sdev = to_scsi_device(dev);
	int err = 0;

	blk_pre_runtime_resume(sdev->request_queue);
	if (cb)
		err = cb(dev);
	blk_post_runtime_resume(sdev->request_queue, err);

	return err;
}

static int
scsi_bus_suspend_common(struct device *dev, int (*cb)(struct device *))
{
	int err = 0;

	if (scsi_is_sdev_device(dev)) {
		/*
		 * All the high-level SCSI drivers that implement runtime
		 * PM treat runtime suspend, system suspend, and system
		 * hibernate identically.
		 */
		if (pm_runtime_suspended(dev))
			return 0;

		err = sdev_suspend(dev, cb);
	}

	return err;
}

static int
scsi_bus_resume_common(struct device *dev, int (*cb)(struct device *))
{
	int err = 0;

	if (scsi_is_sdev_device(dev))
		err = sdev_resume(dev, cb);

	if (err == 0) {
		pm_runtime_disable(dev);
		pm_runtime_set_active(dev);
		pm_runtime_enable(dev);
	}
	return err;
}

static int scsi_bus_prepare(struct device *dev)
{
	if (scsi_is_sdev_device(dev)) {
		/* sd probing uses async_schedule.  Wait until it finishes. */
		async_synchronize_full_domain(&scsi_sd_probe_domain);

	} else if (scsi_is_host_device(dev)) {
		/* Wait until async scanning is finished */
		scsi_complete_async_scans();
	}
	return 0;
}

static int scsi_bus_suspend(struct device *dev)
{
	int ret;
	const struct dev_pm_ops *pm = dev->driver ? dev->driver->pm : NULL;

	ret = scsi_bus_suspend_common(dev, pm ? pm->suspend : NULL);
	if (!ret) {
		__pm_runtime_disable(dev, false);
		pm_runtime_set_suspended(dev);
		pm_runtime_enable(dev);
	}

	return ret;
}

static int scsi_bus_resume(struct device *dev)
{
	return pm_request_resume(dev);
}

static int scsi_bus_freeze(struct device *dev)
{
	const struct dev_pm_ops *pm = dev->driver ? dev->driver->pm : NULL;
	return scsi_bus_suspend_common(dev, pm ? pm->freeze : NULL);
}

static int scsi_bus_thaw(struct device *dev)
{
	const struct dev_pm_ops *pm = dev->driver ? dev->driver->pm : NULL;
	return scsi_bus_resume_common(dev, pm ? pm->thaw : NULL);
}

static int scsi_bus_poweroff(struct device *dev)
{
	const struct dev_pm_ops *pm = dev->driver ? dev->driver->pm : NULL;
	return scsi_bus_suspend_common(dev, pm ? pm->poweroff : NULL);
}

static int scsi_bus_restore(struct device *dev)
{
	const struct dev_pm_ops *pm = dev->driver ? dev->driver->pm : NULL;
	return scsi_bus_resume_common(dev, pm ? pm->restore : NULL);
}

static int scsi_bus_runtime_suspend(struct device *dev)
{
	int err = 0;

	dev_dbg(dev, "scsi_runtime_suspend\n");
	if (scsi_is_sdev_device(dev)) {
		const struct dev_pm_ops *pm = dev->driver ?
					      dev->driver->pm : NULL;
		err = sdev_suspend_common(dev, pm ? pm->runtime_suspend : NULL);
	}

	/* Insert hooks here for targets, hosts, and transport classes */

	return err;
}

static int scsi_bus_runtime_resume(struct device *dev)
{
	int err = 0;

	dev_dbg(dev, "scsi_runtime_resume\n");
	if (scsi_is_sdev_device(dev)) {
		const struct dev_pm_ops *pm = dev->driver ?
					      dev->driver->pm : NULL;
		err = sdev_resume(dev, pm ? pm->runtime_resume : NULL);
	}

	/* Insert hooks here for targets, hosts, and transport classes */

	return err;
}

static int scsi_bus_runtime_idle(struct device *dev)
{
	int err;

	dev_dbg(dev, "scsi_runtime_idle\n");

	/* Insert hooks here for targets, hosts, and transport classes */

	if (scsi_is_sdev_device(dev)) {
		pm_runtime_mark_last_busy(dev);
		err = pm_runtime_autosuspend(dev);
	} else {
		err = pm_runtime_suspend(dev);
	}
	return err;
}

int scsi_autopm_get_device(struct scsi_device *sdev)
{
	int	err;

	err = pm_runtime_get_sync(&sdev->sdev_gendev);
	if (err < 0 && err !=-EACCES)
		pm_runtime_put_sync(&sdev->sdev_gendev);
	else
		err = 0;
	return err;
}
EXPORT_SYMBOL_GPL(scsi_autopm_get_device);

void scsi_autopm_put_device(struct scsi_device *sdev)
{
	pm_runtime_put_sync(&sdev->sdev_gendev);
}
EXPORT_SYMBOL_GPL(scsi_autopm_put_device);

void scsi_autopm_get_target(struct scsi_target *starget)
{
	pm_runtime_get_sync(&starget->dev);
}

void scsi_autopm_put_target(struct scsi_target *starget)
{
	pm_runtime_put_sync(&starget->dev);
}

int scsi_autopm_get_host(struct Scsi_Host *shost)
{
	int	err;

	err = pm_runtime_get_sync(&shost->shost_gendev);
	if (err < 0 && err !=-EACCES)
		pm_runtime_put_sync(&shost->shost_gendev);
	else
		err = 0;
	return err;
}

void scsi_autopm_put_host(struct Scsi_Host *shost)
{
	pm_runtime_put_sync(&shost->shost_gendev);
}

const struct dev_pm_ops scsi_bus_pm_ops = {
	.prepare =		scsi_bus_prepare,
	.suspend =		scsi_bus_suspend,
	.resume =		scsi_bus_resume,
	.freeze =		scsi_bus_freeze,
	.thaw =			scsi_bus_thaw,
	.poweroff =		scsi_bus_poweroff,
	.restore =		scsi_bus_restore,
	.runtime_suspend =	scsi_bus_runtime_suspend,
	.runtime_resume =	scsi_bus_runtime_resume,
	.runtime_idle =		scsi_bus_runtime_idle,
};
