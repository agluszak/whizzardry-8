#pragma once

#include "srDD.h"

/* Native SurRender device on SDL3 GPU (Vulkan, Metal, Direct3D 12). It replaces
   the srDD_*.dll drivers: callers construct srGERD directly with this device. */
srDD* srCreateSDLGPUDevice();
