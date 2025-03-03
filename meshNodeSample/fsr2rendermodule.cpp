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

#include "fsr2rendermodule.h"

#include <FidelityFX/gpu/fsr2/ffx_fsr2_resources.h>

#include <functional>

#include "core/scene.h"
#include "render/dynamicresourcepool.h"
#include "render/profiler.h"
#include "render/rasterview.h"
#include "render/uploadheap.h"
#include "validation_remap.h"

using namespace cauldron;

void FSR2RenderModule::Init(const json& initData)
{
    // Fetch needed resources
    m_pColorTarget   = GetFramework()->GetColorTargetForCallback(GetName());
    m_pDepthTarget   = GetFramework()->GetRenderTexture(L"GBufferDepthTarget");
    m_pMotionVectors = GetFramework()->GetRenderTexture(L"GBufferMotionVectorTarget");

    CauldronAssert(ASSERT_CRITICAL, m_pColorTarget && m_pDepthTarget && m_pMotionVectors, L"Could not get one of the needed resources for FSR2 Rendermodule.");

    // Set our render resolution function as that to use during resize to get render width/height from display
    // width/height
    m_pUpdateFunc = [this](uint32_t displayWidth, uint32_t displayHeight) { return this->UpdateResolution(displayWidth, displayHeight); };

    // UI
    InitUI();

    //////////////////////////////////////////////////////////////////////////
    // Finish up init

    // Start disabled as this will be enabled externally
    SetModuleEnabled(false);

    // That's all we need for now
    SetModuleReady(true);
}

FSR2RenderModule::~FSR2RenderModule()
{
    // Protection
    if (ModuleEnabled())
        EnableModule(false);  // Destroy FSR context
}

void FSR2RenderModule::EnableModule(bool enabled)
{
    // If disabling the render module, we need to disable the upscaler with the framework
    if (enabled)
    {
        // Setup everything needed when activating FSR
        // Will also enable upscaling
        UpdatePreset(nullptr);

        // Toggle this now so we avoid the context changes in OnResize
        SetModuleEnabled(enabled);

        // Setup Cauldron FidelityFX interface.
        const size_t scratchBufferSize = ffxGetScratchMemorySize(FFX_FSR2_CONTEXT_COUNT);
        void*        scratchBuffer     = calloc(scratchBufferSize, 1);
        FfxErrorCode errorCode =
            ffxGetInterface(&m_InitializationParameters.backendInterface, GetDevice(), scratchBuffer, scratchBufferSize, FFX_FSR2_CONTEXT_COUNT);
        CauldronAssert(ASSERT_CRITICAL, errorCode == FFX_OK, L"Could not initialize the FidelityFX SDK backend");

        // Create the FSR2 context
        UpdateFSR2Context(true);

        // Set the jitter callback to use
        CameraJitterCallback jitterCallback = [this](Vec2& values) {
            // Increment jitter index for frame
            ++m_JitterIndex;

            // Update FSR2 jitter for built in TAA
            const ResolutionInfo& resInfo  = GetFramework()->GetResolutionInfo();
            const int32_t jitterPhaseCount = ffxFsr2GetJitterPhaseCount(resInfo.RenderWidth, resInfo.DisplayWidth);
            ffxFsr2GetJitterOffset(&m_JitterX, &m_JitterY, m_JitterIndex, jitterPhaseCount);

            values = Vec2(2.f * m_JitterX / resInfo.RenderWidth, 2.f * m_JitterY / resInfo.RenderHeight);
        };
        CameraComponent::SetJitterCallbackFunc(jitterCallback);

        // ... and register UI elements for active upscaler
        GetUIManager()->RegisterUIElements(m_UISection);
    }
    else
    {
        // Toggle this now so we avoid the context changes in OnResize
        SetModuleEnabled(enabled);

        GetFramework()->EnableUpscaling(false);

        // Destroy the FSR2 context
        UpdateFSR2Context(false);

        // Destroy the FidelityFX interface memory
        free(m_InitializationParameters.backendInterface.scratchBuffer);

        // Deregister UI elements for inactive upscaler
        GetUIManager()->UnRegisterUIElements(m_UISection);
    }
}

void FSR2RenderModule::InitUI()
{
    // Build UI options, but don't register them yet. Registration/Deregistration will be controlled by enabling/disabling the render module
    m_UISection.SectionName = "Upscaling";  // We will piggy-back on existing upscaling section"
    m_UISection.SectionType = UISectionType::Sample;

    // Setup scale preset options
    const char*              preset[] = {"Quality (1.5x)", "Balanced (1.7x)", "Performance (2x)", "Ultra Performance (3x)", "Custom"};
    std::vector<std::string> presetComboOptions;
    presetComboOptions.assign(preset, preset + _countof(preset));
    std::function<void(void*)> presetCallback = [this](void* pParams) { this->UpdatePreset(static_cast<int32_t*>(pParams)); };
    m_UISection.AddCombo("Scale Preset", reinterpret_cast<int32_t*>(&m_ScalePreset), &presetComboOptions, presetCallback);

    // Setup scale factor (disabled for all but custom)
    std::function<void(void*)> ratioCallback = [this](void* pParams) { this->UpdateUpscaleRatio(static_cast<float*>(pParams)); };
    m_UISection.AddFloatSlider("Custom Scale", &m_UpscaleRatio, 1.f, 3.f, ratioCallback, &m_UpscaleRatioEnabled);

    // Sharpening
    m_UISection.AddCheckBox("RCAS Sharpening", &m_RCASSharpen);
    m_UISection.AddFloatSlider("Sharpness", &m_Sharpness, 0.f, 1.f, nullptr, &m_RCASSharpen);
}

void FSR2RenderModule::UpdatePreset(const int32_t* pOldPreset)
{
    switch (m_ScalePreset)
    {
    case FSR2ScalePreset::Quality:
        m_UpscaleRatio = 1.5f;
        break;
    case FSR2ScalePreset::Balanced:
        m_UpscaleRatio = 1.7f;
        break;
    case FSR2ScalePreset::Performance:
        m_UpscaleRatio = 2.0f;
        break;
    case FSR2ScalePreset::UltraPerformance:
        m_UpscaleRatio = 3.0f;
        break;
    case FSR2ScalePreset::Custom:
    default:
        // Leave the upscale ratio at whatever it was
        break;
    }

    // Update whether we can update the custom scale slider
    m_UpscaleRatioEnabled = (m_ScalePreset == FSR2ScalePreset::Custom);

    // Update resolution since rendering ratios have changed
    GetFramework()->EnableUpscaling(true, m_pUpdateFunc);
}

void FSR2RenderModule::UpdateUpscaleRatio(const float* pOldRatio)
{
    // Disable/Enable FSR2 since resolution ratios have changed
    GetFramework()->EnableUpscaling(true, m_pUpdateFunc);
}

void FSR2RenderModule::FfxMsgCallback(FfxMsgType type, const wchar_t* message)
{
    if (type == FFX_MESSAGE_TYPE_ERROR)
    {
        CauldronWarning(L"FSR2_API_DEBUG_ERROR: %ls", message);
    }
    else if (type == FFX_MESSAGE_TYPE_WARNING)
    {
        CauldronWarning(L"FSR2_API_DEBUG_WARNING: %ls", message);
    }
}

void FSR2RenderModule::UpdateFSR2Context(bool enabled)
{
    if (enabled)
    {
        const ResolutionInfo& resInfo                   = GetFramework()->GetResolutionInfo();
        m_InitializationParameters.maxRenderSize.width  = resInfo.RenderWidth;
        m_InitializationParameters.maxRenderSize.height = resInfo.RenderHeight;
        m_InitializationParameters.displaySize.width    = resInfo.DisplayWidth;
        m_InitializationParameters.displaySize.height   = resInfo.DisplayHeight;

        // Enable auto-exposure by default
        m_InitializationParameters.flags = FFX_FSR2_ENABLE_AUTO_EXPOSURE;

        // Note, inverted depth and display mode are currently handled statically for the run of the sample.
        // If they become changeable at runtime, we'll need to modify how this information is queried
        static bool s_InvertedDepth = GetConfig()->InvertedDepth;

        // Setup inverted depth flag according to sample usage
        if (s_InvertedDepth)
            m_InitializationParameters.flags |= FFX_FSR2_ENABLE_DEPTH_INVERTED | FFX_FSR2_ENABLE_DEPTH_INFINITE;

        // Input data is HDR
        m_InitializationParameters.flags |= FFX_FSR2_ENABLE_HIGH_DYNAMIC_RANGE;

        // Motion vectors include frame-to-frame jitter
        m_InitializationParameters.flags |= FFX_FSR2_ENABLE_MOTION_VECTORS_JITTER_CANCELLATION;

// Do eror checking in debug
#if defined(_DEBUG)
        m_InitializationParameters.flags |= FFX_FSR2_ENABLE_DEBUG_CHECKING;
        m_InitializationParameters.fpMessage = &FSR2RenderModule::FfxMsgCallback;
#endif  // #if defined(_DEBUG)

        // Create the FSR2 context
        FfxErrorCode errorCode = ffxFsr2ContextCreate(&m_FSR2Context, &m_InitializationParameters);
        CauldronAssert(ASSERT_CRITICAL, errorCode == FFX_OK, L"Couldn't create the FidelityFX SDK FSR2 context.");
    }

    else
    {
        // Destroy the FSR2 context
        ffxFsr2ContextDestroy(&m_FSR2Context);
    }
}

ResolutionInfo FSR2RenderModule::UpdateResolution(uint32_t displayWidth, uint32_t displayHeight)
{
    return {
        static_cast<uint32_t>((float)displayWidth / m_UpscaleRatio), static_cast<uint32_t>((float)displayHeight / m_UpscaleRatio), displayWidth, displayHeight};
}

void FSR2RenderModule::OnResize(const ResolutionInfo& resInfo)
{
    if (!ModuleEnabled())
        return;

    // Need to recreate the FSR2 context on resource resize
    UpdateFSR2Context(false);  // Destroy
    UpdateFSR2Context(true);   // Re-create

    // Rest jitter index
    m_JitterIndex = 0;
}

void FSR2RenderModule::Execute(double deltaTime, CommandList* pCmdList)
{
    GPUScopedProfileCapture sampleMarker(pCmdList, L"FFX FSR2");
    const ResolutionInfo&   resInfo = GetFramework()->GetResolutionInfo();
    CameraComponent*        pCamera = GetScene()->GetCurrentCamera();

    // All cauldron resources come into a render module in a generic read state (ResourceState::NonPixelShaderResource |
    // ResourceState::PixelShaderResource)
    FfxFsr2DispatchDescription dispatchParameters = {};
    dispatchParameters.commandList                = ffxGetCommandList(pCmdList);
    dispatchParameters.color         = ffxGetResource(m_pColorTarget->GetResource(), L"FSR2_Input_OutputColor", FFX_RESOURCE_STATE_PIXEL_COMPUTE_READ);
    dispatchParameters.depth         = ffxGetResource(m_pDepthTarget->GetResource(), L"FSR2_InputDepth", FFX_RESOURCE_STATE_PIXEL_COMPUTE_READ);
    dispatchParameters.motionVectors = ffxGetResource(m_pMotionVectors->GetResource(), L"FSR2_InputMotionVectors", FFX_RESOURCE_STATE_PIXEL_COMPUTE_READ);
    dispatchParameters.exposure      = ffxGetResource(nullptr, L"FSR2_InputExposure", FFX_RESOURCE_STATE_PIXEL_COMPUTE_READ);
    dispatchParameters.output        = dispatchParameters.color;

    dispatchParameters.reactive                   = ffxGetResource(nullptr, L"FSR2_EmptyInputReactiveMap", FFX_RESOURCE_STATE_PIXEL_COMPUTE_READ);
    dispatchParameters.transparencyAndComposition = ffxGetResource(nullptr, L"FSR2_EmptyTransparencyAndCompositionMap", FFX_RESOURCE_STATE_PIXEL_COMPUTE_READ);

    // Jitter is calculated earlier in the frame using a callback from the camera update
    dispatchParameters.jitterOffset.x      = m_JitterX;
    dispatchParameters.jitterOffset.y      = -m_JitterY;
    dispatchParameters.motionVectorScale.x = resInfo.fRenderWidth() / 2.f;
    dispatchParameters.motionVectorScale.y = -resInfo.fRenderHeight() / 2.f;
    dispatchParameters.reset               = false;
    dispatchParameters.enableSharpening    = m_RCASSharpen;
    dispatchParameters.sharpness           = m_Sharpness;

    // Cauldron keeps time in seconds, but FSR expects miliseconds
    dispatchParameters.frameTimeDelta = static_cast<float>(deltaTime * 1000.f);

    dispatchParameters.preExposure       = GetScene()->GetSceneExposure();
    dispatchParameters.renderSize.width  = resInfo.RenderWidth;
    dispatchParameters.renderSize.height = resInfo.RenderHeight;

    // Note, inverted depth and display mode are currently handled statically for the run of the sample.
    // If they become changeable at runtime, we'll need to modify how this information is queried
    static bool s_InvertedDepth = GetConfig()->InvertedDepth;

    // Setup camera params as required
    dispatchParameters.cameraFovAngleVertical = pCamera->GetFovY();
    if (s_InvertedDepth)
    {
        dispatchParameters.cameraFar  = pCamera->GetNearPlane();
        dispatchParameters.cameraNear = FLT_MAX;
    }
    else
    {
        dispatchParameters.cameraFar  = pCamera->GetFarPlane();
        dispatchParameters.cameraNear = pCamera->GetNearPlane();
    }

    FfxErrorCode errorCode = ffxFsr2ContextDispatch(&m_FSR2Context, &dispatchParameters);
    FFX_ASSERT(errorCode == FFX_OK);

    // FidelityFX contexts modify the set resource view heaps, so set the cauldron one back
    SetAllResourceViewHeaps(pCmdList);

    // We are now done with upscaling
    GetFramework()->SetUpscalingState(UpscalerState::PostUpscale);
}
