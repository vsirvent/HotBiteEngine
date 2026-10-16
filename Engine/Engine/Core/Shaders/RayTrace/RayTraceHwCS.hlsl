//RayTraceCS.hlsl (the reflections and refractions) with the hardware answering which triangle a ray hits: inline ray queries (shader model
//6.5) against the acceleration structure the engine builds each frame from the objects it hands the tracer (Core::AccelStructure, the WORLD
//top level), in place of the walk of the software hierarchy object by object; and the objects' textures indexed from an unbounded array in
//place of the switch over MAX_OBJECTS cases. What happens at a hit is the same code, so the picture is the same and the time is the
//hardware's. RenderSystem dispatches it instead of RayTraceCS.cso when the device has ray queries and the hardware tracer is chosen
//(RenderSystem::SetRayTracer). DXC only: ray queries do not exist in fxc's shader model.
#define RT_RAYQUERY 1
#include "RayTraceCS.hlsl"
