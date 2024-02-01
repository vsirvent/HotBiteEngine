
RenderTarget MainRenderPS(GSOutput input)
{
	int i = 0;
	float2 pos = input.position.xy;
	pos.x /= screenW;
	pos.y /= screenH;
	float spec_intensity = 0.5f;
	float4 wpos = input.worldPos;
	float depth = length(input.worldPos.xyz - cameraPosition) - 5.0f;
	float dz = depthTexture.SampleCmpLevelZero(PCFSampler, pos, depth);
	if (dz == 0.0f) discard;

	RenderTarget output;
	// Calculate the ambient color
	float4 finalColor = { 0.0f, 0.0f, 0.0f, 0.0f };
	float4 lightColor = { 0.0f, 0.0f, 0.0f, 1.0f };

	float3 normal = input.normal;
	float calculated_values[MAX_MULTI_TEXTURE];

	if (multi_texture_count > 0) {
		getValues(calculated_values, basicSampler, input.uv, multi_texture_count,
			multi_texture_operations, multi_maskTexture, multi_texture_values, input.worldPos.xyz);
	}

	if (multi_texture_count > 0) {
		spec_intensity = getMutliTextureValueLevel(basicSampler, 2, MULTITEXT_SPEC, multi_texture_count, multi_texture_operations,
			calculated_values, multi_texture_uv_scales, input.uv, multi_specularTexture).r;

	}
	else {
		spec_intensity = material.specIntensity;
		if (material.flags & SPECULAR_MAP_ENABLED_FLAG) {
			spec_intensity *= specularTexture.Sample(basicSampler, input.uv).r;
		}
		else if (material.flags & ARM_MAP_ENABLED_FLAG) {
			spec_intensity *= armTexture.Sample(basicSampler, input.uv).g;
		}
	}

	if (meshNormalTextureEnable) {
		float3 mesh_normal = meshNormalTexture.Sample(basicSampler, input.mesh_uv).xyz;
		mesh_normal = mesh_normal * 2.0f - 1.0f;
		normal = normalize(mul(float4(mesh_normal, 0.0f), world)).xyz;
	}

	float DirLightParallaxAtt[MAX_LIGHTS];
	float PointLightParallaxAtt[MAX_LIGHTS];
	for (i = 0; i < dirLightsCount; ++i) {
		DirLightParallaxAtt[i] = 1.0f;
	}
	// Calculate the point lights
	for (i = 0; i < pointLightsCount; ++i) {
		PointLightParallaxAtt[i] = 1.0f;
	}
	if (material.flags & NORMAL_MAP_ENABLED_FLAG || multi_texture_count > 0) {
		// Build orthonormal basis.
		float3 N = normal;
		float3 T = normalize(input.tangent - dot(input.tangent, N) * N);
		float3 B = normalize(input.bitangent);
		float4x4 tbn = IDENTITY_MATRIX;
		tbn._m00 = T.x;  tbn._m10 = B.x; tbn._m20 = N.x;
		tbn._m01 = T.y;  tbn._m11 = B.y; tbn._m21 = N.y;
		tbn._m02 = T.z;  tbn._m12 = B.z; tbn._m22 = N.z;

		float scale, steps, angle_steps;
		if (multi_texture_count > 0) {
			scale = multi_parallax_scale;
			steps = 4.0f;
			angle_steps = 5.0f;
		}
		else {
			scale = material.parallax_scale;
			steps = material.parallax_steps;
			angle_steps = material.parallax_angle_steps;
		}
		
		scale = abs(scale);
		if ((material.flags & PARALLAX_MAP_ENABLED_FLAG || multi_texture_count > 0 ) && scale != 0.0f) {
			float h = 0;
			matrix global_to_tbn = inverse(tbn);
			float3 tbn_cam_pos = mul(float4(cameraPosition, 0.0f), global_to_tbn).xyz;
			float3 tbn_fragment_pos = mul(input.worldPos, global_to_tbn).xyz;
			float3 tbn_fragment_displacement;

			input.uv = CalculateDepthUVBinary(scale, steps, angle_steps, input.uv, tbn_cam_pos, tbn_fragment_pos, calculated_values, h, tbn_fragment_displacement);
			wpos.xyz -= (mul(tbn_fragment_displacement, (float3x3)tbn) - normal);

			if (material.flags & PARALLAX_SHADOW_ENABLED_FLAG) {
				float lh = 0.0f;
				float3 tbn_light_pos;
				float3 tbn_dummy_displacement;
				// Calculate the directional light
				for (i = 0; i < dirLightsCount; ++i) {
					float3 tolight = normalize(dirLights[i].DirToLight);
					float3 sky_pos = input.worldPos.xyz + tolight * (1000.0f / tolight.y);
					tbn_light_pos = mul(float4(sky_pos, 0.0f), global_to_tbn).xyz;
					CalculateDepthUVBinary(scale, steps, angle_steps, input.uv, tbn_light_pos, tbn_fragment_pos, calculated_values, lh, tbn_dummy_displacement);
					DirLightParallaxAtt[i] = saturate(1.0f - pow(material.parallax_shadow_scale * (lh - h), 1.0f));
				}
				// Calculate the point lights
				for (i = 0; i < pointLightsCount; ++i) {
					if (length(input.worldPos.xyz - pointLights[i].Position) < pointLights[i].Range) {
						tbn_light_pos = mul(float4(pointLights[i].Position, 0.0f), global_to_tbn).xyz;
						CalculateDepthUVBinary(scale, steps, angle_steps, input.uv, tbn_light_pos, tbn_fragment_pos, calculated_values, lh, tbn_dummy_displacement);
						PointLightParallaxAtt[i] = saturate(1.0f - pow(material.parallax_shadow_scale * (lh - h), 1.0f));
					}
				}
			}
		}
		float3 texture_normal;
		if (multi_texture_count > 0) {
			texture_normal = normalize(getMutliTextureValueLevel(basicSampler, 2, MULTITEXT_NORM, multi_texture_count, multi_texture_operations,
				calculated_values, multi_texture_uv_scales, input.uv, multi_normalTexture).xyz);
		}
		else {
			texture_normal = normalTexture.Sample(basicSampler, input.uv);
		}
		texture_normal = texture_normal * 2.0f - 1.0f;
		normal = normalize(mul(float4(texture_normal, 0.0f), tbn).xyz + normal);
	}
#if 1
	// Calculate the ambient light
	finalColor.rgb += CalcAmbient(normal);
#endif
#if 1
	// Calculate the directional light
	for (i = 0; i < dirLightsCount; ++i) {
		finalColor.rgb += CalcDirectional(normal, wpos, input.uv, material, dirLights[i], cloud_density, i, lightColor) * DirLightParallaxAtt[i];
		if (dirLights[i].density > 0.0f && disable_vol == 0) {
			lightColor.rgb += DirVolumetricLight(wpos, dirLights[i], i, time, cloud_density);
		}
	}
#endif
#if 1
	// Calculate the point lights
	for (i = 0; i < pointLightsCount; ++i) {
		if (length(wpos - pointLights[i].Position) < pointLights[i].Range) {
			finalColor.rgb += CalcPoint(normal, wpos, input.uv, material, pointLights[i], i, lightColor) * PointLightParallaxAtt[i];
			if (pointLights[i].density > 0.0f && disable_vol == 0) {
				lightColor.rgb += VolumetricLight(wpos, pointLights[i], i);
			}
		}
	}
#endif
	// Apply textures
	if (material.flags & DIFFUSSE_MAP_ENABLED_FLAG || multi_texture_count > 0) {
		float3 text_color;
		if (multi_texture_count > 0) {
			text_color = getMutliTextureValue(basicSampler, MULTITEXT_DIFF, multi_texture_count, multi_texture_operations,
				calculated_values, multi_texture_uv_scales, input.uv, multi_diffuseTexture).rgb;
		}
		else {
			text_color = diffuseTexture.Sample(basicSampler, input.uv).rgb;
		}
		if (material.flags & ALPHA_ENABLED_FLAG) {
			if (length(material.alphaColor - text_color) > 0.4f) {
				finalColor.rgb *= text_color;
			}
			else {
				discard;
			}
		}
		else {
			finalColor.rgb *= text_color;
		}
	}
	else {
		finalColor *= material.diffuseColor;
	}

	// Apply ambient occlusion
	if (multi_texture_count > 0) {
		finalColor *= getMutliTextureValueLevel(basicSampler, 2, MULTITEXT_AO, multi_texture_count, multi_texture_operations,
			calculated_values, multi_texture_uv_scales, input.uv, multi_aoTexture).r;
	}
	else {
		if (material.flags & AO_MAP_ENABLED_FLAG) {
			finalColor *= aoTexture.Sample(basicSampler, input.uv).r;
		}
		else if (material.flags & ARM_MAP_ENABLED_FLAG) {
			finalColor *= armTexture.Sample(basicSampler, input.uv).r;
		}
	}

	if (material.flags & EMISSION_MAP_ENABLED_FLAG) {
		float4 emission = emissionTexture.Sample(basicSampler, input.uv);
		finalColor += emission;
		lightColor += emission;
	}
#if 1
	//Emission of point lights	
	matrix worldViewProj = mul(view, projection);
	float3 p2 = mul(input.worldPos, view).xyz;
	for (i = 0; i < pointLightsCount; ++i) {
		float3 p1 = mul(float4(pointLights[i].Position, 1.0f), view).xyz;
		//if we are in front of camera
		if (p1.z > 0) {
			//if pixel is behind light
			if (p1.z < p2.z) {
				finalColor.rgb += EmitPoint(input.position.xyz, worldViewProj, pointLights[i]);
			}
		}
	};
#endif
	output.scene = saturate(finalColor);
	output.light_map = saturate(lightColor);

	RaySource ray;
	ray.orig = wpos.xyz;
	ray.dispersion = 0.0f;
	ray.normal = normal;
	ray.density = 1.0f;

	output.rt_ray0_map = getColor0(ray);
	output.rt_ray1_map = getColor1(ray);
	return output;
}
