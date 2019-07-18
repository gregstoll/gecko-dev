/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "WebGLContext.h"
#include "WebGLContextEndpoint.h"
#include "WebGLContextUtils.h"
#include "WebGLExtensions.h"
#include "ClientWebGLExtensions.h"
#include "gfxPrefs.h"
#include "GLContext.h"

#include "nsString.h"
#include "nsContentUtils.h"
#include "mozilla/Preferences.h"
#include "mozilla/dom/BindingDeclarations.h"
#include "AccessCheck.h"

namespace mozilla {

/*static*/ const char* ClientWebGLContext::GetExtensionString(
    WebGLExtensionID ext) {
  typedef EnumeratedArray<WebGLExtensionID, WebGLExtensionID::Max, const char*>
      names_array_t;

  static names_array_t sExtensionNamesEnumeratedArray;
  static bool initialized = false;

  if (!initialized) {
    initialized = true;

#define WEBGL_EXTENSION_IDENTIFIER(x) \
  sExtensionNamesEnumeratedArray[WebGLExtensionID::x] = #x;

    WEBGL_EXTENSION_IDENTIFIER(ANGLE_instanced_arrays)
    WEBGL_EXTENSION_IDENTIFIER(EXT_blend_minmax)
    WEBGL_EXTENSION_IDENTIFIER(EXT_color_buffer_float)
    WEBGL_EXTENSION_IDENTIFIER(EXT_color_buffer_half_float)
    WEBGL_EXTENSION_IDENTIFIER(EXT_disjoint_timer_query)
    WEBGL_EXTENSION_IDENTIFIER(EXT_float_blend)
    WEBGL_EXTENSION_IDENTIFIER(EXT_frag_depth)
    WEBGL_EXTENSION_IDENTIFIER(EXT_shader_texture_lod)
    WEBGL_EXTENSION_IDENTIFIER(EXT_sRGB)
    WEBGL_EXTENSION_IDENTIFIER(EXT_texture_compression_bptc)
    WEBGL_EXTENSION_IDENTIFIER(EXT_texture_compression_rgtc)
    WEBGL_EXTENSION_IDENTIFIER(EXT_texture_filter_anisotropic)
    WEBGL_EXTENSION_IDENTIFIER(MOZ_debug)
    WEBGL_EXTENSION_IDENTIFIER(OES_element_index_uint)
    WEBGL_EXTENSION_IDENTIFIER(OES_fbo_render_mipmap)
    WEBGL_EXTENSION_IDENTIFIER(OES_standard_derivatives)
    WEBGL_EXTENSION_IDENTIFIER(OES_texture_float)
    WEBGL_EXTENSION_IDENTIFIER(OES_texture_float_linear)
    WEBGL_EXTENSION_IDENTIFIER(OES_texture_half_float)
    WEBGL_EXTENSION_IDENTIFIER(OES_texture_half_float_linear)
    WEBGL_EXTENSION_IDENTIFIER(OES_vertex_array_object)
    WEBGL_EXTENSION_IDENTIFIER(WEBGL_color_buffer_float)
    WEBGL_EXTENSION_IDENTIFIER(WEBGL_compressed_texture_astc)
    WEBGL_EXTENSION_IDENTIFIER(WEBGL_compressed_texture_etc)
    WEBGL_EXTENSION_IDENTIFIER(WEBGL_compressed_texture_etc1)
    WEBGL_EXTENSION_IDENTIFIER(WEBGL_compressed_texture_pvrtc)
    WEBGL_EXTENSION_IDENTIFIER(WEBGL_compressed_texture_s3tc)
    WEBGL_EXTENSION_IDENTIFIER(WEBGL_compressed_texture_s3tc_srgb)
    WEBGL_EXTENSION_IDENTIFIER(WEBGL_debug_renderer_info)
    WEBGL_EXTENSION_IDENTIFIER(WEBGL_debug_shaders)
    WEBGL_EXTENSION_IDENTIFIER(WEBGL_depth_texture)
    WEBGL_EXTENSION_IDENTIFIER(WEBGL_draw_buffers)
    WEBGL_EXTENSION_IDENTIFIER(WEBGL_lose_context)

#undef WEBGL_EXTENSION_IDENTIFIER
  }

  return sExtensionNamesEnumeratedArray[ext];
}

bool WebGLContext::IsExtensionSupported(dom::CallerType callerType,
                                        WebGLExtensionID ext) const {
  bool allowPrivilegedExts = false;

  // Chrome contexts need access to debug information even when
  // webgl.disable-extensions is set. This is used in the graphics
  // section of about:support
  if (callerType == dom::CallerType::System) {
    allowPrivilegedExts = true;
  }

  if (mPrefs.privilegedExtensionsEnabled) {
    allowPrivilegedExts = true;
  }

  if (allowPrivilegedExts) {
    switch (ext) {
      case WebGLExtensionID::EXT_disjoint_timer_query:
        return WebGLExtensionDisjointTimerQuery::IsSupported(this);
      case WebGLExtensionID::MOZ_debug:
        return true;
      case WebGLExtensionID::WEBGL_debug_renderer_info:
        return true;
      case WebGLExtensionID::WEBGL_debug_shaders:
        return true;
      default:
        // For warnings-as-errors.
        break;
    }
  }

  return IsExtensionSupported(ext);
}

bool WebGLContext::IsExtensionSupported(WebGLExtensionID ext) const {
  if (mDisableExtensions) return false;

  switch (ext) {
    // In alphabetical order
    // ANGLE_
    case WebGLExtensionID::ANGLE_instanced_arrays:
      return WebGLExtensionInstancedArrays::IsSupported(this);

    // EXT_
    case WebGLExtensionID::EXT_blend_minmax:
      return WebGLExtensionBlendMinMax::IsSupported(this);

    case WebGLExtensionID::EXT_color_buffer_float:
      return WebGLExtensionEXTColorBufferFloat::IsSupported(this);

    case WebGLExtensionID::EXT_color_buffer_half_float:
      return WebGLExtensionColorBufferHalfFloat::IsSupported(this);

    case WebGLExtensionID::EXT_float_blend:
      return WebGLExtensionFloatBlend::IsSupported(this);

    case WebGLExtensionID::EXT_frag_depth:
      return WebGLExtensionFragDepth::IsSupported(this);

    case WebGLExtensionID::EXT_shader_texture_lod:
      return WebGLExtensionShaderTextureLod::IsSupported(this);

    case WebGLExtensionID::EXT_sRGB:
      return WebGLExtensionSRGB::IsSupported(this);

    case WebGLExtensionID::EXT_texture_compression_bptc:
      return WebGLExtensionCompressedTextureBPTC::IsSupported(this);

    case WebGLExtensionID::EXT_texture_compression_rgtc:
      return WebGLExtensionCompressedTextureRGTC::IsSupported(this);

    case WebGLExtensionID::EXT_texture_filter_anisotropic:
      return gl->IsExtensionSupported(
          gl::GLContext::EXT_texture_filter_anisotropic);

    // OES_
    case WebGLExtensionID::OES_element_index_uint:
      if (IsWebGL2()) return false;
      return gl->IsSupported(gl::GLFeature::element_index_uint);

    case WebGLExtensionID::OES_fbo_render_mipmap:
      return WebGLExtensionFBORenderMipmap::IsSupported(this);

    case WebGLExtensionID::OES_standard_derivatives:
      if (IsWebGL2()) return false;
      return gl->IsSupported(gl::GLFeature::standard_derivatives);

    case WebGLExtensionID::OES_texture_float:
      return WebGLExtensionTextureFloat::IsSupported(this);

    case WebGLExtensionID::OES_texture_float_linear:
      return gl->IsSupported(gl::GLFeature::texture_float_linear);

    case WebGLExtensionID::OES_texture_half_float:
      return WebGLExtensionTextureHalfFloat::IsSupported(this);

    case WebGLExtensionID::OES_texture_half_float_linear:
      if (IsWebGL2()) return false;
      return gl->IsSupported(gl::GLFeature::texture_half_float_linear);

    case WebGLExtensionID::OES_vertex_array_object:
      return !IsWebGL2();  // Always supported in webgl1.

    // WEBGL_
    case WebGLExtensionID::WEBGL_color_buffer_float:
      return WebGLExtensionColorBufferFloat::IsSupported(this);

    case WebGLExtensionID::WEBGL_compressed_texture_astc:
      return WebGLExtensionCompressedTextureASTC::IsSupported(this);

    case WebGLExtensionID::WEBGL_compressed_texture_etc:
      return gl->IsSupported(gl::GLFeature::ES3_compatibility) &&
             !gl->IsANGLE();

    case WebGLExtensionID::WEBGL_compressed_texture_etc1:
      return gl->IsExtensionSupported(
                 gl::GLContext::OES_compressed_ETC1_RGB8_texture) &&
             !gl->IsANGLE();

    case WebGLExtensionID::WEBGL_compressed_texture_pvrtc:
      return gl->IsExtensionSupported(
          gl::GLContext::IMG_texture_compression_pvrtc);

    case WebGLExtensionID::WEBGL_compressed_texture_s3tc:
      return WebGLExtensionCompressedTextureS3TC::IsSupported(this);

    case WebGLExtensionID::WEBGL_compressed_texture_s3tc_srgb:
      return WebGLExtensionCompressedTextureS3TC_SRGB::IsSupported(this);

    case WebGLExtensionID::WEBGL_debug_renderer_info:
      return mPrefs.enableDebugRendererInfo &&
             !mPrefs.shouldResistFingerprinting;

    case WebGLExtensionID::WEBGL_debug_shaders:
      return !mPrefs.shouldResistFingerprinting;

    case WebGLExtensionID::WEBGL_depth_texture:
      return WebGLExtensionDepthTexture::IsSupported(this);

    case WebGLExtensionID::WEBGL_draw_buffers:
      return WebGLExtensionDrawBuffers::IsSupported(this);

    case WebGLExtensionID::WEBGL_lose_context:
      // We always support this extension.
      return true;

    case WebGLExtensionID::EXT_disjoint_timer_query:
    case WebGLExtensionID::MOZ_debug:
    case WebGLExtensionID::Max:
      return false;
  }

  return false;
}

static bool CompareWebGLExtensionName(const nsACString& name,
                                      const char* other) {
  return name.Equals(other, nsCaseInsensitiveCStringComparator());
}

void WebGLContext::EnableExtension(WebGLExtensionID ext,
                                   dom::CallerType callerType) {
#define WEBGL_GET_EXTENSION_CASE(x)                      \
  case WebGLExtensionID::x:                              \
    GetExtension<WebGLExtensionID::x>(true, callerType); \
    return;

  switch (ext) {
    // ANGLE
    WEBGL_GET_EXTENSION_CASE(ANGLE_instanced_arrays)

    // EXT
    WEBGL_GET_EXTENSION_CASE(EXT_blend_minmax)
    WEBGL_GET_EXTENSION_CASE(EXT_color_buffer_float)
    WEBGL_GET_EXTENSION_CASE(EXT_color_buffer_half_float)
    WEBGL_GET_EXTENSION_CASE(EXT_disjoint_timer_query)
    WEBGL_GET_EXTENSION_CASE(EXT_float_blend)
    WEBGL_GET_EXTENSION_CASE(EXT_frag_depth)
    WEBGL_GET_EXTENSION_CASE(EXT_shader_texture_lod)
    WEBGL_GET_EXTENSION_CASE(EXT_sRGB)
    WEBGL_GET_EXTENSION_CASE(EXT_texture_compression_bptc)
    WEBGL_GET_EXTENSION_CASE(EXT_texture_compression_rgtc)
    WEBGL_GET_EXTENSION_CASE(EXT_texture_filter_anisotropic)

    // MOZ
    WEBGL_GET_EXTENSION_CASE(MOZ_debug)

    // OES
    WEBGL_GET_EXTENSION_CASE(OES_element_index_uint)
    WEBGL_GET_EXTENSION_CASE(OES_fbo_render_mipmap)
    WEBGL_GET_EXTENSION_CASE(OES_standard_derivatives)
    WEBGL_GET_EXTENSION_CASE(OES_texture_float)
    WEBGL_GET_EXTENSION_CASE(OES_texture_float_linear)
    WEBGL_GET_EXTENSION_CASE(OES_texture_half_float)
    WEBGL_GET_EXTENSION_CASE(OES_texture_half_float_linear)
    WEBGL_GET_EXTENSION_CASE(OES_vertex_array_object)

    // WEBGL
    WEBGL_GET_EXTENSION_CASE(WEBGL_color_buffer_float)
    WEBGL_GET_EXTENSION_CASE(WEBGL_compressed_texture_astc)
    WEBGL_GET_EXTENSION_CASE(WEBGL_compressed_texture_etc)
    WEBGL_GET_EXTENSION_CASE(WEBGL_compressed_texture_etc1)
    WEBGL_GET_EXTENSION_CASE(WEBGL_compressed_texture_pvrtc)
    WEBGL_GET_EXTENSION_CASE(WEBGL_compressed_texture_s3tc)
    WEBGL_GET_EXTENSION_CASE(WEBGL_compressed_texture_s3tc_srgb)
    WEBGL_GET_EXTENSION_CASE(WEBGL_debug_renderer_info)
    WEBGL_GET_EXTENSION_CASE(WEBGL_debug_shaders)
    WEBGL_GET_EXTENSION_CASE(WEBGL_depth_texture)
    WEBGL_GET_EXTENSION_CASE(WEBGL_draw_buffers)
    WEBGL_GET_EXTENSION_CASE(WEBGL_lose_context)
    default:
      MOZ_ASSERT_UNREACHABLE("Illegal extension value");
  }
}

void ClientWebGLContext::GetExtension(JSContext* cx, const nsAString& wideName,
                                      JS::MutableHandle<JSObject*> retval,
                                      dom::CallerType callerType,
                                      ErrorResult& rv) {
  retval.set(nullptr);
  const FuncScope funcScope(this, "getExtension");

  NS_LossyConvertUTF16toASCII name(wideName);

  WebGLExtensionID ext = WebGLExtensionID::Max;

  // step 1: figure what extension is wanted
  for (size_t i = 0; i < size_t(WebGLExtensionID::Max); i++) {
    WebGLExtensionID extension = WebGLExtensionID(i);

    if (CompareWebGLExtensionName(name, GetExtensionString(extension))) {
      ext = extension;
      break;
    }
  }

  if (ext == WebGLExtensionID::Max) return;

  // step 2: If we have permission to use the extension and if the extension
  // hadn't been previously been created then we have to tell the host to
  // activate it.
  RefPtr<ClientWebGLExtensionBase> extObj = GetExtension(callerType, ext, true);
  if (!extObj) return;

  retval.set(WebGLObjectAsJSObject(cx, std::move(extObj), rv));
}

void WebGLContext::CreateExtension(WebGLExtensionID ext) {
  MOZ_ASSERT(IsExtensionEnabled(ext) == false);

  WebGLExtensionBase* obj = nullptr;
  switch (ext) {
    // ANGLE_
    case WebGLExtensionID::ANGLE_instanced_arrays:
      obj = new WebGLExtensionInstancedArrays(this);
      break;

    // EXT_
    case WebGLExtensionID::EXT_blend_minmax:
      obj = new WebGLExtensionBlendMinMax(this);
      break;
    case WebGLExtensionID::EXT_color_buffer_float:
      obj = new WebGLExtensionEXTColorBufferFloat(this);
      break;
    case WebGLExtensionID::EXT_color_buffer_half_float:
      obj = new WebGLExtensionColorBufferHalfFloat(this);
      break;
    case WebGLExtensionID::EXT_disjoint_timer_query:
      obj = new WebGLExtensionDisjointTimerQuery(this);
      break;
    case WebGLExtensionID::EXT_float_blend:
      obj = new WebGLExtensionFloatBlend(this);
      break;
    case WebGLExtensionID::EXT_frag_depth:
      obj = new WebGLExtensionFragDepth(this);
      break;
    case WebGLExtensionID::EXT_shader_texture_lod:
      obj = new WebGLExtensionShaderTextureLod(this);
      break;
    case WebGLExtensionID::EXT_sRGB:
      obj = new WebGLExtensionSRGB(this);
      break;
    case WebGLExtensionID::EXT_texture_compression_bptc:
      obj = new WebGLExtensionCompressedTextureBPTC(this);
      break;
    case WebGLExtensionID::EXT_texture_compression_rgtc:
      obj = new WebGLExtensionCompressedTextureRGTC(this);
      break;
    case WebGLExtensionID::EXT_texture_filter_anisotropic:
      obj = new WebGLExtensionTextureFilterAnisotropic(this);
      break;

    // MOZ_
    case WebGLExtensionID::MOZ_debug:
      obj = new WebGLExtensionMOZDebug(this);
      break;

    // OES_
    case WebGLExtensionID::OES_element_index_uint:
      obj = new WebGLExtensionElementIndexUint(this);
      break;
    case WebGLExtensionID::OES_fbo_render_mipmap:
      obj = new WebGLExtensionFBORenderMipmap(this);
      break;
    case WebGLExtensionID::OES_standard_derivatives:
      obj = new WebGLExtensionStandardDerivatives(this);
      break;
    case WebGLExtensionID::OES_texture_float:
      obj = new WebGLExtensionTextureFloat(this);
      break;
    case WebGLExtensionID::OES_texture_float_linear:
      obj = new WebGLExtensionTextureFloatLinear(this);
      break;
    case WebGLExtensionID::OES_texture_half_float:
      obj = new WebGLExtensionTextureHalfFloat(this);
      break;
    case WebGLExtensionID::OES_texture_half_float_linear:
      obj = new WebGLExtensionTextureHalfFloatLinear(this);
      break;
    case WebGLExtensionID::OES_vertex_array_object:
      obj = new WebGLExtensionVertexArray(this);
      break;

    // WEBGL_
    case WebGLExtensionID::WEBGL_color_buffer_float:
      obj = new WebGLExtensionColorBufferFloat(this);
      break;
    case WebGLExtensionID::WEBGL_compressed_texture_astc:
      obj = new WebGLExtensionCompressedTextureASTC(this);
      break;
    case WebGLExtensionID::WEBGL_compressed_texture_etc:
      obj = new WebGLExtensionCompressedTextureES3(this);
      break;
    case WebGLExtensionID::WEBGL_compressed_texture_etc1:
      obj = new WebGLExtensionCompressedTextureETC1(this);
      break;
    case WebGLExtensionID::WEBGL_compressed_texture_pvrtc:
      obj = new WebGLExtensionCompressedTexturePVRTC(this);
      break;
    case WebGLExtensionID::WEBGL_compressed_texture_s3tc:
      obj = new WebGLExtensionCompressedTextureS3TC(this);
      break;
    case WebGLExtensionID::WEBGL_compressed_texture_s3tc_srgb:
      obj = new WebGLExtensionCompressedTextureS3TC_SRGB(this);
      break;
    case WebGLExtensionID::WEBGL_debug_renderer_info:
      obj = new WebGLExtensionDebugRendererInfo(this);
      break;
    case WebGLExtensionID::WEBGL_debug_shaders:
      obj = new WebGLExtensionDebugShaders(this);
      break;
    case WebGLExtensionID::WEBGL_depth_texture:
      obj = new WebGLExtensionDepthTexture(this);
      break;
    case WebGLExtensionID::WEBGL_draw_buffers:
      obj = new WebGLExtensionDrawBuffers(this);
      break;
    case WebGLExtensionID::WEBGL_lose_context:
      obj = new WebGLExtensionLoseContext(this);
      break;

    case WebGLExtensionID::Max:
      MOZ_CRASH();
  }

  mExtensions[ext] = obj;
}

const Maybe<ExtensionSets> WebGLContext::GetSupportedExtensions() {
  const FuncScope funcScope(*this, "getSupportedExtensions");
  if (IsContextLost()) return Nothing();

  Maybe<ExtensionSets> ret = Some(ExtensionSets());
  auto& sets = ret.ref();
  for (size_t i = 0; i < size_t(WebGLExtensionID::Max); i++) {
    const auto extension = WebGLExtensionID(i);
    if (IsExtensionSupported(dom::CallerType::NonSystem, extension)) {
      sets.mNonSystem.AppendElement(extension);
    } else if (IsExtensionSupported(dom::CallerType::System, extension)) {
      sets.mSystem.AppendElement(extension);
    }
  }
  return ret;
}

RefPtr<ClientWebGLExtensionBase> ClientWebGLContext::UseExtension(
    WebGLExtensionID ext) {
  if (!mEnabledExtensions[static_cast<uint8_t>(ext)]) {
    return nullptr;
  }

  ClientWebGLExtensionBase* ret = nullptr;
  if (ext < WebGLExtensionID::Max) {
    ret = mExtensions[static_cast<size_t>(ext)];
    if (ret) {
      return ret;
    }
  }

  switch (ext) {
    // ANGLE_
    case WebGLExtensionID::ANGLE_instanced_arrays:
      ret = new ClientWebGLExtensionInstancedArrays(this);
      break;

    // EXT_
    case WebGLExtensionID::EXT_blend_minmax:
      ret = new ClientWebGLExtensionBlendMinMax(this);
      break;
    case WebGLExtensionID::EXT_color_buffer_float:
      ret = new ClientWebGLExtensionEXTColorBufferFloat(this);
      break;
    case WebGLExtensionID::EXT_color_buffer_half_float:
      ret = new ClientWebGLExtensionColorBufferHalfFloat(this);
      break;
    case WebGLExtensionID::EXT_disjoint_timer_query:
      ret = new ClientWebGLExtensionDisjointTimerQuery(this);
      break;
    case WebGLExtensionID::EXT_float_blend:
      ret = new ClientWebGLExtensionFloatBlend(this);
      break;
    case WebGLExtensionID::EXT_frag_depth:
      ret = new ClientWebGLExtensionFragDepth(this);
      break;
    case WebGLExtensionID::EXT_shader_texture_lod:
      ret = new ClientWebGLExtensionShaderTextureLod(this);
      break;
    case WebGLExtensionID::EXT_sRGB:
      ret = new ClientWebGLExtensionSRGB(this);
      break;
    case WebGLExtensionID::EXT_texture_compression_bptc:
      ret = new ClientWebGLExtensionCompressedTextureBPTC(this);
      break;
    case WebGLExtensionID::EXT_texture_compression_rgtc:
      ret = new ClientWebGLExtensionCompressedTextureRGTC(this);
      break;
    case WebGLExtensionID::EXT_texture_filter_anisotropic:
      ret = new ClientWebGLExtensionTextureFilterAnisotropic(this);
      break;

    // MOZ_
    case WebGLExtensionID::MOZ_debug:
      ret = new ClientWebGLExtensionMOZDebug(this);
      break;

      // OES_
      break;
    case WebGLExtensionID::OES_element_index_uint:
      ret = new ClientWebGLExtensionElementIndexUint(this);
      break;
    case WebGLExtensionID::OES_fbo_render_mipmap:
      ret = new ClientWebGLExtensionFBORenderMipmap(this);
      break;
    case WebGLExtensionID::OES_standard_derivatives:
      ret = new ClientWebGLExtensionStandardDerivatives(this);
      break;
    case WebGLExtensionID::OES_texture_float:
      ret = new ClientWebGLExtensionTextureFloat(this);
      break;
    case WebGLExtensionID::OES_texture_float_linear:
      ret = new ClientWebGLExtensionTextureFloatLinear(this);
      break;
    case WebGLExtensionID::OES_texture_half_float:
      ret = new ClientWebGLExtensionTextureHalfFloat(this);
      break;
    case WebGLExtensionID::OES_texture_half_float_linear:
      ret = new ClientWebGLExtensionTextureHalfFloatLinear(this);
      break;
    case WebGLExtensionID::OES_vertex_array_object:
      ret = new ClientWebGLExtensionVertexArray(this);
      break;

    // WEBGL_
    case WebGLExtensionID::WEBGL_color_buffer_float:
      ret = new ClientWebGLExtensionColorBufferFloat(this);
      break;
    case WebGLExtensionID::WEBGL_compressed_texture_astc:
      ret = new ClientWebGLExtensionCompressedTextureASTC(this);
      break;
    case WebGLExtensionID::WEBGL_compressed_texture_etc:
      ret = new ClientWebGLExtensionCompressedTextureES3(this);
      break;
    case WebGLExtensionID::WEBGL_compressed_texture_etc1:
      ret = new ClientWebGLExtensionCompressedTextureETC1(this);
      break;
    case WebGLExtensionID::WEBGL_compressed_texture_pvrtc:
      ret = new ClientWebGLExtensionCompressedTexturePVRTC(this);
      break;
    case WebGLExtensionID::WEBGL_compressed_texture_s3tc:
      ret = new ClientWebGLExtensionCompressedTextureS3TC(this);
      break;
    case WebGLExtensionID::WEBGL_compressed_texture_s3tc_srgb:
      ret = new ClientWebGLExtensionCompressedTextureS3TC_SRGB(this);
      break;
    case WebGLExtensionID::WEBGL_debug_renderer_info:
      ret = new ClientWebGLExtensionDebugRendererInfo(this);
      break;
    case WebGLExtensionID::WEBGL_debug_shaders:
      ret = new ClientWebGLExtensionDebugShaders(this);
      break;
    case WebGLExtensionID::WEBGL_depth_texture:
      ret = new ClientWebGLExtensionDepthTexture(this);
      break;
    case WebGLExtensionID::WEBGL_draw_buffers:
      ret = new ClientWebGLExtensionDrawBuffers(this);
      break;
    case WebGLExtensionID::WEBGL_lose_context:
      ret = new ClientWebGLExtensionLoseContext(this);
      break;
    default:
      MOZ_ASSERT_UNREACHABLE("illegal extension enum");
  }

  if (ret) {
    mExtensions[static_cast<size_t>(ext)] = ret;
  }
  return ret;
}

}  // namespace mozilla
