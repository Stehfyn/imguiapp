# ImGuiAppLayer

```C++
struct DemoAppLayer : ImGuiAppLayer
{
  virtual void OnAttach(ImGuiApp*) override final
  {
    Counter++;
  }

  virtual void OnRender(const ImGuiApp*) override final
  {
    ImGui::Begin("App Layer Window");
    ImGui::Text("Hello from App Layer!");
    ImGui::End();
  }

private:
  int Counter;
};

static void ShowImGuiAppLayerDemo()
{
  static ImGuiApp app;

  if (1 == ImGui::GetFrameCount())
    ImGui::PushAppLayer<DemoAppLayer>(&app);

  ImGui::UpdateApp(&app);
  ImGui::RenderApp(&app);
}
```

An application layer for Dear ImGui. The app is a **composition** (layers → windows/sidebars →
controls → data) and the frame is one pass of a **control loop** — ingest module status & update
state (Task), handle commands (Command), publish own status (Status), then render without mutating
(Window). Input arrives mid-render in immediate mode, so processing is deferred one frame:
`OnRender` records raw input into `TempData`, `OnUpdate` compares it with last frame's and mutates
`PersistData` — events are *derived, not stored*:

```c++
if (temp_data->hovered ^ last_temp_data->hovered)   // the event: it changed
```

Dependencies are `const` pointers keyed by type — one producer per type, push order is topological
order. Because every concept is a graph object, the bundled **Composer** edits the app in its own
algebra and generates the C++ back out.

- **[docs/big-idea.md](docs/big-idea.md)** — the concepts and why they're orthogonal.
- **[docs/bug-classes.md](docs/bug-classes.md)** — the recurring bug classes: rules of thumb and smells.
- **[docs/imgui-house-style.md](docs/imgui-house-style.md)** — the Dear ImGui house-style spec this code is held to.
- **[docs/designs.md](docs/designs.md)** — all per-subsystem design docs, collated.
- **[docs/](docs/)** — dated audits, sweeps, and superseded originals live in [docs/archive/](docs/archive/).
- `imguiapp_demo.cpp` — full demo, including the dogfooded Composer (Tools → Composer).
