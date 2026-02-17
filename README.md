# Custom ImGui Renderer12

DirectX 12 + ImGui + EnTT ECS renderer with Rust-powered async loading for images/media/fonts.

This README is a full handbook for this codebase: startup flow, system ordering, component arguments, custom callback contracts, shader/font/media behavior, and crash/error handling.

## Table Of Contents
- [Architecture](#architecture)
- [Build And Run](#build-and-run)
- [Injected DX12 Test DLL (Bedrock DX12)](#injected-dx12-test-dll-bedrock-dx12)
- [Lifecycle API (`DX12Init::RunApp`)](#lifecycle-api-dx12initrunapp)
- [Frame Update/Render Order](#frame-updaterender-order)
- [Startup Runtime](#startup-runtime)
- [UI Builder API](#ui-builder-api)
- [Custom Component API (All Arguments)](#custom-component-api-all-arguments)
- [Text + Fonts](#text--fonts)
- [Shader System](#shader-system)
- [Image/Media Loader System](#imagemedia-loader-system)
- [Component Reference (All Public Components)](#component-reference-all-public-components)
- [Rust FFI Reference](#rust-ffi-reference)
- [Diagnostics And Troubleshooting](#diagnostics-and-troubleshooting)
- [Strict Warning Policy](#strict-warning-policy)
- [Practical Safety Rules](#practical-safety-rules)

## Architecture

High-level modules:
- `src/Dx12Init/*`
  Win32 + DX12 device/swapchain/frame-lifecycle runtime (`DX12Init::RunApp`) and external host runtime (`ExternalOverlayRuntime`).
- `src/Scene/ShowcaseRuntime.*`
  Shared demo-scene lifecycle used by both standalone app and injected DLL paths.
- `src/Render/ImguiRender.*`
  ImGui frame begin/end bridge.
- `src/Render/RenderUtils/*`
  ECS UI framework (`UIRenderer`, `UIBuilder`, components, shader/font systems).
- `src/RustComponents/*`
  Rust async fetch/decode bridge for media bytes/images/animations.

Core responsibilities:
- `DX12Init`
  Owns window/device loop, frame packet callbacks, and shutdown.
- `ExternalOverlayRuntime`
  Owns injected-host queue capture/resource lifecycle, resize handling, WndProc input capture, and external frame bridging.
- `ShowcaseRuntime`
  Owns scene setup/update/shutdown and UI entity registry state for the demo.
- `UIRenderer`
  ECS update + draw orchestration.
- `ShaderSystem`
  Shader source resolution, compile/cache, draw queue/callback path, fallback behavior.
- `FontSystem`
  Async font fetch, runtime face registry, deferred atlas rebuild, font resolve helpers.
- `Components::ImageLoaderSystem`
  Async source fetch, decode, DX12 upload, fallback source switching, progress/debug stats.

Ownership map:
- `src/main.cpp` + `src/MainRendering.*`
  standalone EXE entry only.
- `src/Scene/ShowcaseRuntime.*`
  shared scene logic (used by EXE and injected DLL).
- `src/Dx12Init/ExternalOverlayRuntime.*`
  injected host runtime core.
- `Test/InjectedDx12/DirectX12HookRuntime.*`
  thin kiero shim (bind/unbind + forward only).

## Build And Run

Prerequisites:
- Windows + DX12 runtime.
- Visual Studio/MSVC toolchain.
- CMake `>= 3.21`.
- Rust toolchain `stable-x86_64-pc-windows-msvc`.

Recommended build command (MSVC env):

```powershell
cmd /c "\"C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat\" && cmake -S . -B out/build/x64-Debug -G Ninja -DCMAKE_BUILD_TYPE=Debug && cmake --build out/build/x64-Debug --config Debug --target ImGuiDX12App"
```

Run:

```powershell
.\out\build\x64-Debug\ImGuiDX12App.exe
```

Dependency notes:
- ImGui, EnTT, Corrosion, battery::embed are fetched by CMake.
- Rust compiler/cargo are resolved via `rustup which` and forced into Corrosion configuration.
- Default shaders are compile-time embedded (`battery::embed`):
  - `shader.default.vs`
  - `glow.default`
  - `shadow.default`

Optional dual-target build (app + injected DX12 test DLL):

```powershell
cmd /c "\"C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat\" && cmake -S . -B out/build/x64-Debug -G Ninja -DCMAKE_BUILD_TYPE=Debug -DIMGUI_DX12APP_BUILD_TEST_DLL=ON && cmake --build out/build/x64-Debug --config Debug --target ImGuiDX12App ImGuiDX12TestHook"
```

## Injected DX12 Test DLL (Bedrock DX12)

Build option:
- `IMGUI_DX12APP_BUILD_TEST_DLL=ON`

Produced target:
- `ImGuiDX12TestHook` (DLL), output to `out/build/<config>/Test/`.

What it does:
- Hooks DX12 host-owned swapchains through `BasedInc/kiero` (D3D12 path).
- Uses the shared `Scene::ShowcaseRuntime` full demo scene by default.
- Initializes the renderer through `DX12Init` external-runtime API.
- Handles swapchain resize by tearing down and reinitializing hook-side resources.
- Uses strict queue ownership checks and per-backbuffer fence synchronization before allocator reuse.
- Uses always-capture input policy in injected mode so dragging/clicking remains reliable.
- Enables font/image loading in injected mode while keeping shader loading disabled by default for host stability.

Runtime scope:
- DX12 hosts only for this test path.
- Intended for authorized local testing environments.
- Manual injection workflow is intentionally high-level only; no anti-cheat bypass guidance is provided.
- Debug and Release test DLL builds are supported; Release is recommended for runtime validation.

Safety defaults in injected mode:
- `AllowFontAtlasRebuild=true`
- `AllowShaderSystem=false`
- `CaptureHostInputAlways=true`
- Raw fallback input polling enabled (`GetCursorPos` + `GetAsyncKeyState`) before `NewFrame`.
- `Scene::ShowcaseRuntime::Options` defaults for injected path:
  - `EnableImageLoading=true`
  - `EnableShaderLoading=false`
  - `ForceImmediateStartup=true`
  - `RelaxRequiredFonts=true`
  - `RelaxRequiredImages=true`
  - `StripLocalPathMediaSources=true`
  - `PreferRemoteBodyFont=true`
- Startup is non-blocking in injected mode (`ImmediateUI` policy).
- Local-path media sources are stripped in injected mode; URL sources are retained.
- Font policy is fallback-first: default ImGui font is allowed until injected body faces become ready.
- Known conflicting overlay modules fail closed (hook auto-disables):
  - `graphics-hook64.dll`
  - `RTSSHooks64.dll`
  - `GameOverlayRenderer64.dll`
  - `DiscordHook64.dll`
  - `NvCameraAllowlisting64.dll`

## Lifecycle API (`DX12Init::RunApp`)

Header: `src/Dx12Init/Dx12Init.hpp`

Primary structs:
- `DX12Init::WindowConfig`
  window class/title/position/style/show options.
- `DX12Init::RunConfig`
  runtime behavior:
  - `ClearColor[4]`
  - `VSync`
  - `AutoInitImGui`
  - `AutoInitShaderSystem`
  - `AutoShowWindow`
  - `MessageHook` (`std::function<LRESULT(HWND, UINT, WPARAM, LPARAM, bool&)>`)
- `DX12Init::FramePacket`
  per-frame data passed to `OnFrame`:
  - `WindowHandle`
  - `FrameContext*`
  - `BackBufferIndex`
  - `ID3D12GraphicsCommandList*`
  - `ImGuiIO*`
  - `DeltaTime`
- `DX12Init::RuntimeCallbacks`
  - `OnSetup(HWND)`
  - `OnFrame(const FramePacket&)`
  - `OnShutdown()`

Entry point:
- `int DX12Init::RunApp(HINSTANCE instance, const RunConfig& runConfig, const RuntimeCallbacks& callbacks);`

Exit:
- `DX12Init::RequestExit()`

External host-owned runtime API (for injected/hooked DX12 renderers):
- `DX12Init::ExternalRuntimeConfig`
  - `WindowHandle`
  - `Device`
  - `CommandQueue`
  - `SrvHeap`
  - `BackbufferFormat`
  - `NumFramesInFlight`
  - `AutoInitImGui`
  - `AutoInitShaderSystem`
  - `AllowFontAtlasRebuild`
  - `AllowShaderSystem`
  - `WaitForGpuIdle`
- `DX12Init::ExternalFrameInput`
  - `CommandList`
  - `CurrentRTV`
  - `DisplaySize`
  - `BackBufferIndex`
  - `DeltaTime`
- `bool DX12Init::AttachExternalRuntime(const ExternalRuntimeConfig&);`
- `void DX12Init::DetachExternalRuntime();`
- `bool DX12Init::BeginExternalFrame(const ExternalFrameInput&, FramePacket& outPacket);`
- `void DX12Init::EndExternalFrame();`

Shared scene/runtime interfaces:
- `Scene::ShowcaseRuntime`
  - `Setup(HWND)`
  - `RenderFrame(const DX12Init::FramePacket&)`
  - `Shutdown()`
- `DX12Init::ExternalOverlayRuntime`
  - `Configure(const Config&)`
  - `CaptureQueueCandidate(ID3D12CommandQueue*)`
  - `OnPresent(...)`
  - `OnResizeBuffers(...)`
  - `OnResizeBuffers1(...)`
  - `OnWndProc(...)`
  - `Shutdown()`
  - `IsReady()`

## Frame Update/Render Order

`UIRenderer::Update` order is intentionally fixed:
- Tab visibility logic
- Tab trigger logic
- `UpdateAnimations`
- custom `OnUpdate` callbacks
- `ResolveTransforms`
- `ResolveDepth`
- `FontSystem::Update`
- `UpdateInput`
- `OptionsSystem::Update`
- `ShaderSystem::UpdateCompile`
- custom `OnInput` callbacks
- `UpdateText`
- `UpdateImageLoader`

`UIRenderer::Render` behavior:
- Builds a local `std::vector<entt::entity>` and sorts deterministically by:
  - `StyleComponent.ZIndexInt` ascending
  - tie-break: `entt::to_integral(entity)` ascending
- This removes sibling ordering instability and transparency flicker.

Render pass highlights:
- Optional parent/self clip stack (`ClipComponent`).
- Shadow before glow before background/content.
- Shader glow/shadow try shader path first, fallback to CPU path if queue/compile fails.
- Text paths resolve fonts through `FontSystem` based on apply flags.
- Custom render callback runs per entity after main text/content draws.

## Startup Runtime

Namespace: `RenderUtils::StartupRuntime` in `src/Render/RenderUtils/UIRenderer.hpp/.cpp`.

Config:
- `Mode::BlockOnRequiredImages`
- `Mode::ImmediateUI`
- `Priority::ImagesFirst`
- `Priority::SystemsFirst`
- `Priority::Balanced`
- `TimeoutSeconds`

State:
- `Initialized`, `Completed`, `TimedOut`
- `StartTimeSec`, `ElapsedSec`
- `Progress` (`Components::ImageLoaderProgress`)
- `SummaryLine`

Behavior:
- In blocking mode, required startup assets include:
  - required images (`ImageLoader.StartupRequired`)
  - required fonts (`FontFaceSpec.StartupRequired`)
- Combined progress is computed from image + font systems.

## UI Builder API

Header: `src/Render/RenderUtils/UIBuilder.hpp`

### `UIBuilder`
- `UIBuilder::Begin(registry)`
- `.Create<ContainerType>(name)`
  creates entity with default:
  - `TransformComponent`
  - `StyleComponent`
  - `ContainerComponent`
  - `InputStateComponent`
  - `WindowHeaderComponent` auto-added for `ContainerType::Window`
- `.With<Component>(args...)`
- `.Child(const UIBuilder&)`
- `.Child<ContainerType>(name, configureLambda)`
- `.DrawAbove(targetName)`
- `.IsTab(tabId)`
- `.IsTabTrigger(tabId, activeColor, inactiveColor)`
- `.Animate()` returns `AnimationBuilder`
- `.AnimateLegacy(...)` (compat path)
- `.End()`, `.Entity()`, `.Registry()`

### `AnimationBuilder`
- `.Duration(float)`
- `.Ease(EasingType)`
- `.Loop(bool)`
- `.Float(start, end, apply)`
- `.Vec2(start, end, apply)`
- `.Color(start, end, apply)`
- `.Custom([](float easedT, entt::registry&, entt::entity){ ... })`
- `.Start()`

## Custom Component API (All Arguments)

Header: `src/Render/RenderUtils/CustomComponents/CustomComponent.hpp`

`CustomComponent` callbacks:
- `RenderCallback`
  ```cpp
  void(entt::registry& registry,
       entt::entity entity,
       ImDrawList* drawList,
       ImVec2 p_min,
       ImVec2 p_max,
       bool isHovered,
       bool isClicked)
  ```
- `UpdateCallback`
  ```cpp
  void(entt::registry& registry, entt::entity entity, float deltaTime)
  ```
- `InputCallback`
  ```cpp
  void(entt::registry& registry, entt::entity entity, const InputStateComponent& input)
  ```

Other fields:
- `Enabled` (default `true`)
- `Priority` (higher runs first for custom callback ordering)

Setters:
- `SetOnRender(...)`
- `SetOnUpdate(...)`
- `SetOnInput(...)`
- `SetEnabled(bool)`
- `SetPriority(int)`

Runtime ordering:
- `OnUpdate` runs before transforms/depth/input.
- `OnInput` runs after input + options + shader compile update.
- `OnRender` runs during draw pass for visible entities.

Best practices:
- Use `input.JustPressed`/`JustReleased` for edge-triggered actions.
- Keep per-frame compute in `OnUpdate`, not `OnRender`.
- For text inside custom draw, prefer `FontSystem::ResolveEntityFont` + `FontSystem::AddText`.

## Text + Fonts

### Text API

Header: `src/Render/RenderUtils/Components/TextComponent.hpp`

Enums:
- `TextAlign`: `Left`, `Center`, `Right`, `Justify`
- `TextFlags`: `Wrap`, `Clip`, `PrecisionMode` (+ bitwise helpers)
- `TextStyle`: `Normal`, `Bold`, `Italic`

`TextSpan`:
- `Text`
- `Color`
- `Style`
- `FontKey` (optional span-level face key)

`TextComponent` fields:
- `RawText`
- `Spans`
- `Color`
- `Alignment`
- `Flags`
- `LineHeight`
- `FontKey` (entity-level default text face key)
- `CharacterTransformCallback`
  ```cpp
  void(int index, char c, ImVec2& pos, float& rotation, ImU32& color, float& scale)
  ```

Builder methods:
- `Span(...)` overloads (with/without color/style/font key)
- `SpanFont(text, fontKey, color, style)`
- `SetFont(key)`
- `Align(...)`
- `SetFlags(...)`
- `AddFlag(...)`
- `SetLineHeight(...)`
- `AddSpan(...)` legacy helper

Layout helpers (`TextLayout`):
- `CalculateLayout(...)`
- `CalculateTextLines(...)`
- Font-aware when resolver is provided.

Notes:
- `PrecisionMode` is used by hit-testing/collision constraints for text rect precision.
- `Clip` pushes text clip rect inside entity content area.

### Fonts Component API

Header: `src/Render/RenderUtils/Components/FontsComponent.hpp`

Enums:
- `FontSourceType`: `LocalPath`, `Url`
- `FontGlyphPreset`: `Default`, `Cyrillic`, `Japanese`, `Korean`, `ChineseFull`

`FontFaceSpec` fields:
- `Key`, `SourceType`, `Source`
- `SizePx`
- `OversampleH`, `OversampleV`
- `PixelSnapH`, `MergeMode`
- `RasterizerMultiply`
- `GlyphPreset`
- `ExtraGlyphs`
- `MaxBytes` (default `8 MiB`)
- `MaxRetryCount` (default `2`)
- `RetryDelayMs` (default `350`)
- `StartupRequired`

`FontFaceSpec` methods:
- `SetGlyphPreset(...)`
- `SetOversample(h, v)`
- `SetPixelSnapH(bool)`
- `SetMergeMode(bool)`
- `SetRasterizerMultiply(float)` (clamped `0.1..4.0`)
- `SetExtraGlyphs(string)`
- `SetMaxBytes(size_t)` (minimum `1024`)
- `SetRetryPolicy(maxRetryCount, retryDelayMs)`
- `SetStartupRequired(bool)`

`FontsComponent` fields:
- `Enabled`
- `InheritFromParent`
- `DefaultFaceKey` (empty means ImGui default)
- apply targets:
  - `ApplyToText`
  - `ApplyToTextInput`
  - `ApplyToOptions`
  - `ApplyToStatusLabels`
- reload controls:
  - `AutoReloadLocalFiles`
  - `ReloadPollSeconds` (minimum `0.05`)
  - `ReloadRequested`
- `Faces`

`FontsComponent` methods:
- `SetEnabled`, `SetDefaultFace`, `SetDefaultFaceSafe`
- `SetInheritFromParent`
- `SetApplyToText`, `SetApplyToTextInput`, `SetApplyToOptions`, `SetApplyToStatusLabels`
- `SetAutoReloadLocalFiles`
- `SetReloadPollSeconds`
- `RequestReload`
- `AddPathFace(key, path, sizePx)`
- `AddUrlFace(key, httpsUrl, sizePx)`
- `FindFace`, `GetFaceKeys`, `FindFaceIndex`, `HasFace`

### Font System Runtime API

Header: `src/Render/RenderUtils/FontSystem.hpp`

Types:
- `FontApplyTarget`: `Text`, `TextInput`, `Options`, `StatusLabel`
- `FontLoadProgress`
- `FontFaceRuntimeState`: `Missing`, `Loading`, `Ready`, `Failed`
- `FontFaceRuntimeInfo`: `Key`, `State`, `Inherited`, `IsDefault`

Methods:
- `Register(registry)`
- `Shutdown(registry)`
- `Update(registry)` (async state polling only)
- `ProcessPendingAtlasRebuild()` (safe pre-`NewFrame` rebuild apply)
- `QueryProgress(registry)`
- `QueryEntityFontFaces(registry, entity, includeInherited)`
- `SetEntityDefaultFace(registry, entity, key)` (empty key => ImGui default)
- `ShouldApply(...)`
- `ResolveEntityFont(...)`
- `CalcTextSize(...)`
- `AddText(...)`

Font fallback chain:
- span `FontKey` -> `TextComponent::FontKey` -> nearest inherited `FontsComponent.DefaultFaceKey` -> ImGui default font.

Critical safety rule:
- Atlas rebuild is deferred and applied before ImGui `NewFrame`.
- Never rebuild font atlas in the middle of active ImGui frame.

## Shader System

Headers:
- `src/Render/RenderUtils/Components/ShaderComponent.hpp`
- `src/Render/RenderUtils/ShaderSystem.hpp`

### Shader Component Types

Enums:
- `ShaderSourceMode`: `EmbeddedCpp`, `EmbeddedRust`, `File`, `Inline`
- `ShaderBackendMode`: `AutoPreferDXC`, `D3DCompileOnly`, `DXCOnly`
- `ShaderCompilePolicy`: `OnDemandCache`, `StartupPrecompile`, `ManualApply`
- `ShaderStageMode`: `PixelOnly`, `VertexAndPixel`
- `ShaderParamType`: `Float`, `Int`, `Vec2`, `Vec3`, `Vec4`, `Color`, `Bool`
- `ShaderAutoUniform`:
  - `TimeSeconds`, `DeltaSeconds`, `MousePos`, `DisplaySize`, `EntityMin`, `EntityMax`, `EntitySize`, `EntityRect`
- `ShaderBindingMode`:
  - `Literal`, `BuiltinAutoUniform`, `RegisteredAutoUniform`

Structs:
- `ShaderParameter`
  - `Name`, `Type`, `Value`, `BindingMode`, `UniformKey`, `AutoUniform`, `ExposedInInspector`
- `ShaderSourceSpec`
  - `Mode`, `KeyOrPathOrInline`, `EntryPoint`, `TargetProfile`
- `ShaderComponent`
  - `Enabled`, `StageMode`, `Backend`, `CompilePolicy`
  - `VertexSource`, `PixelSource`
  - `Parameters`
  - compile state fields: `Dirty`, `CompileRequested`, `AutoReloadFileChanges`, `IsCompiled`, `LastError`, `ProgramHandle`

Builder utilities:
- `ShaderParamBuilder`:
  literal setters (`Float`, `Int`, `Vec2`, `Vec3`, `Vec4`, `Color`, `Bool`)
  built-in auto setters (`AutoFloat`, `AutoVec2`, `AutoVec4`)
  registered uniform bindings (`BindFloat`, `BindInt`, `BindVec2`, `BindVec4`, `BindColor`, `BindBool`)
  plus `BuildCBufferHlsl(...)` and `BuildPackedCBuffer(...)`.

### ShaderSystem Runtime API

Initialization:
- `Initialize(device)`
- `Shutdown()`

Source registries:
- `RegisterSourceAlias`, `UnregisterSourceAlias`, `ResetDefaultSourceAliases`
- `RegisterEmbeddedSource`, `UnregisterEmbeddedSource`, `ResetDefaultEmbeddedSources`

Uniform registry:
- `RegisterUniformResolver`
- `UnregisterUniformResolver`
- `ResetDefaultUniformResolvers`
- `QueryUniformResolverKeys`

Pass integration:
- `BeginImGuiPass(cmd, rtv, displaySize)`
- `EndImGuiPass()`
- Queue APIs:
  - `QueueEntity(...)`
  - `TryQueueEntity(...)`
  - `TryQueueEntityImGui(...)`
- compile/update:
  - `UpdateCompile(registry)`
- draw:
  - `RenderQueued(...)`
  - `ClearQueue()`

Diagnostics:
- `QueryQueuedCount()`
- `QueryImGuiCallbackQueuedCount()`
- `QueryImGuiCallbackRuntimeFailureCount()`

Current policy notes:
- Inline shader source mode is blocked by policy (deterministic error).
- Shader failure falls back to CPU draw path where available, and writes error to `ShaderComponent::LastError`.

### Glow And Shadow (Shader Path Notes)

`GlowComponent` (`src/Render/RenderUtils/Components/GlowComponent.hpp`):
- Defaults:
  - `RenderMode = Shader`
  - `ShaderKey = "glow.default"`
  - `ClipToParent = false` (spill-first natural glow behavior)
- Modes:
  - `GaussianBloom`, `NeonTube`, `AmbientSoft`
- Quality:
  - `Performance`, `Balanced`, `Ultra`
- Supports literal + auto + bound shader params through `Param*`, `ParamAuto*`, `ParamBind*`.

`glow.default` shader behavior:
- Uses expanded invisible envelope (`gDrawMinMax`) for draw region.
- Uses support radius and container-edge fade masks to force energy to zero before envelope edge.
- Final support packing in `Params3`:
  - `x = qualityIndex`
  - `y = supportRadiusPx`
  - `z = edgeFadePx`
  - `w = alphaEpsilon`

`ShadowComponent`:
- Defaults:
  - `RenderMode = Cpu`
  - `ShaderKey = "shadow.default"`
  - `ClipToParent = true`

## Image/Media Loader System

Header: `src/Render/RenderUtils/Components/ImageLoaderComponent.hpp`

`Components::ImageLoader` supports:
- multi-source (`URL` + `LocalPath`)
- static images and animated media (GIF)
- per-source runtime tracking, retry/backoff, staged decode/upload
- source cycling behavior and fallback to last successful source

Public high-level fields you normally set:
- source control:
  - `Sources`, `ActiveSourceIndex`
  - `CycleMode` (`CycleLoadedOnly`, `CycleAllSources`, `RefetchOnClick`)
  - `PreloadAllSources`
- playback:
  - `AutoPlay`, `Loop`, `Paused`, `PlaybackSpeed`
- startup:
  - `StartupRequired`
- resilience:
  - `KeepLastSuccessfulTexture`
  - `SkipFailedSources`
  - `MaxRetryCount`, `RetryDelayMs`
- GIF upload budgets:
  - `AdaptiveGifUpload`
  - `MaxGifUploadFramesPerTick`
  - `MaxUploadBytesPerTick`
  - `MaxResidentGifFrames`
- restart debounce:
  - `RestartDebounceMs`
- status:
  - `LastStatusMessage`

Builder helpers:
- `AddUrl(url)`
- `AddPath(path)`
- `SetSources(...)`
- `SetActiveSource(index)`
- `SetPlayback(autoPlay, loop, paused, speed)`

System APIs (`Components::ImageLoaderSystem`):
- lifecycle:
  - `Register(registry)`
  - `Shutdown(registry)`
  - `Update(registry)`
  - `FreeEntity(registry, entity)`
- control:
  - `RequestRestart(registry, hardRestart)`
  - `CycleSource(registry, entity, direction)`
- diagnostics:
  - `QueryPendingRequestCount()`
  - `QueryDebugStats(registry)`
  - `QueryProgress(registry)`

## Component Reference (All Public Components)

All component headers:
- `src/Render/RenderUtils/UIComponents.hpp` (aggregate include)
- `src/Render/RenderUtils/Components/*.hpp`

### Core/Layout

`ContainerComponent`:
- Fields:
  - `Type` (`Window`, `Panel`, `Button`, `Label`)
  - `Name`, `OwnedName`
- Setters:
  - `SetType(...)`
  - `SetName(const char*)`
  - `SetName(const std::string&)`

`TransformComponent`:
- Fields:
  - `Position`, `Size`, `Scale`, `Pivot`, `Rotation`
- Setters:
  - `SetPosition`, `SetSize`, `SetScale(ImVec2)`, `SetScale(float)`, `SetRotation`, `SetPivot`

`ParentComponent`:
- Fields:
  - `RelativeOffset`, `ParentEntity`
- Setters:
  - `SetParent`, `SetRelativeOffset`

`StyleComponent`:
- Fields:
  - colors: `BackgroundColor`, `BorderColor`, gradient colors, outline color
  - geometry: `BorderSize`, `Rounding`, `RoundingFlags`
  - visibility: `Visible`, `CalculatedVisible`
  - depth: `Layer` (`ZOrder`) and `ZIndexInt`
  - content padding: `ContentPaddingX`, `ContentPaddingY`
- Setters:
  - `SetBackgroundColor`, `SetBorderColor`, `SetBorderSize`
  - `SetRounding`, `SetRoundingFlags`
  - `SetVisible`
  - `SetLayer`
  - `SetGradient`, `SetGradientTopColor`, `SetGradientBottomColor`
  - `SetOutline`, `SetOutlineColor`, `SetOutlineThickness`
  - `SetContentPadding`

`ClipComponent`:
- Field: `ClipChildren`
- Setter: `SetClipChildren`

`TransparencyComponent`:
- Field: `Alpha`
- Setter: `SetAlpha`

`DrawAboveComponent`:
- Field: `TargetEntityName` (`const char*`)
- Setter: `SetTargetEntityName`
- Important: this stores a raw pointer; use string literals or stable storage lifetime.

`ExpandComponent`:
- Fields:
  - `IsExpanded`
  - `ExpandedHeight`
  - `CurrentHeight`
  - `ClipToParent`
  - `Direction` (`Down`, `Up`)
- Setters:
  - `SetExpanded`, `SetExpandedHeight`, `SetClipToParent`, `SetDirection`

`ScrollComponent`:
- Fields:
  - `ScrollY`, `ContentHeight`, `ViewHeight`, `ShowScrollbar`, `Speed`
- Setters:
  - `SetScrollY`, `SetContentHeight`, `SetViewHeight`, `SetShowScrollbar`, `SetSpeed`

### Input/Interaction

`InputStateComponent`:
- Fields:
  - hover/click states:
    - `IsHovered`, `IsClicked`
    - `WasHovered`, `WasClicked`
    - `JustPressed`, `JustReleased`
  - behavior:
    - `BlockInput`
    - `ClipRect`
- Setters:
  - `SetHovered`, `SetClicked`, `SetBlockInput`

`DraggableComponent`:
- Fields:
  - `IsDragging`, `DragOffset`
  - `Mode` (`None`, `Free`, `HorizontalOnly`, `VerticalOnly`)
  - `Constraint` (`None`, `Parent`, `Window`, `Screen`)
- Setters:
  - `SetMode`, `SetConstraint`, `SetDragOffset`, `SetDragging`

`CollisionComponent`:
- Field: `Collides`
- Setter: `SetCollides`

`LockedComponent`:
- Field: `Locked`
- Setter: `SetLocked`

`OptionsComponent`:
- Fields:
  - `Options`
  - `SelectedIndex`
  - `OnSelectCallback`
- Setters:
  - `SetOptions`, `SetSelectedIndex`, `SetCallback`

`SliderComponent`:
- Fields:
  - values: `Value`, `Min`, `Max`, `DataType`
  - smoothing: `VisualValue`, `EnableSmoothing`, `SmoothingSpeed`
  - drag: `IsDragging`
  - style: `TrackHeight`, `KnobRadius`, `ColorTrack`, `ColorFill`, `ColorKnob`
  - interaction: `ScrollSensitivity`
  - callback: `OnChange`
- Setters:
  - `SetValue`, `SetRange`, `SetDataType`
  - `SetColors`, `SetSizes`
  - `SetSmoothing`, `SetSensitivity`, `SetOnChange`
  - `AsInt`

`TextInputComponent`:
- Fields:
  - `Buffer`, `Placeholder`, `MaxLength`
  - `IsFocused`, `CursorPos`
  - `OnChange`
- Setters:
  - `SetBuffer`, `SetPlaceholder`, `SetMaxLength`, `SetOnChange`
- Input note:
  - text queue currently accepts printable single-byte ASCII (`< 0x80`).

`TabSwitchComponent`:
- Fields: `TargetTabId`, `Active`
- Setter: `SetTargetTabId`

`TabTriggerComponent`:
- Fields: `TabId`, `ActiveColor`, `InactiveColor`
- Setters: `SetTabId`, `SetColors`

`WindowHeaderComponent`:
- Fields:
  - `Enabled`, `Height`
  - colors: `BackgroundColor`, `TextColor`, `SeparatorColor`
  - `PaddingX`, `PaddingY`
  - `ShowTitle`
  - `DragFromHeaderOnly`
  - `ClipChildrenBelowHeader`
- Setters:
  - `SetEnabled`, `SetHeight`
  - `SetBackgroundColor`, `SetTextColor`, `SetSeparatorColor`
  - `SetPadding`, `SetShowTitle`
  - `SetDragFromHeaderOnly`
  - `SetClipChildrenBelowHeader`

### Text/Animation

`TextComponent` and `FontsComponent` are documented in [Text + Fonts](#text--fonts).

`AnimationComponent`:
- Stores `std::vector<Animation>`
- Methods:
  - `AddAnimation(const Animation&)`
  - `AddAnimationEx(Animation)` (returns generated ID)
  - `CancelById(int)`
  - `CancelByTag(const std::string&)`
  - `ClearAll()`

`Animation` fields:
- identity: `ID`, `Tag`
- timing: `Duration`, `Elapsed`, `StartTime`, `Loop`, `Finished`
- easing: `Easing`
- value range: `StartVal`, `EndVal`
- callbacks:
  - `Apply(const AnimationValue&)`
  - `CustomUpdate(float easedT, entt::registry&, entt::entity)`
  - `OnComplete()`

`EasingType`:
- `Linear`
- `EaseInQuad`, `EaseOutQuad`, `EaseInOutQuad`
- `EaseInCubic`, `EaseOutCubic`, `EaseInOutCubic`
- `ElasticOut`, `BounceOut`

### Visual Effects

`ShapeComponent`:
- Fields:
  - `Shapes`
  - `Enabled`
  - `DrawBehindContent`
  - `ClipToEntity`
  - `UseForHitTest`
  - `Priority`
  - `OnDrawOverride`
- Methods:
  - `AddShape`, `ClearShapes`
  - `SetEnabled`
  - `SetDrawBehindContent`
  - `SetClipToEntity`
  - `SetUseForHitTest`
  - `SetPriority`
  - `SetOnDrawOverride`

`ShapePrimitive`:
- `Type` (`Rect`, `Circle`, `Line`, `Triangle`, `Polyline`)
- `Points`, `Offset`, `Size`, `Radius`
- rect rounding:
  - `Rounding`, `RoundingFlags`
- style:
  - `Filled`, `FillColor`
  - `StrokeEnabled`, `StrokeColor`, `StrokeThickness`
- `Visible`

`GlowComponent`:
- key fields:
  - `Enabled`
  - `Color`
  - `Radius`
  - `Intensity`
  - `RenderMode` (`Cpu`/`Shader`)
  - `ShaderKey` (default `"glow.default"`)
  - `ShaderParameters`
  - `ClipToParent` (default `false`)
  - `Mode`
  - `QualityMode`
  - `Falloff`
  - `CoreStrength`
  - `InnerGlow`
  - `OuterOnly`
  - `RadiusScale`
  - sample/cache controls: `Samples`, `CacheEnabled`, `MaxSamples`
- fluent methods:
  - base: `SetColor`, `SetRadius`, `SetIntensity`, `SetSamples`, `SetEnabled`, `SetMode`, `SetRenderMode`, `SetShaderKey`, `SetClipToParent`, `SetQualityMode`, `SetFalloff`, `SetCoreStrength`, `SetInnerGlow`, `SetOuterOnly`, `SetRadiusScale`
  - shader param set: `SetShaderParameters`, `ConfigureShaderParameters`, `ClearShaderParameters`
  - shader param literals: `ParamFloat`, `ParamInt`, `ParamVec2`, `ParamVec4`, `ParamColor`, `ParamBool`
  - built-in auto uniforms: `ParamAutoFloat`, `ParamAutoVec2`, `ParamAutoVec4`
  - registered bindings: `ParamBindFloat`, `ParamBindInt`, `ParamBindVec2`, `ParamBindVec4`, `ParamBindColor`, `ParamBindBool`

`ShadowComponent`:
- key fields:
  - `Enabled`, `Color`, `Offset`, `BlurRadius`, `Spread`, `Samples`, `Inset`
  - `RenderMode` (`Cpu`/`Shader`)
  - `ShaderKey` (default `"shadow.default"`)
  - `ShaderParameters`
  - `ClipToParent` (default `true`)
- methods mirror glow parameter APIs (`Set*`, `Param*`, `ParamAuto*`, `ParamBind*`).

`ShaderComponent`:
- documented in [Shader System](#shader-system).

### Media

`Components::ImageLoader`:
- documented in [Image/Media Loader System](#imagemedia-loader-system).

## Rust FFI Reference

Header: `src/RustComponents/RustBridge.hpp`

Enums:
- `ImageFetchStatus`:
  - `Loading = 0`
  - `Ready = 1`
  - `Failed = 2`
  - `InvalidId = 3`
- `ImageSourceKind`:
  - `Url = 0`
  - `LocalPath = 1`
- `FetchedMediaKind`:
  - `StaticRGBA = 0`
  - `AnimatedRGBA = 1`

Key FFI functions:
- Byte/raw:
  - `start_fetch_bytes(source, source_kind)`
  - `check_fetch_bytes_status_ex(...)`
  - `free_rust_bytes(ptr, len)`
- Media:
  - `start_fetch_media(source, source_kind)`
  - `check_fetch_media_status_ex(...)`
  - `free_animation_frames(...)`
- Image legacy:
  - `start_fetch_image(url)`
  - `check_fetch_status(...)`
  - `check_fetch_status_ex(...)`
  - `free_image_data(...)`
- request lifecycle:
  - `cancel_fetch_request(id)`

Rust runtime notes (`src/RustComponents/src/lib.rs`):
- Asynchronous fetches are handled on worker threads.
- URL and local path loading are both supported by bridge; higher-level systems may apply stricter policies (for example, fonts require HTTPS in `FontSystem`).

## Diagnostics And Troubleshooting

### 1) Font crash at `g.Font->ContainerAtlas` (null)

Symptom:
- crash during `ImGui::Begin` with `g.Font->ContainerAtlas` null.

Cause:
- atlas rebuild happened at unsafe time (during active ImGui frame).

Current fix in this codebase:
- `FontSystem::Update` only marks pending rebuild.
- `FontSystem::ProcessPendingAtlasRebuild()` executes rebuild at safe frame boundary before `ImguiRender::NewFrame`.

If this returns:
- Ensure you are not adding any immediate atlas rebuild calls from `UIRenderer::Update` or custom per-frame callbacks.

### 2) Shader effect missing or fallback path used

Check:
- `ShaderComponent::IsCompiled`
- `ShaderComponent::LastError`
- compile policy (`ManualApply` requires `RequestCompile()`)
- source mode (`Inline` is blocked by policy)
- callback failure metric:
  - `ShaderSystem::QueryImGuiCallbackRuntimeFailureCount()`

### 3) Glow appears clipped/boxed unexpectedly

Check:
- `GlowComponent::ClipToParent`
  - `false` => spill allowed
  - `true` => strict clip
- shader key is `glow.default`
- ensure envelope params are reaching shader path (if shader path disabled, CPU fallback appearance differs).

### 4) Image not loading

Check:
- `ImageLoader.LastStatusMessage`
- `ImageLoader.state`
- source list validity (`Sources` non-empty)
- debug stats:
  - `Components::ImageLoaderSystem::QueryDebugStats(registry)`
- progress:
  - `Components::ImageLoaderSystem::QueryProgress(registry)`

### 5) Text/font not switching

Check:
- nearest enabled `FontsComponent` and `InheritFromParent` path
- `DefaultFaceKey` and requested keys are present
- apply flags:
  - `ApplyToText`
  - `ApplyToTextInput`
  - `ApplyToOptions`
  - `ApplyToStatusLabels`
- runtime states:
  - `FontSystem::QueryEntityFontFaces(...)`

### 6) Rust build failure (`stddef.h` / MSVC headers)

Build from MSVC environment:

```powershell
cmd /c "\"C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat\" && cmake --build out/build/x64-Debug --config Debug --target ImGuiDX12App"
```

If toolchain cache is stale:
- delete:
  - `out/build/<config>/CMakeCache.txt`
  - `out/build/<config>/CMakeFiles/`
- reconfigure.

## Strict Warning Policy

Defined in `CMakeLists.txt` and applied to first-party targets (`ImGuiDX12App`, and `ImGuiDX12TestHook` when enabled).

Options:
- `IMGUI_DX12APP_WARNINGS_AS_ERRORS` (default `ON`)
- `IMGUI_DX12APP_ULTRA_WARNING_PROFILE` (default `ON`)

MSVC baseline:
- `/W4 /permissive-`

MSVC ultra profile (first-party target):
- `/sdl`
- `/Zc:preprocessor`
- `/Zc:__cplusplus`
- `/external:anglebrackets /external:W0`
- extra diagnostics (`/w14242 ... /w14928`)

Non-MSVC baseline:
- `-Wall -Wextra -Wpedantic`

Non-MSVC ultra:
- `-Wconversion -Wsign-conversion -Wshadow -Wformat=2 -Wnull-dereference -Wimplicit-fallthrough`

Warnings-as-errors:
- MSVC: `/WX`
- Non-MSVC: `-Werror`

## Practical Safety Rules

- Keep ImGui context-sensitive operations on main/render thread.
- Do not rebuild font atlas during active ImGui frame.
- Prefer stable strings for any `const char*` pointer fields (especially `DrawAboveComponent::TargetEntityName`).
- For custom callbacks:
  - guard entity validity (`registry.valid(entity)`) when callbacks can outlive assumptions.
  - avoid heavy blocking I/O.
- Use startup blocking mode when app correctness depends on required assets.
- Use diagnostics counters regularly:
  - shader callback runtime failure count
  - image loader debug stats
  - startup combined progress (images + fonts)

## Main Demo Reference

End-to-end usage examples live in:
- `src/Scene/ShowcaseRuntime.cpp`

This file demonstrates:
- tabbed UI layout
- glow/shadow/shader usage
- image loader usage
- custom component callbacks
- font component setup and runtime font switching patterns
- startup runtime configuration

## License

See `LICENSE`.
