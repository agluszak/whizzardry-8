#pragma once

#include <cstdint>
#include <iosfwd>

class srStatisticsManager {
public:
    struct Statistics {
        double elapsed_time;
        std::uint32_t meshes_traversed;
        std::uint32_t meshes_submitted;
        std::uint32_t triangles_submitted;
        std::uint32_t triangles_after_culling;
        std::uint32_t vertices_submitted;
        std::uint32_t vertices_after_culling;
        std::uint32_t material_processing_stalls;
        std::uint32_t diffuse_operations;
        std::uint32_t specular_operations;
        std::uint32_t alpha_operations;
        std::uint32_t fog_operations;
        std::uint32_t texture_coordinate_operations;
    };

    SR_DLL_IMPORT void reset();
    SR_DLL_IMPORT Statistics getStatistics() const;
    SR_DLL_IMPORT void dump(std::ostream& stream, const Statistics& statistics);

    /* SurRender's submission pipeline updates these counters directly. */
    Statistics statistics;
};

