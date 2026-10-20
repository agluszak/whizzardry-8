#include "transparent_order.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <numeric>

namespace render_poc {

std::uint32_t depthKey(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c, float bias)
{
    const float depth = -(a.z + b.z + c.z + bias * 3.0f);
    const auto bits = std::bit_cast<std::uint32_t>(depth);
    return (bits & 0x80000000u) == 0 ? bits | 0x80000000u : 0x7fffffffu - (bits & 0x7fffffffu);
}

glm::mat4 projectClipNear(const glm::mat4& projection)
{
    const float numerator = projection[2][3] + projection[2][2];
    const float denominator = projection[3][3] + projection[3][2];
    if ((projection[0][3] != 0 || projection[1][3] != 0 || projection[2][3] != 0) &&
        denominator != 0 && std::fabs(numerator / denominator) != 1.0f)
        return projection * static_cast<float>(std::fabs(numerator / denominator));
    return projection;
}

TransparentOrder orderTransparent(std::span<const SortedDraw> draws)
{
    struct Entry {
        std::uint32_t key;
        std::uint32_t draw, pass, batch, triangle;
    };
    std::vector<Entry> entries;
    for (std::uint32_t draw = 0; draw < draws.size(); ++draw) {
        const auto& mesh = *draws[draw].mesh;
        std::vector<glm::vec3> clip(draws[draw].positions.size());
        std::ranges::transform(draws[draw].positions, clip.begin(), [&](const glm::vec3& position) {
            const auto eye = draws[draw].model_view * glm::vec4(position, 1);
            return glm::vec3(draws[draw].projection * eye);
        });
        for (std::uint32_t pass = 0; pass < mesh.passes.size(); ++pass) {
            const auto& resident = mesh.passes[pass];
            for (std::uint32_t batch = 0; batch < resident.batches.size(); ++batch) {
                const auto& run = resident.batches[batch];
                for (auto index = run.first_index; index < run.first_index + run.index_count;
                     index += 3) {
                    const auto triangle = index / 3;
                    const auto& corners = mesh.triangles[resident.polygons[triangle]];
                    auto key =
                        depthKey(clip[corners.x], clip[corners.y], clip[corners.z], mesh.sort_bias);
                    // The native flat path offsets each further pass's key.
                    if (mesh.offset_pass_keys) key += pass;
                    entries.push_back({key, draw, pass, batch, triangle});
                }
            }
        }
    }
    // Equal keys keep submission order.
    std::ranges::stable_sort(entries, {}, &Entry::key);

    TransparentOrder order;
    order.indices.reserve(entries.size() * 3);
    for (const auto& entry : entries) {
        const auto& indices = draws[entry.draw].mesh->passes[entry.pass].indices;
        const auto first = static_cast<std::uint32_t>(order.indices.size());
        order.indices.insert(order.indices.end(), indices.begin() + entry.triangle * 3,
                             indices.begin() + entry.triangle * 3 + 3);
        if (!order.runs.empty()) {
            auto& last = order.runs.back();
            if (last.draw == entry.draw && last.pass == entry.pass && last.batch == entry.batch) {
                last.index_count += 3;
                continue;
            }
        }
        order.runs.push_back({entry.draw, entry.pass, entry.batch, first, 3});
    }
    return order;
}

} // namespace render_poc
