
#include "../Common/ShaderStructs.hlsli"
#include "../Common/Utils.hlsli"

cbuffer externalData : register(b0)
{
    uint type;
}

Texture2D<float4> input : register(t0);
RWTexture2D<float4> output : register(u0);
Texture2D<float4> normals : register(t1);
Texture2D<float4> positions : register(t2);

float4 RoundColor(float4 color, float precision) {
    return round(color * precision) / precision;
}

// Estructura para almacenar un color y su frecuencia
struct ColorCount {
    float2 pixel;
    float4 rounded;
    uint count;
    uint next;
};

#define NTHREADS 32
[numthreads(NTHREADS, NTHREADS, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    float2 pixel = float2(DTid.x, DTid.y);

    uint2 input_dimensions;
    uint2 normals_dimensions;
    {
        uint w, h;
        input.GetDimensions(w, h);
        input_dimensions.x = w;
        input_dimensions.y = h;

        normals.GetDimensions(w, h);
        normals_dimensions.x = w;
        normals_dimensions.y = h;
    }

    uint2 normalRatio = normals_dimensions / input_dimensions;

    float2 info_pixel = pixel * normalRatio;
    float3 p0_normal = normals[info_pixel].xyz;
    float3 p0_position = positions[info_pixel].xyz;

    float4 c = float4(0.0f, 0.0f, 0.0f, 0.0f);
    float count = 0.0f;

    float2 dir = float2(0.0f, 0.0f);
    if (type == 1 || type == 3) {
        dir = float2(1.0f, 0.0f);
    }
    else if (type == 2 || type == 4) {
        dir = float2(0.0f, 1.0f);
    }

    if (type == 1 || type == 2) {
#define KERNEL 8

        float4 c0 = float4(0.0f, 0.0f, 0.0f, 0.0f);
        for (int i = -KERNEL; i <= KERNEL; ++i) {
            float2 p = pixel + dir * i;
            info_pixel = p * normalRatio;
            float3 p1_normal = normals[info_pixel].xyz;
            float3 p1_position = positions[info_pixel].xyz;
            float4 color = input[p];
            float w = dot(p0_normal, p1_normal) / max(length(p1_position - p0_position), 1.0f);
            c0 += color * w;
            count += w;
        }
        output[pixel] = c0 / count;
    }
    else if (type == 3 || type == 4) {

#define SIZE 16
#define NCOLORS (uint)(2 * SIZE + 1)
        uint maxColors = NCOLORS;
        ColorCount colorCounts[NCOLORS];

        float4 incolor = input[pixel];
        if (incolor.a == 0.0f) {
            output[pixel] = float4(0.0f, 0.0f, 0.0f, 0.0f);
            return;
        }

        // Precisión para la agrupación de colores
        float precisionFactor = 0.1f;
        uint ncolors = 0;
        float4 meanColor = float4(0.0f, 0.0f, 0.0f, 0.0f);

        for (int i = -SIZE; i <= SIZE; ++i) {
            
            if (ncolors >= maxColors - 1) {
                break;
            }

            float2 p = pixel + dir * i;
            // Redondear el color actual para agruparlo
            float4 c = input[p];
            meanColor += c;
            float4 roundedColor = RoundColor(c, precisionFactor);

            // Verificar si ya existe este color en el array
            bool found = false;
            for (uint j = 0; j < ncolors; ++j) {
                if (all(roundedColor == colorCounts[j].rounded)) {
                    // Si el color ya está en el array, incrementar el contador
                    colorCounts[ncolors].pixel = p;
                    colorCounts[ncolors].count = 0;
                    colorCounts[ncolors].next = 0;

                    uint idx = j;
                    while (colorCounts[idx].next != 0) {
                        idx = colorCounts[idx].next;
                    }
                    colorCounts[idx].next = ncolors;
                        
                    ncolors++;
                    found = true;
                    break;
                }
            }

            // Si no se encontró el color, agregarlo como un nuevo color
            if (!found) {
                colorCounts[ncolors].pixel = p;
                colorCounts[ncolors].rounded = roundedColor;
                colorCounts[ncolors].count = 1;
                colorCounts[ncolors].next = 0;
                ncolors++;
            }
        }
    
        meanColor /= maxColors;
        float wtotal = 0.0f;
        float4 mostFrequentColor = float4(0, 0, 0, 0);
        
        // Buscar el color con la mayor frecuencia
        uint n = 0;
        for (i = 0; i < 1; ++i) {
            uint maxCount = 0;
            uint pos = 0;
            bool found = false;
            float w = 0.0f;
            for (n = 0; n < ncolors; ++n) {
                if (colorCounts[n].count > maxCount) {
                    maxCount = colorCounts[n].count;
                    pos = n;
                    found = true;
                }
            }
            if (found) {
                ColorCount cc = colorCounts[pos];
                while (1) {
                    info_pixel = cc.pixel * normalRatio;
                    float3 p1_normal = normals[info_pixel].xyz;
                    float3 p1_position = positions[info_pixel].xyz;
                    float4 color = input[cc.pixel];
                    float dist = length(p1_position - p0_position);
                    w = saturate(dot(p0_normal, p1_normal)) / max(dist, 0.001f);
                    float dcolor = (1.0f - length(color - meanColor));
                    w *= pow(dcolor, 2.0f);
                    mostFrequentColor += color * w;
                    wtotal += w;

                    if (cc.next == 0) {
                        break;
                    }
                    cc = colorCounts[cc.next];
                }

                colorCounts[pos].count = 0;
            } else {
                break;
            }
        }
        if (wtotal > Epsilon) {
            output[pixel] = mostFrequentColor / wtotal;
        }
    }
}
