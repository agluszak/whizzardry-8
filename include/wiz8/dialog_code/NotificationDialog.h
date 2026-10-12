#pragma once

#include <cstdint>
#include "wiz8/dialog_code/MessageDialogBase.h"
#include "compat/compiler.h"

struct W8DialogCloseListener {
    virtual void OnDialogClosed(bool accepted, std::int32_t value) = 0;
};

/* Modal Dialog Code class identified by its constructor. The base owns the
   first 0x98 bytes; this class adds the notification payload and target. */
// VTABLE: WIZ8 0x005eef6c
class W8NotificationDialog : public W8MessageDialogBase {
public:
    W8NotificationDialog(std::int32_t message_index, bool cancel_allowed, std::int32_t notify_value);
    virtual bool ProcessInput() override;

public:
    /* OptionsScreen installs the notification receiver directly. */
    std::int32_t notification_value;
    W8DialogCloseListener* notify_target;
};
