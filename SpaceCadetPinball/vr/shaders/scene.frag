#version 450

layout(location = 0) in vec3 vWorld;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec2 vUv;
layout(location = 3) in vec4 vColor;

layout(set = 0, binding = 0) uniform sampler2D gameTex;

layout(push_constant) uniform Push
{
	mat4 viewProj;
	vec4 eyePos;
	vec4 params;
	vec4 uvRect;
	vec4 glow;
} pc;

layout(location = 0) out vec4 outColor;

const int MODE_LIT = 0;
const int MODE_SCREEN = 1;
const int MODE_GLASS = 2;
const int MODE_FLOOR = 3;
const int MODE_SKY = 4;
const int MODE_UNLIT = 5;
const int MODE_BEAM = 6;
const int MODE_PLANET = 7;

const vec3 KEY_DIR = normalize(vec3(-0.35, 0.85, 0.40));
const vec3 SUN_DIR = normalize(vec3(0.8, 0.35, 0.3));

float hash13(vec3 p)
{
	p = fract(p * 0.1031);
	p += dot(p, p.zyx + 31.32);
	return fract((p.x + p.y) * p.z);
}

float noise3(vec3 p)
{
	vec3 i = floor(p);
	vec3 f = fract(p);
	f = f * f * (3.0 - 2.0 * f);
	float n000 = hash13(i);
	float n100 = hash13(i + vec3(1, 0, 0));
	float n010 = hash13(i + vec3(0, 1, 0));
	float n110 = hash13(i + vec3(1, 1, 0));
	float n001 = hash13(i + vec3(0, 0, 1));
	float n101 = hash13(i + vec3(1, 0, 1));
	float n011 = hash13(i + vec3(0, 1, 1));
	float n111 = hash13(i + vec3(1, 1, 1));
	return mix(mix(mix(n000, n100, f.x), mix(n010, n110, f.x), f.y),
	           mix(mix(n001, n101, f.x), mix(n011, n111, f.x), f.y), f.z);
}

float fbm(vec3 p)
{
	float v = 0.0, a = 0.5;
	for (int i = 0; i < 4; ++i)
	{
		v += a * noise3(p);
		p = p * 2.03 + vec3(1.7, 9.2, 3.1);
		a *= 0.5;
	}
	return v;
}

// Space backdrop: deep blue/violet nebula and a layered starfield.
vec3 skyColor(vec3 dir, bool withStars)
{
	float h = dir.y;
	vec3 base = mix(vec3(0.010, 0.008, 0.030), vec3(0.003, 0.004, 0.012), clamp(h, 0.0, 1.0));
	// glow band near the horizon
	base += vec3(0.06, 0.02, 0.10) * exp(-abs(h) * 6.0);

	float n = fbm(dir * 2.2 + vec3(4.0, 0.0, 0.0));
	float n2 = fbm(dir * 4.5 + vec3(0.0, 7.0, 2.0));
	vec3 neb = vec3(0.22, 0.05, 0.30) * smoothstep(0.45, 0.85, n) + vec3(0.02, 0.10, 0.25) * smoothstep(0.5, 0.9, n2);
	base += neb * 0.55;

	if (withStars)
	{
		float stars = 0.0;
		for (int layer = 0; layer < 2; ++layer)
		{
			float scale = layer == 0 ? 90.0 : 220.0;
			vec3 p = dir * scale;
			float pix = max(length(fwidth(p)), 0.02);
			vec3 cell = floor(p);
			float rnd = hash13(cell + float(layer) * 17.0);
			if (rnd > (layer == 0 ? 0.985 : 0.992))
			{
				vec3 jitter = vec3(hash13(cell + 3.1), hash13(cell + 5.7), hash13(cell + 9.3)) * 0.6 + 0.2;
				float d = length(p - (cell + jitter));
				float size = (layer == 0 ? 0.11 : 0.07) + pix * 0.6;
				float twinkle = 0.75 + 0.25 * sin(pc.eyePos.w * (1.5 + rnd * 3.0) + rnd * 60.0);
				stars += smoothstep(size, 0.0, d) * (layer == 0 ? 1.4 : 0.7) * twinkle;
			}
		}
		vec3 starTint = mix(vec3(0.75, 0.85, 1.0), vec3(1.0, 0.9, 0.75), hash13(floor(dir * 90.0) + 2.0));
		base += starTint * stars;
	}
	return base;
}

// Cheap environment used for reflections on chrome and glass.
vec3 envColor(vec3 r)
{
	vec3 c = skyColor(r, false) * 2.0;
	// soft overhead strip lights (cyan and magenta), like an arcade ceiling
	float strip = smoothstep(0.75, 0.95, r.y) * (0.5 + 0.5 * smoothstep(0.85, 1.0, abs(sin(r.x * 3.0 + r.z * 1.5))));
	c += mix(vec3(0.05, 0.55, 0.75), vec3(0.75, 0.15, 0.6), step(0.0, r.x)) * strip * 0.6;
	// floor reflection darkens the lower hemisphere
	c = mix(c, vec3(0.01, 0.01, 0.02), smoothstep(0.0, -0.3, r.y));
	return c;
}

vec3 screenGlow(vec3 n, vec3 world)
{
	vec3 d = pc.glow.xyz - world;
	float dist2 = dot(d, d);
	float ndl = max(dot(n, normalize(d)), 0.0) * 0.8 + 0.2;
	return vec3(0.35, 0.45, 0.85) * pc.glow.w * ndl / (1.0 + dist2 * 6.0);
}

void main()
{
	int mode = int(pc.params.x + 0.5);
	vec3 eye = pc.eyePos.xyz;
	vec3 v = normalize(eye - vWorld);

	if (mode == MODE_SCREEN)
	{
		vec2 uv = mix(pc.uvRect.xy, pc.uvRect.zw, vUv);
		vec3 c = texture(gameTex, uv).rgb * pc.params.z;
		// Gentle falloff at the very edge so the panel sits inside its bezel.
		vec2 e = min(vUv, 1.0 - vUv);
		float edge = smoothstep(0.0, 0.012, min(e.x, e.y));
		outColor = vec4(c * mix(1.0, mix(0.35, 1.0, edge), pc.params.w), 1.0);
		return;
	}

	if (mode == MODE_SKY)
	{
		outColor = vec4(skyColor(normalize(vWorld - eye), true), 1.0);
		return;
	}

	if (mode == MODE_UNLIT)
	{
		outColor = vec4(vColor.rgb * max(pc.params.y, 1.0), 1.0);
		return;
	}

	if (mode == MODE_BEAM)
	{
		float across = 1.0 - abs(vUv.x * 2.0 - 1.0);
		float along = 1.0 - vUv.y;
		float a = vColor.a * across * across * mix(0.15, 1.0, along);
		outColor = vec4(vColor.rgb * pc.params.y, a);
		return;
	}

	vec3 n = normalize(vNormal);
	// Two-sided lighting: always light the side facing the viewer.
	if (dot(n, v) < 0.0 && mode != MODE_FLOOR)
		n = -n;

	if (mode == MODE_GLASS)
	{
		float ndv = clamp(dot(n, v), 0.0, 1.0);
		float fresnel = 0.04 + 0.96 * pow(1.0 - ndv, 5.0);
		vec3 r = reflect(-v, n);
		vec3 refl = envColor(r);
		// a faint diagonal sheen sliding across the glass
		float sheen = smoothstep(0.985, 1.0, sin((vUv.x * 0.8 + vUv.y) * 9.0)) * 0.08;
		float a = clamp(fresnel * 0.85 + 0.02 + sheen, 0.0, 0.9);
		outColor = vec4(refl + vec3(sheen), a);
		return;
	}

	if (mode == MODE_FLOOR)
	{
		vec2 p = vWorld.xz;
		vec2 g = abs(fract(p * 2.0 - 0.5) - 0.5) / max(fwidth(p * 2.0), vec2(1e-4));
		float line = 1.0 - min(min(g.x, g.y), 1.0);
		float dist = length(p - pc.glow.xz);
		float fade = exp(-dist * 0.22);
		vec3 base = vec3(0.012, 0.012, 0.022);
		vec3 grid = vec3(0.10, 0.35, 0.70) * line * fade * 0.6;
		// warm pool of light under the cabinet
		vec3 pool = vec3(0.25, 0.20, 0.45) * exp(-dist * dist * 1.8) * 0.35;
		// floor picks up a blurry reflection of the environment
		vec3 refl = envColor(reflect(-v, vec3(0, 1, 0))) * 0.12;
		vec3 c = base + grid + pool + refl;
		// fade into the sky at the horizon
		float horizon = smoothstep(18.0, 40.0, length(vWorld.xz - eye.xz));
		c = mix(c, skyColor(normalize(vWorld - eye), false), horizon);
		outColor = vec4(c, 1.0);
		return;
	}

	if (mode == MODE_PLANET)
	{
		float lat = n.y;
		float bands = fbm(vec3(lat * 9.0, lat * 3.0, 0.0) + n * 1.5);
		vec3 albedo = mix(vec3(0.55, 0.30, 0.18), vec3(0.85, 0.70, 0.50), bands);
		albedo = mix(albedo, vec3(0.30, 0.12, 0.10), smoothstep(0.55, 0.8, fbm(n * 6.0)));
		float ndl = max(dot(n, SUN_DIR), 0.0);
		float rim = pow(1.0 - max(dot(n, v), 0.0), 3.0);
		vec3 c = albedo * (ndl * 1.1 + 0.015) + vec3(0.35, 0.55, 1.0) * rim * (0.25 + ndl);
		outColor = vec4(c, 1.0);
		return;
	}

	// MODE_LIT: painted / chrome cabinet surfaces
	vec3 albedo = vColor.rgb;
	float gloss = vColor.a;
	vec3 hemi = mix(vec3(0.020, 0.018, 0.035), vec3(0.060, 0.070, 0.120), n.y * 0.5 + 0.5);
	float ndl = max(dot(n, KEY_DIR), 0.0);
	vec3 h = normalize(KEY_DIR + v);
	float shin = mix(8.0, 256.0, gloss);
	float spec = pow(max(dot(n, h), 0.0), shin) * gloss * (shin + 8.0) / 80.0;
	vec3 r = reflect(-v, n);
	float ndv = clamp(dot(n, v), 0.0, 1.0);
	float fres = mix(0.04, 1.0, pow(1.0 - ndv, 5.0));
	vec3 env = envColor(r) * mix(fres * 0.5, 1.0, gloss * gloss) * gloss;

	vec3 c = albedo * (hemi + ndl * vec3(0.55, 0.52, 0.60) + screenGlow(n, vWorld));
	c += vec3(0.9, 0.9, 1.0) * spec * 0.6 + env * mix(vec3(1.0), albedo, 0.5);
	c += albedo * pc.params.y;
	outColor = vec4(c, 1.0);
}
