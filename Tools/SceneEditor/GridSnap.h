#pragma once

#include <cmath>
#include <DirectXMath.h>

namespace HotBiteEditor {

	// Plain rounding-to-nearest-multiple helpers for the gizmo's grid-snap feature
	// (SelectionGizmo.cpp) and template placement (AssetBrowser.cpp). Deliberately
	// engine-independent: these just round numbers, the callers decide *what*
	// they're rounding (a world-space position component, a drag angle in
	// radians, a scale factor) and at *which point in the drag math* that's
	// correct - see the callers for why each does it at a different spot rather
	// than all funneling through one place.
	namespace GridSnap {

		// Nearest multiple of `step`. A non-positive step is treated as "no
		// snapping" (returns `v` unchanged) so callers don't need to guard a
		// zero/negative UI value themselves.
		inline float SnapValue(float v, float step) {
			if (step <= 0.0f) {
				return v;
			}
			return std::roundf(v / step) * step;
		}

		inline DirectX::XMFLOAT3 SnapVector3(const DirectX::XMFLOAT3& v, float step) {
			return { SnapValue(v.x, step), SnapValue(v.y, step), SnapValue(v.z, step) };
		}

		inline DirectX::XMVECTOR SnapVector3(DirectX::FXMVECTOR v, float step) {
			DirectX::XMFLOAT3 f;
			DirectX::XMStoreFloat3(&f, v);
			f = SnapVector3(f, step);
			return DirectX::XMLoadFloat3(&f);
		}

		// Snaps an angle given in radians to the nearest multiple of `step_degrees`.
		inline float SnapAngleRadians(float radians, float step_degrees) {
			if (step_degrees <= 0.0f) {
				return radians;
			}
			const float step_radians = DirectX::XMConvertToRadians(step_degrees);
			return std::roundf(radians / step_radians) * step_radians;
		}

		// Snaps a scale component to the nearest multiple of `step`, floored so a
		// dragged-down scale can't snap to (or through) zero.
		inline float SnapScale(float scale, float step) {
			constexpr float kMinScale = 0.01f;
			float snapped = SnapValue(scale, step);
			return (std::fabs(snapped) < kMinScale) ? (scale < 0.0f ? -kMinScale : kMinScale) : snapped;
		}
	}
}
