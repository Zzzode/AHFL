#pragma once

#include <string>
#include <string_view>

namespace ahfl::visualize {

/// The HTML template for the workflow canvas.
///
/// The placeholder `/*__AHFL_DATA__*/` is replaced with a JSON object containing
/// the layout result (nodes with positions, edges, workflow metadata).
///
/// The template is a single-file HTML with embedded CSS/JS. Zero external
/// dependencies (no CDN, no npm, no server).
[[nodiscard]] std::string_view html_template();

/// Generate the workflow canvas HTML from a layout result.
[[nodiscard]] std::string generate_canvas_html(std::string_view layout_json,
                                               std::string_view title);

} // namespace ahfl::visualize
