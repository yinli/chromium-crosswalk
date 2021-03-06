// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Represents the browser side of the browser <--> renderer communication
// channel. There will be one RenderProcessHost per renderer process.

#include "content/browser/renderer_host/render_process_host_impl.h"

#include <algorithm>
#include <limits>
#include <vector>

#if defined(OS_POSIX)
#include <utility>  // for pair<>
#endif

#include "base/base_switches.h"
#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/callback.h"
#include "base/command_line.h"
#include "base/debug/trace_event.h"
#include "base/lazy_instance.h"
#include "base/logging.h"
#include "base/metrics/field_trial.h"
#include "base/metrics/histogram.h"
#include "base/path_service.h"
#include "base/platform_file.h"
#include "base/rand_util.h"
#include "base/stl_util.h"
#include "base/strings/string_util.h"
#include "base/supports_user_data.h"
#include "base/sys_info.h"
#include "base/threading/thread.h"
#include "base/threading/thread_restrictions.h"
#include "base/tracked_objects.h"
#include "cc/base/switches.h"
#include "content/browser/appcache/appcache_dispatcher_host.h"
#include "content/browser/appcache/chrome_appcache_service.h"
#include "content/browser/browser_main.h"
#include "content/browser/browser_main_loop.h"
#include "content/browser/browser_plugin/browser_plugin_geolocation_permission_context.h"
#include "content/browser/browser_plugin/browser_plugin_message_filter.h"
#include "content/browser/child_process_security_policy_impl.h"
#include "content/browser/device_orientation/device_motion_message_filter.h"
#include "content/browser/device_orientation/device_orientation_message_filter.h"
#include "content/browser/dom_storage/dom_storage_context_wrapper.h"
#include "content/browser/dom_storage/dom_storage_message_filter.h"
#include "content/browser/download/mhtml_generation_manager.h"
#include "content/browser/fileapi/chrome_blob_storage_context.h"
#include "content/browser/fileapi/fileapi_message_filter.h"
#include "content/browser/frame_host/render_frame_message_filter.h"
#include "content/browser/geolocation/geolocation_dispatcher_host.h"
#include "content/browser/gpu/compositor_util.h"
#include "content/browser/gpu/gpu_data_manager_impl.h"
#include "content/browser/gpu/gpu_process_host.h"
#include "content/browser/gpu/shader_disk_cache.h"
#include "content/browser/histogram_message_filter.h"
#include "content/browser/indexed_db/indexed_db_context_impl.h"
#include "content/browser/indexed_db/indexed_db_dispatcher_host.h"
#include "content/browser/loader/resource_message_filter.h"
#include "content/browser/loader/resource_scheduler_filter.h"
#include "content/browser/media/android/browser_demuxer_android.h"
#include "content/browser/media/media_internals.h"
#include "content/browser/message_port_message_filter.h"
#include "content/browser/mime_registry_message_filter.h"
#include "content/browser/plugin_service_impl.h"
#include "content/browser/profiler_message_filter.h"
#include "content/browser/quota_dispatcher_host.h"
#include "content/browser/renderer_host/clipboard_message_filter.h"
#include "content/browser/renderer_host/database_message_filter.h"
#include "content/browser/renderer_host/file_utilities_message_filter.h"
#include "content/browser/renderer_host/gamepad_browser_message_filter.h"
#include "content/browser/renderer_host/gpu_message_filter.h"
#include "content/browser/renderer_host/media/audio_input_renderer_host.h"
#include "content/browser/renderer_host/media/audio_mirroring_manager.h"
#include "content/browser/renderer_host/media/audio_renderer_host.h"
#include "content/browser/renderer_host/media/device_request_message_filter.h"
#include "content/browser/renderer_host/media/media_stream_dispatcher_host.h"
#include "content/browser/renderer_host/media/midi_dispatcher_host.h"
#include "content/browser/renderer_host/media/midi_host.h"
#include "content/browser/renderer_host/media/peer_connection_tracker_host.h"
#include "content/browser/renderer_host/media/video_capture_host.h"
#include "content/browser/renderer_host/memory_benchmark_message_filter.h"
#include "content/browser/renderer_host/p2p/socket_dispatcher_host.h"
#include "content/browser/renderer_host/pepper/pepper_message_filter.h"
#include "content/browser/renderer_host/pepper/pepper_renderer_connection.h"
#include "content/browser/renderer_host/render_message_filter.h"
#include "content/browser/renderer_host/render_view_host_delegate.h"
#include "content/browser/renderer_host/render_view_host_impl.h"
#include "content/browser/renderer_host/render_widget_helper.h"
#include "content/browser/renderer_host/render_widget_host_impl.h"
#include "content/browser/renderer_host/socket_stream_dispatcher_host.h"
#include "content/browser/renderer_host/text_input_client_message_filter.h"
#include "content/browser/renderer_host/websocket_dispatcher_host.h"
#include "content/browser/resolve_proxy_msg_helper.h"
#include "content/browser/service_worker/service_worker_context.h"
#include "content/browser/service_worker/service_worker_dispatcher_host.h"
#include "content/browser/speech/input_tag_speech_dispatcher_host.h"
#include "content/browser/speech/speech_recognition_dispatcher_host.h"
#include "content/browser/storage_partition_impl.h"
#include "content/browser/streams/stream_context.h"
#include "content/browser/tracing/trace_controller_impl.h"
#include "content/browser/tracing/trace_message_filter.h"
#include "content/browser/webui/web_ui_controller_factory_registry.h"
#include "content/browser/worker_host/worker_message_filter.h"
#include "content/browser/worker_host/worker_storage_partition.h"
#include "content/common/child_process_host_impl.h"
#include "content/common/child_process_messages.h"
#include "content/common/gpu/gpu_messages.h"
#include "content/common/resource_messages.h"
#include "content/common/view_messages.h"
#include "content/port/browser/render_widget_host_view_frame_subscriber.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/content_browser_client.h"
#include "content/public/browser/notification_service.h"
#include "content/public/browser/notification_types.h"
#include "content/public/browser/render_process_host_factory.h"
#include "content/public/browser/render_widget_host.h"
#include "content/public/browser/render_widget_host_iterator.h"
#include "content/public/browser/resource_context.h"
#include "content/public/browser/user_metrics.h"
#include "content/public/common/content_constants.h"
#include "content/public/common/content_switches.h"
#include "content/public/common/process_type.h"
#include "content/public/common/result_codes.h"
#include "content/public/common/url_constants.h"
#include "gpu/command_buffer/service/gpu_switches.h"
#include "ipc/ipc_channel.h"
#include "ipc/ipc_logging.h"
#include "ipc/ipc_platform_file.h"
#include "ipc/ipc_switches.h"
#include "media/base/media_switches.h"
#include "net/url_request/url_request_context_getter.h"
#include "ppapi/shared_impl/ppapi_switches.h"
#include "ui/base/ui_base_switches.h"
#include "ui/events/event_switches.h"
#include "ui/gfx/switches.h"
#include "ui/gl/gl_switches.h"
#include "webkit/browser/fileapi/sandbox_file_system_backend.h"
#include "webkit/common/resource_type.h"

#if defined(OS_ANDROID)
#include "content/browser/android/vibration_message_filter.h"
#endif

#if defined(OS_WIN)
#include "base/win/scoped_com_initializer.h"
#include "content/common/font_cache_dispatcher_win.h"
#include "content/common/sandbox_win.h"
#include "content/public/common/sandboxed_process_launcher_delegate.h"
#endif

#if defined(ENABLE_WEBRTC)
#include "content/browser/renderer_host/media/webrtc_identity_service_host.h"
#endif

#include "third_party/skia/include/core/SkBitmap.h"

extern bool g_exited_main_message_loop;

static const char* kSiteProcessMapKeyName = "content_site_process_map";

namespace content {
namespace {

void CacheShaderInfo(int32 id, base::FilePath path) {
  ShaderCacheFactory::GetInstance()->SetCacheInfo(id, path);
}

void RemoveShaderInfo(int32 id) {
  ShaderCacheFactory::GetInstance()->RemoveCacheInfo(id);
}

net::URLRequestContext* GetRequestContext(
    scoped_refptr<net::URLRequestContextGetter> request_context,
    scoped_refptr<net::URLRequestContextGetter> media_request_context,
    ResourceType::Type resource_type) {
  // If the request has resource type of ResourceType::MEDIA, we use a request
  // context specific to media for handling it because these resources have
  // specific needs for caching.
  if (resource_type == ResourceType::MEDIA)
    return media_request_context->GetURLRequestContext();
  return request_context->GetURLRequestContext();
}

void GetContexts(
    ResourceContext* resource_context,
    scoped_refptr<net::URLRequestContextGetter> request_context,
    scoped_refptr<net::URLRequestContextGetter> media_request_context,
    const ResourceHostMsg_Request& request,
    ResourceContext** resource_context_out,
    net::URLRequestContext** request_context_out) {
  *resource_context_out = resource_context;
  *request_context_out =
      GetRequestContext(request_context, media_request_context,
                        request.resource_type);
}

// the global list of all renderer processes
base::LazyInstance<IDMap<RenderProcessHost> >::Leaky
    g_all_hosts = LAZY_INSTANCE_INITIALIZER;

base::LazyInstance<scoped_refptr<BrowserPluginGeolocationPermissionContext> >
    g_browser_plugin_geolocation_context = LAZY_INSTANCE_INITIALIZER;

// Map of site to process, to ensure we only have one RenderProcessHost per
// site in process-per-site mode.  Each map is specific to a BrowserContext.
class SiteProcessMap : public base::SupportsUserData::Data {
 public:
  typedef base::hash_map<std::string, RenderProcessHost*> SiteToProcessMap;
  SiteProcessMap() {}

  void RegisterProcess(const std::string& site, RenderProcessHost* process) {
    map_[site] = process;
  }

  RenderProcessHost* FindProcess(const std::string& site) {
    SiteToProcessMap::iterator i = map_.find(site);
    if (i != map_.end())
      return i->second;
    return NULL;
  }

  void RemoveProcess(RenderProcessHost* host) {
    // Find all instances of this process in the map, then separately remove
    // them.
    std::set<std::string> sites;
    for (SiteToProcessMap::const_iterator i = map_.begin();
         i != map_.end();
         i++) {
      if (i->second == host)
        sites.insert(i->first);
    }
    for (std::set<std::string>::iterator i = sites.begin();
         i != sites.end();
         i++) {
      SiteToProcessMap::iterator iter = map_.find(*i);
      if (iter != map_.end()) {
        DCHECK_EQ(iter->second, host);
        map_.erase(iter);
      }
    }
  }

 private:
  SiteToProcessMap map_;
};

// Find the SiteProcessMap specific to the given context.
SiteProcessMap* GetSiteProcessMapForBrowserContext(BrowserContext* context) {
  DCHECK(context);
  SiteProcessMap* map = static_cast<SiteProcessMap*>(
      context->GetUserData(kSiteProcessMapKeyName));
  if (!map) {
    map = new SiteProcessMap();
    context->SetUserData(kSiteProcessMapKeyName, map);
  }
  return map;
}

#if defined(OS_WIN)
// NOTE: changes to this class need to be reviewed by the security team.
class RendererSandboxedProcessLauncherDelegate
    : public content::SandboxedProcessLauncherDelegate {
 public:
  RendererSandboxedProcessLauncherDelegate() {}
  virtual ~RendererSandboxedProcessLauncherDelegate() {}

  virtual void ShouldSandbox(bool* in_sandbox) OVERRIDE {
#if !defined (GOOGLE_CHROME_BUILD)
    if (CommandLine::ForCurrentProcess()->HasSwitch(
        switches::kInProcessPlugins)) {
      *in_sandbox = false;
    }
#endif
  }

  virtual void PreSpawnTarget(sandbox::TargetPolicy* policy,
                              bool* success) {
    AddBaseHandleClosePolicy(policy);
    GetContentClient()->browser()->PreSpawnRenderer(policy, success);
  }
};
#endif  // OS_WIN

}  // namespace

RendererMainThreadFactoryFunction g_renderer_main_thread_factory = NULL;

void RenderProcessHost::RegisterRendererMainThreadFactory(
    RendererMainThreadFactoryFunction create) {
  g_renderer_main_thread_factory = create;
}

base::MessageLoop* g_in_process_thread;

base::MessageLoop*
    RenderProcessHostImpl::GetInProcessRendererThreadForTesting() {
  return g_in_process_thread;
}

// Stores the maximum number of renderer processes the content module can
// create.
static size_t g_max_renderer_count_override = 0;

// static
size_t RenderProcessHost::GetMaxRendererProcessCount() {
  if (g_max_renderer_count_override)
    return g_max_renderer_count_override;

  // Defines the maximum number of renderer processes according to the
  // amount of installed memory as reported by the OS. The calculation
  // assumes that you want the renderers to use half of the installed
  // RAM and assuming that each WebContents uses ~40MB.
  // If you modify this assumption, you need to adjust the
  // ThirtyFourTabs test to match the expected number of processes.
  //
  // With the given amounts of installed memory below on a 32-bit CPU,
  // the maximum renderer count will roughly be as follows:
  //
  //   128 MB -> 3
  //   512 MB -> 6
  //  1024 MB -> 12
  //  4096 MB -> 51
  // 16384 MB -> 82 (kMaxRendererProcessCount)

  static size_t max_count = 0;
  if (!max_count) {
    const size_t kEstimatedWebContentsMemoryUsage =
#if defined(ARCH_CPU_64_BITS)
        60;  // In MB
#else
        40;  // In MB
#endif
    max_count = base::SysInfo::AmountOfPhysicalMemoryMB() / 2;
    max_count /= kEstimatedWebContentsMemoryUsage;

    const size_t kMinRendererProcessCount = 3;
    max_count = std::max(max_count, kMinRendererProcessCount);
    max_count = std::min(max_count, kMaxRendererProcessCount);
  }
  return max_count;
}

// static
bool g_run_renderer_in_process_ = false;

// static
void RenderProcessHost::SetMaxRendererProcessCount(size_t count) {
  g_max_renderer_count_override = count;
}

RenderProcessHostImpl::RenderProcessHostImpl(
    BrowserContext* browser_context,
    StoragePartitionImpl* storage_partition_impl,
    bool supports_browser_plugin,
    bool is_guest)
        : fast_shutdown_started_(false),
          deleting_soon_(false),
          pending_views_(0),
          visible_widgets_(0),
          backgrounded_(true),
          cached_dibs_cleaner_(
              FROM_HERE, base::TimeDelta::FromSeconds(5),
              this, &RenderProcessHostImpl::ClearTransportDIBCache),
          is_initialized_(false),
          id_(ChildProcessHostImpl::GenerateChildProcessUniqueId()),
          browser_context_(browser_context),
          storage_partition_impl_(storage_partition_impl),
          sudden_termination_allowed_(true),
          ignore_input_events_(false),
          supports_browser_plugin_(supports_browser_plugin),
          is_guest_(is_guest),
          gpu_observer_registered_(false),
          power_monitor_broadcaster_(this) {
  widget_helper_ = new RenderWidgetHelper();

  ChildProcessSecurityPolicyImpl::GetInstance()->Add(GetID());

  CHECK(!g_exited_main_message_loop);
  RegisterHost(GetID(), this);
  g_all_hosts.Get().set_check_on_null_data(true);
  // Initialize |child_process_activity_time_| to a reasonable value.
  mark_child_process_activity_time();

  if (!GetBrowserContext()->IsOffTheRecord() &&
      !CommandLine::ForCurrentProcess()->HasSwitch(
          switches::kDisableGpuShaderDiskCache)) {
    BrowserThread::PostTask(BrowserThread::IO, FROM_HERE,
                            base::Bind(&CacheShaderInfo, GetID(),
                                       storage_partition_impl_->GetPath()));
  }

  // Note: When we create the RenderProcessHostImpl, it's technically
  //       backgrounded, because it has no visible listeners.  But the process
  //       doesn't actually exist yet, so we'll Background it later, after
  //       creation.
}

RenderProcessHostImpl::~RenderProcessHostImpl() {
  ChildProcessSecurityPolicyImpl::GetInstance()->Remove(GetID());

  if (gpu_observer_registered_) {
    GpuDataManagerImpl::GetInstance()->RemoveObserver(this);
    gpu_observer_registered_ = false;
  }

  // We may have some unsent messages at this point, but that's OK.
  channel_.reset();
  while (!queued_messages_.empty()) {
    delete queued_messages_.front();
    queued_messages_.pop();
  }

  ClearTransportDIBCache();
  UnregisterHost(GetID());

  if (!CommandLine::ForCurrentProcess()->HasSwitch(
      switches::kDisableGpuShaderDiskCache)) {
    BrowserThread::PostTask(BrowserThread::IO, FROM_HERE,
                            base::Bind(&RemoveShaderInfo, GetID()));
  }
}

void RenderProcessHostImpl::EnableSendQueue() {
  is_initialized_ = false;
}

bool RenderProcessHostImpl::Init() {
  // calling Init() more than once does nothing, this makes it more convenient
  // for the view host which may not be sure in some cases
  if (channel_)
    return true;

  CommandLine::StringType renderer_prefix;
#if defined(OS_POSIX)
  // A command prefix is something prepended to the command line of the spawned
  // process. It is supported only on POSIX systems.
  const CommandLine& browser_command_line = *CommandLine::ForCurrentProcess();
  renderer_prefix =
      browser_command_line.GetSwitchValueNative(switches::kRendererCmdPrefix);
#endif  // defined(OS_POSIX)

#if defined(OS_LINUX)
  int flags = renderer_prefix.empty() ? ChildProcessHost::CHILD_ALLOW_SELF :
                                        ChildProcessHost::CHILD_NORMAL;
#else
  int flags = ChildProcessHost::CHILD_NORMAL;
#endif

  // Find the renderer before creating the channel so if this fails early we
  // return without creating the channel.
  base::FilePath renderer_path = ChildProcessHost::GetChildPath(flags);
  if (renderer_path.empty())
    return false;

  // Setup the IPC channel.
  const std::string channel_id =
      IPC::Channel::GenerateVerifiedChannelID(std::string());
  channel_.reset(
          new IPC::ChannelProxy(channel_id,
                                IPC::Channel::MODE_SERVER,
                                this,
                                BrowserThread::GetMessageLoopProxyForThread(
                                    BrowserThread::IO).get()));

  // Call the embedder first so that their IPC filters have priority.
  GetContentClient()->browser()->RenderProcessHostCreated(this);

  CreateMessageFilters();

  // Single-process mode not supported in multiple-dll mode currently.
  if (run_renderer_in_process() && g_renderer_main_thread_factory) {
    // Crank up a thread and run the initialization there.  With the way that
    // messages flow between the browser and renderer, this thread is required
    // to prevent a deadlock in single-process mode.  Since the primordial
    // thread in the renderer process runs the WebKit code and can sometimes
    // make blocking calls to the UI thread (i.e. this thread), they need to run
    // on separate threads.
    in_process_renderer_.reset(g_renderer_main_thread_factory(channel_id));

    base::Thread::Options options;
#if defined(OS_WIN) && !defined(OS_MACOSX)
    // In-process plugins require this to be a UI message loop.
    options.message_loop_type = base::MessageLoop::TYPE_UI;
#else
    // We can't have multiple UI loops on Linux and Android, so we don't support
    // in-process plugins.
    options.message_loop_type = base::MessageLoop::TYPE_DEFAULT;
#endif
    in_process_renderer_->StartWithOptions(options);

    g_in_process_thread = in_process_renderer_->message_loop();

    OnProcessLaunched();  // Fake a callback that the process is ready.
  } else {
    // Build command line for renderer.  We call AppendRendererCommandLine()
    // first so the process type argument will appear first.
    CommandLine* cmd_line = new CommandLine(renderer_path);
    if (!renderer_prefix.empty())
      cmd_line->PrependWrapper(renderer_prefix);
    AppendRendererCommandLine(cmd_line);
    cmd_line->AppendSwitchASCII(switches::kProcessChannelID, channel_id);

    // Spawn the child process asynchronously to avoid blocking the UI thread.
    // As long as there's no renderer prefix, we can use the zygote process
    // at this stage.
    child_process_launcher_.reset(new ChildProcessLauncher(
#if defined(OS_WIN)
        new RendererSandboxedProcessLauncherDelegate,
#elif defined(OS_POSIX)
        renderer_prefix.empty(),
        base::EnvironmentMap(),
        channel_->TakeClientFileDescriptor(),
#endif
        cmd_line,
        GetID(),
        this));

    fast_shutdown_started_ = false;
  }

  if (!gpu_observer_registered_) {
    gpu_observer_registered_ = true;
    GpuDataManagerImpl::GetInstance()->AddObserver(this);
  }

  is_initialized_ = true;
  return true;
}

void RenderProcessHostImpl::CreateMessageFilters() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  AddFilter(new ResourceSchedulerFilter(GetID()));
  MediaInternals* media_internals = MediaInternals::GetInstance();;
  media::AudioManager* audio_manager =
      BrowserMainLoop::GetInstance()->audio_manager();
  // Add BrowserPluginMessageFilter to ensure it gets the first stab at messages
  // from guests.
  if (supports_browser_plugin_) {
    scoped_refptr<BrowserPluginMessageFilter> bp_message_filter(
        new BrowserPluginMessageFilter(GetID(), IsGuest()));
    AddFilter(bp_message_filter.get());
  }

  scoped_refptr<RenderMessageFilter> render_message_filter(
      new RenderMessageFilter(
          GetID(),
          IsGuest(),
#if defined(ENABLE_PLUGINS)
          PluginServiceImpl::GetInstance(),
#else
          NULL,
#endif
          GetBrowserContext(),
          GetBrowserContext()->GetRequestContextForRenderProcess(GetID()),
          widget_helper_.get(),
          audio_manager,
          media_internals,
          storage_partition_impl_->GetDOMStorageContext()));
  AddFilter(render_message_filter.get());
  AddFilter(
      new RenderFrameMessageFilter(GetID(), widget_helper_.get()));
  BrowserContext* browser_context = GetBrowserContext();
  ResourceContext* resource_context = browser_context->GetResourceContext();

  scoped_refptr<net::URLRequestContextGetter> request_context(
      browser_context->GetRequestContextForRenderProcess(GetID()));
  scoped_refptr<net::URLRequestContextGetter> media_request_context(
      browser_context->GetMediaRequestContextForRenderProcess(GetID()));

  ResourceMessageFilter::GetContextsCallback get_contexts_callback(
      base::Bind(&GetContexts, browser_context->GetResourceContext(),
                 request_context, media_request_context));

  ResourceMessageFilter* resource_message_filter = new ResourceMessageFilter(
      GetID(), PROCESS_TYPE_RENDERER,
      storage_partition_impl_->GetAppCacheService(),
      ChromeBlobStorageContext::GetFor(browser_context),
      storage_partition_impl_->GetFileSystemContext(),
      get_contexts_callback);

  AddFilter(resource_message_filter);
  MediaStreamManager* media_stream_manager =
      BrowserMainLoop::GetInstance()->media_stream_manager();
  AddFilter(new AudioInputRendererHost(
      audio_manager,
      media_stream_manager,
      BrowserMainLoop::GetInstance()->audio_mirroring_manager(),
      BrowserMainLoop::GetInstance()->user_input_monitor()));
  // The AudioRendererHost needs to be available for lookup, so it's
  // stashed in a member variable.
  audio_renderer_host_ = new AudioRendererHost(
      GetID(),
      audio_manager,
      BrowserMainLoop::GetInstance()->audio_mirroring_manager(),
      media_internals,
      media_stream_manager);
  AddFilter(audio_renderer_host_);
  AddFilter(
      new MIDIHost(GetID(), BrowserMainLoop::GetInstance()->midi_manager()));
  AddFilter(new MIDIDispatcherHost(GetID(), browser_context));
  AddFilter(new VideoCaptureHost(media_stream_manager));
  AddFilter(new AppCacheDispatcherHost(
      storage_partition_impl_->GetAppCacheService(),
      GetID()));
  AddFilter(new ClipboardMessageFilter);
  AddFilter(new DOMStorageMessageFilter(
      GetID(),
      storage_partition_impl_->GetDOMStorageContext()));
  AddFilter(new IndexedDBDispatcherHost(
      GetID(),
      storage_partition_impl_->GetIndexedDBContext()));
  AddFilter(new ServiceWorkerDispatcherHost(
      GetID(), storage_partition_impl_->GetServiceWorkerContext()));
  if (IsGuest()) {
    if (!g_browser_plugin_geolocation_context.Get().get()) {
      g_browser_plugin_geolocation_context.Get() =
          new BrowserPluginGeolocationPermissionContext();
    }
    AddFilter(GeolocationDispatcherHost::New(
        GetID(), g_browser_plugin_geolocation_context.Get().get()));
  } else {
    AddFilter(GeolocationDispatcherHost::New(
        GetID(), browser_context->GetGeolocationPermissionContext()));
  }
  gpu_message_filter_ = new GpuMessageFilter(GetID(), widget_helper_.get());
  AddFilter(gpu_message_filter_);
#if defined(ENABLE_WEBRTC)
  AddFilter(new WebRTCIdentityServiceHost(
      GetID(), storage_partition_impl_->GetWebRTCIdentityStore()));
  peer_connection_tracker_host_ = new PeerConnectionTrackerHost(GetID());
  AddFilter(peer_connection_tracker_host_.get());
  AddFilter(new MediaStreamDispatcherHost(
      GetID(), media_stream_manager));
  AddFilter(
      new DeviceRequestMessageFilter(resource_context, media_stream_manager));
#endif
#if defined(ENABLE_PLUGINS)
  AddFilter(new PepperRendererConnection(GetID()));
#endif
#if defined(ENABLE_INPUT_SPEECH)
  AddFilter(new InputTagSpeechDispatcherHost(
      IsGuest(), GetID(), storage_partition_impl_->GetURLRequestContext()));
#endif
  AddFilter(new SpeechRecognitionDispatcherHost(
      IsGuest(), GetID(), storage_partition_impl_->GetURLRequestContext()));
  AddFilter(new FileAPIMessageFilter(
      GetID(),
      storage_partition_impl_->GetURLRequestContext(),
      storage_partition_impl_->GetFileSystemContext(),
      ChromeBlobStorageContext::GetFor(browser_context),
      StreamContext::GetFor(browser_context)));
  AddFilter(new FileUtilitiesMessageFilter(GetID()));
  AddFilter(new MimeRegistryMessageFilter());
  AddFilter(new DatabaseMessageFilter(
      storage_partition_impl_->GetDatabaseTracker()));
#if defined(OS_MACOSX)
  AddFilter(new TextInputClientMessageFilter(GetID()));
#elif defined(OS_WIN)
  channel_->AddFilter(new FontCacheDispatcher());
#elif defined(OS_ANDROID)
  browser_demuxer_android_ = new BrowserDemuxerAndroid();
  AddFilter(browser_demuxer_android_);
#endif

  SocketStreamDispatcherHost::GetRequestContextCallback
      request_context_callback(
          base::Bind(&GetRequestContext, request_context,
                     media_request_context));

  SocketStreamDispatcherHost* socket_stream_dispatcher_host =
      new SocketStreamDispatcherHost(
          GetID(), request_context_callback, resource_context);
  AddFilter(socket_stream_dispatcher_host);

  WebSocketDispatcherHost::GetRequestContextCallback
      websocket_request_context_callback(
          base::Bind(&GetRequestContext, request_context,
                     media_request_context, ResourceType::SUB_RESOURCE));

  AddFilter(new WebSocketDispatcherHost(websocket_request_context_callback));

  message_port_message_filter_ = new MessagePortMessageFilter(
      base::Bind(&RenderWidgetHelper::GetNextRoutingID,
                 base::Unretained(widget_helper_.get())));
  AddFilter(message_port_message_filter_);

  AddFilter(new WorkerMessageFilter(
      GetID(),
      resource_context,
      WorkerStoragePartition(
          storage_partition_impl_->GetURLRequestContext(),
          storage_partition_impl_->GetMediaURLRequestContext(),
          storage_partition_impl_->GetAppCacheService(),
          storage_partition_impl_->GetQuotaManager(),
          storage_partition_impl_->GetFileSystemContext(),
          storage_partition_impl_->GetDatabaseTracker(),
          storage_partition_impl_->GetIndexedDBContext()),
      message_port_message_filter_));

#if defined(ENABLE_WEBRTC)
  AddFilter(new P2PSocketDispatcherHost(
      resource_context,
      browser_context->GetRequestContextForRenderProcess(GetID())));
#endif

  AddFilter(new TraceMessageFilter());
  AddFilter(new ResolveProxyMsgHelper(
      browser_context->GetRequestContextForRenderProcess(GetID())));
  AddFilter(new QuotaDispatcherHost(
      GetID(),
      storage_partition_impl_->GetQuotaManager(),
      GetContentClient()->browser()->CreateQuotaPermissionContext()));
  AddFilter(new GamepadBrowserMessageFilter());
  AddFilter(new DeviceMotionMessageFilter());
  AddFilter(new DeviceOrientationMessageFilter());
  AddFilter(new ProfilerMessageFilter(PROCESS_TYPE_RENDERER));
  AddFilter(new HistogramMessageFilter());
#if defined(USE_TCMALLOC) && (defined(OS_LINUX) || defined(OS_ANDROID))
  if (CommandLine::ForCurrentProcess()->HasSwitch(
      switches::kEnableMemoryBenchmarking))
    AddFilter(new MemoryBenchmarkMessageFilter());
#endif
#if defined(OS_ANDROID)
  AddFilter(new VibrationMessageFilter());
#endif
}

int RenderProcessHostImpl::GetNextRoutingID() {
  return widget_helper_->GetNextRoutingID();
}


void RenderProcessHostImpl::ResumeDeferredNavigation(
    const GlobalRequestID& request_id) {
  widget_helper_->ResumeDeferredNavigation(request_id);
}

void RenderProcessHostImpl::AddRoute(
    int32 routing_id,
    IPC::Listener* listener) {
  listeners_.AddWithID(listener, routing_id);
}

void RenderProcessHostImpl::RemoveRoute(int32 routing_id) {
  DCHECK(listeners_.Lookup(routing_id) != NULL);
  listeners_.Remove(routing_id);

#if defined(OS_WIN)
  // Dump the handle table if handle auditing is enabled.
  const CommandLine& browser_command_line =
      *CommandLine::ForCurrentProcess();
  if (browser_command_line.HasSwitch(switches::kAuditHandles) ||
      browser_command_line.HasSwitch(switches::kAuditAllHandles)) {
    DumpHandles();

    // We wait to close the channels until the child process has finished
    // dumping handles and sends us ChildProcessHostMsg_DumpHandlesDone.
    return;
  }
#endif
  // Keep the one renderer thread around forever in single process mode.
  if (!run_renderer_in_process())
    Cleanup();
}

bool RenderProcessHostImpl::WaitForBackingStoreMsg(
    int render_widget_id,
    const base::TimeDelta& max_delay,
    IPC::Message* msg) {
  // The post task to this thread with the process id could be in queue, and we
  // don't want to dispatch a message before then since it will need the handle.
  if (child_process_launcher_.get() && child_process_launcher_->IsStarting())
    return false;

  return widget_helper_->WaitForBackingStoreMsg(render_widget_id,
                                                max_delay, msg);
}

void RenderProcessHostImpl::ReceivedBadMessage() {
  if (run_renderer_in_process()) {
    // In single process mode it is better if we don't suicide but just
    // crash.
    CHECK(false);
  }
  // We kill the renderer but don't include a NOTREACHED, because we want the
  // browser to try to survive when it gets illegal messages from the renderer.
  base::KillProcess(GetHandle(), RESULT_CODE_KILLED_BAD_MESSAGE,
                    false);
}

void RenderProcessHostImpl::WidgetRestored() {
  // Verify we were properly backgrounded.
  DCHECK_EQ(backgrounded_, (visible_widgets_ == 0));
  visible_widgets_++;
  SetBackgrounded(false);
}

void RenderProcessHostImpl::WidgetHidden() {
  // On startup, the browser will call Hide
  if (backgrounded_)
    return;

  DCHECK_EQ(backgrounded_, (visible_widgets_ == 0));
  visible_widgets_--;
  DCHECK_GE(visible_widgets_, 0);
  if (visible_widgets_ == 0) {
    DCHECK(!backgrounded_);
    SetBackgrounded(true);
  }
}

int RenderProcessHostImpl::VisibleWidgetCount() const {
  return visible_widgets_;
}

bool RenderProcessHostImpl::IsGuest() const {
  return is_guest_;
}

StoragePartition* RenderProcessHostImpl::GetStoragePartition() const {
  return storage_partition_impl_;
}

void RenderProcessHostImpl::AppendRendererCommandLine(
    CommandLine* command_line) const {
  // Pass the process type first, so it shows first in process listings.
  command_line->AppendSwitchASCII(switches::kProcessType,
                                  switches::kRendererProcess);

  // Now send any options from our own command line we want to propagate.
  const CommandLine& browser_command_line = *CommandLine::ForCurrentProcess();
  PropagateBrowserCommandLineToRenderer(browser_command_line, command_line);

  // Pass on the browser locale.
  const std::string locale =
      GetContentClient()->browser()->GetApplicationLocale();
  command_line->AppendSwitchASCII(switches::kLang, locale);

  // If we run base::FieldTrials, we want to pass to their state to the
  // renderer so that it can act in accordance with each state, or record
  // histograms relating to the base::FieldTrial states.
  std::string field_trial_states;
  base::FieldTrialList::StatesToString(&field_trial_states);
  if (!field_trial_states.empty()) {
    command_line->AppendSwitchASCII(switches::kForceFieldTrials,
                                    field_trial_states);
  }

  if (content::IsThreadedCompositingEnabled())
    command_line->AppendSwitch(switches::kEnableThreadedCompositing);

  if (content::IsDelegatedRendererEnabled())
    command_line->AppendSwitch(switches::kEnableDelegatedRenderer);

  if (content::IsDeadlineSchedulingEnabled())
    command_line->AppendSwitch(switches::kEnableDeadlineScheduling);

  GetContentClient()->browser()->AppendExtraCommandLineSwitches(
      command_line, GetID());

  // Appending disable-gpu-feature switches due to software rendering list.
  GpuDataManagerImpl* gpu_data_manager = GpuDataManagerImpl::GetInstance();
  DCHECK(gpu_data_manager);
  gpu_data_manager->AppendRendererCommandLine(command_line);
}

void RenderProcessHostImpl::PropagateBrowserCommandLineToRenderer(
    const CommandLine& browser_cmd,
    CommandLine* renderer_cmd) const {
  // Propagate the following switches to the renderer command line (along
  // with any associated values) if present in the browser command line.
  static const char* const kSwitchNames[] = {
    switches::kAllowFiltersOverIPC,
    switches::kAudioBufferSize,
    switches::kAuditAllHandles,
    switches::kAuditHandles,
    switches::kBlockCrossSiteDocuments,
    switches::kDefaultTileWidth,
    switches::kDefaultTileHeight,
    switches::kDisable3DAPIs,
    switches::kDisableAcceleratedCompositing,
    switches::kDisableAcceleratedFixedRootBackground,
    switches::kDisableAcceleratedScrollableFrames,
    switches::kDisableAcceleratedVideoDecode,
    switches::kDisableApplicationCache,
    switches::kDisableAudio,
    switches::kDisableBreakpad,
    switches::kDisableCompositedScrollingForFrames,
    switches::kDisableCompositingForFixedPosition,
    switches::kDisableCompositingForTransition,
    switches::kDisableDatabases,
    switches::kDisableDeadlineScheduling,
    switches::kDisableDelegatedRenderer,
    switches::kDisableDesktopNotifications,
    switches::kDisableDeviceMotion,
    switches::kDisableDeviceOrientation,
    switches::kDisableDirectNPAPIRequests,
    switches::kDisableFileSystem,
    switches::kDisableFullScreen,
    switches::kDisableGeolocation,
    switches::kDisableGLMultisampling,
    switches::kDisableGpu,
    switches::kDisableGpuCompositing,
    switches::kDisableGpuVsync,
    switches::kDisableHistogramCustomizer,
    switches::kDisableLocalStorage,
    switches::kDisableLogging,
    switches::kDisablePinch,
    switches::kDisablePrefixedEncryptedMedia,
    switches::kDisableSeccompFilterSandbox,
    switches::kDisableSessionStorage,
    switches::kDisableSharedWorkers,
    switches::kDisableSpeechInput,
    switches::kDisableThreadedCompositing,
    switches::kDisableTouchAdjustment,
    switches::kDisableTouchDragDrop,
    switches::kDisableTouchEditing,
    switches::kDisableUniversalAcceleratedOverflowScroll,
    switches::kDisableVp8AlphaPlayback,
    switches::kDisableWebAudio,
    switches::kDisableWebKitMediaSource,
    switches::kDomAutomationController,
    switches::kEnableAcceleratedFixedRootBackground,
    switches::kEnableAcceleratedOverflowScroll,
    switches::kEnableAcceleratedScrollableFrames,
    switches::kEnableAccessibilityLogging,
    switches::kEnableBeginFrameScheduling,
    switches::kEnableBrowserPluginForAllViewTypes,
    switches::kEnableCompositedScrollingForFrames,
    switches::kEnableCompositingForFixedPosition,
    switches::kEnableCompositingForTransition,
    switches::kEnableDCHECK,
    switches::kEnableDeadlineScheduling,
    switches::kEnableDeferredImageDecoding,
    switches::kEnableDelegatedRenderer,
    switches::kEnableEac3Playback,
    switches::kEnableEncryptedMedia,
    switches::kEnableExperimentalCanvasFeatures,
    switches::kEnableExperimentalWebPlatformFeatures,
    switches::kEnableExperimentalWebSocket,
    switches::kEnableFastTextAutosizing,
    switches::kEnableGpuBenchmarking,
    switches::kEnableGPUClientLogging,
    switches::kEnableGpuClientTracing,
    switches::kEnableGPUServiceLogging,
    switches::kEnableHighDpiCompositingForFixedPosition,
    switches::kEnableHTMLImports,
    switches::kEnableInbandTextTracks,
    switches::kEnableInputModeAttribute,
    switches::kEnableLogging,
    switches::kEnableMP3StreamParser,
    switches::kEnableMemoryBenchmarking,
    switches::kEnableOpusPlayback,
    switches::kEnableOverlayFullscreenVideo,
    switches::kEnableOverlayScrollbars,
    switches::kEnableOverscrollNotifications,
    switches::kEnablePinch,
    switches::kEnablePreparsedJsCaching,
    switches::kEnablePruneGpuCommandBuffers,
    switches::kEnableRepaintAfterLayout,
    switches::kEnableServiceWorker,
    switches::kEnableSkiaBenchmarking,
    switches::kEnableSoftwareCompositing,
    switches::kEnableSpeechSynthesis,
    switches::kEnableStatsTable,
    switches::kEnableStrictSiteIsolation,
    switches::kEnableThreadedCompositing,
    switches::kEnableUniversalAcceleratedOverflowScroll,
    switches::kEnableTouchDragDrop,
    switches::kEnableTouchEditing,
    switches::kEnableViewport,
    switches::kEnableVtune,
    switches::kEnableWebAnimationsCSS,
    switches::kEnableWebAnimationsSVG,
    switches::kEnableWebGLDraftExtensions,
    switches::kEnableWebMIDI,
    switches::kForceDeviceScaleFactor,
    switches::kFullMemoryCrashReport,
    switches::kJavaScriptFlags,
    switches::kLoggingLevel,
    switches::kMaxUntiledLayerWidth,
    switches::kMaxUntiledLayerHeight,
    switches::kMemoryMetrics,
    switches::kNoReferrers,
    switches::kNoSandbox,
    switches::kPpapiInProcess,
    switches::kRegisterPepperPlugins,
    switches::kRendererAssertTest,
    switches::kRendererStartupDialog,
    switches::kShowPaintRects,
    switches::kSitePerProcess,
    switches::kStatsCollectionController,
    switches::kTestSandbox,
    switches::kTouchEvents,
    switches::kTraceToConsole,
    // This flag needs to be propagated to the renderer process for
    // --in-process-webgl.
    switches::kUseGL,
    switches::kUseMobileUserAgent,
    switches::kUserAgent,
    switches::kV,
    switches::kVideoThreads,
    switches::kVModule,
    switches::kWebCoreLogChannels,
    // Please keep these in alphabetical order. Compositor switches here should
    // also be added to chrome/browser/chromeos/login/chrome_restart_request.cc.
    cc::switches::kBackgroundColorInsteadOfCheckerboard,
    cc::switches::kCompositeToMailbox,
    cc::switches::kDisableCompositedAntialiasing,
    cc::switches::kDisableCompositorTouchHitTesting,
    cc::switches::kDisableImplSidePainting,
    cc::switches::kDisableLCDText,
    cc::switches::kDisableMapImage,
    cc::switches::kDisableThreadedAnimation,
    cc::switches::kEnableImplSidePainting,
    cc::switches::kEnableLCDText,
    cc::switches::kEnableMapImage,
    cc::switches::kEnablePartialSwap,
    cc::switches::kEnablePerTilePainting,
    cc::switches::kEnablePinchVirtualViewport,
    cc::switches::kEnableTopControlsPositionCalculation,
    cc::switches::kForceDirectLayerDrawing,
    cc::switches::kLowResolutionContentsScaleFactor,
    cc::switches::kMaxTilesForInterestArea,
    cc::switches::kMaxUnusedResourceMemoryUsagePercentage,
    cc::switches::kNumRasterThreads,
    cc::switches::kShowCompositedLayerBorders,
    cc::switches::kShowFPSCounter,
    cc::switches::kShowNonOccludingRects,
    cc::switches::kShowOccludingRects,
    cc::switches::kShowPropertyChangedRects,
    cc::switches::kShowReplicaScreenSpaceRects,
    cc::switches::kShowScreenSpaceRects,
    cc::switches::kShowSurfaceDamageRects,
    cc::switches::kSlowDownRasterScaleFactor,
    cc::switches::kStrictLayerPropertyChangeChecking,
    cc::switches::kTopControlsHeight,
    cc::switches::kTopControlsHideThreshold,
    cc::switches::kTopControlsShowThreshold,
    cc::switches::kTraceOverdraw,
#if defined(ENABLE_PLUGINS)
    switches::kEnablePepperTesting,
    switches::kDisablePepper3d,
#endif
#if defined(ENABLE_WEBRTC)
    switches::kDisableDeviceEnumeration,
    switches::kDisableSCTPDataChannels,
    switches::kDisableWebRtcHWDecoding,
    switches::kDisableWebRtcHWEncoding,
    switches::kEnableWebRtcAecRecordings,
    switches::kEnableWebRtcHWVp8Encoding,
    switches::kEnableWebRtcTcpServerSocket,
#endif
#if !defined (GOOGLE_CHROME_BUILD)
    // These are unsupported and not fully tested modes, so don't enable them
    // for official Google Chrome builds.
    switches::kInProcessPlugins,
#endif  // GOOGLE_CHROME_BUILD
#if defined(GOOGLE_TV)
    switches::kUseExternalVideoSurfaceThresholdInPixels,
#endif
#if defined(OS_ANDROID)
    switches::kDisableGestureRequirementForMediaPlayback,
    switches::kDisableWebRTC,
    switches::kEnableSpeechRecognition,
    switches::kHideScrollbars,
    switches::kMediaDrmEnableNonCompositing,
    switches::kNetworkCountryIso,
#endif
#if defined(OS_MACOSX)
    // Allow this to be set when invoking the browser and relayed along.
    switches::kEnableSandboxLogging,
#endif
#if defined(OS_POSIX)
    switches::kChildCleanExit,
#endif
#if defined(OS_WIN)
    switches::kEnableHighResolutionTime,
#endif
  };
  renderer_cmd->CopySwitchesFrom(browser_cmd, kSwitchNames,
                                 arraysize(kSwitchNames));

  if (browser_cmd.HasSwitch(switches::kTraceStartup) &&
      TraceControllerImpl::GetInstance()->is_tracing_startup()) {
    // Pass kTraceStartup switch to renderer only if startup tracing has not
    // finished.
    renderer_cmd->AppendSwitchASCII(
        switches::kTraceStartup,
        browser_cmd.GetSwitchValueASCII(switches::kTraceStartup));
  }

  // Disable databases in incognito mode.
  if (GetBrowserContext()->IsOffTheRecord() &&
      !browser_cmd.HasSwitch(switches::kDisableDatabases)) {
    renderer_cmd->AppendSwitch(switches::kDisableDatabases);
#if defined(OS_ANDROID)
    renderer_cmd->AppendSwitch(switches::kDisableMediaHistoryLogging);
#endif
  }

  // Enforce the extra command line flags for impl-side painting.
  if (cc::switches::IsImplSidePaintingEnabled() &&
      !browser_cmd.HasSwitch(switches::kEnableDeferredImageDecoding))
    renderer_cmd->AppendSwitch(switches::kEnableDeferredImageDecoding);
}

base::ProcessHandle RenderProcessHostImpl::GetHandle() const {
  if (run_renderer_in_process())
    return base::Process::Current().handle();

  if (!child_process_launcher_.get() || child_process_launcher_->IsStarting())
    return base::kNullProcessHandle;

  return child_process_launcher_->GetHandle();
}

bool RenderProcessHostImpl::FastShutdownIfPossible() {
  if (run_renderer_in_process())
    return false;  // Single process mode never shutdown the renderer.

  if (!GetContentClient()->browser()->IsFastShutdownPossible())
    return false;

  if (!child_process_launcher_.get() ||
      child_process_launcher_->IsStarting() ||
      !GetHandle())
    return false;  // Render process hasn't started or is probably crashed.

  // Test if there's an unload listener.
  // NOTE: It's possible that an onunload listener may be installed
  // while we're shutting down, so there's a small race here.  Given that
  // the window is small, it's unlikely that the web page has much
  // state that will be lost by not calling its unload handlers properly.
  if (!SuddenTerminationAllowed())
    return false;

  ProcessDied(false /* already_dead */);
  fast_shutdown_started_ = true;
  return true;
}

void RenderProcessHostImpl::DumpHandles() {
#if defined(OS_WIN)
  Send(new ChildProcessMsg_DumpHandles());
  return;
#endif

  NOTIMPLEMENTED();
}

// This is a platform specific function for mapping a transport DIB given its id
TransportDIB* RenderProcessHostImpl::MapTransportDIB(
    TransportDIB::Id dib_id) {
#if defined(OS_WIN)
  // On Windows we need to duplicate the handle from the remote process
  HANDLE section;
  DuplicateHandle(GetHandle(), dib_id.handle, GetCurrentProcess(), &section,
                  STANDARD_RIGHTS_REQUIRED | FILE_MAP_READ | FILE_MAP_WRITE,
                  FALSE, 0);
  return TransportDIB::Map(section);
#elif defined(TOOLKIT_GTK)
  return TransportDIB::Map(dib_id.shmkey);
#elif defined(OS_ANDROID)
  return TransportDIB::Map(dib_id);
#else
  // On POSIX, the browser allocates all DIBs and keeps a file descriptor around
  // for each.
  return widget_helper_->MapTransportDIB(dib_id);
#endif
}

TransportDIB* RenderProcessHostImpl::GetTransportDIB(
    TransportDIB::Id dib_id) {
  if (!TransportDIB::is_valid_id(dib_id))
    return NULL;

  const std::map<TransportDIB::Id, TransportDIB*>::iterator
      i = cached_dibs_.find(dib_id);
  if (i != cached_dibs_.end()) {
    cached_dibs_cleaner_.Reset();
    return i->second;
  }

  TransportDIB* dib = MapTransportDIB(dib_id);
  if (!dib)
    return NULL;

  if (cached_dibs_.size() >= MAX_MAPPED_TRANSPORT_DIBS) {
    // Clean a single entry from the cache
    std::map<TransportDIB::Id, TransportDIB*>::iterator smallest_iterator;
    size_t smallest_size = std::numeric_limits<size_t>::max();

    for (std::map<TransportDIB::Id, TransportDIB*>::iterator
         i = cached_dibs_.begin(); i != cached_dibs_.end(); ++i) {
      if (i->second->size() <= smallest_size) {
        smallest_iterator = i;
        smallest_size = i->second->size();
      }
    }

#if defined(TOOLKIT_GTK)
    smallest_iterator->second->Detach();
#else
    delete smallest_iterator->second;
#endif
    cached_dibs_.erase(smallest_iterator);
  }

  cached_dibs_[dib_id] = dib;
  cached_dibs_cleaner_.Reset();
  return dib;
}

void RenderProcessHostImpl::ClearTransportDIBCache() {
#if defined(TOOLKIT_GTK)
  std::map<TransportDIB::Id, TransportDIB*>::const_iterator dib =
      cached_dibs_.begin();
  for (; dib != cached_dibs_.end(); ++dib)
    dib->second->Detach();
#else
  STLDeleteContainerPairSecondPointers(
      cached_dibs_.begin(), cached_dibs_.end());
#endif
  cached_dibs_.clear();
}

bool RenderProcessHostImpl::Send(IPC::Message* msg) {
  if (!channel_) {
    if (!is_initialized_) {
      queued_messages_.push(msg);
      return true;
    } else {
      delete msg;
      return false;
    }
  }

  if (child_process_launcher_.get() && child_process_launcher_->IsStarting()) {
    queued_messages_.push(msg);
    return true;
  }

  return channel_->Send(msg);
}

bool RenderProcessHostImpl::OnMessageReceived(const IPC::Message& msg) {
  // If we're about to be deleted, or have initiated the fast shutdown sequence,
  // we ignore incoming messages.

  if (deleting_soon_ || fast_shutdown_started_)
    return false;

  mark_child_process_activity_time();
  if (msg.routing_id() == MSG_ROUTING_CONTROL) {
    // Dispatch control messages.
    bool msg_is_ok = true;
    IPC_BEGIN_MESSAGE_MAP_EX(RenderProcessHostImpl, msg, msg_is_ok)
      IPC_MESSAGE_HANDLER(ChildProcessHostMsg_ShutdownRequest,
                          OnShutdownRequest)
      IPC_MESSAGE_HANDLER(ChildProcessHostMsg_DumpHandlesDone,
                          OnDumpHandlesDone)
      IPC_MESSAGE_HANDLER(ViewHostMsg_SuddenTerminationChanged,
                          SuddenTerminationChanged)
      IPC_MESSAGE_HANDLER(ViewHostMsg_UserMetricsRecordAction,
                          OnUserMetricsRecordAction)
      IPC_MESSAGE_HANDLER(ViewHostMsg_SavedPageAsMHTML, OnSavedPageAsMHTML)
      // Adding single handlers for your service here is fine, but once your
      // service needs more than one handler, please extract them into a new
      // message filter and add that filter to CreateMessageFilters().
    IPC_END_MESSAGE_MAP_EX()

    if (!msg_is_ok) {
      // The message had a handler, but its de-serialization failed.
      // We consider this a capital crime. Kill the renderer if we have one.
      LOG(ERROR) << "bad message " << msg.type() << " terminating renderer.";
      RecordAction(UserMetricsAction("BadMessageTerminate_BRPH"));
      ReceivedBadMessage();
    }
    return true;
  }

  // Dispatch incoming messages to the appropriate IPC::Listener.
  IPC::Listener* listener = listeners_.Lookup(msg.routing_id());
  if (!listener) {
    if (msg.is_sync()) {
      // The listener has gone away, so we must respond or else the caller will
      // hang waiting for a reply.
      IPC::Message* reply = IPC::SyncMessage::GenerateReply(&msg);
      reply->set_reply_error();
      Send(reply);
    }

    // If this is a SwapBuffers, we need to ack it if we're not going to handle
    // it so that the GPU process doesn't get stuck in unscheduled state.
    bool msg_is_ok = true;
    IPC_BEGIN_MESSAGE_MAP_EX(RenderProcessHostImpl, msg, msg_is_ok)
      IPC_MESSAGE_HANDLER(ViewHostMsg_CompositorSurfaceBuffersSwapped,
                          OnCompositorSurfaceBuffersSwappedNoHost)
    IPC_END_MESSAGE_MAP_EX()
    return true;
  }
  return listener->OnMessageReceived(msg);
}

void RenderProcessHostImpl::OnChannelConnected(int32 peer_pid) {
#if defined(IPC_MESSAGE_LOG_ENABLED)
  Send(new ChildProcessMsg_SetIPCLoggingEnabled(
      IPC::Logging::GetInstance()->Enabled()));
#endif

  tracked_objects::ThreadData::Status status =
      tracked_objects::ThreadData::status();
  Send(new ChildProcessMsg_SetProfilerStatus(status));

  Send(new ViewMsg_SetRendererProcessID(GetID()));
}

void RenderProcessHostImpl::OnChannelError() {
  ProcessDied(true /* already_dead */);
}

BrowserContext* RenderProcessHostImpl::GetBrowserContext() const {
  return browser_context_;
}

bool RenderProcessHostImpl::InSameStoragePartition(
    StoragePartition* partition) const {
  return storage_partition_impl_ == partition;
}

int RenderProcessHostImpl::GetID() const {
  return id_;
}

bool RenderProcessHostImpl::HasConnection() const {
  return channel_.get() != NULL;
}

void RenderProcessHostImpl::SetIgnoreInputEvents(bool ignore_input_events) {
  ignore_input_events_ = ignore_input_events;
}

bool RenderProcessHostImpl::IgnoreInputEvents() const {
  return ignore_input_events_;
}

void RenderProcessHostImpl::Cleanup() {
  // When no other owners of this object, we can delete ourselves
  if (listeners_.IsEmpty()) {
    DCHECK_EQ(0, pending_views_);
    NotificationService::current()->Notify(
        NOTIFICATION_RENDERER_PROCESS_TERMINATED,
        Source<RenderProcessHost>(this),
        NotificationService::NoDetails());

    base::MessageLoop::current()->DeleteSoon(FROM_HERE, this);
    deleting_soon_ = true;
    // It's important not to wait for the DeleteTask to delete the channel
    // proxy. Kill it off now. That way, in case the profile is going away, the
    // rest of the objects attached to this RenderProcessHost start going
    // away first, since deleting the channel proxy will post a
    // OnChannelClosed() to IPC::ChannelProxy::Context on the IO thread.
    channel_.reset();
    gpu_message_filter_ = NULL;
    message_port_message_filter_ = NULL;

    // Remove ourself from the list of renderer processes so that we can't be
    // reused in between now and when the Delete task runs.
    UnregisterHost(GetID());
  }
}

void RenderProcessHostImpl::AddPendingView() {
  pending_views_++;
}

void RenderProcessHostImpl::RemovePendingView() {
  DCHECK(pending_views_);
  pending_views_--;
}

void RenderProcessHostImpl::SetSuddenTerminationAllowed(bool enabled) {
  sudden_termination_allowed_ = enabled;
}

bool RenderProcessHostImpl::SuddenTerminationAllowed() const {
  return sudden_termination_allowed_;
}

base::TimeDelta RenderProcessHostImpl::GetChildProcessIdleTime() const {
  return base::TimeTicks::Now() - child_process_activity_time_;
}

void RenderProcessHostImpl::SurfaceUpdated(int32 surface_id) {
  if (!gpu_message_filter_)
    return;
  BrowserThread::PostTask(BrowserThread::IO, FROM_HERE, base::Bind(
      &GpuMessageFilter::SurfaceUpdated,
      gpu_message_filter_,
      surface_id));
}

void RenderProcessHostImpl::ResumeRequestsForView(int route_id) {
  widget_helper_->ResumeRequestsForView(route_id);
}

IPC::ChannelProxy* RenderProcessHostImpl::GetChannel() {
  return channel_.get();
}

void RenderProcessHostImpl::AddFilter(BrowserMessageFilter* filter) {
  channel_->AddFilter(filter->GetFilter());
}

bool RenderProcessHostImpl::FastShutdownForPageCount(size_t count) {
  if (static_cast<size_t>(GetActiveViewCount()) == count)
    return FastShutdownIfPossible();
  return false;
}

bool RenderProcessHostImpl::FastShutdownStarted() const {
  return fast_shutdown_started_;
}

// static
void RenderProcessHostImpl::RegisterHost(int host_id, RenderProcessHost* host) {
  g_all_hosts.Get().AddWithID(host, host_id);
}

// static
void RenderProcessHostImpl::UnregisterHost(int host_id) {
  RenderProcessHost* host = g_all_hosts.Get().Lookup(host_id);
  if (!host)
    return;

  g_all_hosts.Get().Remove(host_id);

  // Look up the map of site to process for the given browser_context,
  // in case we need to remove this process from it.  It will be registered
  // under any sites it rendered that use process-per-site mode.
  SiteProcessMap* map =
      GetSiteProcessMapForBrowserContext(host->GetBrowserContext());
  map->RemoveProcess(host);
}

// static
bool RenderProcessHostImpl::IsSuitableHost(
    RenderProcessHost* host,
    BrowserContext* browser_context,
    const GURL& site_url) {
  if (run_renderer_in_process())
    return true;

  if (host->GetBrowserContext() != browser_context)
    return false;

  // All URLs are suitable if this is associated with a guest renderer process.
  // TODO(fsamuel, creis): Further validation is needed to ensure that only
  // normal web URLs are permitted in guest processes. We need to investigate
  // where this validation should happen.
  if (host->IsGuest())
    return true;

  if (!host->IsGuest() && site_url.SchemeIs(kGuestScheme))
    return false;

  // Check whether the given host and the intended site_url will be using the
  // same StoragePartition, since a RenderProcessHost can only support a single
  // StoragePartition.  This is relevant for packaged apps and isolated sites.
  StoragePartition* dest_partition =
      BrowserContext::GetStoragePartitionForSite(browser_context, site_url);
  if (!host->InSameStoragePartition(dest_partition))
    return false;

  if (ChildProcessSecurityPolicyImpl::GetInstance()->HasWebUIBindings(
          host->GetID()) !=
      WebUIControllerFactoryRegistry::GetInstance()->UseWebUIBindingsForURL(
          browser_context, site_url)) {
    return false;
  }

  return GetContentClient()->browser()->IsSuitableHost(host, site_url);
}

// static
bool RenderProcessHost::run_renderer_in_process() {
  return g_run_renderer_in_process_;
}

// static
void RenderProcessHost::SetRunRendererInProcess(bool value) {
  g_run_renderer_in_process_ = value;

  CommandLine* command_line = CommandLine::ForCurrentProcess();
  if (value && !command_line->HasSwitch(switches::kLang)) {
    // Modify the current process' command line to include the browser locale,
    // as the renderer expects this flag to be set.
    const std::string locale =
        GetContentClient()->browser()->GetApplicationLocale();
    command_line->AppendSwitchASCII(switches::kLang, locale);
  }
}

RenderProcessHost::iterator RenderProcessHost::AllHostsIterator() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  return iterator(g_all_hosts.Pointer());
}

// static
RenderProcessHost* RenderProcessHost::FromID(int render_process_id) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  return g_all_hosts.Get().Lookup(render_process_id);
}

// static
bool RenderProcessHost::ShouldTryToUseExistingProcessHost(
    BrowserContext* browser_context, const GURL& url) {
  // Experimental:
  // If --enable-strict-site-isolation or --site-per-process is enabled, do not
  // try to reuse renderer processes when over the limit.  (We could allow pages
  // from the same site to share, if we knew what the given process was
  // dedicated to.  Allowing no sharing is simpler for now.)  This may cause
  // resource exhaustion issues if too many sites are open at once.
  const CommandLine& command_line = *CommandLine::ForCurrentProcess();
  if (command_line.HasSwitch(switches::kEnableStrictSiteIsolation) ||
      command_line.HasSwitch(switches::kSitePerProcess))
    return false;

  if (run_renderer_in_process())
    return true;

  // NOTE: Sometimes it's necessary to create more render processes than
  //       GetMaxRendererProcessCount(), for instance when we want to create
  //       a renderer process for a browser context that has no existing
  //       renderers. This is OK in moderation, since the
  //       GetMaxRendererProcessCount() is conservative.
  if (g_all_hosts.Get().size() >= GetMaxRendererProcessCount())
    return true;

  return GetContentClient()->browser()->
      ShouldTryToUseExistingProcessHost(browser_context, url);
}

// static
RenderProcessHost* RenderProcessHost::GetExistingProcessHost(
    BrowserContext* browser_context,
    const GURL& site_url) {
  // First figure out which existing renderers we can use.
  std::vector<RenderProcessHost*> suitable_renderers;
  suitable_renderers.reserve(g_all_hosts.Get().size());

  iterator iter(AllHostsIterator());
  while (!iter.IsAtEnd()) {
    if (RenderProcessHostImpl::IsSuitableHost(
            iter.GetCurrentValue(),
            browser_context, site_url))
      suitable_renderers.push_back(iter.GetCurrentValue());

    iter.Advance();
  }

  // Now pick a random suitable renderer, if we have any.
  if (!suitable_renderers.empty()) {
    int suitable_count = static_cast<int>(suitable_renderers.size());
    int random_index = base::RandInt(0, suitable_count - 1);
    return suitable_renderers[random_index];
  }

  return NULL;
}

// static
bool RenderProcessHost::ShouldUseProcessPerSite(
    BrowserContext* browser_context,
    const GURL& url) {
  // Returns true if we should use the process-per-site model.  This will be
  // the case if the --process-per-site switch is specified, or in
  // process-per-site-instance for particular sites (e.g., WebUI).
  // Note that --single-process is handled in ShouldTryToUseExistingProcessHost.
  const CommandLine& command_line = *CommandLine::ForCurrentProcess();
  if (command_line.HasSwitch(switches::kProcessPerSite))
    return true;

  // We want to consolidate particular sites like WebUI even when we are using
  // the process-per-tab or process-per-site-instance models.
  // Note: DevTools pages have WebUI type but should not reuse the same host.
  if (WebUIControllerFactoryRegistry::GetInstance()->UseWebUIForURL(
          browser_context, url) &&
      !url.SchemeIs(chrome::kChromeDevToolsScheme)) {
    return true;
  }

  // Otherwise let the content client decide, defaulting to false.
  return GetContentClient()->browser()->ShouldUseProcessPerSite(browser_context,
                                                                url);
}

// static
RenderProcessHost* RenderProcessHostImpl::GetProcessHostForSite(
    BrowserContext* browser_context,
    const GURL& url) {
  // Look up the map of site to process for the given browser_context.
  SiteProcessMap* map =
      GetSiteProcessMapForBrowserContext(browser_context);

  // See if we have an existing process with appropriate bindings for this site.
  // If not, the caller should create a new process and register it.
  std::string site = SiteInstance::GetSiteForURL(browser_context, url)
      .possibly_invalid_spec();
  RenderProcessHost* host = map->FindProcess(site);
  if (host && !IsSuitableHost(host, browser_context, url)) {
    // The registered process does not have an appropriate set of bindings for
    // the url.  Remove it from the map so we can register a better one.
    RecordAction(UserMetricsAction("BindingsMismatch_GetProcessHostPerSite"));
    map->RemoveProcess(host);
    host = NULL;
  }

  return host;
}

void RenderProcessHostImpl::RegisterProcessHostForSite(
    BrowserContext* browser_context,
    RenderProcessHost* process,
    const GURL& url) {
  // Look up the map of site to process for the given browser_context.
  SiteProcessMap* map =
      GetSiteProcessMapForBrowserContext(browser_context);

  // Only register valid, non-empty sites.  Empty or invalid sites will not
  // use process-per-site mode.  We cannot check whether the process has
  // appropriate bindings here, because the bindings have not yet been granted.
  std::string site = SiteInstance::GetSiteForURL(browser_context, url)
      .possibly_invalid_spec();
  if (!site.empty())
    map->RegisterProcess(site, process);
}

void RenderProcessHostImpl::ProcessDied(bool already_dead) {
  // Our child process has died.  If we didn't expect it, it's a crash.
  // In any case, we need to let everyone know it's gone.
  // The OnChannelError notification can fire multiple times due to nested sync
  // calls to a renderer. If we don't have a valid channel here it means we
  // already handled the error.

  // child_process_launcher_ can be NULL in single process mode or if fast
  // termination happened.
  int exit_code = 0;
  base::TerminationStatus status =
      child_process_launcher_.get() ?
      child_process_launcher_->GetChildTerminationStatus(already_dead,
                                                         &exit_code) :
      base::TERMINATION_STATUS_NORMAL_TERMINATION;

  RendererClosedDetails details(GetHandle(), status, exit_code);
  NotificationService::current()->Notify(
      NOTIFICATION_RENDERER_PROCESS_CLOSED,
      Source<RenderProcessHost>(this),
      Details<RendererClosedDetails>(&details));

  child_process_launcher_.reset();
  channel_.reset();
  gpu_message_filter_ = NULL;
  message_port_message_filter_ = NULL;

  IDMap<IPC::Listener>::iterator iter(&listeners_);
  while (!iter.IsAtEnd()) {
    iter.GetCurrentValue()->OnMessageReceived(
        ViewHostMsg_RenderProcessGone(iter.GetCurrentKey(),
                                      static_cast<int>(status),
                                      exit_code));
    iter.Advance();
  }

  ClearTransportDIBCache();

  // this object is not deleted at this point and may be reused later.
  // TODO(darin): clean this up
}

int RenderProcessHostImpl::GetActiveViewCount() {
  int num_active_views = 0;
  scoped_ptr<RenderWidgetHostIterator> widgets(
      RenderWidgetHost::GetRenderWidgetHosts());
  while (RenderWidgetHost* widget = widgets->GetNextHost()) {
    // Count only RenderWidgetHosts in this process.
    if (widget->GetProcess()->GetID() == GetID())
      num_active_views++;
  }
  return num_active_views;
}

// Frame subscription API for this class is for accelerated composited path
// only. These calls are redirected to GpuMessageFilter.
void RenderProcessHostImpl::BeginFrameSubscription(
    int route_id,
    scoped_ptr<RenderWidgetHostViewFrameSubscriber> subscriber) {
  if (!gpu_message_filter_)
    return;
  BrowserThread::PostTask(BrowserThread::IO, FROM_HERE, base::Bind(
      &GpuMessageFilter::BeginFrameSubscription,
      gpu_message_filter_,
      route_id, base::Passed(&subscriber)));
}

void RenderProcessHostImpl::EndFrameSubscription(int route_id) {
  if (!gpu_message_filter_)
    return;
  BrowserThread::PostTask(BrowserThread::IO, FROM_HERE, base::Bind(
      &GpuMessageFilter::EndFrameSubscription,
      gpu_message_filter_,
      route_id));
}

void RenderProcessHostImpl::OnShutdownRequest() {
  // Don't shut down if there are active RenderViews, or if there are pending
  // RenderViews being swapped back in.
  // In single process mode, we never shutdown the renderer.
  int num_active_views = GetActiveViewCount();
  if (pending_views_ || num_active_views > 0 || run_renderer_in_process())
    return;

  // Notify any contents that might have swapped out renderers from this
  // process. They should not attempt to swap them back in.
  NotificationService::current()->Notify(
      NOTIFICATION_RENDERER_PROCESS_CLOSING,
      Source<RenderProcessHost>(this),
      NotificationService::NoDetails());

  Send(new ChildProcessMsg_Shutdown());
}

void RenderProcessHostImpl::SuddenTerminationChanged(bool enabled) {
  SetSuddenTerminationAllowed(enabled);
}

void RenderProcessHostImpl::OnDumpHandlesDone() {
  Cleanup();
}

void RenderProcessHostImpl::SetBackgrounded(bool backgrounded) {
  // Note: we always set the backgrounded_ value.  If the process is NULL
  // (and hence hasn't been created yet), we will set the process priority
  // later when we create the process.
  backgrounded_ = backgrounded;
  if (!child_process_launcher_.get() || child_process_launcher_->IsStarting())
    return;

#if defined(OS_WIN)
  // The cbstext.dll loads as a global GetMessage hook in the browser process
  // and intercepts/unintercepts the kernel32 API SetPriorityClass in a
  // background thread. If the UI thread invokes this API just when it is
  // intercepted the stack is messed up on return from the interceptor
  // which causes random crashes in the browser process. Our hack for now
  // is to not invoke the SetPriorityClass API if the dll is loaded.
  if (GetModuleHandle(L"cbstext.dll"))
    return;
#endif  // OS_WIN

  child_process_launcher_->SetProcessBackgrounded(backgrounded);
}

void RenderProcessHostImpl::OnProcessLaunched() {
  // No point doing anything, since this object will be destructed soon.  We
  // especially don't want to send the RENDERER_PROCESS_CREATED notification,
  // since some clients might expect a RENDERER_PROCESS_TERMINATED afterwards to
  // properly cleanup.
  if (deleting_soon_)
    return;

  if (child_process_launcher_) {
    if (!child_process_launcher_->GetHandle()) {
      OnChannelError();
      return;
    }

    child_process_launcher_->SetProcessBackgrounded(backgrounded_);
  }

  // NOTE: This needs to be before sending queued messages because
  // ExtensionService uses this notification to initialize the renderer process
  // with state that must be there before any JavaScript executes.
  //
  // The queued messages contain such things as "navigate". If this notification
  // was after, we can end up executing JavaScript before the initialization
  // happens.
  NotificationService::current()->Notify(
      NOTIFICATION_RENDERER_PROCESS_CREATED,
      Source<RenderProcessHost>(this),
      NotificationService::NoDetails());

  while (!queued_messages_.empty()) {
    Send(queued_messages_.front());
    queued_messages_.pop();
  }
}

scoped_refptr<AudioRendererHost>
RenderProcessHostImpl::audio_renderer_host() const {
  return audio_renderer_host_;
}

void RenderProcessHostImpl::OnUserMetricsRecordAction(
    const std::string& action) {
  RecordComputedAction(action);
}

void RenderProcessHostImpl::OnSavedPageAsMHTML(int job_id, int64 data_size) {
  MHTMLGenerationManager::GetInstance()->MHTMLGenerated(job_id, data_size);
}

void RenderProcessHostImpl::OnCompositorSurfaceBuffersSwappedNoHost(
      const ViewHostMsg_CompositorSurfaceBuffersSwapped_Params& params) {
  TRACE_EVENT0("renderer_host",
               "RenderWidgetHostImpl::OnCompositorSurfaceBuffersSwappedNoHost");
  AcceleratedSurfaceMsg_BufferPresented_Params ack_params;
  ack_params.sync_point = 0;
  RenderWidgetHostImpl::AcknowledgeBufferPresent(params.route_id,
                                                 params.gpu_process_host_id,
                                                 ack_params);
}

void RenderProcessHostImpl::OnGpuSwitching() {
  // We are updating all widgets including swapped out ones.
  scoped_ptr<RenderWidgetHostIterator> widgets(
      RenderWidgetHostImpl::GetAllRenderWidgetHosts());
  while (RenderWidgetHost* widget = widgets->GetNextHost()) {
    if (!widget->IsRenderView())
      continue;

    // Skip widgets in other processes.
    if (widget->GetProcess()->GetID() != GetID())
      continue;

    RenderViewHost* rvh = RenderViewHost::From(widget);
    rvh->UpdateWebkitPreferences(rvh->GetWebkitPreferences());
  }
}

}  // namespace content
