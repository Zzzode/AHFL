#include "visualize_html.hpp"

#include <stdexcept>

namespace ahfl::visualize {

namespace {

// The HTML template uses a placeholder /*__AHFL_DATA__*/ that is replaced
// with the layout JSON. The template is a single-file HTML with embedded
// CSS/JS — zero external dependencies.
constexpr std::string_view kTemplate = R"HTML(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>/*__TITLE__*/</title>
<style>
* { margin: 0; padding: 0; box-sizing: border-box; }

:root {
  --bg: #1e1e1e;
  --bg-panel: #252526;
  --bg-card: #2d2d2d;
  --bg-card-hover: #333333;
  --border: #404040;
  --border-light: #555;
  --text: #d4d4d4;
  --text-dim: #888;
  --accent: #0e639c;
  --accent-light: #1177bb;
  --green: #66bb6a;
  --red: #ef5350;
  --yellow: #ffee58;
  --orange: #ffa726;
  --purple: #ab47bc;
  --blue: #42a5f5;
  --gray: #666;
  --font: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif;
  --mono: 'Fira Code', 'Cascadia Code', 'Consolas', monospace;
}

body {
  font-family: var(--font);
  background: var(--bg);
  color: var(--text);
  height: 100vh;
  display: flex;
  flex-direction: column;
  overflow: hidden;
}

/* ── Toolbar ─────────────────────────────────────────── */
header {
  background: var(--bg-panel);
  padding: 8px 16px;
  display: flex;
  align-items: center;
  justify-content: space-between;
  border-bottom: 1px solid var(--border);
  z-index: 100;
}
header .title {
  font-size: 14px;
  font-weight: 600;
  color: var(--blue);
}
header .title .workflow-name {
  color: var(--text);
  margin-left: 8px;
}
header .controls {
  display: flex;
  gap: 8px;
  align-items: center;
}
header .controls button {
  background: #3c3c3c;
  color: var(--text);
  border: 1px solid var(--border);
  padding: 4px 12px;
  border-radius: 4px;
  cursor: pointer;
  font-size: 12px;
  font-family: var(--font);
}
header .controls button:hover {
  background: var(--accent);
  border-color: var(--accent);
}
header .controls .zoom-level {
  font-size: 12px;
  color: var(--text-dim);
  min-width: 40px;
  text-align: center;
}

/* ── Main layout ─────────────────────────────────────── */
main {
  flex: 1;
  display: flex;
  overflow: hidden;
}

/* ── Canvas ──────────────────────────────────────────── */
#canvas-container {
  flex: 1;
  position: relative;
  overflow: hidden;
  cursor: grab;
  background-image: radial-gradient(circle, #333 1px, transparent 1px);
  background-size: 24px 24px;
}
#canvas-container.grabbing { cursor: grabbing; }

#viewport {
  position: absolute;
  top: 0;
  left: 0;
  transform-origin: 0 0;
}

#connections {
  position: absolute;
  top: 0;
  left: 0;
  overflow: visible;
}

#cards {
  position: absolute;
  top: 0;
  left: 0;
}

/* ── Card ────────────────────────────────────────────── */
.card {
  position: absolute;
  width: 260px;
  background: var(--bg-card);
  border: 1px solid var(--border);
  border-radius: 8px;
  box-shadow: 0 2px 8px rgba(0,0,0,0.3);
  cursor: pointer;
  transition: border-color 0.15s, box-shadow 0.15s;
  user-select: none;
}
.card:hover {
  background: var(--bg-card-hover);
  border-color: var(--border-light);
  box-shadow: 0 4px 16px rgba(0,0,0,0.4);
}
.card.selected {
  border-color: var(--accent);
  box-shadow: 0 0 0 2px var(--accent), 0 4px 16px rgba(0,0,0,0.4);
}

.card-header {
  display: flex;
  align-items: center;
  padding: 8px 12px;
  border-bottom: 1px solid var(--border);
  gap: 8px;
}
.card-header .icon {
  width: 20px;
  height: 20px;
  border-radius: 4px;
  background: var(--accent);
  display: flex;
  align-items: center;
  justify-content: center;
  font-size: 11px;
  color: white;
  flex-shrink: 0;
}
.card-header .name {
  font-size: 13px;
  font-weight: 600;
  flex: 1;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}
.card-header .agent-type {
  font-size: 10px;
  color: var(--text-dim);
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
  max-width: 100px;
}

.card-body {
  padding: 8px 12px;
  font-size: 11px;
}
.card-body .type-row {
  display: flex;
  align-items: center;
  gap: 6px;
  margin-bottom: 4px;
  color: var(--text-dim);
}
.card-body .type-row .type-label {
  color: var(--text-dim);
  min-width: 20px;
}
.card-body .type-row .type-value {
  color: var(--text);
  font-family: var(--mono);
  font-size: 10px;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}

.card-body .divider {
  border: none;
  border-top: 1px solid var(--border);
  margin: 6px 0;
}

.card-body .capabilities {
  display: flex;
  flex-wrap: wrap;
  gap: 4px;
}
.card-body .cap-badge {
  background: #3c3c3c;
  border: 1px solid var(--border);
  border-radius: 3px;
  padding: 2px 6px;
  font-size: 10px;
  color: var(--text-dim);
  font-family: var(--mono);
}

.card-footer {
  padding: 6px 12px;
  border-top: 1px solid var(--border);
  font-size: 10px;
  color: var(--text-dim);
  display: flex;
  justify-content: space-between;
}

/* ── Status badges (execution overlay) ────────────────── */
.status-badge {
  display: inline-flex;
  align-items: center;
  gap: 4px;
  font-size: 10px;
  font-weight: 600;
  padding: 2px 8px;
  border-radius: 10px;
  text-transform: uppercase;
  letter-spacing: 0.5px;
}
.status-badge .dot {
  width: 6px;
  height: 6px;
  border-radius: 50%;
}
.status-badge.completed { background: rgba(102,187,106,0.15); color: var(--green); }
.status-badge.completed .dot { background: var(--green); }
.status-badge.running { background: rgba(66,165,245,0.15); color: var(--blue); }
.status-badge.running .dot { background: var(--blue); animation: pulse 1.5s infinite; }
.status-badge.failed { background: rgba(239,83,80,0.15); color: var(--red); }
.status-badge.failed .dot { background: var(--red); }
.status-badge.skipped { background: rgba(255,167,38,0.15); color: var(--orange); }
.status-badge.skipped .dot { background: var(--orange); }
.status-badge.scheduled { background: rgba(255,238,88,0.1); color: var(--yellow); }
.status-badge.scheduled .dot { background: var(--yellow); }
.status-badge.pending { background: rgba(102,102,102,0.15); color: var(--gray); }
.status-badge.pending .dot { background: var(--gray); }

@keyframes pulse {
  0%, 100% { opacity: 1; }
  50% { opacity: 0.3; }
}

/* Card status border accents */
.card.exec-completed { border-left: 3px solid var(--green); }
.card.exec-running { border-left: 3px solid var(--blue); }
.card.exec-failed { border-left: 3px solid var(--red); }
.card.exec-skipped { border-left: 3px solid var(--orange); }
.card.exec-scheduled { border-left: 3px solid var(--yellow); }

/* Duration display in card footer */
.card-footer .duration {
  font-family: var(--mono);
  color: var(--text);
}

/* ── Flowing edges (execution overlay) ────────────────── */
.edge.flowing {
  stroke: var(--green);
  stroke-width: 2.5;
  stroke-dasharray: 8 4;
  animation: flow 0.8s linear infinite;
}
.edge.flowing-failed {
  stroke: var(--red);
  stroke-width: 2.5;
}
@keyframes flow {
  to { stroke-dashoffset: -12; }
}

/* ── Timeline (execution overlay) ─────────────────────── */
#timeline-bar {
  display: none;
  background: var(--bg-panel);
  border-top: 1px solid var(--border);
  padding: 8px 16px;
  height: 80px;
  z-index: 50;
}
#timeline-bar.visible { display: block; }
#timeline-bar .timeline-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 6px;
}
#timeline-bar .timeline-title {
  font-size: 11px;
  text-transform: uppercase;
  letter-spacing: 1px;
  color: var(--text-dim);
}
#timeline-bar .timeline-summary {
  font-size: 11px;
  color: var(--text-dim);
  font-family: var(--mono);
}
#timeline-bar .timeline-track {
  position: relative;
  height: 24px;
  background: var(--bg);
  border-radius: 4px;
  overflow: hidden;
}
#timeline-bar .timeline-block {
  position: absolute;
  top: 2px;
  height: 20px;
  border-radius: 3px;
  min-width: 4px;
  cursor: pointer;
  transition: opacity 0.15s;
}
#timeline-bar .timeline-block:hover { opacity: 0.8; }
#timeline-bar .timeline-block.completed { background: var(--green); }
#timeline-bar .timeline-block.running { background: var(--blue); }
#timeline-bar .timeline-block.failed { background: var(--red); }
#timeline-bar .timeline-block.skipped { background: var(--orange); }
#timeline-bar .timeline-label {
  position: absolute;
  top: -1px;
  font-size: 9px;
  color: var(--text-dim);
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
  max-width: 80px;
}

/* ── State machine path (Phase 3) ─────────────────────── */
.state-path {
  display: flex;
  align-items: center;
  flex-wrap: wrap;
  gap: 4px;
  padding: 8px;
  background: var(--bg);
  border: 1px solid var(--border);
  border-radius: 4px;
}
.state-node {
  display: inline-flex;
  align-items: center;
  padding: 3px 8px;
  border-radius: 10px;
  font-size: 10px;
  font-family: var(--mono);
  background: #3c3c3c;
  border: 1px solid var(--border);
  color: var(--text);
}
.state-node.initial {
  background: rgba(66,165,245,0.15);
  border-color: var(--blue);
  color: var(--blue);
}
.state-node.final {
  background: rgba(102,187,106,0.15);
  border-color: var(--green);
  color: var(--green);
}
.state-arrow {
  color: var(--text-dim);
  font-size: 10px;
}

/* ── Ports ───────────────────────────────────────────── */
.port {
  position: absolute;
  width: 10px;
  height: 10px;
  border-radius: 50%;
  background: var(--bg-card);
  border: 2px solid var(--border-light);
  top: 50%;
  transform: translateY(-50%);
  z-index: 10;
  transition: border-color 0.15s, transform 0.15s;
}
.port.input { left: -5px; }
.port.output { right: -5px; }
.card:hover .port { border-color: var(--accent); transform: translateY(-50%) scale(1.2); }

/* ── Detail Panel ────────────────────────────────────── */
#detail-panel {
  width: 320px;
  background: var(--bg-panel);
  border-left: 1px solid var(--border);
  display: none;
  flex-direction: column;
  overflow-y: auto;
  z-index: 50;
}
#detail-panel.visible { display: flex; }

#detail-panel .panel-header {
  padding: 12px 16px;
  border-bottom: 1px solid var(--border);
  display: flex;
  justify-content: space-between;
  align-items: center;
}
#detail-panel .panel-header h3 {
  font-size: 13px;
  font-weight: 600;
}
#detail-panel .panel-header .close {
  background: none;
  border: none;
  color: var(--text-dim);
  cursor: pointer;
  font-size: 16px;
  padding: 0;
}
#detail-panel .panel-header .close:hover { color: var(--text); }

#detail-panel .panel-body {
  padding: 12px 16px;
  font-size: 12px;
}
#detail-panel .panel-body .section {
  margin-bottom: 16px;
}
#detail-panel .panel-body .section-title {
  font-size: 11px;
  text-transform: uppercase;
  letter-spacing: 1px;
  color: var(--text-dim);
  margin-bottom: 6px;
}
#detail-panel .panel-body .kv {
  display: flex;
  justify-content: space-between;
  margin-bottom: 4px;
}
#detail-panel .panel-body .kv .key { color: var(--text-dim); }
#detail-panel .panel-body .kv .val { color: var(--text); font-family: var(--mono); font-size: 11px; }
#detail-panel .panel-body pre {
  background: var(--bg);
  border: 1px solid var(--border);
  border-radius: 4px;
  padding: 8px;
  font-family: var(--mono);
  font-size: 11px;
  overflow-x: auto;
  white-space: pre-wrap;
  word-break: break-all;
}

/* ── Empty state ─────────────────────────────────────── */
.empty-state {
  display: flex;
  align-items: center;
  justify-content: center;
  height: 100%;
  color: var(--text-dim);
  font-size: 14px;
}
</style>
</head>
<body>

<header>
  <div class="title">
    AHFL Workflow Canvas
    <span class="workflow-name">/*__TITLE__*/</span>
  </div>
  <div class="controls">
    <span id="exec-summary" style="font-size:11px;color:var(--text-dim);margin-right:12px;"></span>
    <button id="btn-zoom-out" title="Zoom out">−</button>
    <span class="zoom-level" id="zoom-level">100%</span>
    <button id="btn-zoom-in" title="Zoom in">+</button>
    <button id="btn-fit" title="Fit to view">Fit</button>
  </div>
</header>

<main>
  <div id="canvas-container">
    <div id="viewport">
      <svg id="connections" width="100" height="100"></svg>
      <div id="cards"></div>
    </div>
  </div>
  <aside id="detail-panel">
    <div class="panel-header">
      <h3 id="panel-title">Node Details</h3>
      <button class="close" id="panel-close">×</button>
    </div>
    <div class="panel-body" id="panel-body"></div>
  </aside>
</main>

<div id="timeline-bar">
  <div class="timeline-header">
    <span class="timeline-title">Execution Timeline</span>
    <span class="timeline-summary" id="timeline-summary"></span>
  </div>
  <div class="timeline-track" id="timeline-track"></div>
</div>

<script>
(function() {
  'use strict';

  // Embedded layout data (replaced by C++ code).
  const DATA = /*__AHFL_DATA__*/;

  // ── State ──────────────────────────────────────────
  let viewX = 0, viewY = 0, viewScale = 1;
  let selectedNode = null;
  // Build name → execution status map (if trace present).
  const nodeStatus = {};
  if (DATA.trace !== undefined) {
    for (const node of DATA.nodes) {
      nodeStatus[node.name] = node.exec ? node.exec.status : 'pending';
    }
  }

  const container = document.getElementById('canvas-container');
  const viewport = document.getElementById('viewport');
  const svg = document.getElementById('connections');
  const cardsEl = document.getElementById('cards');
  const panel = document.getElementById('detail-panel');
  const panelTitle = document.getElementById('panel-title');
  const panelBody = document.getElementById('panel-body');
  const zoomLevel = document.getElementById('zoom-level');

  // ── Rendering ──────────────────────────────────────
  function render() {
    renderCards();
    renderConnections();
    renderTimeline();
    updateTransform();
  }

  function hasTrace() {
    return DATA.trace !== undefined;
  }

  function formatDuration(ns) {
    if (!ns || ns === 0) return '—';
    if (ns < 1000) return ns + 'ns';
    if (ns < 1000000) return (ns / 1000).toFixed(1) + 'µs';
    if (ns < 1000000000) return (ns / 1000000).toFixed(1) + 'ms';
    return (ns / 1000000000).toFixed(2) + 's';
  }

  function statusBadge(status) {
    return '<span class="status-badge ' + status + '">' +
      '<span class="dot"></span>' + status + '</span>';
  }

  function renderCards() {
    cardsEl.innerHTML = '';
    if (!DATA.nodes || DATA.nodes.length === 0) {
      cardsEl.innerHTML = '<div class="empty-state">No nodes in this workflow</div>';
      return;
    }
    for (const node of DATA.nodes) {
      const card = document.createElement('div');
      card.className = 'card';
      card.dataset.name = node.name;
      const x = node.layer * (260 + 80) + 60;
      const y = node.index * (120 + 40) + 60;
      card.style.left = x + 'px';
      card.style.top = y + 'px';

      const shortName = node.target ? node.target.split('::').pop() : '';
      const caps = (node.capabilities || []).map(c =>
        '<span class="cap-badge">' + escapeHtml(c) + '</span>'
      ).join('');

      // Execution overlay.
      let statusHtml = '';
      let durationHtml = '';
      let execClass = '';
      if (hasTrace() && node.exec) {
        const st = node.exec.status || 'pending';
        statusHtml = statusBadge(st);
        execClass = ' exec-' + st;
        if (st === 'completed' || st === 'failed') {
          durationHtml = '<span class="duration">' + formatDuration(node.exec.duration_ns) + '</span>';
        }
      }

      card.classList.add(execClass);

      card.innerHTML =
        '<div class="card-header">' +
          '<div class="icon">' + escapeHtml(shortName.charAt(0) || '?') + '</div>' +
          '<div class="name" title="' + escapeHtml(node.name) + '">' + escapeHtml(node.name) + '</div>' +
          '<div class="agent-type" title="' + escapeHtml(node.target) + '">' + escapeHtml(shortName) + '</div>' +
        '</div>' +
        '<div class="card-body">' +
          '<div class="type-row"><span class="type-label">in</span><span class="type-value">' + escapeHtml(node.input_type || DATA.input_type || '—') + '</span></div>' +
          '<div class="type-row"><span class="type-label">out</span><span class="type-value">' + escapeHtml(node.output_type || DATA.output_type || '—') + '</span></div>' +
          (caps ? '<hr class="divider"><div class="capabilities">' + caps + '</div>' : '') +
        '</div>' +
        '<div class="card-footer">' +
          (hasTrace() && node.exec ?
            statusHtml + durationHtml :
            '<span>' + (node.capabilities ? node.capabilities.length : 0) + ' cap' + (node.capabilities && node.capabilities.length !== 1 ? 's' : '') + '</span>' +
            '<span>L' + node.layer + '</span>') +
        '</div>' +
        '<div class="port input"></div>' +
        '<div class="port output"></div>';

      card.addEventListener('click', function(e) {
        e.stopPropagation();
        selectNode(node);
      });

      cardsEl.appendChild(card);
    }
  }

  function renderConnections() {
    svg.innerHTML = '';
    if (!DATA.edges || DATA.edges.length === 0) return;

    // Build name → position map.
    const pos = {};
    for (const node of DATA.nodes) {
      pos[node.name] = {
        x: node.layer * (260 + 80) + 60,
        y: node.index * (120 + 40) + 60
      };
    }

    // Size the SVG to cover the canvas.
    const maxX = Math.max(...DATA.nodes.map(n => n.layer * (260 + 80) + 60 + 260), 100);
    const maxY = Math.max(...DATA.nodes.map(n => n.index * (120 + 40) + 60 + 120), 100);
    svg.setAttribute('width', maxX + 100);
    svg.setAttribute('height', maxY + 100);

    for (const edge of DATA.edges) {
      const from = pos[edge.from];
      const to = pos[edge.to];
      if (!from || !to) continue;

      const x1 = from.x + 260;  // right edge of source card
      const y1 = from.y + 60;   // vertical center
      const x2 = to.x;          // left edge of target card
      const y2 = to.y + 60;

      const dx = Math.abs(x2 - x1);
      const offset = Math.max(dx * 0.5, 50);

      const path = document.createElementNS('http://www.w3.org/2000/svg', 'path');
      path.setAttribute('d',
        'M ' + x1 + ' ' + y1 +
        ' C ' + (x1 + offset) + ' ' + y1 + ', ' +
        (x2 - offset) + ' ' + y2 + ', ' +
        x2 + ' ' + y2
      );
      path.setAttribute('fill', 'none');
      path.setAttribute('stroke', '#555');
      path.setAttribute('stroke-width', '2');
      path.dataset.from = edge.from;
      path.dataset.to = edge.to;
      path.classList.add('edge');

      // Execution overlay: flowing animation for edges where source completed
      // and target is running or completed.
      if (hasTrace()) {
        const fromStatus = nodeStatus[edge.from];
        const toStatus = nodeStatus[edge.to];
        if (fromStatus === 'completed' && (toStatus === 'running' || toStatus === 'completed')) {
          path.classList.add('flowing');
        } else if (fromStatus === 'failed' || toStatus === 'failed') {
          path.classList.add('flowing-failed');
        }
      }

      svg.appendChild(path);
    }
  }

  // ── Timeline (execution overlay) ───────────────────
  function renderTimeline() {
    const bar = document.getElementById('timeline-bar');
    if (!hasTrace()) {
      bar.classList.remove('visible');
      return;
    }
    bar.classList.add('visible');

    const track = document.getElementById('timeline-track');
    const summary = document.getElementById('timeline-summary');
    track.innerHTML = '';

    // Find the time range across all nodes.
    let minStart = Infinity, maxEnd = 0;
    const execNodes = DATA.nodes.filter(n => n.exec && n.exec.started_at_ns > 0);
    for (const n of execNodes) {
      minStart = Math.min(minStart, n.exec.started_at_ns);
      const end = n.exec.completed_at_ns || n.exec.started_at_ns;
      maxEnd = Math.max(maxEnd, end);
    }
    if (execNodes.length === 0 || minStart >= maxEnd) {
      summary.textContent = 'No execution data';
      return;
    }

    const range = maxEnd - minStart;
    const trackWidth = track.clientWidth || 800;

    for (const n of execNodes) {
      const st = n.exec;
      const start = st.started_at_ns;
      const end = st.completed_at_ns || start;
      const left = ((start - minStart) / range) * (trackWidth - 80) + 40;
      const width = Math.max(((end - start) / range) * (trackWidth - 80), 4);

      const block = document.createElement('div');
      block.className = 'timeline-block ' + (st.status || 'pending');
      block.style.left = left + 'px';
      block.style.width = width + 'px';
      block.title = n.name + ' — ' + (st.status || '?') + ' — ' + formatDuration(st.duration_ns);
      block.dataset.name = n.name;
      block.addEventListener('click', function() {
        selectNode(n);
      });

      const label = document.createElement('span');
      label.className = 'timeline-label';
      label.textContent = n.name;
      label.style.left = (left + 4) + 'px';
      label.style.top = '4px';
      block.appendChild(label);

      track.appendChild(block);
    }

    // Summary line.
    const t = DATA.trace;
    const parts = [];
    parts.push((t.run_status || 'unknown').toUpperCase());
    parts.push(formatDuration(t.total_duration_ns));
    if (t.total_capability_calls > 0) {
      parts.push(t.total_capability_calls + ' cap calls');
    }
    if (t.total_tokens > 0) {
      parts.push(t.total_tokens + ' tokens');
    }
    if (t.total_cost_usd > 0) {
      parts.push('$' + t.total_cost_usd.toFixed(4));
    }
    summary.textContent = parts.join(' · ');
  }

  // ── Pan / Zoom ─────────────────────────────────────
  function updateTransform() {
    viewport.style.transform =
      'translate(' + viewX + 'px, ' + viewY + 'px) scale(' + viewScale + ')';
    zoomLevel.textContent = Math.round(viewScale * 100) + '%';
  }

  function zoomAt(cx, cy, factor) {
    const newScale = Math.min(4, Math.max(0.25, viewScale * factor));
    const ratio = newScale / viewScale;
    viewX = cx - (cx - viewX) * ratio;
    viewY = cy - (cy - viewY) * ratio;
    viewScale = newScale;
    updateTransform();
  }

  function fitToView() {
    if (!DATA.nodes || DATA.nodes.length === 0) return;
    const maxX = Math.max(...DATA.nodes.map(n => n.layer * (260 + 80) + 60 + 260));
    const maxY = Math.max(...DATA.nodes.map(n => n.index * (120 + 40) + 60 + 120));
    const cw = container.clientWidth;
    const ch = container.clientHeight;
    const scaleX = (cw - 40) / (maxX + 120);
    const scaleY = (ch - 40) / (maxY + 120);
    viewScale = Math.min(scaleX, scaleY, 1);
    viewX = (cw - (maxX + 120) * viewScale) / 2;
    viewY = (ch - (maxY + 120) * viewScale) / 2;
    updateTransform();
  }

  // Pan: drag empty canvas.
  let isPanning = false, panStartX = 0, panStartY = 0;
  container.addEventListener('mousedown', function(e) {
    if (e.target === container || e.target === viewport || e.target === svg || e.target === cardsEl) {
      isPanning = true;
      panStartX = e.clientX - viewX;
      panStartY = e.clientY - viewY;
      container.classList.add('grabbing');
    }
  });
  window.addEventListener('mousemove', function(e) {
    if (!isPanning) return;
    viewX = e.clientX - panStartX;
    viewY = e.clientY - panStartY;
    updateTransform();
  });
  window.addEventListener('mouseup', function() {
    isPanning = false;
    container.classList.remove('grabbing');
  });

  // Zoom: scroll wheel (cursor-anchored).
  container.addEventListener('wheel', function(e) {
    e.preventDefault();
    const rect = container.getBoundingClientRect();
    const cx = e.clientX - rect.left;
    const cy = e.clientY - rect.top;
    const factor = e.deltaY < 0 ? 1.1 : 0.9;
    zoomAt(cx, cy, factor);
  }, { passive: false });

  // Zoom buttons.
  document.getElementById('btn-zoom-in').addEventListener('click', function() {
    zoomAt(container.clientWidth / 2, container.clientHeight / 2, 1.2);
  });
  document.getElementById('btn-zoom-out').addEventListener('click', function() {
    zoomAt(container.clientWidth / 2, container.clientHeight / 2, 0.8);
  });
  document.getElementById('btn-fit').addEventListener('click', fitToView);

  // ── Node Selection ─────────────────────────────────
  function selectNode(node) {
    // Deselect previous.
    if (selectedNode) {
      const prev = cardsEl.querySelector('[data-name="' + cssEscape(selectedNode.name) + '"]');
      if (prev) prev.classList.remove('selected');
    }
    selectedNode = node;
    const card = cardsEl.querySelector('[data-name="' + cssEscape(node.name) + '"]');
    if (card) card.classList.add('selected');

    // Highlight connected edges.
    const edges = svg.querySelectorAll('.edge');
    for (const edge of edges) {
      if (edge.dataset.from === node.name || edge.dataset.to === node.name) {
        edge.setAttribute('stroke', '#0e639c');
        edge.setAttribute('stroke-width', '3');
        edge.classList.remove('flowing');
      } else {
        // Restore default or flowing state.
        if (hasTrace()) {
          const fromStatus = nodeStatus[edge.dataset.from];
          const toStatus = nodeStatus[edge.dataset.to];
          if (fromStatus === 'completed' && (toStatus === 'running' || toStatus === 'completed')) {
            edge.setAttribute('stroke', '');
            edge.setAttribute('stroke-width', '');
            edge.classList.add('flowing');
          } else if (fromStatus === 'failed' || toStatus === 'failed') {
            edge.setAttribute('stroke', '');
            edge.setAttribute('stroke-width', '');
            edge.classList.add('flowing-failed');
          } else {
            edge.setAttribute('stroke', '#555');
            edge.setAttribute('stroke-width', '2');
            edge.classList.remove('flowing');
          }
        } else {
          edge.setAttribute('stroke', '#555');
          edge.setAttribute('stroke-width', '2');
        }
      }
    }

    // Show detail panel.
    panelTitle.textContent = node.name;

    // Execution details section (if trace present).
    let execSection = '';
    if (hasTrace() && node.exec) {
      const st = node.exec;
      // State machine path.
      let stateMachineHtml = '';
      if (st.state_transitions && st.state_transitions.length > 0) {
        const path = st.state_transitions.map(function(tr, i) {
          const label = 'S' + tr.state_id;
          const time = formatDuration(tr.entered_at_ns);
          return '<span class="state-node" title="state_id=' + tr.state_id + ' at ' + time + '">' +
            label + '</span>' + (i < st.state_transitions.length - 1 ? '<span class="state-arrow">→</span>' : '');
        }).join('');
        stateMachineHtml =
          '<div class="section">' +
            '<div class="section-title">State Machine Path</div>' +
            '<div class="state-path">' +
              (st.initial_state ? '<span class="state-node initial" title="Initial state">' + escapeHtml(st.initial_state) + '</span><span class="state-arrow">→</span>' : '') +
              path +
              (st.final_states && st.final_states.length > 0 ? '<span class="state-arrow">→</span><span class="state-node final" title="Final state">' + escapeHtml(st.final_states.join(', ')) + '</span>' : '') +
            '</div>' +
          '</div>';
      } else if (st.initial_state) {
        stateMachineHtml =
          '<div class="section">' +
            '<div class="section-title">State Machine</div>' +
            '<div class="kv"><span class="key">Initial</span><span class="val">' + escapeHtml(st.initial_state) + '</span></div>' +
            (st.final_states && st.final_states.length > 0 ?
              '<div class="kv"><span class="key">Final</span><span class="val">' + escapeHtml(st.final_states.join(', ')) + '</span></div>' : '') +
          '</div>';
      }

      execSection =
        '<div class="section">' +
          '<div class="section-title">Execution</div>' +
          '<div class="kv"><span class="key">Status</span><span class="val">' + statusBadge(st.status || 'pending') + '</span></div>' +
          '<div class="kv"><span class="key">Started</span><span class="val">' + (st.started_at_ns ? formatDuration(st.started_at_ns) : '—') + '</span></div>' +
          '<div class="kv"><span class="key">Duration</span><span class="val">' + formatDuration(st.duration_ns) + '</span></div>' +
          (st.capability_calls && st.capability_calls.length > 0 ?
            '<div class="kv"><span class="key">Cap calls</span><span class="val">' + st.capability_calls.length + '</span></div>' +
            st.capability_calls.map(function(c, i) {
              return '<div class="kv"><span class="key">cap[' + i + ']</span><span class="val">' +
                formatDuration(c.duration_ns) +
                (c.cache_hit ? ' (cache)' : '') +
                (c.attempts > 1 ? ' ×' + c.attempts : '') +
                (c.total_tokens > 0 ? ' ' + c.total_tokens + 'tok' : '') +
                '</span></div>';
            }).join('') : '') +
        '</div>' +
        stateMachineHtml;
    }

    panelBody.innerHTML =
      '<div class="section">' +
        '<div class="section-title">Agent</div>' +
        '<div class="kv"><span class="key">Type</span><span class="val">' + escapeHtml(node.target) + '</span></div>' +
        '<div class="kv"><span class="key">Layer</span><span class="val">' + node.layer + '</span></div>' +
        '<div class="kv"><span class="key">Position</span><span class="val">(' + node.layer + ', ' + node.index + ')</span></div>' +
      '</div>' +
      execSection +
      '<div class="section">' +
        '<div class="section-title">Types</div>' +
        '<div class="kv"><span class="key">Input</span><span class="val">' + escapeHtml(node.input_type || DATA.input_type || '—') + '</span></div>' +
        '<div class="kv"><span class="key">Output</span><span class="val">' + escapeHtml(node.output_type || DATA.output_type || '—') + '</span></div>' +
      '</div>' +
      (node.capabilities && node.capabilities.length > 0 ?
        '<div class="section">' +
          '<div class="section-title">Capabilities</div>' +
          node.capabilities.map(function(c) {
            return '<div class="kv"><span class="key">cap</span><span class="val">' + escapeHtml(c) + '</span></div>';
          }).join('') +
        '</div>' : '') +
      '<div class="section">' +
        '<div class="section-title">Dependencies</div>' +
        (DATA.edges || []).filter(function(e) { return e.to === node.name; }).map(function(e) {
          return '<div class="kv"><span class="key">after</span><span class="val">' + escapeHtml(e.from) + '</span></div>';
        }).join('') +
        (DATA.edges || []).filter(function(e) { return e.from === node.name; }).map(function(e) {
          return '<div class="kv"><span class="key">→</span><span class="val">' + escapeHtml(e.to) + '</span></div>';
        }).join('') +
      '</div>';
    panel.classList.add('visible');
  }

  // Close panel.
  document.getElementById('panel-close').addEventListener('click', function() {
    panel.classList.remove('visible');
    if (selectedNode) {
      const card = cardsEl.querySelector('[data-name="' + cssEscape(selectedNode.name) + '"]');
      if (card) card.classList.remove('selected');
      selectedNode = null;
    }
    // Reset edge colors (restore flowing if trace).
    const edges = svg.querySelectorAll('.edge');
    for (const edge of edges) {
      if (hasTrace()) {
        const fromStatus = nodeStatus[edge.dataset.from];
        const toStatus = nodeStatus[edge.dataset.to];
        if (fromStatus === 'completed' && (toStatus === 'running' || toStatus === 'completed')) {
          edge.setAttribute('stroke', '');
          edge.setAttribute('stroke-width', '');
          edge.classList.add('flowing');
        } else if (fromStatus === 'failed' || toStatus === 'failed') {
          edge.setAttribute('stroke', '');
          edge.setAttribute('stroke-width', '');
          edge.classList.add('flowing-failed');
        } else {
          edge.setAttribute('stroke', '#555');
          edge.setAttribute('stroke-width', '2');
          edge.classList.remove('flowing');
        }
      } else {
        edge.setAttribute('stroke', '#555');
        edge.setAttribute('stroke-width', '2');
      }
    }
  });

  // Click empty canvas deselects.
  container.addEventListener('click', function(e) {
    if (e.target === container || e.target === viewport || e.target === svg || e.target === cardsEl) {
      document.getElementById('panel-close').click();
    }
  });

  // ── Utilities ──────────────────────────────────────
  function escapeHtml(s) {
    if (!s) return '';
    return String(s).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;');
  }
  function cssEscape(s) {
    return String(s).replace(/[^a-zA-Z0-9_-]/g, '\\$&');
  }

  // ── Init ───────────────────────────────────────────
  // Populate execution summary in toolbar.
  if (hasTrace()) {
    const t = DATA.trace;
    const p = [];
    p.push((t.run_status || 'unknown').toUpperCase());
    if (t.total_duration_ns > 0) p.push(formatDuration(t.total_duration_ns));
    if (t.total_capability_calls > 0) p.push(t.total_capability_calls + ' caps');
    if (t.total_tokens > 0) p.push(t.total_tokens + ' tok');
    document.getElementById('exec-summary').textContent = p.join(' · ');
  }

  render();
  fitToView();
})();
</script>
</body>
</html>
)HTML";

} // namespace

std::string_view html_template() {
    return kTemplate;
}

std::string generate_canvas_html(std::string_view layout_json,
                                 std::string_view title) {
    std::string result(kTemplate);

    // Replace title placeholder.
    const std::string title_placeholder = "/*__TITLE__*/";
    std::size_t pos = result.find(title_placeholder);
    while (pos != std::string::npos) {
        result.replace(pos, title_placeholder.size(), title);
        pos = result.find(title_placeholder, pos + title.size());
    }

    // Replace data placeholder.
    const std::string data_placeholder = "/*__AHFL_DATA__*/";
    pos = result.find(data_placeholder);
    if (pos == std::string::npos) {
        throw std::runtime_error("AHFL data placeholder not found in HTML template");
    }
    result.replace(pos, data_placeholder.size(), layout_json);

    return result;
}

} // namespace ahfl::visualize
