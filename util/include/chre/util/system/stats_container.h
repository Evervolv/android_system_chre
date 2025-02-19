/*
 * Copyright (C) 2021 The Android Open Source Project
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

#ifndef CHRE_UTIL_SYSTEM_STATS_CONTAINER_H_
#define CHRE_UTIL_SYSTEM_STATS_CONTAINER_H_

#include <type_traits>

#include "chre/util/macros.h"

namespace chre {

/**
 * A Stats tool used to collect and compute metrics of interests
 */

template <typename T>
class StatsContainer {
  static_assert(std::is_arithmetic<T>::value,
                "Type must support arithmetic operations");

 public:
  /**
   * @brief Construct a new Stats Container object
   */
  StatsContainer() {}

  /**
   * Add a new value to the metric collection and update max value
   *
   * @param value a T instance
   */
  void addValue(T value) {
    mMax = MAX(value, mMax);
  }

  /**
   * @return the max value
   */
  T getMax() const {
    return mMax;
  }

 private:
  //! Max of stats
  T mMax = 0;
};

}  // namespace chre

#endif  // CHRE_UTIL_SYSTEM_STATS_CONTAINER_H_
