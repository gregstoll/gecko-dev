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
using ABI::Windows::Security::Authorization::AppCapabilityAccess::IAppCapabilityStatics;
using ABI::Windows::Security::Authorization::AppCapabilityAccess::IAppCapabilityAccessChangedEventArgs;
using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;
using Microsoft::WRL::Wrappers::HStringReference;

namespace {

/* static */
template <typename TypeToCreate>
ComPtr<TypeToCreate> CreateFromActivationFactory(const wchar_t* aNamespace) {
  ComPtr<TypeToCreate> newObject;
  GetActivationFactory(HStringReference(aNamespace).Get(), &newObject);
  return newObject;
}

RefPtr<
    ABI::Windows::Security::Authorization::AppCapabilityAccess::IAppCapability>
  GetWifiControlAppCapability() {
  ComPtr<IAppCapabilityStatics> appCapabilityStatics = CreateFromActivationFactory<
      IAppCapabilityStatics>(
      RuntimeClass_Windows_Security_Authorization_AppCapabilityAccess_AppCapability);
  NS_ENSURE_TRUE(appCapabilityStatics, nullptr);

  using ABI::Windows::System::IUserStatics2;
  ComPtr<IUserStatics2> userStatics =
      CreateFromActivationFactory<IUserStatics2>(
          RuntimeClass_Windows_System_User);
  NS_ENSURE_TRUE(userStatics, nullptr);

  using ABI::Windows::System::IUser;
  RefPtr<IUser> user;
  HRESULT hr = userStatics->GetDefault(getter_AddRefs(user));
  if (FAILED(hr)) {
    return nullptr;
  }
  NS_ENSURE_TRUE(user, nullptr);

  RefPtr<IAppCapability> appCapability;
  hr = appCapabilityStatics->CreateWithProcessIdForUser(
      user, HStringReference(L"wifiControl").Get(), ::GetCurrentProcessId(), getter_AddRefs(appCapability));
  if (FAILED(hr)) {
    return nullptr;
  }
  NS_ENSURE_TRUE(appCapability, nullptr);
  return appCapability;
}

class LocationSettingsListener final : public nsISupports {
public:
  NS_DECL_ISUPPORTS

  void SetToken(EventRegistrationToken aToken) { mToken = aToken; }

  void Stop() {
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

    RefPtr<IAppCapability> appCapability = GetWifiControlAppCapability();
    if (NS_WARN_IF(!appCapability)) {
      return;
    }

    appCapability->remove_AccessChanged(mToken);
    mToken = {};
  }

protected:
  virtual ~LocationSettingsListener() { Stop(); }

  RefPtr<OpenSettingsPromise::Private> mOpenPromise;
  EventRegistrationToken mToken = {};
};

NS_IMPL_ISUPPORTS0(LocationSettingsListener)

/* static */
already_AddRefed<OpenSettingsPromise::Private> OpenWindowsLocationSettings() {
  using ABI::Windows::System::ILauncherStatics;
  ComPtr<ILauncherStatics> launcherStatics =
      CreateFromActivationFactory<ILauncherStatics>(RuntimeClass_Windows_System_Launcher);
  NS_ENSURE_TRUE(launcherStatics, nullptr);

  using ABI::Windows::Foundation::IUriRuntimeClass;
  using ABI::Windows::Foundation::IUriRuntimeClassFactory;
  ComPtr<IUriRuntimeClassFactory> uriFactory =
      CreateFromActivationFactory<IUriRuntimeClassFactory>(RuntimeClass_Windows_Foundation_Uri);
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

static Maybe<ABI::Windows::Security::Authorization::AppCapabilityAccess::AppCapabilityAccessStatus> GetWifiControlAccess() {
  using ABI::Windows::Security::Authorization::AppCapabilityAccess::IAppCapabilityStatics;
  auto appCapability = GetWifiControlAppCapability();
  NS_ENSURE_TRUE(appCapability, Nothing());

  using ABI::Windows::Security::Authorization::AppCapabilityAccess::AppCapabilityAccessStatus;
  AppCapabilityAccessStatus status;
  HRESULT hr = appCapability->CheckAccess(&status);
  if (FAILED(hr)) {
    return Nothing();
  }
  return Some(status);
}

/* static */
bool SystemWillPromptForPermissionHint() {
  auto wifiAccess = GetWifiControlAccess();
  return wifiAccess ==
         mozilla::Some(ABI::Windows::Security::Authorization::
                           AppCapabilityAccess::AppCapabilityAccessStatus::
                               AppCapabilityAccessStatus_UserPromptRequired);
}

bool LocationIsPermittedHint() {
  auto wifiAccess = GetWifiControlAccess();
  // This API wasn't available on earlier versions of Windows,
  // so a failure to get the result means that location is permitted.
  return wifiAccess.isNothing() ||
         *wifiAccess ==
             ABI::Windows::Security::Authorization::AppCapabilityAccess::
                 AppCapabilityAccessStatus::AppCapabilityAccessStatus_Allowed;
}

already_AddRefed<OpenSettingsPromise::Private>
PresentSystemSettings() {
  RefPtr<OpenSettingsPromise::Private> openPromise = OpenWindowsLocationSettings();
  if (NS_WARN_IF(!openPromise)) {
    return nullptr;
  }

  // We need two promises because openPromise is resolved when the settings
  // window has opened and retPromise resolves when system permission is
  // granted (and rejects when the user presses cancel in the modal in
  // the Geolocation class).

  // This creates a circular reference between openPromise and retPromise
  // but this is ok because retPromise is guaranteed to be resolved or
  // rejected by the caller (if not by us) and it rejects openPromise
  // in either case. 
  RefPtr<OpenSettingsPromise::Private> retPromise =
      new OpenSettingsPromise::Private(__func__);

  RefPtr<LocationSettingsListener> locationListener =
      new LocationSettingsListener();
  openPromise->Then(
    GetCurrentSerialEventTarget(), __func__,
    [locationListener, retPromise](bool aWasOpened){
      if (!aWasOpened) {
        return false;
      }
      RefPtr<IAppCapability> appCapability = GetWifiControlAppCapability();
      if (!appCapability) {
        return false;
      }

      EventRegistrationToken token{};

      using ABI::Windows::Foundation::ITypedEventHandler;
      using AccessChangedHandler = ITypedEventHandler<AppCapability*, AppCapabilityAccessChangedEventArgs*>;

      appCapability->add_AccessChanged(Callback<AccessChangedHandler>(
        [locationListener, retPromise](IAppCapability*, IAppCapabilityAccessChangedEventArgs*) {
          if (LocationIsPermittedHint()) {
            retPromise->Resolve(kSystemPermissionGranted, __func__);
            locationListener->Stop();
          }
          return S_OK;
        }).Get(), &token);

      locationListener->SetToken(token);
      return token.value != 0;
    },
    [locationListener, retPromise](nsresult){
      locationListener->Stop();
      retPromise->Resolve(kSystemPermissionGranted, __func__);
    });

  retPromise->Then(
    GetCurrentSerialEventTarget(), __func__,
    [openPromise, locationListener](){
      // We got system permission.  Nothing to do here. We reject the promise
      // and stop the location listener solely as a hedge.  The reject should
      // simply be ignored.
      locationListener->Stop();
      openPromise->Reject(NS_ERROR_FAILURE, __func__);
    },
    [openPromise, locationListener](){
      // We were canceled or got an error.  Make sure we stop watching the
      // system setting, and don't start listening if openPromise hasn't
      // resolved yet.  (If it has then the call to Reject is ignored.)
      locationListener->Stop();
      openPromise->Reject(NS_ERROR_FAILURE, __func__);
    });
  return retPromise.forget();
}

}  // namespace mozilla::dom::geolocation
