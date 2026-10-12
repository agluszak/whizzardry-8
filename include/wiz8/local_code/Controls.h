#pragma once

#include <vector>
#include <optional>
#include <utility>

#include "wiz8/local_code/ControlsRect.h"
#include "wiz8/local_code/Widget.h"
#include "wiz8/catalog_artwork.h"
#include "wiz8/regions.h"
#include <memory>
#include <algorithm>
#include "input.h"

struct W8Region;

/* The single-space separator wrapped notice lines are re-joined with. */
extern char g_W8TextSeparator[];


/* The region callback a widget without its own region is given. It answers
   whether the event was consumed; the screen-input dispatcher returns that
   byte to its caller. */

/* The simple button-region policy shared by camp tabs, scrolling and
   movement controls. Repeat presses again; release is unconditional. The
   panel-owned dispatcher above has a different repeat/release policy. */
unsigned char DispatchButtonRegionEvent(const InputEvent* event, W8Region* region,
                                               W8Widget* button);

/* A panel is a widget that owns and positions child widgets, which may be
   panels themselves. Child ownership is held by unique_ptr; widgets retain a
   non-owning back-pointer in m_pPanel and their rectangles are relative to
   the panel. TakeControl transfers ownership to the caller; DestroyAllControls
   permits early release when rebuilding. */
// VTABLE: WIZ8 0x005ed5a4
struct Controls : W8Widget {
    Controls();
    Controls(int left, int top, int right, int bottom, std::optional<CatalogArtwork> background,
             int background_image);
    ~Controls() override;

    /* Activation is mirrored into every child. */
    void SetActive(bool active) override;
    /* Queue the whole panel, or just an area of it, for repainting. */
    virtual void InvalidateAll();
    void InvalidateArea(const W8ControlsRect& rect);
    void Redraw(bool full_redraw) override;
    bool m_childrenDirty = false;
    std::optional<CatalogArtwork> m_background;
    int m_backgroundImage;
    /* The region set this panel's widgets register in; 0 until CreateRegionSet. */
    unsigned int RegionSet() const { return m_regionSet; }
    void EnableRegionSet(bool enable);
    // Takes ownership of an unattached widget; callers keep borrowed pointers.
    void AddControl(std::unique_ptr<W8Widget> control);
    template <class T, class... Args>
    T* EmplaceControl(Args&&... args)
    {
        auto control = std::make_unique<T>(std::forward<Args>(args)...);
        auto* result = control.get();
        AddControl(std::move(control));
        return result;
    }
    [[nodiscard]] std::unique_ptr<W8Widget> TakeControl(W8Widget* control);
    // Remove and destroy a child. Callers must not delete borrowed pointers.
    void RemoveControl(W8Widget* control);
    size_t ControlCount() const { return m_controls.size(); }
    void DestroyAllControls();
    void InvalidateLayout();
    /* Creates the panel's own region set, or empties it so the panel can repopulate it. */
    void CreateRegionSet();

    /* Non-owning view of a child; event dispatch does not use this index. */
    inline W8Widget* ControlAt(int index) { return m_controls[index].get(); }

  private:
    std::vector<std::unique_ptr<W8Widget>> m_controls;
    unsigned int m_regionSet = 0;
    /* While m_dirty: the bounded area pending redraw, or none for the whole panel. */
    std::optional<W8ControlsRect> m_dirtyRect;

  protected:
    void OnGeometryChanged() override;
    void RedrawChildren(bool full_redraw)
    {
        for (int index = 0; index < static_cast<int>(m_controls.size()); ++index) {
            if (ControlAt(index)->m_active) {
                ControlAt(index)->Redraw(full_redraw);
            }
        }
        m_childrenDirty = false;
    }
    void RedrawControls(bool full_redraw);
};

