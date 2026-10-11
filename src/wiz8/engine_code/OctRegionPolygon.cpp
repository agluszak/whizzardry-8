#include "wiz8/engine_code/3d.h"
#include <array>
#include <span>
#include <cstddef>
#include "surrender/srBoundingBox.h"
#include <cstdlib>
#include <cstring>

#include "wiz8/engine_code/OctBuildPreTree.h"
#include "wiz8/engine_code/OctPreTree.h"
#include "wiz8/engine_code/materials.h"
#include "geometry.h"
#include "surrender/srMath.h"

/* Test the polygon's representative point against six frustum planes; inside
   means every plane distance is non-negative. */
// FUNCTION: WIZ8 0x004cfae0
bool W8OctRegionPolygon::InsideFrustumPlanes(std::span<const W8Plane, 6> planes) const
{
    return PointInsideFrustum(position.glmValue(), planes);
}

/* Test the polygon's representative point against an inclusive axis-aligned
   box. The method's original translation-unit owner is not yet proved. */
// FUNCTION: WIZ8 0x004cfb30
bool W8OctRegionPolygon::containsPoint(const srBoundingBox& bounds) const
{
    return bounds.contains(position.glmValue());
}

/* Grow `*run` to (count + capacity) dwords when count lands on a capacity
   boundary, preserving existing entries. */
// FUNCTION: WIZ8 0x004cfb70
unsigned char W8OctPreTreeGeometry::CheckArrayLength(int** run, unsigned short count,
                                                     unsigned short capacity)
{
    if (capacity == 0) {
        return 0;
    }
    if (count % capacity == 0) {
        if (unsigned(count) + capacity > 65535) {
            return 0;
        }
        const unsigned short total = count + capacity;
        int* grown = static_cast<int*>(malloc(total * sizeof(int)));
        if (grown != 0) {
            memset(grown, 0, total * sizeof(*grown));
            if (*run != 0) {
                memcpy(grown, *run, count * sizeof(*grown));
                free(*run);
            }
            *run = grown;
            return 1;
        }
        ReportBuildStatus(7, "CheckArrayLength: Could not allocate new array.\n");
    }
    return 0;
}

// Records reserve index zero. Keep channel frees interleaved in record order.
template<class Record, std::size_t Channels>
static void ReleaseSharedRuns(std::span<Record> records,
                              const std::array<int* Record::*, Channels>& fields)
{
    if (records.empty()) return;
    std::array<int*, Channels> previous;
    for (std::size_t channel = 0; channel < Channels; ++channel)
        previous[channel] = records.front().*fields[channel];
    for (auto& record : records) {
        for (std::size_t channel = 0; channel < Channels; ++channel) {
            auto& run = record.*fields[channel];
            if (run && run != previous[channel]) {
                std::free(previous[channel]);
                previous[channel] = run;
                run = nullptr;
            }
        }
    }
    for (auto* run : previous) std::free(run);
}

/* Free every run the geometry owns: per-vertex face-index and owned arrays
   (consecutive vertices may share one allocation, so each distinct pointer is
   freed once when it changes), the vertex and polygon arrays themselves, and
   the owned +0x14 buffer. */
// FUNCTION: WIZ8 0x004cfc10
void W8OctPreTreeGeometry::Release()
{
    if (m_vertices != 0) {
        if (vertex_count > 1)
            ReleaseSharedRuns(std::span{m_vertices + 1, vertex_count - 1},
                std::array{&W8OctPreTreeVertex::face_indices, &W8OctPreTreeVertex::owned});
        free(m_vertices);
        m_vertices = nullptr;
    }
    vertex_count = 0;
    if (m_polygons != 0) {
        if (m_polygon_count > 1)
            ReleaseSharedRuns(std::span{m_polygons + 1, m_polygon_count - 1},
                std::array{&W8OctRegionPolygon::face_indices});
        free(m_polygons);
        m_polygons = nullptr;
    }
    m_polygon_count = 0;
    free(owned);
    owned = nullptr;
}
