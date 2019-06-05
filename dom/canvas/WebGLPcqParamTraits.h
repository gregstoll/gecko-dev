/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef WEBGLPCQPARAMTRAITS_H_
#define WEBGLPCQPARAMTRAITS_H_

#include "mozilla/ipc/ProducerConsumerQueue.h"
#include "TexUnpackBlob.h"
#include "WebGLActiveInfo.h"
#include "WebGLContext.h"
#include "WebGLTypes.h"

namespace mozilla {

namespace ipc {
template <typename T>
struct PcqParamTraits;

template <typename WebGLType>
struct IsTriviallySerializable<WebGLId<WebGLType>> : TrueType {};

template <>
struct IsTriviallySerializable<FloatOrInt> : TrueType {};

template <>
struct IsTriviallySerializable<WebGLShaderPrecisionFormat> : TrueType {};

template <>
struct IsTriviallySerializable<WebGLContextOptions> : TrueType {};

template <>
struct IsTriviallySerializable<WebGLPixelStore> : TrueType {};

template <>
struct IsTriviallySerializable<WebGLTexImageData> : TrueType {};

template <>
struct IsTriviallySerializable<WebGLTexPboOffset> : TrueType {};

template <>
struct IsTriviallySerializable<SetDimensionsData> : TrueType {};

template <>
struct IsTriviallySerializable<ICRData> : TrueType {};

template <>
struct IsTriviallySerializable<gfx::IntSize> : TrueType {};

template <>
struct PcqParamTraits<ExtensionSets> {
  using ParamType = ExtensionSets;

  static PcqStatus Write(ProducerView& aProducerView, const ParamType& aArg) {
    PcqStatus status = aProducerView.WriteParam(aArg.mNonSystem);
    return IsSuccess(status) ? aProducerView.WriteParam(aArg.mSystem) : status;
  }

  static PcqStatus Read(ConsumerView& aConsumerView, ParamType* aArg) {
    PcqStatus status =
        aConsumerView.ReadParam(aArg ? &aArg->mNonSystem : nullptr);
    return IsSuccess(status)
               ? aConsumerView.ReadParam(aArg ? &aArg->mSystem : nullptr)
               : status;
  }

  template <typename View>
  static size_t MinSize(View& aView, const ParamType* aArg) {
    return aView.MinSizeParam(aArg ? (&aArg->mNonSystem) : nullptr) +
           aView.MinSizeParam(aArg ? (&aArg->mSystem) : nullptr);
  }
};

template <>
struct PcqParamTraits<WebGLActiveInfo> {
  using ParamType = WebGLActiveInfo;

  static PcqStatus Write(ProducerView& aProducerView, const ParamType& aArg) {
    PcqStatus status = aProducerView.WriteParam(aArg.mElemCount);
    status =
        IsSuccess(status) ? aProducerView.WriteParam(aArg.mElemType) : status;
    status = IsSuccess(status) ? aProducerView.WriteParam(aArg.mBaseUserName)
                               : status;
    status =
        IsSuccess(status) ? aProducerView.WriteParam(aArg.mIsArray) : status;
    status =
        IsSuccess(status) ? aProducerView.WriteParam(aArg.mElemSize) : status;
    status = IsSuccess(status) ? aProducerView.WriteParam(aArg.mBaseMappedName)
                               : status;
    return IsSuccess(status) ? aProducerView.WriteParam(aArg.mBaseType)
                             : status;
  }

  static PcqStatus Read(ConsumerView& aConsumerView, ParamType* aArg) {
    PcqStatus status =
        aConsumerView.ReadParam(aArg ? &aArg->mElemCount : nullptr);
    status = IsSuccess(status)
                 ? aConsumerView.ReadParam(aArg ? &aArg->mElemType : nullptr)
                 : status;
    status =
        IsSuccess(status)
            ? aConsumerView.ReadParam(aArg ? &aArg->mBaseUserName : nullptr)
            : status;
    status = IsSuccess(status)
                 ? aConsumerView.ReadParam(aArg ? &aArg->mIsArray : nullptr)
                 : status;
    status = IsSuccess(status)
                 ? aConsumerView.ReadParam(aArg ? &aArg->mElemSize : nullptr)
                 : status;
    status =
        IsSuccess(status)
            ? aConsumerView.ReadParam(aArg ? &aArg->mBaseMappedName : nullptr)
            : status;
    return IsSuccess(status)
               ? aConsumerView.ReadParam(aArg ? &aArg->mBaseType : nullptr)
               : status;
  }

  template <typename View>
  static size_t MinSize(View& aView, const ParamType* aArg) {
    return aView.MinSizeParam(aArg ? &aArg->mElemCount : nullptr) +
           aView.MinSizeParam(aArg ? &aArg->mElemType : nullptr) +
           aView.MinSizeParam(aArg ? &aArg->mBaseUserName : nullptr) +
           aView.MinSizeParam(aArg ? &aArg->mIsArray : nullptr) +
           aView.MinSizeParam(aArg ? &aArg->mElemSize : nullptr) +
           aView.MinSizeParam(aArg ? &aArg->mBaseMappedName : nullptr) +
           aView.MinSizeParam(aArg ? &aArg->mBaseType : nullptr);
  }
};

template <typename T>
struct PcqParamTraits<RawBuffer<T>> {
  using ParamType = RawBuffer<T>;

  static PcqStatus Write(ProducerView& aProducerView, const ParamType& aArg) {
    PcqStatus status = aProducerView.WriteParam(aArg.mLength);
    return ((aArg.mLength > 0) && IsSuccess(status))
               ? aProducerView.Write(aArg.mData, aArg.mLength * sizeof(T))
               : status;
  }

  template <typename ElementType =
                typename RemoveCV<typename ParamType::ElementType>::Type>
  static PcqStatus Read(ConsumerView& aConsumerView, ParamType* aArg) {
    size_t len;
    PcqStatus status = aConsumerView.ReadParam(&len);
    if ((len == 0) || (!IsSuccess(status))) {
      return status;
    }

    if (aArg) {
      auto data = new ElementType[len];
      if (!data) {
        return PcqStatus::PcqOOMError;
      }
      aArg->mData = data;
      aArg->mLength = len;
      aArg->mOwnsData = true;
      return aConsumerView.Read(data, len * sizeof(ElementType));
    }
    return aConsumerView.Read(nullptr, len * sizeof(ElementType));
  }

  template <typename View>
  static size_t MinSize(View& aView, const ParamType* aArg) {
    return aView.template MinSizeParam<size_t>() +
           aView.MinSizeBytes(aArg ? aArg->mLength : 0);
  }
};

// Specialization of PcqParamTraits that adapts the TexUnpack type in order to
// efficiently convert types.  For example, a TexUnpackSurface may deserialize
// as a TexUnpackBytes.
template <>
struct PcqParamTraits<WebGLTexUnpackVariant> {
  using ParamType = WebGLTexUnpackVariant;

  static PcqStatus Write(ProducerView& aProducerView, const ParamType& aArg) {
    MOZ_ASSERT_UNREACHABLE("TODO:");
    return PcqStatus::PcqFatalError;
  }

  static PcqStatus Read(ConsumerView& aConsumerView, ParamType* aArg) {
    MOZ_ASSERT_UNREACHABLE("TODO:");
    return PcqStatus::PcqFatalError;
  }

  template <typename View>
  static size_t MinSize(View& aView, const ParamType* aArg) {
    MOZ_ASSERT_UNREACHABLE("TODO:");
    return 0;
  }
};

}  // namespace ipc
}  // namespace mozilla

#endif  // WEBGLPCQPARAMTRAITS_H_
