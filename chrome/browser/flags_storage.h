// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_FLAGS_STORAGE_H_
#define CHROME_BROWSER_FLAGS_STORAGE_H_

#include <set>
#include <string>

namespace about_flags {

// Base class for flags storage implementations.  Enables the about_flags
// functions to store and retrieve data from various sources like PrefService
// and CrosSettings.
class FlagsStorage {
 public:
  virtual ~FlagsStorage() {}

  // Retrieves the flags as a set of strings.
  virtual std::set<std::string> GetFlags() = 0;
  // Stores the |flags| and returns true on success.
  virtual bool SetFlags(const std::set<std::string>& flags) = 0;
};

}  // namespace about_flags

#endif  // CHROME_BROWSER_FLAGS_STORAGE_H_
