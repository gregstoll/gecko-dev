/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "GeolocationSystem.h"
#include "mozilla/dom/Promise.h"

namespace mozilla::dom::geolocation {

NS_IMPL_ISUPPORTS0(LocationSettingsListener)

bool SystemWillPromptForPermissionHint() {
  return false;
}

bool LocationIsPermittedHint() {
  return true;
}

already_AddRefed<LocationSettingsListener>
PresentSystemSettings(BrowsingContext* aBC,
                      mozilla::dom::Promise* aSystemPermissionPromise) {
  MOZ_ASSERT(false, "Should not warn user of need for system location permission since we cannot open system settings on this platform.");
  aSystemPermissionPromise->MaybeResolve(kSystemPermissionGranted);
  return nullptr;
}

}  // namespace mozilla::dom::geolocation
