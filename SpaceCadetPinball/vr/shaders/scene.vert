#version 450

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUv;
layout(location = 3) in vec4 inColor;

layout(push_constant) uniform Push
{
	mat4 viewProj;
	vec4 eyePos;   // xyz eye position, w time (seconds)
	vec4 params;   // x mode, y emissive, z screen brightness, w unused
	vec4 uvRect;   // screen modes: u0 v0 u1 v1
	vec4 glow;     // xyz playfield screen centre, w glow strength
} pc;

layout(location = 0) out vec3 vWorld;
layout(location = 1) out vec3 vNormal;
layout(location = 2) out vec2 vUv;
layout(location = 3) out vec4 vColor;

void main()
{
	vWorld = inPos;
	vNormal = inNormal;
	vUv = inUv;
	vColor = inColor;
	gl_Position = pc.viewProj * vec4(inPos, 1.0);
}
