#include <span>

#include "geometry.h"
#include "surrender/srBoundingBox.h"
#include "wiz8/engine_code/3d.h"
#include "wiz8/engine_code/OctBuildPreTree.h"

/* Test the polygon's representative point against six frustum planes; inside
   means every plane distance is non-negative. */
// FUNCTION: WIZ8 0x004cfae0
bool W8OctRegionPolygon::InsideFrustumPlanes(std::span<const W8Plane, 6> planes) const
{
    return PointInsideFrustum(position, planes);
}

/* Test the polygon's representative point against an inclusive axis-aligned
   box. The method's original translation-unit owner is not yet proved. */
// FUNCTION: WIZ8 0x004cfb30
bool W8OctRegionPolygon::containsPoint(const srBoundingBox& bounds) const
{
    return bounds.contains(position);
}
