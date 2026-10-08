#pragma once

#include <cstddef>
#include <cstdint>

namespace command_text_viewport {

struct Window {
  size_t first = 0;
  size_t last = 0;  // Exclusive.
  int textWidth = 0;
  int cursorX = 0;  // Relative to the viewport's left edge.
};

// prefixWidths[i] is the measured pixel width of the first i characters.
// The returned character window is chosen so that the cursor is always visible
// and the rendered text plus cursor fits inside viewportWidth.
template <size_t N>
inline Window compute(const uint16_t (&prefixWidths)[N],
                      size_t textLength,
                      size_t cursor,
                      int viewportWidth,
                      int cursorWidth = 2) {
  Window result;
  if (N == 0 || viewportWidth <= cursorWidth) return result;

  const size_t safeLength = textLength < (N - 1) ? textLength : (N - 1);
  const size_t safeCursor = cursor < safeLength ? cursor : safeLength;
  const int availableForText = viewportWidth - cursorWidth;

  size_t first = 0;
  while (first < safeCursor &&
         static_cast<int>(prefixWidths[safeCursor] - prefixWidths[first]) > availableForText) {
    ++first;
  }

  size_t last = first;
  while (last < safeLength &&
         static_cast<int>(prefixWidths[last + 1] - prefixWidths[first]) <= availableForText) {
    ++last;
  }

  // A single glyph wider than the viewport still gets a one-character window;
  // the caller can render it inside the field's clipping rectangle.
  if (last == first && first < safeLength) ++last;

  result.first = first;
  result.last = last;
  result.textWidth = static_cast<int>(prefixWidths[last] - prefixWidths[first]);
  result.cursorX = static_cast<int>(prefixWidths[safeCursor] - prefixWidths[first]);
  return result;
}

}  // namespace command_text_viewport
