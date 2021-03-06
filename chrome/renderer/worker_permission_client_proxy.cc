// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/common/render_messages.h"
#include "chrome/renderer/worker_permission_client_proxy.h"
#include "content/public/renderer/render_thread.h"
#include "content/public/renderer/render_view.h"
#include "ipc/ipc_sync_message_filter.h"
#include "third_party/WebKit/public/web/WebDocument.h"
#include "third_party/WebKit/public/web/WebFrame.h"
#include "third_party/WebKit/public/web/WebSecurityOrigin.h"

WorkerPermissionClientProxy::WorkerPermissionClientProxy(
    content::RenderView* render_view,
    WebKit::WebFrame* frame)
    : routing_id_(render_view->GetRoutingID()),
      is_unique_origin_(false) {
  if (frame->document().securityOrigin().isUnique() ||
      frame->top()->document().securityOrigin().isUnique())
    is_unique_origin_ = true;
  sync_message_filter_ = content::RenderThread::Get()->GetSyncMessageFilter();
  document_origin_url_ = GURL(frame->document().securityOrigin().toString());
  top_frame_origin_url_ = GURL(
      frame->top()->document().securityOrigin().toString());
}

WorkerPermissionClientProxy::~WorkerPermissionClientProxy() {}

bool WorkerPermissionClientProxy::allowDatabase(
    const WebKit::WebString& name,
    const WebKit::WebString& display_name,
    unsigned long estimated_size) {
  if (is_unique_origin_)
    return false;

  bool result = false;
  sync_message_filter_->Send(new ChromeViewHostMsg_AllowDatabase(
      routing_id_, document_origin_url_, top_frame_origin_url_,
      name, display_name, &result));
  return result;
}

bool WorkerPermissionClientProxy::allowFileSystem() {
  if (is_unique_origin_)
    return false;

  bool result = false;
  sync_message_filter_->Send(new ChromeViewHostMsg_AllowFileSystem(
      routing_id_, document_origin_url_, top_frame_origin_url_, &result));
  return result;
}

bool WorkerPermissionClientProxy::allowIndexedDB(
    const WebKit::WebString& name) {
  if (is_unique_origin_)
    return false;

  bool result = false;
  sync_message_filter_->Send(new ChromeViewHostMsg_AllowIndexedDB(
      routing_id_, document_origin_url_, top_frame_origin_url_, name, &result));
  return result;
}
