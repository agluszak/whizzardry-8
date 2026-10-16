#pragma once

#include <cstdint>

#include <math.h>

#include "glm/ext/vector_float3.hpp"
#include "model_view_scope.h"
#include "srModel.h"
#include "srNode.h"

// class srModelInstance
class srModelInstance : public srNode {
public:
    virtual ~srModelInstance() override;
    srModelInstance(srNode* parent = nullptr, Ownership ownership = Ownership::parent);

    srModelInstance& operator=(const srModelInstance& other);

    virtual void traverse(TraverseInfo& info) override;
    virtual void process(const ProcessInfo& info, e_processType type) override;
    virtual BoundInfo getLocalBounds() override;

    // The instance shares ownership of its model; renderers borrow it.
    srModel* getModel() const { return model_.get(); }
    void setModel(std::shared_ptr<srModel> model) { model_ = std::move(model); }

    double getAlignAngle() const;
    glm::vec3 getAlignAxis() const;
    w8_ulong getExclusionMask() const;
    int isAligned() const;
    void setAlignAngle(double angle);
    void setAlignAxis(const glm::vec3& axis);
    void setAlignment(int enabled);
    void setExclusionMask(w8_ulong mask);

    std::uint32_t alignment_flags = 0;

protected:
    class RenderScope {
    public:
        ~RenderScope();
        RenderScope(const RenderScope&) = delete;
        RenderScope& operator=(const RenderScope&) = delete;

    private:
        friend class srModelInstance;
        RenderScope(const srModelInstance& instance, srGERD& renderer, bool apply_exclusion);
        srGERD& renderer;
        srModelViewScope transform;
        w8_ulong previous_mask = 0;
        bool restore_mask = false;
    };
    RenderScope scopedRender(srGERD& renderer, bool apply_exclusion = true) const;

    // Push the transform used by both ordinary and screen-facing model draws.
    srModelViewScope scopedRenderTransform(srGERD& renderer) const;

    glm::vec3 align_axis;
    float align_angle;
    w8_ulong exclusion_mask;

private:
    std::shared_ptr<srModel> model_;
};
