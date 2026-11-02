// canvasmetrics.h – the size of an element box. The canvas draws with these and the Controller
// places new elements with them, so both agree on what "overlap" means.
#pragma once

namespace metrics {

inline constexpr double kBoxWidth = 240;
inline constexpr double kHeader = 44;
inline constexpr double kPadRow = 30;  // pad name on top, media type below it
inline constexpr double kPropLine = 16;
inline constexpr double kPadRadius = 6;
inline constexpr int kMaxPropLines = 4;
inline constexpr int kNoteLines = 3;  // an error text of GStreamer needs about three lines at this width
inline constexpr double kColumn = 280; // distance between the left edges of two neighbours: where Controller::addAfter puts a new box
inline constexpr double kGap = 28;

inline double boxHeight(int padRows, int propLines, bool note) {
    return kHeader + 6 + padRows * kPadRow + (propLines ? 4 + propLines * kPropLine : 0) + (note ? 4 + kNoteLines * kPropLine : 0) + 8;
}

}  // namespace metrics
