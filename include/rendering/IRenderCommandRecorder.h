#pragma once

#include "rendering/RenderFrameContext.h"

class IRenderCommandRecorder {
public:
    virtual ~IRenderCommandRecorder() = default;

    virtual void record(const RenderFrameContext& frame) = 0;
};
