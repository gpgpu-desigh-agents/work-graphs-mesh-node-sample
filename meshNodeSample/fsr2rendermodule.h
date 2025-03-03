// This file is part of the AMD Work Graph Mesh Node Sample.
//
// Copyright (c) 2024 Advanced Micro Devices, Inc. All rights reserved.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

#pragma once

#include "render/rendermodule.h"
#include "core/framework.h"
#include "core/uimanager.h"

#include <FidelityFX/host/ffx_fsr2.h>

#include <functional>

namespace cauldron
{
    class Texture;
}  // namespace cauldron

/**
 * FSR2RenderModule takes care of:
 *      - creating UI section that enable users to select upscaling options
 *      - creating GPU resources
 *      - clearing and/or generating the reactivity masks
 *      - dispatch workloads for upscaling using FSR 2
 */
class FSR2RenderModule : public cauldron::RenderModule
{
public:
    /**
     * @brief   Constructor with default behavior.
     */
    FSR2RenderModule()
        : RenderModule(L"FSR2RenderModule")
    {
    }

    /**
     * @brief   Tear down the FSR 2 API Context and release resources.
     */
    virtual ~FSR2RenderModule();

    /**
     * @brief   Initialize FSR 2 API Context, create resources, and setup UI section for FSR 2.
     */
    void Init(const json& initData);

    /**
     * @brief   If render module is enabled, initialize the FSR 2 API Context. If disabled, destroy the FSR 2 API Context.
     */
    void EnableModule(bool enabled) override;

    /**
     * @brief   Setup parameters that the FSR 2 API needs this frame and then call the FFX Dispatch.
     */
    void Execute(double deltaTime, cauldron::CommandList* pCmdList) override;

    /**
     * @brief   Recreate the FSR 2 API Context to resize internal resources. Called by the framework when the resolution changes.
     */
    void OnResize(const cauldron::ResolutionInfo& resInfo) override;

private:
    // Enum representing the FSR 2 quality modes.
    enum class FSR2ScalePreset
    {
        Quality = 0,       // 1.5f
        Balanced,          // 1.7f
        Performance,       // 2.f
        UltraPerformance,  // 3.f
        Custom             // 1.f - 3.f range
    };

    static void FfxMsgCallback(FfxMsgType type, const wchar_t* message);

    void InitUI();
    void UpdatePreset(const int32_t* pOldPreset);
    void UpdateUpscaleRatio(const float* pOldRatio);

    cauldron::ResolutionInfo UpdateResolution(uint32_t displayWidth, uint32_t displayHeight);
    void                     UpdateFSR2Context(bool enabled);

    FSR2ScalePreset m_ScalePreset  = FSR2ScalePreset::Custom;
    float           m_UpscaleRatio = 1.f;
    float           m_Sharpness    = 0.8f;
    uint32_t        m_JitterIndex  = 0;
    float           m_JitterX      = 0.f;
    float           m_JitterY      = 0.f;

    bool m_UpscaleRatioEnabled = false;
    bool m_RCASSharpen         = true;

    // FidelityFX Super Resolution 2 information
    FfxFsr2ContextDescription m_InitializationParameters = {};
    FfxFsr2Context            m_FSR2Context;

    // For UI params
    cauldron::UISection m_UISection;

    // FidelityFX Super Resolution 2 resources
    const cauldron::Texture* m_pColorTarget   = nullptr;
    const cauldron::Texture* m_pDepthTarget   = nullptr;
    const cauldron::Texture* m_pMotionVectors = nullptr;

    // For resolution updates
    std::function<cauldron::ResolutionInfo(uint32_t, uint32_t)> m_pUpdateFunc = nullptr;

};
