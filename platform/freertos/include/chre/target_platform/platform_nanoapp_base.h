/*
 * Copyright (C) 2019 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef CHRE_PLATFORM_FREERTOS_PLATFORM_NANOAPP_BASE_H_
#define CHRE_PLATFORM_FREERTOS_PLATFORM_NANOAPP_BASE_H_

#include "chre/platform/shared/memory.h"
#include "chre/platform/shared/nanoapp_support_lib_dso.h"

namespace chre {

/**
 * FREERTOS-specific nanoapp functionality.
 */
class PlatformNanoappBase {
 public:
  /**
   * Associate this Nanoapp instance with a nanoapp that is statically built
   * into the CHRE binary with the given app info structure.
   */
  void loadStatic(const struct chreNslNanoappInfo *appInfo);

  /**
   * @return true if the app's binary data is resident in memory or if the app's
   *         filename is saved, i.e. all binary fragments are loaded through
   *         copyNanoappFragment, loadFromFile/loadStatic() was successful
   */
  bool isLoaded() const;

  /**
   * @return true if this app is loaded into TCM.
   */
  bool isTcmApp() const;

  /**
   * Sets app info that will be used later when the app is loaded into the
   * system.
   *
   * @param appId The unique app identifier associated with this binary
   * @param appVersion An application-defined version number
   * @param appFilename The filename of the app that should be loaded from disk
   * @param targetApiVersion The target API version the nanoapp was compiled for
   *
   * @return true if the info was successfully stored
   */
  bool setAppInfo(uint64_t appId, uint32_t appVersion, const char *appFilename,
                  uint32_t targetApiVersion);

  /**
   * Reserves buffer space for a nanoapp's binary. This method should be called
   * before copyNanoappFragment is called.
   *
   * @param appId The unique app identifier associated with this binary
   * @param appVersion An application-defined version number
   * @param appFlags The flags provided by the app being loaded
   * @param appBinaryLen Size of appBinary, in bytes
   * @param targetApiVersion The target API version of the nanoapp
   *
   * @return true if the allocation was successful, false otherwise
   */
  bool reserveBuffer(uint64_t appId, uint32_t appVersion, uint32_t appFlags,
                     size_t appBinaryLen, uint32_t targetApiVersion);

  /**
   * Copies the (possibly fragmented) application binary data into the allocated
   * buffer, and updates the pointer to the next address to write into. The
   * application may be invalid - full checking and initialization happens just
   * before invoking start() nanoapp entry point.
   *
   * @param buffer The pointer to the buffer
   * @param bufferSize The size of the buffer in bytes
   *
   * @return true if the reserved buffer did not overflow, false otherwise
   */
  bool copyNanoappFragment(const void *buffer, size_t bufferSize);

 protected:
  //! The app ID we received in the metadata alongside the nanoapp binary. This
  //! is also included in (and checked against) mAppInfo.
  uint64_t mExpectedAppId;

  //! The application-defined version number we received in the metadata
  //! alongside the nanoapp binary. This is also included in (and checked
  //! against) mAppInfo.
  uint32_t mExpectedAppVersion = 0;

  //! Whether the nanoapp is expected to be loaded into TCM.
  bool mExpectedTcmCapable = false;

  //! The app target API version in the metadata alongside the nanoapp binary.
  uint32_t mExpectedTargetApiVersion = 0;

  //! Whether this nanoapp is loaded into TCM.
  bool mIsTcmNanoapp = false;

  //! Buffer containing the complete DSO binary - only populated if
  //! copyNanoappFragment() was used to load this nanoapp
  void *mAppBinary = nullptr;
  size_t mAppBinaryLen = 0;

  //! Null-terminated ASCII string containing the file name that contains the
  //! app binary to be loaded. This is used over mAppBinary to load the nanoapp
  //! if set.
  char *mAppFilename = nullptr;

  //! The dynamic shared object (DSO) handle returned by dlopenbuf()
  void *mDsoHandle = nullptr;

  //! Pointer to the app info structure within this nanoapp
  const struct chreNslNanoappInfo *mAppInfo = nullptr;

  //! Pointer containing the unstable ID section for this nanoapp
  const char *mAppUnstableId = nullptr;

  //! Set to true if this app is built into the CHRE binary, and was loaded via
  //! loadStatic(). In this case, the member variables above are not valid or
  //! applicable.
  bool mIsStatic = false;

  //! The number of bytes of the binary that has been loaded so far.
  size_t mBytesLoaded = 0;

  /**
   * Loads the nanoapp symbols from the currently loaded binary and verifies
   * they match the expected information the nanoapp should have.
   *
   * @return true if the app info structure passed validation.
   */
  bool verifyNanoappInfo();

  /**
   * Calls through to openNanoappFromBuffer or openNanoappFromFile, depending on
   * how this nanoapp was loaded.
   *
   * @return true if the nanoapp is successfully opened.
   */
  bool openNanoapp();

  /**
   * Releases the DSO handle if it was active, by calling dlclose(). This will
   * result in execution of any unload handlers in the nanoapp.
   */
  void closeNanoapp();

  /**
   * Retrieves the nanoapp's version string. This is intended to be a human
   * readable version string to aid in debugging. This must always return a
   * valid string so if none is available it is recommended to return
   * "<undefined>" or similar.
   *
   * @param length The length of the returned version string
   * @return A char array containing the version string for this nanoapp.
   */
  const char *getAppVersionString(size_t *length) const;

  /**
   * Send the app id, token database offset and size to the host. Do nothing
   * if the token database section is not found in the nanoapp binary.
   */
  void sendTokenDatabaseInfo();

  /** If this app needs to access DRAM to function, enables DRAM access. */
  inline void enableDramAccessIfRequired() const {
    if (!isTcmApp()) {
      forceDramAccess();
    }
  }
};

}  // namespace chre

#endif  // CHRE_PLATFORM_FREERTOS_PLATFORM_NANOAPP_BASE_H_
