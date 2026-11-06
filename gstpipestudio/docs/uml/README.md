> The checked-in generated diagrams below describe the pre-dev-zero implementation. For the current units, ownership and interfaces, use [the maintained architecture diagram](../../../docs/architecture/README.md). Regenerate these snapshots with the updated `.clang-uml` configuration after building.

# Architecture & Class Diagrams

Automated UML diagrams for **GstPipeStudio**, generated from C++ source via `clang-uml`.

> **Interactive Viewer**: Open [interactive.html](interactive.html) in your browser to pan, zoom, and search across diagrams.

---

## 1. System Architecture

```mermaid
graph TD
    subgraph Core ["gstpipestudio::core (Pure C++17 / GStreamer)"]
        Engine["gps::Engine"]
        Plan["gps::PipelinePlan"]
        Inspector["gps::GstInspector"]
    end

    subgraph Model ["App :: Model"]
        CanvasModel["CanvasModel"]
        ElementNode["Element / Pad / Link"]
    end

    subgraph Controller ["App :: Controller"]
        Ctrl["Controller"]
    end

    subgraph UI ["App :: UI & Canvas (Qt6)"]
        Canvas["CanvasScene & CanvasView"]
        Window["MainWindow & Drawer"]
    end

    UI -->|User Actions| Controller
    Controller -->|Updates| Model
    Controller -->|Executes / Validates| Core
    Core -->|Status Events| Controller
    Model -->|Signals| UI
```

---

## 2. Core Module (Relationships Only)

```mermaid
classDiagram
    direction TB
    class Engine
    class PipelinePlan
    class ElementPlan
    class PadPlan
    class LinkPlan
    class GstInspector
    class FactoryEntry
    class PropertyInfo
    class JsonWriter

    PipelinePlan *-- ElementPlan : elements
    PipelinePlan *-- LinkPlan : links
    ElementPlan *-- PadPlan : pads
    Engine ..> PipelinePlan : executes
    GstInspector ..> FactoryEntry : inspects
    FactoryEntry *-- PropertyInfo : properties
```

<details>
<summary>🔍 <b>Core Module: Detailed Members & Methods (Zoom-In)</b></summary>

![Core Detailed UML](core_detailed.svg)

- Mermaid source: [core_detailed.mmd](core_detailed.mmd)
- PlantUML source: [core_detailed.puml](core_detailed.puml)
</details>

---

## 3. App Module (Relationships Only)

```mermaid
classDiagram
    direction TB
    class MainWindow
    class Drawer
    class Controller
    class CanvasModel
    class CanvasScene
    class CanvasView
    class ElementItem
    class PadItem
    class LinkItem
    class Element
    class Pad
    class Link

    MainWindow *-- Drawer : owns
    MainWindow *-- CanvasView : contains
    CanvasView o-- CanvasScene : displays
    MainWindow o-- Controller : uses
    Controller o-- CanvasModel : manages

    CanvasModel *-- Element : contains
    CanvasModel *-- Link : contains
    Element *-- Pad : pads

    CanvasScene o-- ElementItem : visual items
    CanvasScene o-- LinkItem : visual items
    ElementItem *-- PadItem : visual pads

    Controller ..> CanvasScene : coordinates
```

<details>
<summary>🔍 <b>App Module: Detailed Members & Methods (Zoom-In)</b></summary>

![App Detailed UML](app_detailed.svg)

- Mermaid source: [app_detailed.mmd](app_detailed.mmd)
- PlantUML source: [app_detailed.puml](app_detailed.puml)
</details>

---

## Generated Files

| Diagram | SVG | Source |
| :--- | :--- | :--- |
| **Directory Packages** | [architecture_overview.svg](architecture_overview.svg) | [architecture_overview.mmd](architecture_overview.mmd) |
| **Core (High-Level)** | [core_overview.svg](core_overview.svg) | [core_overview.mmd](core_overview.mmd) |
| **Core (Detailed)** | [core_detailed.svg](core_detailed.svg) | [core_detailed.mmd](core_detailed.mmd) |
| **App (High-Level)** | [app_overview.svg](app_overview.svg) | [app_overview.mmd](app_overview.mmd) |
| **App (Detailed)** | [app_detailed.svg](app_detailed.svg) | [app_detailed.mmd](app_detailed.mmd) |
