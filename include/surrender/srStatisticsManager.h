#pragma once

class srStatisticsManager {
public:
    struct Statistics {
        double elapsed_time;
        w8_ulong meshes_traversed;
        w8_ulong meshes_submitted;
        w8_ulong triangles_submitted;
        w8_ulong triangles_after_culling;
        w8_ulong vertices_submitted;
        w8_ulong vertices_after_culling;
        w8_ulong material_processing_stalls;
        w8_ulong diffuse_operations;
        w8_ulong specular_operations;
        w8_ulong alpha_operations;
        w8_ulong fog_operations;
        w8_ulong texture_coordinate_operations;
    };

    void reset();
    Statistics getStatistics() const;

    /* SurRender's submission pipeline updates these counters directly. */
    Statistics statistics;
};
