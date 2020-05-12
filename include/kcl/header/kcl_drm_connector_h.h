#ifndef AMDKCL_DRM_CONNECTOR_H_H
#define AMDKCL_DRM_CONNECTOR_H_H

#ifdef HAVE_DRM_CONNECTOR_H
#include <drm/drm_connector.h>
#else
#include <drm/drm_crtc.h>
#endif
#endif
