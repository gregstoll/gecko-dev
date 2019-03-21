/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "WebGLExtensions.h"

#include "gfxPrefs.h"
#include "GLContext.h"
#include "mozilla/dom/WebGLRenderingContextBinding.h"
#include "WebGLContext.h"

namespace mozilla {

WebGLExtensionBase::WebGLExtensionBase(WebGLContext* context)
    : WebGLContextBoundObject<WebGLExtensionBase>(context), mIsLost(false) {}

WebGLExtensionBase::~WebGLExtensionBase() {}

void WebGLExtensionBase::MarkLost() {
  mIsLost = true;

  OnMarkLost();
}

// -

WebGLExtensionFloatBlend::WebGLExtensionFloatBlend(WebGLContext* const webgl)
    : WebGLExtensionBase(webgl) {
  MOZ_ASSERT(IsSupported(webgl), "Don't construct extension if unsupported.");
}

WebGLExtensionFloatBlend::~WebGLExtensionFloatBlend() = default;

bool WebGLExtensionFloatBlend::IsSupported(const WebGLContext* const webgl) {
  if (!gfxPrefs::WebGLDraftExtensionsEnabled()) return false;
  if (!WebGLExtensionColorBufferFloat::IsSupported(webgl) &&
      !WebGLExtensionEXTColorBufferFloat::IsSupported(webgl))
    return false;

  const auto& gl = webgl->gl;
  return !gl->IsGLES() || gl->IsANGLE() ||
         gl->IsExtensionSupported(gl::GLContext::EXT_float_blend);
}

// -

WebGLExtensionFBORenderMipmap::WebGLExtensionFBORenderMipmap(
    WebGLContext* const webgl)
    : WebGLExtensionBase(webgl) {
  MOZ_ASSERT(IsSupported(webgl), "Don't construct extension if unsupported.");
}

WebGLExtensionFBORenderMipmap::~WebGLExtensionFBORenderMipmap() = default;

bool WebGLExtensionFBORenderMipmap::IsSupported(
    const WebGLContext* const webgl) {
  if (webgl->IsWebGL2()) return false;
  if (!gfxPrefs::WebGLDraftExtensionsEnabled()) return false;

  const auto& gl = webgl->gl;
  if (!gl->IsGLES()) return true;
  if (gl->Version() >= 300) return true;
  return gl->IsExtensionSupported(gl::GLContext::OES_fbo_render_mipmap);
}

}  // namespace mozilla
