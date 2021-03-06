// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/basictypes.h"
#include "base/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/memory/scoped_ptr.h"
#include "base/message_loop/message_loop.h"
#include "base/message_loop/message_loop_proxy.h"
#include "base/run_loop.h"
#include "content/public/test/test_file_system_context.h"
#include "content/public/test/test_file_system_options.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "webkit/browser/fileapi/async_file_test_helper.h"
#include "webkit/browser/fileapi/file_system_context.h"
#include "webkit/browser/fileapi/obfuscated_file_util.h"
#include "webkit/browser/fileapi/plugin_private_file_system_backend.h"

namespace fileapi {

namespace {

const GURL kOrigin("http://www.example.com");
const std::string kPlugin1("plugin1");
const std::string kPlugin2("plugin2");
const FileSystemType kType = kFileSystemTypePluginPrivate;

void DidOpenFileSystem(GURL* root_url_out,
                       std::string* filesystem_id_out,
                       base::PlatformFileError* error_out,
                       const GURL& root_url,
                       const std::string& filesystem_id,
                       base::PlatformFileError error) {
  *root_url_out = root_url;
  *filesystem_id_out = filesystem_id;
  *error_out = error;
}

}  // namespace

class PluginPrivateFileSystemBackendTest : public testing::Test {
 protected:
  virtual void SetUp() OVERRIDE {
    ASSERT_TRUE(data_dir_.CreateUniqueTempDir());
    context_ = CreateFileSystemContextForTesting(
        NULL /* quota_manager_proxy */,
        data_dir_.path());
  }

  FileSystemURL CreateURL(const GURL& root_url, const std::string& relative) {
    FileSystemURL root = context_->CrackURL(root_url);
    return context_->CreateCrackedFileSystemURL(
        root.origin(),
        root.mount_type(),
        root.virtual_path().AppendASCII(relative));
  }

  PluginPrivateFileSystemBackend* backend() const {
    return context_->plugin_private_backend();
  }

  const base::FilePath& base_path() const { return backend()->base_path(); }

  base::ScopedTempDir data_dir_;
  base::MessageLoop message_loop_;
  scoped_refptr<FileSystemContext> context_;
};

TEST_F(PluginPrivateFileSystemBackendTest, OpenFileSystemBasic) {
  GURL root_url;
  std::string filesystem_id;
  base::PlatformFileError error = base::PLATFORM_FILE_ERROR_FAILED;

  backend()->OpenPrivateFileSystem(
      kOrigin, kType, kPlugin1, OPEN_FILE_SYSTEM_CREATE_IF_NONEXISTENT,
      base::Bind(&DidOpenFileSystem, &root_url, &filesystem_id, &error));
  base::RunLoop().RunUntilIdle();
  ASSERT_EQ(base::PLATFORM_FILE_OK, error);

  // Run this again with FAIL_IF_NONEXISTENT to see if it succeeds.
  error = base::PLATFORM_FILE_ERROR_FAILED;
  backend()->OpenPrivateFileSystem(
      kOrigin, kType, kPlugin1, OPEN_FILE_SYSTEM_FAIL_IF_NONEXISTENT,
      base::Bind(&DidOpenFileSystem, &root_url, &filesystem_id, &error));
  base::RunLoop().RunUntilIdle();
  ASSERT_EQ(base::PLATFORM_FILE_OK, error);

  FileSystemURL file = CreateURL(root_url, "foo");
  base::FilePath platform_path;
  EXPECT_EQ(base::PLATFORM_FILE_OK,
            AsyncFileTestHelper::CreateFile(context_.get(), file));
  EXPECT_EQ(base::PLATFORM_FILE_OK,
            AsyncFileTestHelper::GetPlatformPath(context_.get(), file,
                                                 &platform_path));
  EXPECT_TRUE(base_path().AppendASCII("000").AppendASCII(kPlugin1).IsParent(
      platform_path));
}

TEST_F(PluginPrivateFileSystemBackendTest, PluginIsolation) {
  GURL root_url1, root_url2;
  std::string filesystem_id1, filesystem_id2;

  // Open filesystem for kPlugin1 and kPlugin2.
  base::PlatformFileError error = base::PLATFORM_FILE_ERROR_FAILED;
  backend()->OpenPrivateFileSystem(
      kOrigin, kType, kPlugin1, OPEN_FILE_SYSTEM_CREATE_IF_NONEXISTENT,
      base::Bind(&DidOpenFileSystem, &root_url1, &filesystem_id1, &error));
  base::RunLoop().RunUntilIdle();
  ASSERT_EQ(base::PLATFORM_FILE_OK, error);

  error = base::PLATFORM_FILE_ERROR_FAILED;
  backend()->OpenPrivateFileSystem(
      kOrigin, kType, kPlugin2, OPEN_FILE_SYSTEM_CREATE_IF_NONEXISTENT,
      base::Bind(&DidOpenFileSystem, &root_url2, &filesystem_id2, &error));
  base::RunLoop().RunUntilIdle();
  ASSERT_EQ(base::PLATFORM_FILE_OK, error);

  // Create 'foo' in kPlugin1.
  FileSystemURL file1 = CreateURL(root_url1, "foo");
  base::FilePath platform_path;
  EXPECT_EQ(base::PLATFORM_FILE_OK,
            AsyncFileTestHelper::CreateFile(context_.get(), file1));
  EXPECT_TRUE(AsyncFileTestHelper::FileExists(
      context_.get(), file1, AsyncFileTestHelper::kDontCheckSize));

  // See the same path is not available in kPlugin2.
  FileSystemURL file2 = CreateURL(root_url2, "foo");
  EXPECT_FALSE(AsyncFileTestHelper::FileExists(
      context_.get(), file2, AsyncFileTestHelper::kDontCheckSize));
}

// TODO(kinuko,nhiroki): also test if DeleteOriginDataOnFileThread
// works fine when there's multiple plugin partitions.

}  // namespace fileapi
