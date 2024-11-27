/*
 * Copyright © 2009 Keith Packard
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that copyright
 * notice and this permission notice appear in supporting documentation, and
 * that the name of the copyright holders not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  The copyright holders make no representations
 * about the suitability of this software for any purpose.  It is provided "as
 * is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
 * OF THIS SOFTWARE.
 */

#include <kcl/kcl_drm_dp_mst_helper.h>
#include <kcl/kcl_drm_print.h>

#ifndef HAVE_DRM_DP_MST_TOPOLOGY_QUEUE_PROBE
static void _kcl_drm_dp_mst_queue_probe_work(struct drm_dp_mst_topology_mgr *mgr)
{
	queue_work(system_long_wq, &mgr->work);
}

static void
_kcl_drm_dp_mst_topology_mgr_invalidate_mstb(struct drm_dp_mst_branch *mstb)
{
	struct drm_dp_mst_port *port;

	/* The link address will need to be re-sent on resume */
	mstb->link_address_sent = false;

	list_for_each_entry(port, &mstb->ports, next)
		if (port->mstb)
			_kcl_drm_dp_mst_topology_mgr_invalidate_mstb(port->mstb);
}

/**
 * drm_dp_mst_topology_queue_probe - Queue a topology probe
 * @mgr: manager to probe
 *
 * Queue a work to probe the MST topology. Driver's should call this only to
 * sync the topology's HW->SW state after the MST link's parameters have
 * changed in a way the state could've become out-of-sync. This is the case
 * for instance when the link rate between the source and first downstream
 * branch device has switched between UHBR and non-UHBR rates. Except of those
 * cases - for instance when a sink gets plugged/unplugged to a port - the SW
 * state will get updated automatically via MST UP message notifications.
 */
void _kcl_drm_dp_mst_topology_queue_probe(struct drm_dp_mst_topology_mgr *mgr)
{
	mutex_lock(&mgr->lock);

	if (drm_WARN_ON(mgr->dev, !mgr->mst_state || !mgr->mst_primary))
		goto out_unlock;

	_kcl_drm_dp_mst_topology_mgr_invalidate_mstb(mgr->mst_primary);
	_kcl_drm_dp_mst_queue_probe_work(mgr);

out_unlock:
	mutex_unlock(&mgr->lock);
}
EXPORT_SYMBOL(_kcl_drm_dp_mst_topology_queue_probe);
#endif