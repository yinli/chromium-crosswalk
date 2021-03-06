// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CC_TEST_FAKE_OUTPUT_SURFACE_CLIENT_H_
#define CC_TEST_FAKE_OUTPUT_SURFACE_CLIENT_H_

#include "cc/output/managed_memory_policy.h"
#include "cc/output/output_surface_client.h"

namespace cc {

class FakeOutputSurfaceClient : public OutputSurfaceClient {
 public:
  FakeOutputSurfaceClient()
      : begin_impl_frame_count_(0),
        deferred_initialize_result_(true),
        deferred_initialize_called_(false),
        did_lose_output_surface_called_(false),
        memory_policy_(0) {}

  virtual bool DeferredInitialize(
      scoped_refptr<ContextProvider> offscreen_context_provider) OVERRIDE;
  virtual void ReleaseGL() OVERRIDE {}
  virtual void SetNeedsRedrawRect(gfx::Rect damage_rect) OVERRIDE {}
  virtual void BeginImplFrame(const BeginFrameArgs& args) OVERRIDE;
  virtual void OnSwapBuffersComplete() OVERRIDE {}
  virtual void ReclaimResources(const CompositorFrameAck* ack) OVERRIDE {}
  virtual void DidLoseOutputSurface() OVERRIDE;
  virtual void SetExternalDrawConstraints(
      const gfx::Transform& transform,
      gfx::Rect viewport,
      gfx::Rect clip,
      bool valid_for_tile_management) OVERRIDE {}
  virtual void SetMemoryPolicy(const ManagedMemoryPolicy& policy) OVERRIDE;
  virtual void SetTreeActivationCallback(const base::Closure&) OVERRIDE {}

  int begin_impl_frame_count() {
    return begin_impl_frame_count_;
  }

  void set_deferred_initialize_result(bool result) {
    deferred_initialize_result_ = result;
  }

  bool deferred_initialize_called() {
    return deferred_initialize_called_;
  }

  bool did_lose_output_surface_called() {
    return did_lose_output_surface_called_;
  }

  const ManagedMemoryPolicy& memory_policy() const { return memory_policy_; }

 private:
  int begin_impl_frame_count_;
  bool deferred_initialize_result_;
  bool deferred_initialize_called_;
  bool did_lose_output_surface_called_;
  ManagedMemoryPolicy memory_policy_;
};

}  // namespace cc

#endif  // CC_TEST_FAKE_OUTPUT_SURFACE_CLIENT_H_
