#pragma once
// Fork-only (BlackByteDE): POST /geometry/frames — deformed figure vertices for
// a list of frames as a binary float32 payload. Kept in its own files so the
// upstream sources only gain a handful of wiring lines.
//
// Request (JSON):
//   {
//     "frames":     [int, ...],          required, non-empty
//     "node":       "name or label",     optional; default = first figure
//                                        (DzSkeleton without follow target)
//     "indices":    [int, ...],          optional vertex subset
//     "surfaces":   ["Face", ...],       optional; adds every vertex of the
//                                        surfaces' facets to the subset
//     "bones":      ["lHand", ...],      optional; world-space bone origins
//     "resolution": "base" | "current",  optional, default "base"; "base"
//                                        switches Resolution Level to Base
//                                        only if the cached mesh is not
//                                        already the base cage (rare; ~1 s)
//     "sceneUpdate": true,               optional; false skips the final
//                                        Scene update (~130 ms on a dressed
//                                        figure) — for back-to-back calls,
//                                        let only the last one update
//     "channels":   [spec, ...],         optional override channels, each
//                                        {"bone": "lIndex2", "property": "XRotate"},
//                                        {"node": "name/label", "property": p},
//                                        {"morph": "name/label"} or
//                                        {"property": p} on the figure;
//                                        properties match name, then label
//     "values":     [[float, ...], ...]  required with channels: one override
//                                        set per row, one value per channel
//   }
//   Without indices/surfaces all vertices are returned.
//
//   Override sets: every frame is evaluated once per set, with the set's
//   values written to the channels' own (local) value at that frame's time —
//   ERC/limits apply as when set from DazScript. Afterwards each channel is
//   restored: a key that existed gets its value back, a key the override
//   created is deleted, a channel without keys loses the keys again (any
//   setValue() keys it; keyless it sits at its default).
//
// Response 200 (application/octet-stream, little-endian):
//   char    magic[4]   "DZGF"
//   uint32  version    1 without override sets, 2 with
//   uint32  numFrames
//   uint32  numVerts   vertices per evaluation (subset size if a subset was given)
//   uint32  numBones
//   uint32  flags      bit 0: index section present, bit 1: override sets
//   uint32  numSets    v2: override sets per frame (v1: 0)
//   uint32  mismatches v2: channel restores that could not be verified
//                      (key count or value differ afterwards; should be 0)
//   int32   frames[numFrames]
//   uint32  indices[numVerts]                    only if flags & 1
//   v1: float32 verts[numFrames][numVerts][3]
//       float32 bones[numFrames][numBones][3]
//   v2: float32 verts[numFrames][numSets][numVerts][3]
//       float32 bones[numFrames][numSets][numBones][3]
// Errors are the usual JSON error body.
//
// Per frame only the figure is evaluated (setFrame + forceCacheUpdate), not the
// whole scene; the original frame and resolution level are restored afterwards
// followed by one Scene update (unless sceneUpdate is false).

#include <QtCore/qobject.h>
#include <QtCore/qbytearray.h>
#include "DzScriptServerPane.h"  // HttpResult + its metatype, IRequestHandler

// Lives on the main thread; called via BlockingQueuedConnection.
class GeometryExporter : public QObject {
	Q_OBJECT
public:
	explicit GeometryExporter(QObject* parent = nullptr) : QObject(parent) {}
	// 200 → binary payload, anything else → JSON error body.
	Q_INVOKABLE HttpResult exportFrames(const QByteArray& jsonBody);
};

class GeometryFramesHandler : public IRequestHandler {
public:
	// Must be constructed on the main thread (the exporter's thread affinity).
	GeometryFramesHandler();
	~GeometryFramesHandler() override;
	void handle(HttpContext& ctx) override;
private:
	GeometryExporter* m_pExporter;
};
