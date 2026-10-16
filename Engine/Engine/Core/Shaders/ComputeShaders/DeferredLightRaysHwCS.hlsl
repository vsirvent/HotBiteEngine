//DeferredLightRaysCS.hlsl with the shadow rays answered by inline ray queries (shader model 6.5) against the acceleration structure the
//engine builds each frame (Core::AccelStructure), in place of the walk of the software hierarchy of Core::ShadowRays. The same lights, the
//same jitter, the same margins: the picture is the same and the time is the hardware's. RenderSystem dispatches it instead of the rays
//permutation when the device has ray queries and the hardware tracer is chosen (RenderSystem::SetShadowRayTracer). DXC only: ray queries
//do not exist in fxc's shader model.
#define SR_RAYQUERY 1
#include "DeferredLightRaysCS.hlsl"
