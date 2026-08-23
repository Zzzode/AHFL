#pragma once

#include <iosfwd>

namespace ahfl::incremental {

class DependencyGraph;
class IncrementalCompiler;

// Runs the RFC 0016 daemon invalidation loop. Reads JSON Lines from `input`
// and maps each notification to an action on `compiler`:
//
//   {"type":"didChange","uri":"file:///path/to/module.ahfl"}
//   {"type":"didSave",  "uri":"file:///path/to/module.ahfl"}
//     -> extract the path from the file:// URI, ensure it is a node in
//        `graph`, and call compile_changed({path}), which handles transitive
//        invalidation via the dependency graph and signature fingerprint
//        propagation. A stats object is emitted to `output`.
//
//   {"type":"didClose","uri":"file:///path/to/module.ahfl"}
//     -> no-op: the file may still exist on disk, so nothing is invalidated.
//
//   manifest change (ahfl.toml / ahfl.workspace.toml)
//     -> invalidate_all() + reset_stats(): the project structure itself
//        changed, so every cache entry is stale.
//
// Malformed JSON, missing fields, and unknown notification types produce an
// {"type":"error",...} object on `output` but never stop the loop. The loop
// returns cleanly when `input` reaches EOF.
//
// `graph` is passed separately because the daemon registers previously
// unknown modules (e.g. newly created files) before compiling them; the
// compiler observes the mutation since it holds the graph by reference.
void run_daemon(IncrementalCompiler &compiler,
                DependencyGraph &graph,
                std::istream &input,
                std::ostream &output);

} // namespace ahfl::incremental
