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

#include "common.hlsl"

DeferredPixelShaderOutput InsectPixelShader(const InsectVertex input)
{
    DeferredPixelShaderOutput output;

    output.motion        = input.clipSpaceMotion.xy;
    output.baseColor.rgb = input.color.rgb;
    output.baseColor.a   = 1.f;

    // compute normal from object space position derivatives
    output.normal.xyz = normalize(cross(ddy(input.objectSpacePosition.xyz), ddx(input.objectSpacePosition.xyz)));
    output.normal.w   = 1.0;

    return output;
}