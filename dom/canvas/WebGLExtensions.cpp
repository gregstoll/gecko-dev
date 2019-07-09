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
    : WebGLContextBoundObject(context), mIsLost(false) {}

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

// List the IDs of any extensions that should be implicitly activated when
// an extension is activated.
#define DEFINE_WEBGL_EXTENSION_CLASS_MAP_ENTRY(_extensionID, _implicit...)  \
  const WebGLExtensionID WebGLExtensionClassMap<                            \
      WebGLExtensionID::_extensionID>::implicitlyActivates[] = {_implicit}; \
  const size_t WebGLExtensionClassMap<                                      \
      WebGLExtensionID::_extensionID>::nImplicitlyActivates =               \
      sizeof(WebGLExtensionClassMap<                                        \
             WebGLExtensionID::_extensionID>::implicitlyActivates) /        \
      sizeof(WebGLExtensionID);

DEFINE_WEBGL_EXTENSION_CLASS_MAP_ENTRY(ANGLE_instanced_arrays)
DEFINE_WEBGL_EXTENSION_CLASS_MAP_ENTRY(EXT_blend_minmax)
DEFINE_WEBGL_EXTENSION_CLASS_MAP_ENTRY(EXT_color_buffer_float,
                                       WebGLExtensionID::EXT_float_blend)
DEFINE_WEBGL_EXTENSION_CLASS_MAP_ENTRY(EXT_color_buffer_half_float)
DEFINE_WEBGL_EXTENSION_CLASS_MAP_ENTRY(EXT_texture_compression_bptc)
DEFINE_WEBGL_EXTENSION_CLASS_MAP_ENTRY(EXT_texture_compression_rgtc)
DEFINE_WEBGL_EXTENSION_CLASS_MAP_ENTRY(EXT_float_blend)
DEFINE_WEBGL_EXTENSION_CLASS_MAP_ENTRY(EXT_frag_depth)
DEFINE_WEBGL_EXTENSION_CLASS_MAP_ENTRY(EXT_sRGB)
DEFINE_WEBGL_EXTENSION_CLASS_MAP_ENTRY(EXT_shader_texture_lod)
DEFINE_WEBGL_EXTENSION_CLASS_MAP_ENTRY(EXT_texture_filter_anisotropic)
DEFINE_WEBGL_EXTENSION_CLASS_MAP_ENTRY(EXT_disjoint_timer_query)
DEFINE_WEBGL_EXTENSION_CLASS_MAP_ENTRY(MOZ_debug)
DEFINE_WEBGL_EXTENSION_CLASS_MAP_ENTRY(OES_element_index_uint)
DEFINE_WEBGL_EXTENSION_CLASS_MAP_ENTRY(OES_fbo_render_mipmap)
DEFINE_WEBGL_EXTENSION_CLASS_MAP_ENTRY(OES_standard_derivatives)
DEFINE_WEBGL_EXTENSION_CLASS_MAP_ENTRY(
    OES_texture_float, WebGLExtensionID::WEBGL_color_buffer_float,
    WebGLExtensionID::EXT_float_blend)
DEFINE_WEBGL_EXTENSION_CLASS_MAP_ENTRY(OES_texture_float_linear)
DEFINE_WEBGL_EXTENSION_CLASS_MAP_ENTRY(
    OES_texture_half_float, WebGLExtensionID::EXT_color_buffer_half_float)
DEFINE_WEBGL_EXTENSION_CLASS_MAP_ENTRY(OES_texture_half_float_linear)
DEFINE_WEBGL_EXTENSION_CLASS_MAP_ENTRY(OES_vertex_array_object)
DEFINE_WEBGL_EXTENSION_CLASS_MAP_ENTRY(WEBGL_color_buffer_float,
                                       WebGLExtensionID::EXT_float_blend)
DEFINE_WEBGL_EXTENSION_CLASS_MAP_ENTRY(WEBGL_compressed_texture_astc)
DEFINE_WEBGL_EXTENSION_CLASS_MAP_ENTRY(WEBGL_compressed_texture_etc)
DEFINE_WEBGL_EXTENSION_CLASS_MAP_ENTRY(WEBGL_compressed_texture_etc1)
DEFINE_WEBGL_EXTENSION_CLASS_MAP_ENTRY(WEBGL_compressed_texture_pvrtc)
DEFINE_WEBGL_EXTENSION_CLASS_MAP_ENTRY(WEBGL_compressed_texture_s3tc)
DEFINE_WEBGL_EXTENSION_CLASS_MAP_ENTRY(WEBGL_compressed_texture_s3tc_srgb)
DEFINE_WEBGL_EXTENSION_CLASS_MAP_ENTRY(WEBGL_debug_renderer_info)
DEFINE_WEBGL_EXTENSION_CLASS_MAP_ENTRY(WEBGL_debug_shaders)
DEFINE_WEBGL_EXTENSION_CLASS_MAP_ENTRY(WEBGL_depth_texture)
DEFINE_WEBGL_EXTENSION_CLASS_MAP_ENTRY(WEBGL_draw_buffers)
DEFINE_WEBGL_EXTENSION_CLASS_MAP_ENTRY(WEBGL_lose_context)

#undef DEFINE_WEBGL_EXTENSION_CLASS_MAP_ENTRY

}  // namespace mozilla
