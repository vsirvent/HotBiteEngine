/*
The HotBite Game Engine

Copyright(c) 2023 Vicente Sirvent Orts

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace HotBite {
	namespace Engine {
		namespace Core {
			/**
			 * Decides, frame by frame, which of the render scale's steps (RenderScale) the next frame is drawn at, so the GPU
			 * finishes it inside the budget of the target refresh rate. No graphics calls: it is fed the GPU time of
			 * the frames that have finished and answers with a step, which makes it testable on its own.
			 *
			 *  - Down is fast. A frame over the budget picks the step the cost model says fits (one jump, not one step per
			 *    frame), because every frame spent over is a missed refresh.
			 *  - Up is slow and has hysteresis: only after the next step up is predicted to leave a comfortable margin, for
			 *    about a second, and not right after coming down. This is what stops it from flickering between two sizes.
			 *  - The cost model is `time(step) = time(now) * ((1 - p) + p * area(step) / area(now))`, where p is the part of
			 *    the frame that scales with the number of pixels. p starts at a guess and is measured from what the frame
			 *    really did after each change, so a scene that is mostly geometry (small p) does not make it fall to the
			 *    smallest size for nothing.
			 */
			class DynamicResolutionController {
			public:
				struct Settings {
					float target_hz = 60.0f;
					//The scales the controller may pick between (1 = native). max_scale <= 1.
					float min_scale = 0.5f;
					float max_scale = 1.0f;
				};

				//Size of the budget kept free of the target frame time: the GPU time that counts as "over" is the frame time
				//of the target rate times this, so the frame still has room for the CPU's submission and the present.
				static constexpr float BUDGET_SAFETY = 0.90f;
				//The next step up must be predicted this far under the budget to be taken.
				static constexpr float UP_MARGIN = 0.80f;
				//A step down aims this far under the budget so one jump is enough.
				static constexpr float DOWN_AIM = 0.93f;

				void Reset(float initial_ms_guess = 0.0f) {
					ema_ms = initial_ms_guess;
					have_ema = false;
					up_frames = 0;
					cooldown = 0;
					skip = SETTLE_FRAMES;
					stable = 0;
					calibrating = false;
					changes = 0;
				}

				//One finished frame: `gpu_ms` the GPU took, `step` the step it was drawn at, `current` the step in use now
				//(frames still in flight when the step changed were drawn at another one and only count when they match).
				//Returns the step to draw the next frame at, among [first_step, last_step] (first = biggest picture).
				template <class ScaleOf>
				int Update(float gpu_ms, int step, int current, int first_step, int last_step, const Settings& settings, ScaleOf&& scale_of) {
					current = (std::min)((std::max)(current, first_step), last_step);
					if (step != current || !(gpu_ms >= 0.0f) || gpu_ms > 1000.0f) {
						return current;
					}
					if (skip > 0) {
						--skip;
						return current;
					}
					const float frame_ms = 1000.0f / (std::max)(settings.target_hz, 1.0f);
					const float budget = frame_ms * BUDGET_SAFETY;
					ema_ms = have_ema ? ema_ms + (gpu_ms - ema_ms) * EMA_ALPHA : gpu_ms;
					have_ema = true;
					++stable;
					if (cooldown > 0) {
						--cooldown;
					}

					Learn(current, scale_of);

					//A spike is acted on at once; a drift, by the average.
					const float estimate = (std::max)(ema_ms, 0.6f * gpu_ms + 0.4f * ema_ms);
					const float area_now = Area(current, scale_of);

					if (estimate > budget && current < last_step) {
						int pick = last_step;
						for (int s = current + 1; s <= last_step; ++s) {
							if (Predict(estimate, area_now, Area(s, scale_of)) <= budget * DOWN_AIM) {
								pick = s;
								break;
							}
						}
						return Change(current, pick, estimate, area_now);
					}

					//Room to grow: the next step up is predicted to fit with margin, for long enough.
					if (current > first_step && estimate <= budget && cooldown == 0) {
						const float predicted = Predict(estimate, area_now, Area(current - 1, scale_of));
						if (predicted <= budget * UP_MARGIN) {
							if (++up_frames >= UP_FRAMES_AT_60 * (std::max)(settings.target_hz, 1.0f) / 60.0f) {
								return Change(current, current - 1, estimate, area_now);
							}
							return current;
						}
					}
					up_frames = 0;
					return current;
				}

				//For the stats: the smoothed GPU time, and how many times the step changed.
				float SmoothedMs() const { return ema_ms; }
				uint32_t Changes() const { return changes; }
				float PixelCostFraction() const { return pixel_fraction; }

			private:
				static constexpr float EMA_ALPHA = 0.25f;
				static constexpr int SETTLE_FRAMES = 4;
				//Frames of headroom wanted before going up, at 60 Hz (scaled with the rate: it is about a second).
				static constexpr float UP_FRAMES_AT_60 = 60.0f;
				//Samples at a size before its cost counts as measured.
				static constexpr int STABLE_FRAMES = 12;
				//How long after coming down it will not go up again, in frames at 60 Hz.
				static constexpr int COOLDOWN_FRAMES = 120;

				float ema_ms = 0.0f;
				bool have_ema = false;
				int up_frames = 0;
				int cooldown = 0;
				int skip = SETTLE_FRAMES;
				int stable = 0;
				uint32_t changes = 0;
				//The part of the frame that scales with the pixel count, guessed until measured.
				float pixel_fraction = 0.8f;

				//What a change is measured against.
				bool calibrating = false;
				float before_ms = 0.0f;
				float before_area = 1.0f;
				int before_stable = 0;

				template <class ScaleOf>
				static float Area(int step, ScaleOf&& scale_of) {
					const float s = scale_of(step);
					return s * s;
				}

				float Predict(float ms, float area_now, float area_new) const {
					return ms * ((1.0f - pixel_fraction) + pixel_fraction * area_new / (std::max)(area_now, 1.0e-4f));
				}

				int Change(int from, int to, float ms, float area_from) {
					if (to == from) {
						return from;
					}
					calibrating = stable >= STABLE_FRAMES;
					before_ms = ms;
					before_area = area_from;
					before_stable = stable;
					stable = 0;
					up_frames = 0;
					skip = SETTLE_FRAMES;
					have_ema = false;
					if (to > from) {
						cooldown = COOLDOWN_FRAMES;
					}
					++changes;
					return to;
				}

				//After a change, once the new size has been timed long enough, what the frame really did says how much of it
				//scales with the pixels: after = before * ((1 - p) + p * r) with r the ratio of the areas.
				template <class ScaleOf>
				void Learn(int current, ScaleOf&& scale_of) {
					if (!calibrating || stable < STABLE_FRAMES) {
						return;
					}
					calibrating = false;
					const float r = Area(current, scale_of) / (std::max)(before_area, 1.0e-4f);
					if (std::fabs(1.0f - r) < 0.12f || before_ms <= 0.0f) {
						return;
					}
					const float ratio = ema_ms / before_ms;
					const float measured = (1.0f - ratio) / (1.0f - r);
					if (!std::isfinite(measured)) {
						return;
					}
					const float clamped = (std::min)((std::max)(measured, 0.35f), 1.0f);
					pixel_fraction += (clamped - pixel_fraction) * 0.5f;
				}
			};
		}
	}
}
