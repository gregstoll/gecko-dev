/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_GeolocationSystem_h
#define mozilla_dom_GeolocationSystem_h

// Microsoft's API Name hackery sucks
#undef CreateEvent

#include "mozilla/StaticPtr.h"
#include "nsCOMPtr.h"
#include "nsTArray.h"
#include "nsITimer.h"
#include "nsIObserver.h"
#include "nsIWeakReferenceUtils.h"
#include "nsWrapperCache.h"

#include "nsCycleCollectionParticipant.h"

#include "GeolocationPosition.h"
#include "GeolocationCoordinates.h"
#include "nsIDOMGeoPosition.h"
#include "nsIDOMGeoPositionCallback.h"
#include "nsIDOMGeoPositionErrorCallback.h"
#include "mozilla/dom/BindingDeclarations.h"
#include "mozilla/dom/GeolocationBinding.h"
#include "mozilla/dom/CallbackObject.h"

namespace mozilla::dom::geolocation {

// Value system settings promise resolves to when the user presses the cancel
// button.  See PresentSystemSettings.
const int32_t kSystemPermissionCanceled = 0;

// Value system settings promise resolves to when permission was given.
// See PresentSystemSettings.
const int32_t kSystemPermissionGranted = 1;

// Allows callers of PresentSystemSettings to stop any OS system settings
// listeners we registered for.  Listeners will be automatically unregistered
// if they exist when this object is destroyed.
class LocationSettingsListener : public nsISupports {
public:
  NS_DECL_ISUPPORTS

  // Stop listening.
  virtual void Stop() = 0;

protected:
  virtual ~LocationSettingsListener() = default;
};

/**
 * If true then expect that the system will request permission from the user
 * when geolocation or wifi adapter access is requested.  This is not
 * guaranteed to be accurate on all platforms but should not return
 * false positives.
 */
bool SystemWillPromptForPermissionHint();

/**
 * If true, the system will grant access to either geolocation or wifi
 * adapter scanning (which is used by the geolocation fallback MLSFallback).
 * It won't need to bother the user (if it did, this would return false).
 * This is not guaranteed to be accurate on all platforms but should
 * not return false negatives.
 */
bool LocationIsPermittedHint();

/**
 * Opens the system settings application to the right spot and waits for the
 * user to give us geolocation permission.  Callers can use the return value
 * to cancel listening for the settings change.  This method will always
 * reject the given promise whenever it returns null.
 */
already_AddRefed<LocationSettingsListener>
PresentSystemSettings(BrowsingContext* aBC,
                      mozilla::dom::Promise* aSystemPermissionPromise);

}  // namespace mozilla::dom

#endif /* mozilla_dom_GeolocationSystem_h */
