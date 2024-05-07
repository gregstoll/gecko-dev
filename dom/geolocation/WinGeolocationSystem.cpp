/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "GeolocationSystem.h"
#include "mozilla/dom/Promise.h"
#include "mozilla/MozPromise.h"

#include <windows.system.h>
#include <windows.security.authorization.appcapabilityaccess.h>
#include <wrl.h>

namespace mozilla::dom::geolocation {

using ABI::Windows::Foundation::GetActivationFactory;
using ABI::Windows::Security::Authorization::AppCapabilityAccess::AppCapability;
using ABI::Windows::Security::Authorization::AppCapabilityAccess::AppCapabilityAccessChangedEventArgs;
using ABI::Windows::Security::Authorization::AppCapabilityAccess::IAppCapability;
using ABI::Windows::Security::Authorization::AppCapabilityAccess::IAppCapabilityAccessChangedEventArgs;
using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;
using Microsoft::WRL::Wrappers::HStringReference;

using OpenSettingsPromise = MozPromise<bool, nsresult, true>;

NS_IMPL_ISUPPORTS0(LocationSettingsListener)

namespace {

/* static */
template <typename TypeToCreate>
ComPtr<TypeToCreate> CreateFromActivationFactory(const wchar_t* aNamespace) {
  ComPtr<TypeToCreate> newObject;
  GetActivationFactory(HStringReference(aNamespace).Get(), &newObject);
  return newObject;
}

// Allows callers of PresentSystemSettings to stop the AccessChanged listener
// registered with Windows.
class WindowsLocationSettingsListener final : public LocationSettingsListener {
public:
  explicit WindowsLocationSettingsListener(OpenSettingsPromise::Private* aOpenPromise) :
      mOpenPromise(aOpenPromise) {
  }

  void SetToken(EventRegistrationToken aToken) { mToken = aToken; }

  void Stop() override {
    // If the promise to open system settings is still waiting, reject it.
    if (mOpenPromise) {
      mOpenPromise->Reject(NS_ERROR_FAILURE, __func__);
      mOpenPromise = nullptr;
    }

    // If the promise resolved then we may be watching system settings.  Stop
    // doing that, too.
    if (mToken.value == 0) {
      // Not currently watching system settings.
      return;
    }

    ComPtr<IAppCapability> appCapability =
        CreateFromActivationFactory<IAppCapability>(InterfaceName_Windows_Security_Authorization_AppCapabilityAccess_IAppCapability);
    if (NS_WARN_IF(!appCapability)) {
      return;
    }

    appCapability->remove_AccessChanged(mToken);
    mToken = {};
  }

protected:
  virtual ~WindowsLocationSettingsListener() { Stop(); }

  RefPtr<OpenSettingsPromise::Private> mOpenPromise;
  EventRegistrationToken mToken = {};
};

/* static */
already_AddRefed<OpenSettingsPromise::Private> OpenWindowsLocationSettings() {
  using ABI::Windows::System::ILauncherStatics;
  ComPtr<ILauncherStatics> launcherStatics =
      CreateFromActivationFactory<ILauncherStatics>(RuntimeClass_Windows_System_Launcher);
  NS_ENSURE_TRUE(launcherStatics, nullptr);

  using ABI::Windows::Foundation::IUriRuntimeClass;
  using ABI::Windows::Foundation::IUriRuntimeClassFactory;
  ComPtr<IUriRuntimeClassFactory> uriFactory =
      CreateFromActivationFactory<IUriRuntimeClassFactory>(InterfaceName_Windows_Foundation_IUriRuntimeClassFactory);
  NS_ENSURE_TRUE(uriFactory, nullptr);
  RefPtr<IUriRuntimeClass> uri;
  HRESULT hr = uriFactory->CreateUri(HStringReference(L"ms-settings:privacy-location").Get(), getter_AddRefs(uri));
  if (FAILED(hr)) {
    return nullptr;
  }

  RefPtr<ABI::Windows::Foundation::IAsyncOperation<bool>> handler;
  hr = launcherStatics->LaunchUriAsync(uri, getter_AddRefs(handler));
  if (FAILED(hr)) {
    return nullptr;
  }

  RefPtr<OpenSettingsPromise::Private> promise =
      new OpenSettingsPromise::Private(__func__);
  using ABI::Windows::Foundation::IAsyncOperation;
  using ABI::Windows::Foundation::IAsyncOperationCompletedHandler;
  handler->put_Completed(
      Callback<IAsyncOperationCompletedHandler<bool>>(
          [promise](IAsyncOperation<bool>* asyncInfo, AsyncStatus status){
            unsigned char verdict = 0;
            asyncInfo->GetResults(&verdict);
            if (verdict) {
              promise->Resolve(true, __func__);
            } else {
              promise->Reject(NS_ERROR_FAILURE, __func__);
            }
            return S_OK;
          }).Get());
  return promise.forget();
}

}  // anon namespace

//-----------------------------------------------------------------------------

/* static */
bool SystemWillPromptForPermissionHint() {
  using ABI::Windows::Security::Authorization::AppCapabilityAccess::IAppCapabilityStatics;
  ComPtr<IAppCapabilityStatics> appCapabilityStatics =
      CreateFromActivationFactory<IAppCapabilityStatics>(InterfaceName_Windows_Security_Authorization_AppCapabilityAccess_IAppCapability);
  NS_ENSURE_TRUE(appCapabilityStatics, false);

  using ABI::Windows::System::IUserStatics2;
  ComPtr<IUserStatics2> userStatics =
      CreateFromActivationFactory<IUserStatics2>(InterfaceName_Windows_System_IUserStatics2);
  NS_ENSURE_TRUE(userStatics, false);

  using ABI::Windows::System::IUser;
  RefPtr<IUser> user;
  HRESULT hr = userStatics->GetDefault(getter_AddRefs(user));
  if (FAILED(hr)) {
    return false;
  }
  NS_ENSURE_TRUE(user, false);

  RefPtr<IAppCapability> appCapability;
  hr = appCapabilityStatics->CreateWithProcessIdForUser(
      user, HStringReference(L"wifiControl").Get(), ::GetCurrentProcessId(), getter_AddRefs(appCapability));
  if (FAILED(hr)) {
    return false;
  }
  NS_ENSURE_TRUE(appCapability, false);

  using ABI::Windows::Security::Authorization::AppCapabilityAccess::AppCapabilityAccessStatus;
  AppCapabilityAccessStatus status;
  hr = appCapability->CheckAccess(&status);
  if (FAILED(hr)) {
    return false;
  }
  return status == AppCapabilityAccessStatus::AppCapabilityAccessStatus_UserPromptRequired;
}

/* static */
bool LocationIsPermittedHint() {
  // TODO:
  return true;
}

/* static */
already_AddRefed<LocationSettingsListener>
PresentSystemSettings(BrowsingContext* aBC,
                      mozilla::dom::Promise* aSystemPermissionPromise) {
  RefPtr<OpenSettingsPromise::Private> openPromise = OpenWindowsLocationSettings();
  if (NS_WARN_IF(!openPromise)) {
    aSystemPermissionPromise->MaybeReject(NS_ERROR_FAILURE);
    return nullptr;
  }

  RefPtr<WindowsLocationSettingsListener> locationListener =
      new WindowsLocationSettingsListener(openPromise);
  openPromise->Then(
    GetCurrentSerialEventTarget(), __func__,
    [promise = RefPtr{aSystemPermissionPromise}, locationListener](bool aWasOpened){
      if (!aWasOpened) {
        promise->MaybeReject(NS_ERROR_FAILURE);
        return false;
      }
      ComPtr<IAppCapability> appCapability =
          CreateFromActivationFactory<IAppCapability>(InterfaceName_Windows_Security_Authorization_AppCapabilityAccess_IAppCapability);
      NS_ENSURE_TRUE(appCapability, false);
      EventRegistrationToken token{};

      using ABI::Windows::Foundation::ITypedEventHandler;
      using AccessChangedHandler = ITypedEventHandler<AppCapability*, AppCapabilityAccessChangedEventArgs*>;

      appCapability->add_AccessChanged(Callback<AccessChangedHandler>(
        [promise, locationListener](IAppCapability*, IAppCapabilityAccessChangedEventArgs*) {
          if (LocationIsPermittedHint()) {
            promise->MaybeResolve(kSystemPermissionGranted);
            locationListener->Stop();
          }
          return S_OK;
        }).Get(), &token);

      locationListener->SetToken(token);
      return token.value != 0;
    },
    [promise = RefPtr{aSystemPermissionPromise}](nsresult){
      promise->MaybeReject(NS_ERROR_FAILURE);
    });

  return locationListener.forget();
}

}  // namespace mozilla::dom::geolocation
