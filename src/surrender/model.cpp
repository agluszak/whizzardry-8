#include "surrender/srClass.h"
#include "surrender/srModel.h"

// FUNCTION: SURRENDER 0x1003C520
srModel::srModel() = default;

// FUNCTION: SURRENDER 0x1003C500
srModel& srModel::operator=(const srModel& other)
{
    if (this != &other) {
        srClass::operator=(other);
    }
    return *this;
}

// FUNCTION: SURRENDER 0x1003C470
srModel::~srModel() = default;
