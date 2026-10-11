#pragma once

#include <vector>
#include <optional>

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
extern unsigned char DispatchControlRegionEvent(const InputEvent* event, struct W8Region* region);

/* The simple button-region policy shared by camp tabs, scrolling and
   movement controls. Repeat presses again; release is unconditional. The
   panel-owned dispatcher above has a different repeat/release policy. */
unsigned char DispatchButtonRegionEvent(const InputEvent* event, W8Region* region,
                                               W8Widget* button);

/* Panel/container, not a W8Widget base. Child ownership is held by unique_ptr;
   widgets retain a non-owning back-pointer in m_pPanel. TakeControl transfers
   ownership to the caller; DestroyAllControls permits early release when rebuilding.
   The accumulated redraw rectangle uses left == -1 for an empty rectangle. */
// VTABLE: WIZ8 0x005ed5a4
struct Controls {
    Controls();
    Controls(int left, int top, int right, int bottom, std::optional<CatalogArtwork> background,
             int background_image);
    virtual ~Controls();
    Controls(const Controls&) = delete;
    Controls& operator=(const Controls&) = delete;

    virtual void SetEnabled(bool enable);
    virtual void Invalidate(const W8ControlsRect* rect);
    virtual void Redraw();
    /* SetEnabled mirrors m_fEnabled into every child's m_active. */
    bool m_fEnabled;
    bool m_fDirty;
    bool m_fLayoutDirty;
    W8ControlsRect m_bounds; /* widget rectangles are relative to its origin */
    std::optional<CatalogArtwork> m_background;
    int m_backgroundImage;
    W8ControlsRect m_dirtyRect;
    bool m_fWholeAreaDirty; /* set when a caller passes no rectangle */
    unsigned int m_uiRegionSetId;

    void EnableRegionSet(bool enable);
    void AdoptControl(std::unique_ptr<W8Widget> control);
    [[nodiscard]] std::unique_ptr<W8Widget> TakeControl(W8Widget* control);
    size_t ControlCount() const { return m_controls.size(); }
    void DestroyAllControls();
    void InvalidateLayout();
    void SetBounds(int left, int top, int right, int bottom);
    void AcquireRegionSet(unsigned int* shared_region_set);

    /* Non-owning view of a child; event dispatch does not use this index. */
    inline W8Widget* ControlAt(int index) { return m_controls[index].get(); }

  private:
    friend class W8Widget;
    std::vector<std::unique_ptr<W8Widget>> m_controls;

  protected:
    void RedrawChildren(bool full_redraw)
    {
        for (int index = 0; index < static_cast<int>(m_controls.size()); ++index) {
            if (ControlAt(index)->m_active) {
                ControlAt(index)->Redraw(full_redraw);
            }
        }
        m_fLayoutDirty = false;
    }
    void RedrawControls(bool full_redraw);
};

