# AHFL Workflow Canvas — Card+Connection-Line Visualization Design

**Status**: Phase 1 + 2 + 3 + 5 landed (2026-10-05)
**Date**: 2026-10-05
**Author**: Coordinator (autonomous)

## Revision History

- **v1**: Simple DAG boxes + static DOT export. Rejected — not industry best practice.
- **v2**: Card+connection-line canvas. Direction correct, details refined.
- **v3**: Incorporates research on Coze, Dify, n8n, Nuke, Houdini, Node-RED, Step Functions,
  XState, React Flow, and litegraph.js. Key corrections: status badge (not full-card color),
  auto-layout (not manual positioning), flowing-dash edge animation, hover-highlight pattern.

## Industry Research Summary

Research covered 10+ tools across 4 categories. The consensus patterns:

| Pattern | Consensus | Tools |
|---------|-----------|-------|
| Card shape | Rounded rectangle, 200-250px wide | All |
| Connections | **Cubic bezier curves** (not straight lines) | Coze, Dify, n8n, Node-RED, React Flow |
| Status display | **Badge in card corner** (not full-card color change) | n8n, Coze, Dify |
| Edge animation | **Flowing-dash (marching ants)** on active path | n8n (signature), Dify |
| Config panel | **Right side panel** (never modal) | n8n, Dify, Coze, Nuke, Fusion |
| Layout | **Auto-layout** for viewers; manual for editors | Step Functions, XState |
| Pan/zoom | Drag background + scroll wheel (cursor-anchored) | Universal |
| Minimap | Bottom-right, for >10 nodes | n8n, React Flow |
| Hover highlight | Node hover → highlight connected edges | Nuke, n8n |
| Sub-graphs | Dive-in containers with breadcrumb | Nuke Groups, Houdini Subnets, XState nested states |

**AHFL's unique differentiators** (no other tool has these):
1. **Two-level graph**: Workflow DAG + Agent state machines (like Nuke Groups + Houdini Subnets,
   but built into the language)
2. **Capability calls**: LLM/tool invocations with token usage, cost, retry — no other workflow
   tool visualizes this
3. **Formal verification**: Safety/liveness properties that could be shown as graph annotations

## Design

### Card Design

Each workflow node (agent) renders as a card. **Category color stays; status is a badge overlay.**

```
┌──────────────────────────────────────┐
│ 🔵 intake          IntakeAgent    ⚙  │  ← Header: category icon, node name, agent type, expand
│                                ┌───┐ │
│                                │ ✓ │ │  ← Status badge (top-right corner, 16px)
│                                └───┘ │
├──────────────────────────────────────┤
│  📥 LoanApplication                  │  ← Input type
│  📤 NormalizedApplication            │  ← Output type
│  ──────────────────────────────────  │
│  🔧 http_post    🔧 db_query         │  ← Capability badges (with call count)
├──────────────────────────────────────┤
│  ⏱ 340ms  │  1.2k tokens             │  ← Metrics bar (execution mode only)
└──────────────────────────────────────┘
     ○                                    ← Output port (right edge)
  ○                                       ← Input port (left edge)
```

**Status badge (top-right corner, 16px circle):**

| State | Badge | Color | Card Effect |
|-------|-------|-------|-------------|
| `pending` | — | Gray | Normal |
| `running` | Spinner | Blue `#42a5f5` | Blue border + subtle pulse |
| `completed` | ✓ | Green `#66bb6a` | Green left-border (4px) |
| `failed` | ✗ | Red `#ef5350` | Red border + red glow |
| `skipped` | ⊘ | Yellow `#ffee58` | Yellow left-border |
| `suspended` | ⏸ | Purple `#ab47bc` | Purple left-border |

**Why badge, not full-card color?** (n8n/Coze/Dify consensus)
- Preserves category color coding (agent type at a glance)
- Status is glanceable in the corner without overwhelming the card
- Failed nodes get a red border + glow for attention; completed nodes get a subtle green strip

### Connection Design

**Cubic bezier curves** — the universal choice for workflow/DAG tools.

```
                    ╭─────────────────╮
                    │                 │
  ┌──────────┐      │  ╭──────────╮   │
  │  intake  │──────╯  │  credit  │   │
  │          │─────────│          │   │
  └──────────┘         ╰──────────╯   │
                    │                 │
                    ╰─────────────────╯
```

**Bezier path formula** (React Flow / n8n standard):
```
dx = |x2 - x1|
controlOffset = max(dx * 0.5, 50)
path = M x1 y1  C  (x1+offset) y1,  (x2-offset) y2,  x2 y2
```

**Edge states:**
- **Idle**: gray `#555`, 2px
- **Active (executing)**: brand blue `#0e639c`, 2px, **flowing-dash animation**
- **Completed**: green `#66bb6a`, 2px
- **Failed**: red `#ef5350`, 2px
- **Hover**: highlight + show data type tooltip

**Flowing-dash animation** (n8n's signature):
```css
.edge-active {
  stroke-dasharray: 8 4;
  animation: flow 0.5s linear infinite;
}
@keyframes flow { to { stroke-dashoffset: -12; } }
```

**Hover pattern** (Nuke/n8n): hovering a card highlights all connected edges and dims
unconnected ones — helps trace data flow in dense graphs.

### Canvas

- **Dark theme** (VS Code style, matching the playground)
- **Dot-grid background** (like n8n/Dify/React Flow)
- **Pan**: drag empty canvas
- **Zoom**: scroll wheel, cursor-anchored (0.25x–4x)
- **Auto-layout**: layered DAG layout (Kahn's topological sort + barycenter crossing reduction)
- **Minimap**: bottom-right corner (for workflows >10 nodes)
- **Toolbar**: top bar with workflow name, run status, zoom controls, fit-to-view

**Why auto-layout, not manual positioning?** (Step Functions/XState model)
- This is a **trace viewer**, not an editor. Users want to see the execution, not rearrange nodes.
- Auto-layout ensures the graph is always readable, regardless of workflow complexity.
- Manual dragging adds complexity without value for a read-only report.
- If we later build a visual editor, manual positioning makes sense then.

### Agent State Machine Expansion

Click the expand icon (⚙) on a card → the card expands to show the agent's state machine.
This is AHFL's **unique differentiator** — no other workflow tool has this.

```
┌──────────────────────────────────────┐
│ 🔵 underwrite    UnderwritingAgent ▼ │
├──────────────────────────────────────┤
│  📥 UnderwritingInput                │
│  📤 LoanDecision                     │
│  ──────────────────────────────────  │
│  🔧 credit_check   🔧 fraud_screen   │
│  ──────────────────────────────────  │
│  State Machine:                      │
│                                      │
│      ┌──────────┐                    │
│      │ Evaluate │ ← current (blue)   │
│      └────┬─────┘                    │
│           │                          │
│      ┌────┼────┬──────────┐          │
│      ▼    ▼    ▼          ▼          │
│  ┌──────┐ ┌──────┐ ┌──────────┐     │
│  │Approved│ │Rejected│ │Escalated│     │
│  └──────┘ └──────┘ └──────────┘     │
│   (green)   (gray)     (gray)        │
│                                      │
├──────────────────────────────────────┤
│  ⏱ 340ms  │  1.2k tokens             │
└──────────────────────────────────────┘
```

- States as rounded boxes, transitions as labeled arrows
- **Current state**: blue border + glow
- **Executed transitions**: green
- **Unexecuted transitions**: dimmed gray
- **Final states**: green border (if reached) or gray

**Alternative**: side panel shows the state machine as a larger diagram. The expandable card
is more compact; the side panel is more detailed. Start with the side panel (simpler), add
expandable cards later.

### Detail Panel (Right Side)

Click a card → right side panel shows full details. **Never a modal** (n8n/Dify/Coze consensus).

```
┌─────────────────────────────┐
│ Node: underwrite            │
│ Agent: UnderwritingAgent    │
│ Status: ✓ Completed         │
│ Duration: 340ms             │
├─────────────────────────────┤
│ State Transitions:          │
│  Evaluate → Approved        │
│  (2 transitions, 120ms)     │
├─────────────────────────────┤
│ Capability Calls:           │
│  ● credit_check   240ms     │
│    1.2k tokens, $0.003      │
│  ● fraud_screen   180ms     │
│    cache hit                 │
├─────────────────────────────┤
│ Input:                      │
│  { application: {...},      │
│    credit: {...},           │
│    fraud: {...} }           │
│ Output:                     │
│  { decision: "Approved",    │
│    reason: "..." }          │
├─────────────────────────────┤
│ Events (6):                 │
│  12:01:03.400 NodeStarted   │
│  12:01:03.410 StateEntered  │
│  12:01:03.750 CapStarted    │
│  12:01:03.740 CapCompleted  │
│  12:01:03.750 StateEntered  │
│  12:01:03.750 NodeCompleted │
└─────────────────────────────┘
```

### Timeline (Execution Mode Only)

```
┌──────────────────────────────────────────────────┐
│ TIMELINE                                         │
│ ┌──────────────────────────────────────────────┐ │
│ │ intake    ████                               │ │
│ │ credit         ██████                        │ │
│ │ fraud          ████                          │ │
│ │ underwrite         ██████████                │ │
│ │ fulfill                   ████               │ │
│ └──────────────────────────────────────────────┘ │
└──────────────────────────────────────────────────┘
```

**Bidirectional sync** (Airflow/Prefect pattern):
- Hover a timeline bar → highlight the node in the graph
- Click a node → scroll the timeline to show its bar

### Two Modes

**Static Mode** (compile-time, no execution):
- All cards gray, no status badges
- No metrics bar
- For documentation, code review, architecture understanding

**Execution Mode** (runtime, with trace):
- Status badges, metrics bars, edge animations
- Detail panel with full event history
- Timeline at bottom

## Implementation

### Architecture

```
ahflc run --format jsonl > trace.jsonl
ahflc visualize trace.jsonl -o trace.html
```

**Single-file HTML** with embedded CSS/JS. Zero external dependencies (no CDN, no npm, no server).

### Rendering Approach

**SVG for connections + HTML for cards** — the industry-standard approach (React Flow,
litegraph, Cytoscape all use this pattern):

- **SVG layer**: cubic bezier paths for connections, rendered in a full-canvas `<svg>`
- **HTML layer**: card divs positioned absolutely on top of the SVG
- **CSS transforms**: `translate(x, y) scale(z)` on the container for pan/zoom
- **Vanilla JS**: event handling, state management, DOM manipulation

### Layout Algorithm

**Layered DAG layout** (simplified Sugiyama/dagre — sufficient for AHFL's scale):

1. **Topological sort** via Kahn's algorithm
2. **Assign layers**: longest-path layering (each node's layer = 1 + max(layer of predecessors))
3. **Order within layers**: barycenter heuristic (sort by average position of predecessors)
4. **Position**: `x = layer * (card_width + h_gap)`, `y = index * (card_height + v_gap)`

### Data Sources

| Element | Source |
|---------|--------|
| Card name, agent type | `execution-plan.v1` → `nodes[].name`, `nodes[].target` |
| Input/output types | `execution-plan.v1` → `input_type`, `output_type` |
| Connections | `execution-plan.v1` → `dependency_edges[]` |
| Capabilities | `execution-plan.v1` → `nodes[].capability_bindings[]` |
| Lifecycle | `execution-plan.v1` → `nodes[].lifecycle` |
| Agent states | IR `AgentDecl.states` + `AgentDecl.transitions` |
| Execution state | `run-event.v1` JSONL → replay events |
| Duration | `run-event.v1` → `monotonic_offset_ns` deltas |
| Token usage | `run-event.v1` → `capability_usage_recorded` |
| Input/output values | `run-event.v1` → `node_completed.output` |

### CLI Integration

```
ahflc visualize <trace.jsonl> [-o <output.html>] [--open]
ahflc visualize --plan <execution-plan.json> [-o <output.html>]  # static mode
```

| Option | Default | Description |
|--------|---------|-------------|
| `-o` | `trace.html` | Output file |
| `--open` | off | Open in browser after generation |
| `--plan` | — | Static mode (no execution trace) |
| `--title` | workflow name | Custom page title |

### File Structure

```
src/tooling/cli/
  visualize.cpp          # CLI command implementation
  visualize_html.hpp     # HTML template generator
  visualize_layout.cpp   # DAG layout algorithm
  visualize_events.cpp   # Event replay → node states
```

### HTML Template Structure

```html
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <title>AHFL Workflow Canvas</title>
  <style>
    /* Dark theme, dot-grid background, card styles, bezier edge styles,
       flowing-dash animation, status badges, detail panel, timeline,
       toolbar, minimap */
  </style>
</head>
<body>
  <header>Toolbar</header>
  <main>
    <div id="canvas-container">
      <svg id="connections"></svg>      <!-- Bezier curves -->
      <div id="cards"></div>            <!-- Card divs -->
    </div>
    <aside id="detail-panel"></aside>   <!-- Click → details -->
  </main>
  <footer id="timeline"></footer>       <!-- Execution mode only -->
  <script>
    // Embedded trace data
    const TRACE = {...};
    const PLAN = {...};
    // Layout, rendering, interaction logic
  </script>
</body>
</html>
```

## Implementation Plan

### Phase 1: Static Canvas (no execution trace) — LANDED 2026-10-05
- `ahflc visualize execution-plan.json`
- Card+bezier connection rendering, auto-layout, pan/zoom
- Click to inspect (side panel with agent details)
- Dark theme, dot-grid background

### Phase 2: Execution Overlay — LANDED 2026-10-05
- `ahflc visualize execution-plan.json trace.jsonl`
- Event replay → per-node status (pending/scheduled/running/completed/failed/skipped)
- Status badges in card footer, duration display
- Flowing-dash edge animation on completed→running/completed paths
- Timeline bar at bottom with per-node execution blocks
- Toolbar summary: run status, total duration, capability calls, tokens, cost
- Detail panel extended with execution section (status, duration, capability calls)

### Phase 3: Agent State Machine — LANDED 2026-10-05
- State machine path in detail panel (from `agent_state_entered` events)
- Initial/final state names from execution plan lifecycle
- Executed path highlighted as S0 → S1 → S2 → ... → Done
- Hover tooltips with state_id and entry time

### Phase 4: Polish
- Minimap
- Edge hover → data type tooltip
- Capability call badges with token/cost
- Export SVG/PNG

### Phase 5: Static DOT/Mermaid — LANDED 2026-10-05
- `ahflc visualize plan.json --format dot` → Graphviz DOT
- `ahflc visualize plan.json --format mermaid` → Mermaid graph
- For README, Markdown docs, code review comments
- Default output: `workflow.dot` / `workflow.mmd`
