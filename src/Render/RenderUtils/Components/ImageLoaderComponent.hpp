#pragma once

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
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

        std::vector<ImageSource> Sources;
        int ActiveSourceIndex = 0;
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

        std::string requested_url;
        bool request_started = false;
        bool restart_requested = false;
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
            int Width = 0;
            int Height = 0;
            uint32_t DelayMs = 100;
        };

        struct SourceRuntime {
            ImageSource Source;
            uint64_t FetchId = 0;
            bool RequestStarted = false;
            bool Failed = false;
            bool PollComplete = false;
            bool ReadyForUpload = false;
            bool Uploaded = false;
            bool IsAnimated = false;
            std::vector<DecodedFrame> StagedFrames;
            std::vector<GpuFrame> GpuFrames;
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

    namespace ImageLoaderSystem {
        namespace detail {
            struct DeferredRelease {
                UINT64 fenceValue = 0;
                ID3D12Resource* resource = nullptr;
            };

            enum class UploadResult {
                Uploaded,
                RetryLater,
                FatalError
            };

            inline std::atomic<int> s_NextSrvIndex = 10;
            inline std::vector<DeferredRelease> s_DeferredReleases;
            inline std::unordered_set<entt::registry*> s_RegisteredRegistries;

            inline bool IsLikelyUrl(const std::string& value) {
                return value.rfind("http://", 0) == 0 || value.rfind("https://", 0) == 0;
            }

            inline UINT64 NextFenceTarget() {
                if (!DX12Init::g_fence) {
                    return 0;
                }
                return DX12Init::g_fenceLastSignaledValue + 1;
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
                if (s_DeferredReleases.empty()) {
                    return;
                }

                if (!DX12Init::g_fence) {
                    for (auto& pending : s_DeferredReleases) {
                        if (pending.resource) {
                            pending.resource->Release();
                        }
                    }
                    s_DeferredReleases.clear();
                    return;
                }

                const UINT64 completedFence = DX12Init::g_fence->GetCompletedValue();
                auto it = s_DeferredReleases.begin();
                while (it != s_DeferredReleases.end()) {
                    if (completedFence >= it->fenceValue) {
                        if (it->resource) {
                            it->resource->Release();
                        }
                        it = s_DeferredReleases.erase(it);
                    } else {
                        ++it;
                    }
                }
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
                if (!DX12Init::g_pd3dSrvDescHeap) {
                    return false;
                }

                const D3D12_DESCRIPTOR_HEAP_DESC heapDesc = DX12Init::g_pd3dSrvDescHeap->GetDesc();
                while (true) {
                    int current = s_NextSrvIndex.load(std::memory_order_relaxed);
                    if (current >= static_cast<int>(heapDesc.NumDescriptors)) {
                        return false;
                    }

                    if (s_NextSrvIndex.compare_exchange_weak(current, current + 1, std::memory_order_relaxed)) {
                        outSrvIndex = current;
                        return true;
                    }
                }
            }

            inline void ReleaseGpuFrames(std::vector<ImageLoader::GpuFrame>& frames, bool immediate) {
                for (auto& frame : frames) {
                    ReleaseResource(frame.TextureResource, immediate);
                    frame.Texture = nullptr;
                }
                frames.clear();
            }

            inline void ResetSourceRuntime(ImageLoader::SourceRuntime& runtime, bool immediateRelease) {
                runtime.FetchId = 0;
                runtime.RequestStarted = false;
                runtime.Failed = false;
                runtime.PollComplete = false;
                runtime.ReadyForUpload = false;
                runtime.Uploaded = false;
                runtime.IsAnimated = false;
                runtime.StagedFrames.clear();
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
                loader.state = ImageLoader::LoadState::Idle;
                loader.CurrentFrame = 0;
                loader.FrameAccumulatorMs = 0.0f;
                loader.IsAnimated = false;
                loader.FrameTextures.clear();
                loader.FrameDurationsMs.clear();
                loader.texture_resource = nullptr;
                loader.upload_resource = nullptr;
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

            inline bool StartFetch(ImageLoader::SourceRuntime& runtime) {
                runtime.FetchId = start_fetch_media(
                    runtime.Source.Value.c_str(),
                    runtime.Source.Type == ImageLoader::ImageSourceType::Url
                        ? static_cast<int32_t>(ImageSourceKind::Url)
                        : static_cast<int32_t>(ImageSourceKind::LocalPath));

                runtime.RequestStarted = runtime.FetchId != 0;
                runtime.Failed = !runtime.RequestStarted;
                if (runtime.Failed) {
                    runtime.PollComplete = true;
                }
                return runtime.RequestStarted;
            }

            inline void EnsureRuntimeInitialized(ImageLoader& loader, bool forceRestart) {
                const std::vector<ImageLoader::ImageSource> desiredSources = BuildNormalizedSources(loader);
                const bool sourcesChanged = !SourcesEqual(loader.normalized_sources, desiredSources);
                const bool legacyUrlChanged = loader.Sources.empty() && loader.requested_url != loader.url;

                if (!loader.sources_initialized || sourcesChanged || forceRestart || legacyUrlChanged) {
                    ResetLoader(loader, false);
                    loader.normalized_sources = desiredSources;
                    loader.requested_url = loader.url;
                    loader.sources_initialized = true;

                    if (loader.normalized_sources.empty()) {
                        loader.state = ImageLoader::LoadState::Failed;
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
                            StartFetch(runtime);
                        }
                    } else {
                        StartFetch(loader.source_states[static_cast<size_t>(loader.ActiveSourceIndex)]);
                    }

                    loader.state = ImageLoader::LoadState::Loading;
                }
            }

            inline void PollFetch(ImageLoader::SourceRuntime& runtime) {
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

                if (status != static_cast<int32_t>(ImageFetchStatus::Ready)) {
                    runtime.Failed = true;
                    runtime.PollComplete = true;
                    return;
                }

                runtime.StagedFrames.clear();
                runtime.IsAnimated = false;

                if (mediaKind == static_cast<int32_t>(FetchedMediaKind::StaticRGBA)) {
                    if (!staticData || staticLen == 0 || staticWidth <= 0 || staticHeight <= 0) {
                        if (staticData) {
                            free_image_data(staticData, staticLen);
                        }
                        runtime.Failed = true;
                        runtime.PollComplete = true;
                        return;
                    }

                    const size_t expectedLen = static_cast<size_t>(staticWidth) * static_cast<size_t>(staticHeight) * 4u;
                    if (staticLen < expectedLen) {
                        free_image_data(staticData, staticLen);
                        runtime.Failed = true;
                        runtime.PollComplete = true;
                        return;
                    }

                    ImageLoader::DecodedFrame frame;
                    frame.Width = staticWidth;
                    frame.Height = staticHeight;
                    frame.DelayMs = 100;
                    frame.Pixels.assign(staticData, staticData + expectedLen);
                    runtime.StagedFrames.push_back(std::move(frame));
                    free_image_data(staticData, staticLen);
                } else {
                    if (!frames || frameCount == 0) {
                        runtime.Failed = true;
                        runtime.PollComplete = true;
                        return;
                    }

                    runtime.IsAnimated = frameCount > 1;
                    runtime.StagedFrames.reserve(frameCount);
                    for (size_t i = 0; i < frameCount; ++i) {
                        const AnimatedFrameFFI& ffiFrame = frames[i];
                        if (!ffiFrame.data || ffiFrame.len == 0 || ffiFrame.width <= 0 || ffiFrame.height <= 0) {
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
                runtime.Failed = !runtime.ReadyForUpload;
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
                    return UploadResult::FatalError;
                }

                void* mapped = nullptr;
                if (FAILED(uploadResource->Map(0, nullptr, &mapped)) || !mapped) {
                    uploadResource->Release();
                    textureResource->Release();
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

                int srvIndex = -1;
                if (!ReserveSrvIndex(srvIndex)) {
                    uploadResource->Release();
                    textureResource->Release();
                    return UploadResult::FatalError;
                }

                D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
                srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
                srvDesc.Texture2D.MipLevels = 1;
                DX12Init::g_pd3dDevice->CreateShaderResourceView(textureResource, &srvDesc, DX12Init::GetCpuSrvHandle(srvIndex));

                outFrame.Texture = reinterpret_cast<void*>(static_cast<intptr_t>(srvIndex));
                outFrame.TextureResource = textureResource;
                outFrame.Width = frame.Width;
                outFrame.Height = frame.Height;
                outFrame.DelayMs = frame.DelayMs == 0 ? 100u : frame.DelayMs;

                outUploadResource = uploadResource;
                return UploadResult::Uploaded;
            }

            inline UploadResult UploadStagedFrames(ImageLoader::SourceRuntime& runtime) {
                if (!runtime.ReadyForUpload || runtime.StagedFrames.empty()) {
                    return UploadResult::Uploaded;
                }

                std::vector<ImageLoader::GpuFrame> newFrames;
                newFrames.reserve(runtime.StagedFrames.size());

                for (const auto& stagedFrame : runtime.StagedFrames) {
                    ImageLoader::GpuFrame gpuFrame;
                    ID3D12Resource* uploadResource = nullptr;
                    const UploadResult result = UploadFrameToGpu(stagedFrame, gpuFrame, uploadResource);
                    if (result == UploadResult::RetryLater) {
                        ReleaseGpuFrames(newFrames, false);
                        if (uploadResource) {
                            uploadResource->Release();
                        }
                        return UploadResult::RetryLater;
                    }
                    if (result == UploadResult::FatalError) {
                        ReleaseGpuFrames(newFrames, false);
                        if (uploadResource) {
                            uploadResource->Release();
                        }
                        return UploadResult::FatalError;
                    }

                    if (uploadResource) {
                        QueueDeferredRelease(uploadResource, NextFenceTarget());
                    }
                    newFrames.push_back(gpuFrame);
                }

                ReleaseGpuFrames(runtime.GpuFrames, false);
                runtime.GpuFrames = std::move(newFrames);
                runtime.StagedFrames.clear();
                runtime.ReadyForUpload = false;
                runtime.Uploaded = true;
                runtime.Failed = runtime.GpuFrames.empty();
                return runtime.Failed ? UploadResult::FatalError : UploadResult::Uploaded;
            }

            inline void StartNeededSources(ImageLoader& loader) {
                if (loader.source_states.empty()) {
                    return;
                }

                if (loader.PreloadAllSources) {
                    for (auto& runtime : loader.source_states) {
                        if (!runtime.RequestStarted && !runtime.PollComplete && !runtime.Failed && runtime.FetchId == 0) {
                            StartFetch(runtime);
                        }
                    }
                    return;
                }

                const int activeIndex = (std::max)(0, (std::min)(loader.ActiveSourceIndex, static_cast<int>(loader.source_states.size()) - 1));
                loader.ActiveSourceIndex = activeIndex;
                auto& active = loader.source_states[static_cast<size_t>(activeIndex)];
                if (!active.RequestStarted && !active.PollComplete && !active.Failed && active.FetchId == 0) {
                    StartFetch(active);
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

                if (loader.source_states.empty()) {
                    loader.state = ImageLoader::LoadState::Failed;
                    return;
                }

                const int activeIndex = (std::max)(0, (std::min)(loader.ActiveSourceIndex, static_cast<int>(loader.source_states.size()) - 1));
                loader.ActiveSourceIndex = activeIndex;
                auto& active = loader.source_states[static_cast<size_t>(activeIndex)];

                loader.fetch_id = active.FetchId;
                loader.request_started = active.RequestStarted;

                if (active.Failed) {
                    loader.state = ImageLoader::LoadState::Failed;
                    return;
                }

                if (!active.Uploaded || active.GpuFrames.empty()) {
                    loader.state = ImageLoader::LoadState::Loading;
                    return;
                }

                loader.IsAnimated = active.IsAnimated && active.GpuFrames.size() > 1;
                loader.FrameTextures.reserve(active.GpuFrames.size());
                loader.FrameDurationsMs.reserve(active.GpuFrames.size());
                for (const auto& frame : active.GpuFrames) {
                    loader.FrameTextures.push_back(frame.Texture);
                    loader.FrameDurationsMs.push_back(frame.DelayMs);
                }

                if (loader.CurrentFrame < 0) {
                    loader.CurrentFrame = 0;
                }
                if (loader.CurrentFrame >= static_cast<int>(active.GpuFrames.size())) {
                    loader.CurrentFrame = static_cast<int>(active.GpuFrames.size()) - 1;
                }

                if (loader.IsAnimated && loader.AutoPlay && !loader.Paused) {
                    const float deltaMs = ImGui::GetIO().DeltaTime * 1000.0f;
                    const float speed = (std::max)(0.01f, loader.PlaybackSpeed);
                    loader.FrameAccumulatorMs += deltaMs * speed;

                    while (!active.GpuFrames.empty()) {
                        const uint32_t frameDuration = (std::max)(1u, active.GpuFrames[static_cast<size_t>(loader.CurrentFrame)].DelayMs);
                        if (loader.FrameAccumulatorMs < static_cast<float>(frameDuration)) {
                            break;
                        }

                        loader.FrameAccumulatorMs -= static_cast<float>(frameDuration);
                        ++loader.CurrentFrame;
                        if (loader.CurrentFrame >= static_cast<int>(active.GpuFrames.size())) {
                            if (loader.Loop) {
                                loader.CurrentFrame = 0;
                            } else {
                                loader.CurrentFrame = static_cast<int>(active.GpuFrames.size()) - 1;
                                loader.Paused = true;
                                loader.FrameAccumulatorMs = 0.0f;
                                break;
                            }
                        }
                    }
                } else {
                    loader.FrameAccumulatorMs = 0.0f;
                    loader.CurrentFrame = (std::max)(0, (std::min)(loader.CurrentFrame, static_cast<int>(active.GpuFrames.size()) - 1));
                }

                const auto& current = active.GpuFrames[static_cast<size_t>(loader.CurrentFrame)];
                loader.texture = current.Texture;
                loader.texture_resource = current.TextureResource;
                loader.width = current.Width;
                loader.height = current.Height;
                loader.state = ImageLoader::LoadState::Loaded;
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
            loader->restart_requested = true;
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

        inline void Update(entt::registry& registry) {
            detail::DrainDeferredReleases();

            auto view = registry.view<ImageLoader>();
            for (auto entity : view) {
                auto& loader = view.get<ImageLoader>(entity);
                detail::EnsureRuntimeInitialized(loader, loader.restart_requested);
                loader.restart_requested = false;

                if (loader.normalized_sources.empty()) {
                    loader.state = ImageLoader::LoadState::Failed;
                    continue;
                }

                detail::StartNeededSources(loader);

                for (auto& runtime : loader.source_states) {
                    detail::PollFetch(runtime);
                }

                for (auto& runtime : loader.source_states) {
                    if (runtime.ReadyForUpload && !runtime.Failed) {
                        const detail::UploadResult result = detail::UploadStagedFrames(runtime);
                        if (result == detail::UploadResult::FatalError) {
                            runtime.Failed = true;
                            runtime.ReadyForUpload = false;
                            runtime.StagedFrames.clear();
                        }
                    }
                }

                detail::SyncLegacyFields(loader);
            }
        }

        inline void Shutdown(entt::registry& registry) {
            detail::DrainDeferredReleases();

            auto view = registry.view<ImageLoader>();
            for (auto entity : view) {
                auto& loader = view.get<ImageLoader>(entity);
                detail::ResetLoader(loader, true);
            }

            for (auto& pending : detail::s_DeferredReleases) {
                if (pending.resource) {
                    pending.resource->Release();
                }
            }
            detail::s_DeferredReleases.clear();
        }
    } // namespace ImageLoaderSystem
} // namespace Components
