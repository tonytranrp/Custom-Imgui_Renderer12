# Custom ImGui Renderer12

DirectX 12 + ImGui + EnTT ECS renderer framework with a Rust media bridge.  
This project is set up as a reusable rendering library style runtime: lifecycle management is centralized in `DX12Init::RunApp`, UI composition is ECS-driven via `RenderUtils`, shader effects support CPU/shader fallback paths, and media loading (including GIF) is handled through Rust FFI.

## Architecture Overview

- `DX12Init`:
  owns Win32 + DX12 runtime lifecycle (`RunApp`, frame loop, present, teardown).
- `RenderUtils`:
  ECS UI framework (`UIRenderer`, `UIBuilder`, components, systems).
- `ShaderSystem`:
  compile/cache/queue/render shader effects, uniform resolvers, fallback handling.
- `RustComponents`:
  async media fetch/decode (URL/local path, static + animated RGBA) bridged to C++.

## Feature Highlights

- ECS component-driven UI (EnTT).
- Fluent UI scene construction (`UIBuilder`).
- Startup loading gate with modes/priorities/timeouts (`StartupRuntime`).
- Animation, tab switching, input handling, collisions, clipping, drag/lock systems.
- Shape, glow, shadow, style, transparency, header, and custom rendering components.
- Shader-backed effects with CPU fallback and runtime compile policies.
- Async media: static images, multi-source switching, GIF playback.
- Compile-time embedded default shader assets via `battery::embed`.

## Requirements

- Windows (DX12 runtime target).
- Visual Studio/MSVC toolchain.
- CMake `>= 3.21`.
- Ninja (or Visual Studio CMake profiles).
- Rust + rustup toolchain:
  - `stable-x86_64-pc-windows-msvc`
- Git.

Dependencies are resolved from `CMakeLists.txt`:
- ImGui (`ocornut/imgui`)
- EnTT (`skypjack/entt`)
- Corrosion (`corrosion-rs/corrosion`)
- battery::embed (`batterycenter/embed`)

## Fetch & Setup (Clone + Build + Embed)

```powershell
git clone <your-repo-url> Custom-Imgui_Renderer12
cd Custom-Imgui_Renderer12
rustup toolchain install stable-x86_64-pc-windows-msvc
```

Use an MSVC developer environment for builds (recommended):

```powershell
cmd /c "\"C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat\" && cmake -S . -B out/build/x64-Debug -G Ninja -DCMAKE_BUILD_TYPE=Debug && cmake --build out/build/x64-Debug --config Debug --target ImGuiDX12App"
```

Notes:
- Default shaders are compile-time embedded with `battery::embed`.
- No runtime shader file copy step is required for shipped defaults.

## Run

If built with Ninja in Debug:

```powershell
.\out\build\x64-Debug\ImGuiDX12App.exe
```

From Visual Studio CMake, run the generated debug target directly in IDE.

## Quickstart: Lifecycle API (`DX12Init::RunApp`)

```cpp
#include "Dx12Init/Dx12Init.hpp"
#include "Render/RenderUtils/UIRenderer.hpp"

entt::registry registry;

int RunMyApp(HINSTANCE hInstance) {
    DX12Init::RunConfig config;
    config.Window.Title = L"My Renderer App";
    config.Window.Width = 1600;
    config.Window.Height = 900;
    config.AutoInitImGui = true;
    config.AutoInitShaderSystem = true;

    DX12Init::RuntimeCallbacks callbacks;
    callbacks.OnSetup = [&](HWND) {
        RenderUtils::UIRenderer::Init(registry);
        // Build ECS UI tree here.
    };

    callbacks.OnFrame = [&](const DX12Init::FramePacket& frame) {
        const float dt = frame.DeltaTime;
        RenderUtils::UIRenderer::Update(registry, dt);
        RenderUtils::UIRenderer::Render(registry);
    };

    callbacks.OnShutdown = [&]() {
        RenderUtils::UIRenderer::Shutdown(registry);
    };

    return DX12Init::RunApp(hInstance, config, callbacks);
}
```

Compatibility wrapper remains available:
- `MainRendering::Run(HINSTANCE)` (see `src/MainRendering.cpp`).

## Quickstart: ECS UI Construction (`UIBuilder`)

```cpp
RenderUtils::UIBuilder::Begin(registry)
    .Create<RenderUtils::ContainerType::Window>("MainWindow")
        .With<RenderUtils::TransformComponent>(
            RenderUtils::TransformComponent()
                .SetPosition(ImVec2(80, 80))
                .SetSize(ImVec2(900, 560)))
        .With<RenderUtils::StyleComponent>(
            RenderUtils::StyleComponent()
                .SetBackgroundColor(IM_COL32(34, 38, 47, 255))
                .SetRounding(10.0f))
        .Child(
            RenderUtils::UIBuilder::Begin(registry)
                .Create<RenderUtils::ContainerType::Panel>("Header")
                .With<RenderUtils::TransformComponent>(
                    RenderUtils::TransformComponent()
                        .SetPosition(ImVec2(0, 0))
                        .SetSize(ImVec2(900, 40)))
                .With<RenderUtils::TextComponent>(
                    RenderUtils::TextComponent("Hello ECS UI", IM_COL32(255, 255, 255, 255))))
    .End();
```

Typical runtime flow in frame callback:
- `UIRenderer::Update(...)`
- `UIRenderer::Render(...)`
- optional `UIRenderer::RenderInspector(...)` when debug mode is enabled.

## Startup Orchestrator (`RenderUtils::StartupRuntime`)

Use startup gate for required media loading before full interaction:

```cpp
RenderUtils::StartupRuntime::Config startupConfig;
RenderUtils::StartupRuntime::State startupState;

startupConfig.ModeValue = RenderUtils::StartupRuntime::Mode::BlockOnRequiredImages;
startupConfig.PriorityValue = RenderUtils::StartupRuntime::Priority::ImagesFirst;
startupConfig.TimeoutSeconds = 15.0f;

RenderUtils::StartupRuntime::Update(registry, deltaTime, startupConfig, startupState);
```

Modes:
- `BlockOnRequiredImages`
- `ImmediateUI`

Priorities:
- `ImagesFirst`
- `SystemsFirst`
- `Balanced`

State provides:
- `Completed`, `TimedOut`, `ElapsedSec`
- `Progress` (`ImageLoaderProgress`)
- `SummaryLine`

## Shader System Usage

Primary header:
- `src/Render/RenderUtils/ShaderSystem.hpp`
- `src/Render/RenderUtils/Components/ShaderComponent.hpp`

Source modes:
- `EmbeddedCpp` / `EmbeddedRust`: resolve compile-time embedded/file-aliased keys.
- `File`: load external shader file path.
- `Inline`: currently disabled by policy at runtime (returns deterministic error).

Compile policies:
- `OnDemandCache`
- `StartupPrecompile`
- `ManualApply`

Backend modes:
- `AutoPreferDXC`
- `D3DCompileOnly`
- `DXCOnly`

CPU fallback:
- If source resolution, compile, or runtime execution fails, effect paths fall back to CPU render path and error details are written into `ShaderComponent::LastError`.

Uniform resolver example:

```cpp
RenderUtils::ShaderSystem::RegisterUniformResolver(
    "app.mouse_norm",
    [](const RenderUtils::ShaderAutoUniformContext& ctx,
       RenderUtils::ShaderParamType expected,
       RenderUtils::ShaderParamValue& outValue,
       std::string& outError) -> bool {
        if (expected != RenderUtils::ShaderParamType::Vec2) {
            outError = "app.mouse_norm expects Vec2";
            return false;
        }
        outValue = ImVec2(
            ctx.MousePos.x / (std::max)(1.0f, ctx.DisplaySize.x),
            ctx.MousePos.y / (std::max)(1.0f, ctx.DisplaySize.y));
        return true;
    });
```

Shipped default embedded shader keys:
- `shader.default.vs`
- `glow.default`
- `shadow.default`

## Media / Image Loader Usage

Header:
- `src/Render/RenderUtils/Components/ImageLoaderComponent.hpp`

Basic example:

```cpp
Components::ImageLoader loader;
loader.AddUrl("https://example.com/image.png");
loader.AddPath("assets/local_preview.png");
loader.SetActiveSource(0);
loader.StartupRequired = true;
loader.CycleMode = Components::ImageLoader::SourceCycleMode::CycleLoadedOnly;
loader.SetPlayback(true, true, false, 1.0f); // autoplay, loop, paused, speed
```

Capabilities:
- URL + local-path sources.
- Multi-source preloading.
- GIF/animated frame playback.
- Retry/fallback controls.
- Startup-required participation in startup gate.

## Component Reference (Practical)

All primary component headers live in:
- `src/Render/RenderUtils/Components`

| Category | Component | Purpose | Header |
|---|---|---|---|
| Layout/Core | `ContainerComponent` | Entity container type/name metadata | `src/Render/RenderUtils/Components/ContainerComponent.hpp` |
| Layout/Core | `TransformComponent` | Position, size, rotation | `src/Render/RenderUtils/Components/TransformComponent.hpp` |
| Layout/Core | `ParentComponent` | Parent-child hierarchy + offsets | `src/Render/RenderUtils/Components/ParentComponent.hpp` |
| Layout/Core | `StyleComponent` | Colors, border, rounding, gradient, outline, padding | `src/Render/RenderUtils/Components/StyleComponent.hpp` |
| Layout/Core | `ClipComponent` | Child clipping behavior | `src/Render/RenderUtils/Components/ClipComponent.hpp` |
| Text/Input | `TextComponent` | Text content + alignment/layout settings | `src/Render/RenderUtils/Components/TextComponent.hpp` |
| Text/Input | `TextInputComponent` | Editable text input behavior/state | `src/Render/RenderUtils/Components/TextInputComponent.hpp` |
| Text/Input | `InputStateComponent` | Hover/click edge states and blocking | `src/Render/RenderUtils/Components/InputStateComponent.hpp` |
| Text/Input | `OptionsComponent` | Segmented options/select control | `src/Render/RenderUtils/Components/OptionsComponent.hpp` |
| Text/Input | `SliderComponent` | Slider value and visuals | `src/Render/RenderUtils/Components/SliderComponent.hpp` |
| Interaction | `DraggableComponent` | Drag mode/constraints | `src/Render/RenderUtils/Components/DraggableComponent.hpp` |
| Interaction | `CollisionComponent` | UI collision/hit bounds options | `src/Render/RenderUtils/Components/CollisionComponent.hpp` |
| Interaction | `LockedComponent` | Interaction locking | `src/Render/RenderUtils/Components/LockedComponent.hpp` |
| Interaction | `ScrollComponent` | Scroll area state/config | `src/Render/RenderUtils/Components/ScrollComponent.hpp` |
| Interaction | `ExpandComponent` | Expand/collapse state behavior | `src/Render/RenderUtils/Components/ExpandComponent.hpp` |
| Interaction | `DrawAboveComponent` | Draw-above dependency by target entity | `src/Render/RenderUtils/Components/DrawAboveComponent.hpp` |
| Tabs/Views | `TabSwitchComponent` | Tab panel visibility control | `src/Render/RenderUtils/Components/TabSwitchComponent.hpp` |
| Tabs/Views | `TabTriggerComponent` | Tab trigger behavior/colors | `src/Render/RenderUtils/Components/TabTriggerComponent.hpp` |
| Visual FX | `GlowComponent` | Glow effect config (CPU/shader path) | `src/Render/RenderUtils/Components/GlowComponent.hpp` |
| Visual FX | `ShadowComponent` | Shadow effect config (CPU/shader path) | `src/Render/RenderUtils/Components/ShadowComponent.hpp` |
| Visual FX | `ShapeComponent` | Primitive shape drawing/hit test | `src/Render/RenderUtils/Components/ShapeComponent.hpp` |
| Visual FX | `TransparencyComponent` | Entity alpha multiplier | `src/Render/RenderUtils/Components/TransparencyComponent.hpp` |
| Visual FX | `WindowHeaderComponent` | Optional window header rendering/drag zone | `src/Render/RenderUtils/Components/WindowHeaderComponent.hpp` |
| Animation | `AnimationComponent` | Keyed animation runtime storage/control | `src/Render/RenderUtils/Components/AnimationComponent.hpp` |
| Shader | `ShaderComponent` | Shader source/policy/backend/parameters | `src/Render/RenderUtils/Components/ShaderComponent.hpp` |
| Media | `ImageLoader` | Async media/image loading + GIF playback | `src/Render/RenderUtils/Components/ImageLoaderComponent.hpp` |

Related custom extension:
- `src/Render/RenderUtils/CustomComponents/CustomComponent.hpp`

Aggregate include:
- `src/Render/RenderUtils/UIComponents.hpp`

## Troubleshooting

### Rust `ring` build error (`stddef.h` missing)

Symptom:
- Rust build fails in `ring` with `fatal error C1083: Cannot open include file: 'stddef.h'`.

Cause:
- Build invoked without MSVC developer environment variables.

Fix:
- Build from `vcvars64.bat` initialized shell.

```powershell
cmd /c "\"C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat\" && cmake --build out\build\x64-Debug --config Debug --target ImGuiDX12App"
```

### Corrosion/Rust toolchain detection issues

This project already resolves real rust toolchain binaries in CMake (`rustup which rustc` / `rustup which cargo`) and disables rustup toolchain descent parsing for Corrosion.

If configuration is still stale:
- delete cache artifacts:
  - `out/build/<config>/CMakeCache.txt`
  - `out/build/<config>/CMakeFiles/`
- reconfigure and rebuild.

### Shader effect not rendering

Check:
- `ShaderComponent::IsCompiled`
- `ShaderComponent::LastError`
- selected render mode (`GlowRenderMode` / `ShadowRenderMode`)
- compile policy (`ManualApply` requires explicit compile request).

## Development Notes

- Strict warning policy is enabled for project target:
  - MSVC `/W4 /WX`
- Rust side check:

```powershell
cargo check --manifest-path src/RustComponents/Cargo.toml
```

- Main demo scene and runtime orchestration are in:
  - `src/MainRendering.cpp`

## License

See `LICENSE`.

