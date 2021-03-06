// Copyright 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CC_OUTPUT_RENDERER_H_
#define CC_OUTPUT_RENDERER_H_

#include "base/basictypes.h"
#include "cc/base/cc_export.h"
#include "cc/quads/render_pass.h"
#include "cc/trees/layer_tree_host.h"

namespace cc {

class CompositorFrameAck;
class CompositorFrameMetadata;
class ScopedResource;

class CC_EXPORT RendererClient {
 public:
  // These return the draw viewport and clip in non-y-flipped window space.
  // Note that while a draw is in progress, these are guaranteed to be
  // contained within the output surface size.
  virtual gfx::Rect DeviceViewport() const = 0;
  virtual gfx::Rect DeviceClip() const = 0;
  virtual void SetFullRootLayerDamage() = 0;
  virtual CompositorFrameMetadata MakeCompositorFrameMetadata() const = 0;

 protected:
  virtual ~RendererClient() {}
};

class CC_EXPORT Renderer {
 public:
  virtual ~Renderer() {}

  virtual const RendererCapabilities& Capabilities() const = 0;

  virtual void ViewportChanged() {}

  virtual bool CanReadPixels() const = 0;

  virtual void DecideRenderPassAllocationsForFrame(
      const RenderPassList& render_passes_in_draw_order) {}
  virtual bool HasAllocatedResourcesForTesting(RenderPass::Id id) const;

  // This passes ownership of the render passes to the renderer. It should
  // consume them, and empty the list. The parameters here may change from frame
  // to frame and should not be cached.
  virtual void DrawFrame(RenderPassList* render_passes_in_draw_order,
                         ContextProvider* offscreen_context_provider,
                         float device_scale_factor,
                         bool allow_partial_swap,
                         bool disable_picture_quad_image_filtering) = 0;

  // Waits for rendering to finish.
  virtual void Finish() = 0;

  virtual void DoNoOp() {}

  // Puts backbuffer onscreen.
  virtual void SwapBuffers() = 0;
  virtual void ReceiveSwapBuffersAck(const CompositorFrameAck& ack) {}

  virtual void GetFramebufferPixels(void* pixels, gfx::Rect rect) = 0;

  virtual bool IsContextLost();

  virtual void SetVisible(bool visible) = 0;

  virtual void SendManagedMemoryStats(size_t bytes_visible,
                                      size_t bytes_visible_and_nearby,
                                      size_t bytes_allocated) = 0;

 protected:
  explicit Renderer(RendererClient* client, const LayerTreeSettings* settings)
      : client_(client), settings_(settings) {}

  RendererClient* client_;
  const LayerTreeSettings* settings_;

 private:
  DISALLOW_COPY_AND_ASSIGN(Renderer);
};

}  // namespace cc

#endif  // CC_OUTPUT_RENDERER_H_
