#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "entt/entt.hpp"
#include "imgui.h"

#include "Dx12Init/Dx12Init.hpp"
#include "RustComponents/RustBridge.hpp"

namespace Components {
    struct ImageLoader {
        std::string url;
        std::vector<uint8_t> imageData;
        int width = 0;
        int height = 0;
        void* texture = nullptr;

        enum class LoadState {
            Idle,
            Loading,
            Loaded,
            Failed
        };

        LoadState state = LoadState::Idle;
        uint64_t fetch_id = 0;

        enum class ImageSourceType {
            Url,
            LocalPath
        };

        struct ImageSource {
            std::string Value;
            ImageSourceType Type = ImageSourceType::Url;

            bool operator==(const ImageSource& other) const {
                return Type == other.Type && Value == other.Value;
            }
        };

        enum class SourceCycleMode {
            CycleLoadedOnly,
            CycleAllSources,
            RefetchOnClick
        };

        std::vector<ImageSource> Sources;
        int ActiveSourceIndex = 0;
        SourceCycleMode CycleMode = SourceCycleMode::CycleLoadedOnly;
        bool PreloadAllSources = true;
        bool AutoPlay = true;
        bool Loop = true;
        bool Paused = false;
        float PlaybackSpeed = 1.0f;
        int CurrentFrame = 0;
        float FrameAccumulatorMs = 0.0f;
        bool IsAnimated = false;
        std::vector<void*> FrameTextures;
        std::vector<uint32_t> FrameDurationsMs;
        bool StartupRequired = true;
        bool KeepLastSuccessfulTexture = true;
        bool SkipFailedSources = true;
        int MaxRetryCount = 2;
        float RetryDelayMs = 350.0f;
        bool AdaptiveGifUpload = true;
        int MaxGifUploadFramesPerTick = 2;
        size_t MaxUploadBytesPerTick = 8u * 1024u * 1024u;
        int MaxResidentGifFrames = 64;
        float RestartDebounceMs = 250.0f;
        float LastRestartAtMs = -1.0f;
        int LastGoodSourceIndex = -1;
        int LastGoodFrameIndex = 0;
        std::string LastStatusMessage;

        std::string requested_url;
        bool request_started = false;
        bool restart_requested = false;
        bool hard_restart_requested = false;
        ID3D12Resource* texture_resource = nullptr;
        ID3D12Resource* upload_resource = nullptr;

        struct DecodedFrame {
            std::vector<uint8_t> Pixels;
            int Width = 0;
            int Height = 0;
            uint32_t DelayMs = 100;
        };

        struct GpuFrame {
            void* Texture = nullptr;
            ID3D12Resource* TextureResource = nullptr;
            int SrvIndex = -1;
            int Width = 0;
            int Height = 0;
            uint32_t DelayMs = 100;
        };

        struct SourceRuntime {
            enum class ErrorClass {
                None,
                StartFailed,
                FetchFailed,
                DecodeFailed,
                UploadFailed,
                InvalidData
            };

            ImageSource Source;
            uint64_t FetchId = 0;
            bool RequestStarted = false;
            bool Failed = false;
            bool TerminalFailure = false;
            bool PollComplete = false;
            bool ReadyForUpload = false;
            bool Uploaded = false;
            bool IsAnimated = false;
            int RetryCount = 0;
            float NextRetryAtMs = 0.0f;
            ErrorClass LastError = ErrorClass::None;
            std::string LastErrorMessage;
            std::vector<DecodedFrame> StagedFrames;
            std::vector<GpuFrame> GpuFrames;
            size_t NextUploadFrameIndex = 0;
            size_t TotalDecodedFrames = 0;
            bool UploadTruncated = false;
        };

        bool sources_initialized = false;
        std::vector<ImageSource> normalized_sources;
        std::vector<SourceRuntime> source_states;

        ImageLoader(const std::string& sourceUrl = "")
            : url(sourceUrl) {}

        ImageLoader& AddUrl(const std::string& sourceUrl) {
            Sources.push_back({ sourceUrl, ImageSourceType::Url });
            return *this;
        }

        ImageLoader& AddPath(const std::string& path) {
            Sources.push_back({ path, ImageSourceType::LocalPath });
            return *this;
        }

        ImageLoader& SetSources(std::vector<ImageSource> sources) {
            Sources = std::move(sources);
            return *this;
        }

        ImageLoader& SetActiveSource(int index) {
            ActiveSourceIndex = index;
            return *this;
        }

        ImageLoader& SetPlayback(bool autoPlay, bool loop, bool paused, float speed = 1.0f) {
            AutoPlay = autoPlay;
            Loop = loop;
            Paused = paused;
            PlaybackSpeed = speed;
            return *this;
        }
    };

    struct ImageLoaderProgress {
        int totalRequired = 0;
        int readyRequired = 0;
        int failedRequired = 0;
        int loadingRequired = 0;
        bool done = true;
        std::string currentLabel;
        float percent = 1.0f;
    };

    struct ImageLoaderDebugStats {
        int descriptorUsed = 0;
        int descriptorFree = 0;
        int deferredResourceReleases = 0;
        int deferredDescriptorRecycles = 0;
        int pendingFetches = 0;
        int uploadedFramesLastTick = 0;
        size_t uploadedBytesLastTick = 0;
    };

    namespace ImageLoaderSystem {
        namespace detail {
            constexpr int kSrvReservedStart = 10;

            struct DeferredRelease {
                UINT64 fenceValue = 0;
                ID3D12Resource* resource = nullptr;
            };

            struct DeferredSrvRecycle {
                UINT64 fenceValue = 0;
                int srvIndex = -1;
            };

            enum class UploadResult {
                Uploaded,
                RetryLater,
                FatalError
            };

            inline std::atomic<int> s_NextSrvIndex = kSrvReservedStart;
            inline std::vector<int> s_FreeSrvIndices;
            inline std::vector<DeferredRelease> s_DeferredReleases;
            inline std::vector<DeferredSrvRecycle> s_DeferredSrvRecycles;
            inline std::unordered_set<entt::registry*> s_RegisteredRegistries;
            inline std::atomic<int> s_PendingFetchCount{ 0 };
            inline int s_LastUploadedFrames = 0;
            inline size_t s_LastUploadedBytes = 0;

            inline bool IsLikelyUrl(const std::string& value) {
                return value.rfind("http://", 0) == 0 || value.rfind("https://", 0) == 0;
            }

            inline float NowMs() {
                if (ImGui::GetCurrentContext()) {
                    return static_cast<float>(ImGui::GetTime() * 1000.0);
                }

                using clock = std::chrono::steady_clock;
                static const clock::time_point s_Start = clock::now();
                const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(clock::now() - s_Start);
                return static_cast<float>(elapsed.count());
            }

            inline std::string SourceLabel(const ImageLoader::SourceRuntime& runtime) {
                if (runtime.Source.Value.empty()) {
                    return "Unknown source";
                }
                return runtime.Source.Value;
            }

            inline bool IsRuntimeLoading(const ImageLoader::SourceRuntime& runtime) {
                if (runtime.RequestStarted || runtime.FetchId != 0 || runtime.ReadyForUpload) {
                    return true;
                }
                if (runtime.Uploaded || runtime.TerminalFailure) {
                    return false;
                }
                return !runtime.PollComplete || runtime.NextRetryAtMs > NowMs();
            }

            inline bool IsRuntimeRenderable(const ImageLoader::SourceRuntime& runtime) {
                return runtime.Uploaded && !runtime.GpuFrames.empty();
            }

            inline UINT64 NextFenceTarget() {
                if (!DX12Init::g_fence) {
                    return 0;
                }
                return DX12Init::g_fenceLastSignaledValue + 1;
            }

            inline int SrvCapacity() {
                if (!DX12Init::g_pd3dSrvDescHeap) {
                    return 0;
                }
                const D3D12_DESCRIPTOR_HEAP_DESC heapDesc = DX12Init::g_pd3dSrvDescHeap->GetDesc();
                return static_cast<int>(heapDesc.NumDescriptors);
            }

            inline bool IsSrvIndexValid(int index) {
                const int capacity = SrvCapacity();
                return capacity > 0 && index >= kSrvReservedStart && index < capacity;
            }

            inline void RecycleSrvIndexImmediate(int srvIndex) {
                if (!IsSrvIndexValid(srvIndex)) {
                    return;
                }
                s_FreeSrvIndices.push_back(srvIndex);
            }

            inline void QueueDeferredSrvRecycle(int srvIndex, UINT64 fenceValue) {
                if (!IsSrvIndexValid(srvIndex)) {
                    return;
                }

                if (!DX12Init::g_fence || fenceValue == 0) {
                    RecycleSrvIndexImmediate(srvIndex);
                    return;
                }

                s_DeferredSrvRecycles.push_back({ fenceValue, srvIndex });
            }

            inline void QueueDeferredRelease(ID3D12Resource* resource, UINT64 fenceValue) {
                if (!resource) {
                    return;
                }

                if (!DX12Init::g_fence || fenceValue == 0) {
                    resource->Release();
                    return;
                }

                s_DeferredReleases.push_back({ fenceValue, resource });
            }

            inline void DrainDeferredReleases() {
                if (!DX12Init::g_fence) {
                    for (auto& pending : s_DeferredReleases) {
                        if (pending.resource) {
                            pending.resource->Release();
                        }
                    }
                    s_DeferredReleases.clear();

                    for (const auto& pendingSrv : s_DeferredSrvRecycles) {
                        RecycleSrvIndexImmediate(pendingSrv.srvIndex);
                    }
                    s_DeferredSrvRecycles.clear();
                    return;
                }

                const UINT64 completedFence = DX12Init::g_fence->GetCompletedValue();

                auto releaseIt = s_DeferredReleases.begin();
                while (releaseIt != s_DeferredReleases.end()) {
                    if (completedFence >= releaseIt->fenceValue) {
                        if (releaseIt->resource) {
                            releaseIt->resource->Release();
                        }
                        releaseIt = s_DeferredReleases.erase(releaseIt);
                    } else {
                        ++releaseIt;
                    }
                }

                auto srvIt = s_DeferredSrvRecycles.begin();
                while (srvIt != s_DeferredSrvRecycles.end()) {
                    if (completedFence >= srvIt->fenceValue) {
                        RecycleSrvIndexImmediate(srvIt->srvIndex);
                        srvIt = s_DeferredSrvRecycles.erase(srvIt);
                    } else {
                        ++srvIt;
                    }
                }
            }

            inline void ReleaseSrvIndex(int& srvIndex, bool immediate) {
                if (!IsSrvIndexValid(srvIndex)) {
                    srvIndex = -1;
                    return;
                }
                if (immediate) {
                    RecycleSrvIndexImmediate(srvIndex);
                } else {
                    QueueDeferredSrvRecycle(srvIndex, NextFenceTarget());
                }
                srvIndex = -1;
            }

            inline void ReleaseResource(ID3D12Resource*& resource, bool immediate) {
                if (!resource) {
                    return;
                }

                if (immediate) {
                    resource->Release();
                } else {
                    QueueDeferredRelease(resource, NextFenceTarget());
                }

                resource = nullptr;
            }

            inline bool ReserveSrvIndex(int& outSrvIndex) {
                outSrvIndex = -1;
                if (!DX12Init::g_pd3dSrvDescHeap) {
                    return false;
                }

                const int capacity = SrvCapacity();
                if (capacity <= kSrvReservedStart) {
                    return false;
                }

                while (!s_FreeSrvIndices.empty()) {
                    const int reused = s_FreeSrvIndices.back();
                    s_FreeSrvIndices.pop_back();
                    if (reused >= kSrvReservedStart && reused < capacity) {
                        outSrvIndex = reused;
                        return true;
                    }
                }

                while (true) {
                    int current = s_NextSrvIndex.load(std::memory_order_relaxed);
                    if (current < kSrvReservedStart) {
                        current = kSrvReservedStart;
                    }
                    if (current >= capacity) {
                        return false;
                    }

                    const int desired = current + 1;
                    if (s_NextSrvIndex.compare_exchange_weak(current, desired, std::memory_order_relaxed)) {
                        outSrvIndex = current;
                        return true;
                    }
                }
            }

            inline void ReleaseGpuFrames(std::vector<ImageLoader::GpuFrame>& frames, bool immediate) {
                for (auto& frame : frames) {
                    ReleaseResource(frame.TextureResource, immediate);
                    ReleaseSrvIndex(frame.SrvIndex, immediate);
                    frame.Texture = nullptr;
                }
                frames.clear();
            }

            inline void CancelInFlightFetch(ImageLoader::SourceRuntime& runtime) {
                if (runtime.FetchId != 0) {
                    cancel_fetch_request(runtime.FetchId);
                    runtime.FetchId = 0;
                    int pending = s_PendingFetchCount.load(std::memory_order_relaxed);
                    while (pending > 0 && !s_PendingFetchCount.compare_exchange_weak(
                        pending,
                        pending - 1,
                        std::memory_order_relaxed)) {}
                }
                runtime.RequestStarted = false;
            }

            inline int DescriptorUsedCount() {
                const int capacity = SrvCapacity();
                if (capacity <= kSrvReservedStart) {
                    return 0;
                }
                const int allocated = (std::max)(0, s_NextSrvIndex.load(std::memory_order_relaxed) - kSrvReservedStart);
                const int used = allocated - static_cast<int>(s_FreeSrvIndices.size());
                const int maxDynamic = capacity - kSrvReservedStart;
                return (std::max)(0, (std::min)(used, maxDynamic));
            }

            inline void ResetSourceRuntime(ImageLoader::SourceRuntime& runtime, bool immediateRelease) {
                CancelInFlightFetch(runtime);
                runtime.Failed = false;
                runtime.TerminalFailure = false;
                runtime.PollComplete = false;
                runtime.ReadyForUpload = false;
                runtime.Uploaded = false;
                runtime.IsAnimated = false;
                runtime.RetryCount = 0;
                runtime.NextRetryAtMs = 0.0f;
                runtime.LastError = ImageLoader::SourceRuntime::ErrorClass::None;
                runtime.LastErrorMessage.clear();
                runtime.StagedFrames.clear();
                runtime.NextUploadFrameIndex = 0;
                runtime.TotalDecodedFrames = 0;
                runtime.UploadTruncated = false;
                ReleaseGpuFrames(runtime.GpuFrames, immediateRelease);
            }

            inline void ResetLoader(ImageLoader& loader, bool immediateRelease) {
                for (auto& runtime : loader.source_states) {
                    ResetSourceRuntime(runtime, immediateRelease);
                }
                loader.source_states.clear();
                loader.normalized_sources.clear();
                loader.sources_initialized = false;

                loader.imageData.clear();
                loader.width = 0;
                loader.height = 0;
                loader.texture = nullptr;
                loader.fetch_id = 0;
                loader.request_started = false;
                loader.restart_requested = false;
                loader.hard_restart_requested = false;
                loader.state = ImageLoader::LoadState::Idle;
                loader.CurrentFrame = 0;
                loader.FrameAccumulatorMs = 0.0f;
                loader.IsAnimated = false;
                loader.FrameTextures.clear();
                loader.FrameDurationsMs.clear();
                loader.texture_resource = nullptr;
                loader.upload_resource = nullptr;
                loader.LastGoodSourceIndex = -1;
                loader.LastGoodFrameIndex = 0;
                loader.LastStatusMessage.clear();
            }

            inline std::vector<ImageLoader::ImageSource> BuildNormalizedSources(const ImageLoader& loader) {
                if (!loader.Sources.empty()) {
                    return loader.Sources;
                }

                std::vector<ImageLoader::ImageSource> result;
                if (!loader.url.empty()) {
                    result.push_back({
                        loader.url,
                        IsLikelyUrl(loader.url) ? ImageLoader::ImageSourceType::Url : ImageLoader::ImageSourceType::LocalPath
                    });
                }
                return result;
            }

            inline bool SourcesEqual(const std::vector<ImageLoader::ImageSource>& lhs, const std::vector<ImageLoader::ImageSource>& rhs) {
                if (lhs.size() != rhs.size()) {
                    return false;
                }

                for (size_t i = 0; i < lhs.size(); ++i) {
                    if (!(lhs[i] == rhs[i])) {
                        return false;
                    }
                }
                return true;
            }

            inline int FindNextRenderableSource(const ImageLoader& loader, int startIndex) {
                if (loader.source_states.empty()) {
                    return -1;
                }

                const int sourceCount = static_cast<int>(loader.source_states.size());
                if (startIndex < 0 || startIndex >= sourceCount) {
                    startIndex = 0;
                }

                for (int offset = 0; offset < sourceCount; ++offset) {
                    const int idx = (startIndex + offset) % sourceCount;
                    if (IsRuntimeRenderable(loader.source_states[static_cast<size_t>(idx)])) {
                        return idx;
                    }
                }
                return -1;
            }

            inline void MarkRuntimeFailure(
                ImageLoader& loader,
                ImageLoader::SourceRuntime& runtime,
                ImageLoader::SourceRuntime::ErrorClass errorClass,
                const char* message,
                bool retryable) {

                CancelInFlightFetch(runtime);
                runtime.ReadyForUpload = false;
                runtime.StagedFrames.clear();
                runtime.NextUploadFrameIndex = 0;
                runtime.UploadTruncated = false;
                runtime.LastError = errorClass;
                runtime.LastErrorMessage = message ? message : "";

                const bool canRetry = retryable && runtime.RetryCount < (std::max)(0, loader.MaxRetryCount);
                if (canRetry) {
                    runtime.Failed = false;
                    runtime.TerminalFailure = false;
                    runtime.PollComplete = false;
                    ++runtime.RetryCount;
                    runtime.NextRetryAtMs = NowMs() + (std::max)(0.0f, loader.RetryDelayMs);
                    return;
                }

                runtime.Failed = true;
                runtime.TerminalFailure = true;
                runtime.PollComplete = true;
            }

            inline bool StartFetch(ImageLoader& loader, ImageLoader::SourceRuntime& runtime, bool force = false) {
                if (!force && (runtime.RequestStarted || runtime.FetchId != 0 || runtime.ReadyForUpload || runtime.Uploaded)) {
                    return runtime.RequestStarted;
                }

                if (runtime.NextRetryAtMs > 0.0f && NowMs() < runtime.NextRetryAtMs) {
                    return false;
                }

                runtime.FetchId = start_fetch_media(
                    runtime.Source.Value.c_str(),
                    runtime.Source.Type == ImageLoader::ImageSourceType::Url
                        ? static_cast<int32_t>(ImageSourceKind::Url)
                        : static_cast<int32_t>(ImageSourceKind::LocalPath));

                runtime.RequestStarted = runtime.FetchId != 0;
                if (!runtime.RequestStarted) {
                    MarkRuntimeFailure(loader, runtime, ImageLoader::SourceRuntime::ErrorClass::StartFailed, "Failed to start fetch.", true);
                    return false;
                }
                s_PendingFetchCount.fetch_add(1, std::memory_order_relaxed);

                runtime.Failed = false;
                runtime.TerminalFailure = false;
                runtime.PollComplete = false;
                runtime.NextRetryAtMs = 0.0f;
                runtime.LastError = ImageLoader::SourceRuntime::ErrorClass::None;
                runtime.LastErrorMessage.clear();
                runtime.UploadTruncated = false;
                return runtime.RequestStarted;
            }

            inline void EnsureRuntimeInitialized(ImageLoader& loader, bool forceFullReset) {
                const std::vector<ImageLoader::ImageSource> desiredSources = BuildNormalizedSources(loader);
                const bool sourcesChanged = !SourcesEqual(loader.normalized_sources, desiredSources);
                const bool legacyUrlChanged = loader.Sources.empty() && loader.requested_url != loader.url;

                if (!loader.sources_initialized || sourcesChanged || forceFullReset || legacyUrlChanged) {
                    ResetLoader(loader, false);
                    loader.normalized_sources = desiredSources;
                    loader.requested_url = loader.url;
                    loader.sources_initialized = true;

                    if (loader.normalized_sources.empty()) {
                        loader.state = ImageLoader::LoadState::Failed;
                        loader.LastStatusMessage = "Image failed: no sources.";
                        return;
                    }

                    loader.source_states.resize(loader.normalized_sources.size());
                    for (size_t i = 0; i < loader.normalized_sources.size(); ++i) {
                        loader.source_states[i].Source = loader.normalized_sources[i];
                    }

                    const int clampedActive = (std::max)(0, (std::min)(loader.ActiveSourceIndex, static_cast<int>(loader.source_states.size()) - 1));
                    loader.ActiveSourceIndex = clampedActive;

                    if (loader.PreloadAllSources) {
                        for (auto& runtime : loader.source_states) {
                            StartFetch(loader, runtime);
                        }
                    } else {
                        StartFetch(loader, loader.source_states[static_cast<size_t>(loader.ActiveSourceIndex)]);
                    }

                    loader.LastStatusMessage = "Loading image...";
                    loader.state = ImageLoader::LoadState::Loading;
                }
            }

            inline void SoftRestartLoader(ImageLoader& loader) {
                if (!loader.sources_initialized || loader.source_states.empty()) {
                    loader.restart_requested = false;
                    return;
                }

                bool anyRenderable = false;
                for (auto& runtime : loader.source_states) {
                    const bool keepRenderable = IsRuntimeRenderable(runtime);
                    anyRenderable = anyRenderable || keepRenderable;

                    CancelInFlightFetch(runtime);
                    runtime.StagedFrames.clear();
                    runtime.ReadyForUpload = false;
                    runtime.NextUploadFrameIndex = 0;
                    runtime.TotalDecodedFrames = keepRenderable ? runtime.GpuFrames.size() : 0;
                    runtime.LastError = ImageLoader::SourceRuntime::ErrorClass::None;
                    runtime.LastErrorMessage.clear();
                    runtime.RetryCount = 0;
                    runtime.NextRetryAtMs = 0.0f;
                    runtime.UploadTruncated = false;
                    runtime.Failed = false;
                    runtime.TerminalFailure = false;

                    if (keepRenderable) {
                        runtime.Uploaded = true;
                        runtime.PollComplete = true;
                    } else {
                        runtime.Uploaded = false;
                        runtime.PollComplete = false;
                        ReleaseGpuFrames(runtime.GpuFrames, false);
                    }
                }

                if (loader.LastGoodSourceIndex < 0 ||
                    loader.LastGoodSourceIndex >= static_cast<int>(loader.source_states.size()) ||
                    !IsRuntimeRenderable(loader.source_states[static_cast<size_t>(loader.LastGoodSourceIndex)])) {
                    loader.LastGoodSourceIndex = FindNextRenderableSource(loader, 0);
                    loader.LastGoodFrameIndex = 0;
                }

                loader.restart_requested = false;
                loader.hard_restart_requested = false;
                loader.FrameAccumulatorMs = 0.0f;
                loader.state = anyRenderable ? ImageLoader::LoadState::Loaded : ImageLoader::LoadState::Loading;
                loader.LastStatusMessage = anyRenderable ? "Refreshing image sources..." : "Loading image...";
            }

            inline void PollFetch(ImageLoader& loader, ImageLoader::SourceRuntime& runtime) {
                if (!runtime.RequestStarted || runtime.FetchId == 0) {
                    return;
                }

                int32_t mediaKind = static_cast<int32_t>(FetchedMediaKind::StaticRGBA);
                uint8_t* staticData = nullptr;
                size_t staticLen = 0;
                int staticWidth = 0;
                int staticHeight = 0;
                AnimatedFrameFFI* frames = nullptr;
                size_t frameCount = 0;

                const int32_t status = check_fetch_media_status_ex(
                    runtime.FetchId,
                    &mediaKind,
                    &staticData,
                    &staticLen,
                    &staticWidth,
                    &staticHeight,
                    &frames,
                    &frameCount);

                if (status == static_cast<int32_t>(ImageFetchStatus::Loading)) {
                    return;
                }

                runtime.RequestStarted = false;
                runtime.FetchId = 0;
                int pending = s_PendingFetchCount.load(std::memory_order_relaxed);
                while (pending > 0 && !s_PendingFetchCount.compare_exchange_weak(
                    pending,
                    pending - 1,
                    std::memory_order_relaxed)) {}

                if (status != static_cast<int32_t>(ImageFetchStatus::Ready)) {
                    MarkRuntimeFailure(loader, runtime, ImageLoader::SourceRuntime::ErrorClass::FetchFailed, "Fetch failed.", true);
                    return;
                }

                runtime.StagedFrames.clear();
                runtime.IsAnimated = false;
                runtime.NextUploadFrameIndex = 0;
                runtime.TotalDecodedFrames = 0;
                runtime.UploadTruncated = false;

                if (mediaKind == static_cast<int32_t>(FetchedMediaKind::StaticRGBA)) {
                    if (!staticData || staticLen == 0 || staticWidth <= 0 || staticHeight <= 0) {
                        if (staticData) {
                            free_image_data(staticData, staticLen);
                        }
                        MarkRuntimeFailure(loader, runtime, ImageLoader::SourceRuntime::ErrorClass::InvalidData, "Invalid static image payload.", true);
                        return;
                    }

                    const size_t expectedLen = static_cast<size_t>(staticWidth) * static_cast<size_t>(staticHeight) * 4u;
                    if (staticLen < expectedLen) {
                        free_image_data(staticData, staticLen);
                        MarkRuntimeFailure(loader, runtime, ImageLoader::SourceRuntime::ErrorClass::DecodeFailed, "Static image decode length mismatch.", true);
                        return;
                    }

                    ImageLoader::DecodedFrame frame;
                    frame.Width = staticWidth;
                    frame.Height = staticHeight;
                    frame.DelayMs = 100;
                    frame.Pixels.assign(staticData, staticData + expectedLen);
                    runtime.StagedFrames.push_back(std::move(frame));
                    runtime.TotalDecodedFrames = 1;
                    free_image_data(staticData, staticLen);
                } else {
                    if (!frames || frameCount == 0) {
                        MarkRuntimeFailure(loader, runtime, ImageLoader::SourceRuntime::ErrorClass::DecodeFailed, "Animated image decode returned no frames.", true);
                        return;
                    }

                    runtime.IsAnimated = frameCount > 1;
                    runtime.TotalDecodedFrames = frameCount;
                    const size_t residentLimit = static_cast<size_t>((std::max)(1, loader.MaxResidentGifFrames));
                    runtime.StagedFrames.reserve((std::min)(frameCount, residentLimit));
                    for (size_t i = 0; i < frameCount; ++i) {
                        const AnimatedFrameFFI& ffiFrame = frames[i];
                        if (!ffiFrame.data || ffiFrame.len == 0 || ffiFrame.width <= 0 || ffiFrame.height <= 0) {
                            continue;
                        }

                        if (runtime.StagedFrames.size() >= residentLimit) {
                            runtime.UploadTruncated = true;
                            continue;
                        }

                        const size_t expectedLen = static_cast<size_t>(ffiFrame.width) * static_cast<size_t>(ffiFrame.height) * 4u;
                        if (ffiFrame.len < expectedLen) {
                            continue;
                        }

                        ImageLoader::DecodedFrame frame;
                        frame.Width = ffiFrame.width;
                        frame.Height = ffiFrame.height;
                        frame.DelayMs = ffiFrame.delay_ms == 0 ? 100u : ffiFrame.delay_ms;
                        frame.Pixels.assign(ffiFrame.data, ffiFrame.data + expectedLen);
                        runtime.StagedFrames.push_back(std::move(frame));
                    }

                    free_animation_frames(frames, frameCount);
                    runtime.IsAnimated = runtime.StagedFrames.size() > 1;
                }

                runtime.ReadyForUpload = !runtime.StagedFrames.empty();
                runtime.PollComplete = true;
                runtime.Failed = false;
                runtime.TerminalFailure = false;
                runtime.NextRetryAtMs = 0.0f;
                runtime.LastError = ImageLoader::SourceRuntime::ErrorClass::None;
                runtime.LastErrorMessage.clear();

                if (!runtime.ReadyForUpload) {
                    MarkRuntimeFailure(loader, runtime, ImageLoader::SourceRuntime::ErrorClass::DecodeFailed, "No decodable frames.", true);
                }
            }

            inline UploadResult UploadFrameToGpu(const ImageLoader::DecodedFrame& frame, ImageLoader::GpuFrame& outFrame, ID3D12Resource*& outUploadResource) {
                outUploadResource = nullptr;

                if (!DX12Init::g_pd3dDevice || !DX12Init::g_pd3dCommandList || !DX12Init::g_pd3dSrvDescHeap) {
                    return UploadResult::RetryLater;
                }

                if (frame.Width <= 0 || frame.Height <= 0) {
                    return UploadResult::FatalError;
                }

                const size_t expectedLen = static_cast<size_t>(frame.Width) * static_cast<size_t>(frame.Height) * 4u;
                if (frame.Pixels.size() < expectedLen) {
                    return UploadResult::FatalError;
                }

                int srvIndex = -1;
                if (!ReserveSrvIndex(srvIndex)) {
                    return UploadResult::FatalError;
                }

                D3D12_HEAP_PROPERTIES textureHeapProps = {};
                textureHeapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

                D3D12_RESOURCE_DESC textureDesc = {};
                textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
                textureDesc.Width = static_cast<UINT64>(frame.Width);
                textureDesc.Height = static_cast<UINT>(frame.Height);
                textureDesc.DepthOrArraySize = 1;
                textureDesc.MipLevels = 1;
                textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                textureDesc.SampleDesc.Count = 1;
                textureDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
                textureDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

                ID3D12Resource* textureResource = nullptr;
                if (FAILED(DX12Init::g_pd3dDevice->CreateCommittedResource(
                    &textureHeapProps,
                    D3D12_HEAP_FLAG_NONE,
                    &textureDesc,
                    D3D12_RESOURCE_STATE_COPY_DEST,
                    nullptr,
                    IID_PPV_ARGS(&textureResource)))) {
                    ReleaseSrvIndex(srvIndex, true);
                    return UploadResult::FatalError;
                }

                const UINT uploadPitch = (frame.Width * 4 + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1u) &
                                         ~(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1u);
                const UINT64 uploadSize = static_cast<UINT64>(frame.Height) * uploadPitch;

                D3D12_HEAP_PROPERTIES uploadHeapProps = {};
                uploadHeapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

                D3D12_RESOURCE_DESC uploadDesc = {};
                uploadDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
                uploadDesc.Width = uploadSize;
                uploadDesc.Height = 1;
                uploadDesc.DepthOrArraySize = 1;
                uploadDesc.MipLevels = 1;
                uploadDesc.Format = DXGI_FORMAT_UNKNOWN;
                uploadDesc.SampleDesc.Count = 1;
                uploadDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
                uploadDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

                ID3D12Resource* uploadResource = nullptr;
                if (FAILED(DX12Init::g_pd3dDevice->CreateCommittedResource(
                    &uploadHeapProps,
                    D3D12_HEAP_FLAG_NONE,
                    &uploadDesc,
                    D3D12_RESOURCE_STATE_GENERIC_READ,
                    nullptr,
                    IID_PPV_ARGS(&uploadResource)))) {
                    textureResource->Release();
                    ReleaseSrvIndex(srvIndex, true);
                    return UploadResult::FatalError;
                }

                void* mapped = nullptr;
                if (FAILED(uploadResource->Map(0, nullptr, &mapped)) || !mapped) {
                    uploadResource->Release();
                    textureResource->Release();
                    ReleaseSrvIndex(srvIndex, true);
                    return UploadResult::FatalError;
                }

                for (int y = 0; y < frame.Height; ++y) {
                    std::memcpy(
                        static_cast<uint8_t*>(mapped) + static_cast<size_t>(y) * uploadPitch,
                        frame.Pixels.data() + static_cast<size_t>(y) * static_cast<size_t>(frame.Width) * 4u,
                        static_cast<size_t>(frame.Width) * 4u);
                }
                uploadResource->Unmap(0, nullptr);

                D3D12_TEXTURE_COPY_LOCATION srcLocation = {};
                srcLocation.pResource = uploadResource;
                srcLocation.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
                srcLocation.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                srcLocation.PlacedFootprint.Footprint.Width = static_cast<UINT>(frame.Width);
                srcLocation.PlacedFootprint.Footprint.Height = static_cast<UINT>(frame.Height);
                srcLocation.PlacedFootprint.Footprint.Depth = 1;
                srcLocation.PlacedFootprint.Footprint.RowPitch = uploadPitch;

                D3D12_TEXTURE_COPY_LOCATION dstLocation = {};
                dstLocation.pResource = textureResource;
                dstLocation.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                dstLocation.SubresourceIndex = 0;

                DX12Init::g_pd3dCommandList->CopyTextureRegion(&dstLocation, 0, 0, 0, &srcLocation, nullptr);

                D3D12_RESOURCE_BARRIER barrier = {};
                barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                barrier.Transition.pResource = textureResource;
                barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
                barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
                DX12Init::g_pd3dCommandList->ResourceBarrier(1, &barrier);

                D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
                srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
                srvDesc.Texture2D.MipLevels = 1;
                DX12Init::g_pd3dDevice->CreateShaderResourceView(textureResource, &srvDesc, DX12Init::GetCpuSrvHandle(srvIndex));

                outFrame.Texture = reinterpret_cast<void*>(static_cast<intptr_t>(srvIndex));
                outFrame.TextureResource = textureResource;
                outFrame.SrvIndex = srvIndex;
                outFrame.Width = frame.Width;
                outFrame.Height = frame.Height;
                outFrame.DelayMs = frame.DelayMs == 0 ? 100u : frame.DelayMs;

                outUploadResource = uploadResource;
                return UploadResult::Uploaded;
            }

            inline UploadResult UploadStagedFrames(
                ImageLoader& loader,
                ImageLoader::SourceRuntime& runtime,
                int& uploadedFrames,
                size_t& uploadedBytes) {

                uploadedFrames = 0;
                uploadedBytes = 0;
                if (!runtime.ReadyForUpload || runtime.StagedFrames.empty()) {
                    return UploadResult::Uploaded;
                }

                const bool adaptive = runtime.IsAnimated && loader.AdaptiveGifUpload;
                const int frameBudget = adaptive ? (std::max)(1, loader.MaxGifUploadFramesPerTick) : (std::numeric_limits<int>::max)();
                const size_t byteBudget = adaptive ? (std::max)(size_t(1024), loader.MaxUploadBytesPerTick) : static_cast<size_t>(-1);
                const size_t residentLimit = static_cast<size_t>((std::max)(1, loader.MaxResidentGifFrames));

                std::vector<ImageLoader::GpuFrame> oldFrames;
                bool replacingExisting = false;
                if (runtime.NextUploadFrameIndex == 0 && !runtime.GpuFrames.empty()) {
                    oldFrames = std::move(runtime.GpuFrames);
                    replacingExisting = true;
                }

                while (runtime.NextUploadFrameIndex < runtime.StagedFrames.size()) {
                    if (adaptive && uploadedFrames >= frameBudget) {
                        break;
                    }
                    if (runtime.GpuFrames.size() >= residentLimit) {
                        runtime.UploadTruncated = true;
                        runtime.NextUploadFrameIndex = runtime.StagedFrames.size();
                        break;
                    }

                    const auto& stagedFrame = runtime.StagedFrames[runtime.NextUploadFrameIndex];
                    const size_t frameBytes = static_cast<size_t>(stagedFrame.Width) * static_cast<size_t>(stagedFrame.Height) * 4u;
                    if (adaptive && uploadedFrames > 0 && (uploadedBytes + frameBytes) > byteBudget) {
                        break;
                    }

                    ImageLoader::GpuFrame gpuFrame;
                    ID3D12Resource* uploadResource = nullptr;
                    const UploadResult result = UploadFrameToGpu(stagedFrame, gpuFrame, uploadResource);
                    if (result == UploadResult::RetryLater) {
                        if (uploadResource) {
                            QueueDeferredRelease(uploadResource, NextFenceTarget());
                        }
                        if (runtime.GpuFrames.empty() && replacingExisting) {
                            runtime.GpuFrames = std::move(oldFrames);
                        }
                        runtime.Uploaded = !runtime.GpuFrames.empty();
                        return runtime.Uploaded ? UploadResult::Uploaded : UploadResult::RetryLater;
                    }
                    if (result == UploadResult::FatalError) {
                        if (uploadResource) {
                            QueueDeferredRelease(uploadResource, NextFenceTarget());
                        }
                        if (runtime.GpuFrames.empty() && replacingExisting) {
                            runtime.GpuFrames = std::move(oldFrames);
                        }
                        runtime.Uploaded = !runtime.GpuFrames.empty();
                        if (!runtime.Uploaded) {
                            return UploadResult::FatalError;
                        }
                        runtime.UploadTruncated = true;
                        runtime.NextUploadFrameIndex = runtime.StagedFrames.size();
                        break;
                    }

                    if (uploadResource) {
                        QueueDeferredRelease(uploadResource, NextFenceTarget());
                    }
                    runtime.GpuFrames.push_back(gpuFrame);
                    runtime.Uploaded = true;
                    if (replacingExisting) {
                        ReleaseGpuFrames(oldFrames, false);
                        replacingExisting = false;
                    }

                    ++runtime.NextUploadFrameIndex;
                    ++uploadedFrames;
                    uploadedBytes += frameBytes;

                    if (adaptive && uploadedBytes >= byteBudget) {
                        break;
                    }
                }

                if (replacingExisting) {
                    runtime.GpuFrames = std::move(oldFrames);
                    runtime.Uploaded = !runtime.GpuFrames.empty();
                }

                if (runtime.NextUploadFrameIndex >= runtime.StagedFrames.size()) {
                    runtime.StagedFrames.clear();
                    runtime.ReadyForUpload = false;
                    runtime.NextUploadFrameIndex = 0;
                } else {
                    runtime.ReadyForUpload = true;
                }

                runtime.Failed = runtime.GpuFrames.empty();
                return runtime.Failed ? UploadResult::RetryLater : UploadResult::Uploaded;
            }

            inline void StartNeededSources(ImageLoader& loader) {
                if (loader.source_states.empty()) {
                    return;
                }

                const float nowMs = NowMs();

                if (loader.PreloadAllSources) {
                    for (auto& runtime : loader.source_states) {
                        if (!runtime.RequestStarted &&
                            runtime.FetchId == 0 &&
                            !runtime.ReadyForUpload &&
                            !runtime.Uploaded &&
                            !runtime.TerminalFailure &&
                            nowMs >= runtime.NextRetryAtMs) {
                            StartFetch(loader, runtime);
                        }
                    }
                    return;
                }

                const int activeIndex = (std::max)(0, (std::min)(loader.ActiveSourceIndex, static_cast<int>(loader.source_states.size()) - 1));
                loader.ActiveSourceIndex = activeIndex;
                auto& active = loader.source_states[static_cast<size_t>(activeIndex)];
                if (!active.RequestStarted &&
                    active.FetchId == 0 &&
                    !active.ReadyForUpload &&
                    !active.Uploaded &&
                    !active.TerminalFailure &&
                    nowMs >= active.NextRetryAtMs) {
                    StartFetch(loader, active);
                }
            }

            inline void SyncLegacyFields(ImageLoader& loader) {
                loader.texture = nullptr;
                loader.texture_resource = nullptr;
                loader.width = 0;
                loader.height = 0;
                loader.fetch_id = 0;
                loader.request_started = false;
                loader.imageData.clear();
                loader.FrameTextures.clear();
                loader.FrameDurationsMs.clear();
                loader.IsAnimated = false;
                loader.LastStatusMessage.clear();

                if (loader.source_states.empty()) {
                    loader.state = ImageLoader::LoadState::Failed;
                    loader.LastStatusMessage = "Image failed: no sources.";
                    return;
                }

                const int activeIndex = (std::max)(0, (std::min)(loader.ActiveSourceIndex, static_cast<int>(loader.source_states.size()) - 1));
                loader.ActiveSourceIndex = activeIndex;
                auto& active = loader.source_states[static_cast<size_t>(activeIndex)];

                loader.fetch_id = active.FetchId;
                loader.request_started = active.RequestStarted;

                int completedSources = 0;
                bool anyLoading = false;
                for (const auto& runtime : loader.source_states) {
                    if (runtime.TerminalFailure || IsRuntimeRenderable(runtime)) {
                        ++completedSources;
                    }
                    anyLoading = anyLoading || IsRuntimeLoading(runtime);
                }

                int displaySourceIndex = -1;
                if (IsRuntimeRenderable(active)) {
                    displaySourceIndex = activeIndex;
                } else if (active.TerminalFailure && loader.SkipFailedSources) {
                    const int fallback = FindNextRenderableSource(loader, activeIndex + 1);
                    if (fallback >= 0) {
                        displaySourceIndex = fallback;
                    }
                }

                if (displaySourceIndex < 0 && loader.KeepLastSuccessfulTexture) {
                    const int lastGood = loader.LastGoodSourceIndex;
                    if (lastGood >= 0 && lastGood < static_cast<int>(loader.source_states.size()) &&
                        IsRuntimeRenderable(loader.source_states[static_cast<size_t>(lastGood)])) {
                        displaySourceIndex = lastGood;
                    }
                }

                if (displaySourceIndex >= 0) {
                    auto& displayed = loader.source_states[static_cast<size_t>(displaySourceIndex)];
                    loader.IsAnimated = displayed.IsAnimated && displayed.GpuFrames.size() > 1;
                    loader.FrameTextures.reserve(displayed.GpuFrames.size());
                    loader.FrameDurationsMs.reserve(displayed.GpuFrames.size());
                    for (const auto& frame : displayed.GpuFrames) {
                        loader.FrameTextures.push_back(frame.Texture);
                        loader.FrameDurationsMs.push_back(frame.DelayMs);
                    }

                    if (loader.CurrentFrame < 0) {
                        loader.CurrentFrame = 0;
                    }
                    if (loader.CurrentFrame >= static_cast<int>(displayed.GpuFrames.size())) {
                        loader.CurrentFrame = static_cast<int>(displayed.GpuFrames.size()) - 1;
                    }

                    if (loader.IsAnimated && loader.AutoPlay && !loader.Paused) {
                        const float deltaMs = ImGui::GetIO().DeltaTime * 1000.0f;
                        const float speed = (std::max)(0.01f, loader.PlaybackSpeed);
                        loader.FrameAccumulatorMs += deltaMs * speed;

                        while (!displayed.GpuFrames.empty()) {
                            const uint32_t frameDuration = (std::max)(1u, displayed.GpuFrames[static_cast<size_t>(loader.CurrentFrame)].DelayMs);
                            if (loader.FrameAccumulatorMs < static_cast<float>(frameDuration)) {
                                break;
                            }

                            loader.FrameAccumulatorMs -= static_cast<float>(frameDuration);
                            ++loader.CurrentFrame;
                            if (loader.CurrentFrame >= static_cast<int>(displayed.GpuFrames.size())) {
                                if (loader.Loop) {
                                    loader.CurrentFrame = 0;
                                } else {
                                    loader.CurrentFrame = static_cast<int>(displayed.GpuFrames.size()) - 1;
                                    loader.Paused = true;
                                    loader.FrameAccumulatorMs = 0.0f;
                                    break;
                                }
                            }
                        }
                    } else {
                        loader.FrameAccumulatorMs = 0.0f;
                        loader.CurrentFrame = (std::max)(0, (std::min)(loader.CurrentFrame, static_cast<int>(displayed.GpuFrames.size()) - 1));
                    }

                    const auto& current = displayed.GpuFrames[static_cast<size_t>(loader.CurrentFrame)];
                    loader.texture = current.Texture;
                    loader.texture_resource = current.TextureResource;
                    loader.width = current.Width;
                    loader.height = current.Height;
                    loader.state = ImageLoader::LoadState::Loaded;
                    loader.LastGoodSourceIndex = displaySourceIndex;
                    loader.LastGoodFrameIndex = loader.CurrentFrame;

                    if (displaySourceIndex != activeIndex) {
                        if (active.TerminalFailure) {
                            loader.LastStatusMessage = "Selected source failed. Showing fallback image.";
                        } else {
                            loader.LastStatusMessage = "Loading selected source. Showing last successful image.";
                        }
                    } else if (anyLoading) {
                        loader.LastStatusMessage = "Loaded image (" + std::to_string(completedSources) + "/" +
                            std::to_string(static_cast<int>(loader.source_states.size())) + " ready).";
                    } else {
                        loader.LastStatusMessage = "Image loaded.";
                    }
                    if (displayed.UploadTruncated) {
                        loader.LastStatusMessage += " (frame budget cap reached)";
                    }
                    return;
                }

                if (anyLoading) {
                    loader.state = ImageLoader::LoadState::Loading;
                    loader.LastStatusMessage = "Loading: " + SourceLabel(active) + " (" +
                        std::to_string(completedSources) + "/" +
                        std::to_string(static_cast<int>(loader.source_states.size())) + ")";
                    return;
                }

                loader.state = ImageLoader::LoadState::Failed;
                if (!active.LastErrorMessage.empty()) {
                    loader.LastStatusMessage = "Image failed: " + active.LastErrorMessage;
                } else {
                    loader.LastStatusMessage = "Image failed.";
                }
            }
        } // namespace detail

        inline void FreeEntity(entt::registry& registry, entt::entity entity) {
            auto* loader = registry.try_get<ImageLoader>(entity);
            if (!loader) {
                return;
            }

            detail::ResetLoader(*loader, false);
        }

        inline void OnConstruct(entt::registry& registry, entt::entity entity) {
            auto* loader = registry.try_get<ImageLoader>(entity);
            if (!loader) {
                return;
            }

            detail::ResetLoader(*loader, false);
            loader->hard_restart_requested = true;
            detail::EnsureRuntimeInitialized(*loader, true);
        }

        inline void OnDestroy(entt::registry& registry, entt::entity entity) {
            FreeEntity(registry, entity);
        }

        inline void Register(entt::registry& registry) {
            if (detail::s_RegisteredRegistries.insert(&registry).second) {
                registry.on_construct<ImageLoader>().connect<&OnConstruct>();
                registry.on_destroy<ImageLoader>().connect<&OnDestroy>();
            }

            auto view = registry.view<ImageLoader>();
            for (auto entity : view) {
                auto& loader = view.get<ImageLoader>(entity);
                detail::EnsureRuntimeInitialized(loader, false);
            }
        }

        inline void RequestRestart(entt::registry& registry, bool hardRestart = false) {
            const float nowMs = detail::NowMs();
            auto view = registry.view<ImageLoader>();
            for (auto entity : view) {
                auto& loader = view.get<ImageLoader>(entity);
                const float minRestartGap = (std::max)(0.0f, loader.RestartDebounceMs);
                if (loader.LastRestartAtMs >= 0.0f && (nowMs - loader.LastRestartAtMs) < minRestartGap) {
                    continue;
                }
                loader.LastRestartAtMs = nowMs;
                loader.hard_restart_requested = hardRestart;
                loader.restart_requested = !hardRestart;
                loader.LastStatusMessage.clear();
            }
        }

        inline bool CycleSource(entt::registry& registry, entt::entity entity, int direction = +1) {
            auto* loader = registry.try_get<ImageLoader>(entity);
            if (!loader) {
                return false;
            }

            detail::EnsureRuntimeInitialized(*loader, false);
            if (loader->source_states.empty()) {
                loader->state = ImageLoader::LoadState::Failed;
                loader->LastStatusMessage = "Image failed: no sources.";
                return false;
            }

            const int sourceCount = static_cast<int>(loader->source_states.size());
            if (sourceCount <= 0) {
                return false;
            }

            const int stepDir = direction < 0 ? -1 : 1;
            auto wrapIndex = [sourceCount](int idx) {
                int wrapped = idx % sourceCount;
                if (wrapped < 0) {
                    wrapped += sourceCount;
                }
                return wrapped;
            };

            int activeIndex = (std::max)(0, (std::min)(loader->ActiveSourceIndex, sourceCount - 1));
            loader->ActiveSourceIndex = activeIndex;

            auto selectSource = [&](int newIndex, bool forceRefetch) -> bool {
                if (newIndex < 0 || newIndex >= sourceCount) {
                    return false;
                }

                if (loader->ActiveSourceIndex != newIndex) {
                    loader->ActiveSourceIndex = newIndex;
                    loader->CurrentFrame = 0;
                    loader->FrameAccumulatorMs = 0.0f;
                }

                auto& runtime = loader->source_states[static_cast<size_t>(loader->ActiveSourceIndex)];
                if (forceRefetch) {
                    detail::CancelInFlightFetch(runtime);
                    runtime.TerminalFailure = false;
                    runtime.Failed = false;
                    runtime.PollComplete = false;
                    runtime.NextRetryAtMs = 0.0f;
                    runtime.LastError = ImageLoader::SourceRuntime::ErrorClass::None;
                    runtime.LastErrorMessage.clear();
                    runtime.UploadTruncated = false;
                    runtime.ReadyForUpload = false;
                    runtime.StagedFrames.clear();
                    runtime.NextUploadFrameIndex = 0;
                    detail::StartFetch(*loader, runtime, true);
                    loader->LastStatusMessage = "Refetching selected source...";
                    return true;
                }

                if (!runtime.RequestStarted &&
                    runtime.FetchId == 0 &&
                    !runtime.ReadyForUpload &&
                    !runtime.Uploaded &&
                    !runtime.TerminalFailure &&
                    detail::NowMs() >= runtime.NextRetryAtMs) {
                    detail::StartFetch(*loader, runtime);
                }

                if (detail::IsRuntimeRenderable(runtime)) {
                    loader->LastStatusMessage = "Switched image source.";
                } else if (runtime.TerminalFailure) {
                    loader->LastStatusMessage = "Selected source failed. Waiting for fallback.";
                } else {
                    loader->LastStatusMessage = "Loading selected source...";
                }
                return true;
            };

            if (loader->CycleMode == ImageLoader::SourceCycleMode::CycleLoadedOnly) {
                int candidate = -1;
                for (int offset = 1; offset < sourceCount; ++offset) {
                    const int idx = wrapIndex(activeIndex + stepDir * offset);
                    if (detail::IsRuntimeRenderable(loader->source_states[static_cast<size_t>(idx)])) {
                        candidate = idx;
                        break;
                    }
                }
                if (candidate < 0) {
                    loader->LastStatusMessage = "No other loaded source available.";
                    return false;
                }
                return selectSource(candidate, false);
            }

            if (loader->CycleMode == ImageLoader::SourceCycleMode::CycleAllSources) {
                const int nextIndex = sourceCount > 1 ? wrapIndex(activeIndex + stepDir) : activeIndex;
                return selectSource(nextIndex, false);
            }

            const int refetchIndex = sourceCount > 1 ? wrapIndex(activeIndex + stepDir) : activeIndex;
            return selectSource(refetchIndex, true);
        }

        inline int QueryPendingRequestCount() {
            return (std::max)(0, detail::s_PendingFetchCount.load(std::memory_order_relaxed));
        }

        inline ImageLoaderDebugStats QueryDebugStats(entt::registry& registry) {
            ImageLoaderDebugStats stats;
            const int capacity = detail::SrvCapacity();
            const int used = detail::DescriptorUsedCount();
            const int dynamicCapacity = (std::max)(0, capacity - detail::kSrvReservedStart);

            stats.descriptorUsed = used;
            stats.descriptorFree = (std::max)(0, dynamicCapacity - used);
            stats.deferredResourceReleases = static_cast<int>(detail::s_DeferredReleases.size());
            stats.deferredDescriptorRecycles = static_cast<int>(detail::s_DeferredSrvRecycles.size());
            (void)registry;
            stats.pendingFetches = QueryPendingRequestCount();
            stats.uploadedFramesLastTick = detail::s_LastUploadedFrames;
            stats.uploadedBytesLastTick = detail::s_LastUploadedBytes;
            return stats;
        }

        inline void Update(entt::registry& registry) {
            detail::DrainDeferredReleases();
            detail::s_LastUploadedFrames = 0;
            detail::s_LastUploadedBytes = 0;

            auto view = registry.view<ImageLoader>();
            for (auto entity : view) {
                auto& loader = view.get<ImageLoader>(entity);
                detail::EnsureRuntimeInitialized(loader, false);
                if (loader.hard_restart_requested) {
                    detail::EnsureRuntimeInitialized(loader, true);
                    loader.hard_restart_requested = false;
                    loader.restart_requested = false;
                } else if (loader.restart_requested) {
                    detail::SoftRestartLoader(loader);
                    loader.restart_requested = false;
                }

                if (loader.normalized_sources.empty()) {
                    loader.state = ImageLoader::LoadState::Failed;
                    loader.LastStatusMessage = "Image failed: no sources.";
                    continue;
                }

                detail::StartNeededSources(loader);

                for (auto& runtime : loader.source_states) {
                    detail::PollFetch(loader, runtime);
                }

                for (auto& runtime : loader.source_states) {
                    if (runtime.ReadyForUpload && !runtime.Failed) {
                        int uploadedFrames = 0;
                        size_t uploadedBytes = 0;
                        const detail::UploadResult result = detail::UploadStagedFrames(loader, runtime, uploadedFrames, uploadedBytes);
                        detail::s_LastUploadedFrames += uploadedFrames;
                        detail::s_LastUploadedBytes += uploadedBytes;
                        if (result == detail::UploadResult::FatalError) {
                            detail::MarkRuntimeFailure(
                                loader,
                                runtime,
                                ImageLoader::SourceRuntime::ErrorClass::UploadFailed,
                                "GPU upload failed.",
                                true);
                        }
                    }
                }

                detail::SyncLegacyFields(loader);
            }
        }

        inline ImageLoaderProgress QueryProgress(entt::registry& registry) {
            ImageLoaderProgress progress;
            progress.done = true;
            progress.percent = 1.0f;

            auto view = registry.view<ImageLoader>();
            for (auto entity : view) {
                auto& loader = view.get<ImageLoader>(entity);
                if (!loader.StartupRequired) {
                    continue;
                }

                ++progress.totalRequired;

                if (!loader.sources_initialized || loader.source_states.empty()) {
                    const auto normalizedSources = detail::BuildNormalizedSources(loader);
                    if (normalizedSources.empty() || loader.state == ImageLoader::LoadState::Failed) {
                        ++progress.failedRequired;
                    } else {
                        ++progress.loadingRequired;
                        if (progress.currentLabel.empty()) {
                            progress.currentLabel = loader.LastStatusMessage.empty() ? "Loading image..." : loader.LastStatusMessage;
                        }
                    }
                    continue;
                }

                bool hasRenderable = false;
                bool hasLoading = false;
                for (const auto& runtime : loader.source_states) {
                    hasRenderable = hasRenderable || detail::IsRuntimeRenderable(runtime);
                    hasLoading = hasLoading || detail::IsRuntimeLoading(runtime);
                }
                if (!hasRenderable && loader.state == ImageLoader::LoadState::Loaded && loader.texture != nullptr) {
                    hasRenderable = true;
                }

                if (hasRenderable) {
                    ++progress.readyRequired;
                    continue;
                }

                if (hasLoading || loader.state == ImageLoader::LoadState::Loading || loader.state == ImageLoader::LoadState::Idle) {
                    ++progress.loadingRequired;
                    if (progress.currentLabel.empty()) {
                        progress.currentLabel = loader.LastStatusMessage.empty() ? "Loading image..." : loader.LastStatusMessage;
                    }
                    continue;
                }

                ++progress.failedRequired;
            }

            if (progress.totalRequired <= 0) {
                progress.done = true;
                progress.percent = 1.0f;
                return progress;
            }

            const int completed = progress.readyRequired + progress.failedRequired;
            progress.percent = static_cast<float>(completed) / static_cast<float>(progress.totalRequired);
            if (progress.percent < 0.0f) {
                progress.percent = 0.0f;
            }
            if (progress.percent > 1.0f) {
                progress.percent = 1.0f;
            }
            progress.done = progress.loadingRequired == 0;
            if (progress.currentLabel.empty() && !progress.done) {
                progress.currentLabel = "Loading images...";
            }

            return progress;
        }

        inline void Shutdown(entt::registry& registry) {
            detail::DrainDeferredReleases();

            auto view = registry.view<ImageLoader>();
            for (auto entity : view) {
                auto& loader = view.get<ImageLoader>(entity);
                detail::ResetLoader(loader, true);
            }

            registry.on_construct<ImageLoader>().disconnect<&OnConstruct>();
            registry.on_destroy<ImageLoader>().disconnect<&OnDestroy>();

            for (auto& pending : detail::s_DeferredReleases) {
                if (pending.resource) {
                    pending.resource->Release();
                }
            }
            detail::s_DeferredReleases.clear();
            for (const auto& pendingSrv : detail::s_DeferredSrvRecycles) {
                detail::RecycleSrvIndexImmediate(pendingSrv.srvIndex);
            }
            detail::s_DeferredSrvRecycles.clear();
            detail::s_FreeSrvIndices.clear();
            detail::s_NextSrvIndex.store(detail::kSrvReservedStart, std::memory_order_relaxed);
            detail::s_PendingFetchCount.store(0, std::memory_order_relaxed);
            detail::s_RegisteredRegistries.erase(&registry);
        }
    } // namespace ImageLoaderSystem
} // namespace Components
