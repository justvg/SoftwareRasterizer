#include "parser.h"

#define ArraySize(Array) sizeof(Array)/sizeof(Array[0])

#define TO_RASTER(V) glm::vec4(ScreenWidth * (V.x + V.w) / 2, ScreenHeight * (V.w - V.y) / 2, V.z, V.w)

#define ScreenWidth 960
#define ScreenHeight 540
static void
OutputFrame(std::vector<glm::vec3>& FrameBuffer, char* Filename)
{
    Assert(FrameBuffer.size() >= (ScreenWidth * ScreenHeight));

    FILE* pFile = 0;
    fopen_s(&pFile, Filename, "w");
    fprintf(pFile, "P3\n%d %d\n%d\n ", ScreenWidth, ScreenHeight, 255);
    for (int PixelIndex = 0; PixelIndex < ScreenWidth * ScreenHeight; PixelIndex++)
    {
        // Write out color values clamped to [0, 255] 
		uint32_t R = (uint32_t)(255 * glm::clamp(FrameBuffer[PixelIndex].r, 0.0f, 1.0f));
		uint32_t G = (uint32_t)(255 * glm::clamp(FrameBuffer[PixelIndex].g, 0.0f, 1.0f));
		uint32_t B = (uint32_t)(255 * glm::clamp(FrameBuffer[PixelIndex].b, 0.0f, 1.0f));
        fprintf(pFile, "%d %d %d ", R, G, B);
    }
    fclose(pFile);
}

static bool 
EdgeFunction(glm::vec3& E, glm::vec3& Sample)
{
	// Interpolate edge function at given SampleLocation
	float Result = (E.x * Sample.x) + (E.y * Sample.y) + E.z;

	// Apply tie-breaking rules on shared vertices in order to avoid double-shading fragments
	if (Result > 0.0f) return true;
	else if (Result < 0.0f) return false;

	if (E.x > 0.f) return true;
	else if (E.x < 0.0f) return false;

	if ((E.x == 0.0f) && (E.y < 0.0f)) return false;
	else return true;
}

glm::vec4 VS(vertex_input& Input, glm::mat4& MVP, fragment_input& Output)
{
	Output.TexCoords = Input.TexCoords;

	glm::vec4 Result = MVP * glm::vec4(Input.Pos, 1.0f);
    return (Result);
}

glm::vec3 FS(fragment_input& Input, texture* pTexture)
{
	// By using fractional part of texture coordinates only, we will REPEAT (or WRAP) the same texture multiple times
	uint32_t idxS = (uint32_t)((Input.TexCoords.s - (int64_t)(Input.TexCoords.s)) * pTexture->m_Width - 0.5f);
	uint32_t idxT = (uint32_t)((Input.TexCoords.t - (int64_t)(Input.TexCoords.t)) * pTexture->m_Height - 0.5f);
	uint32_t TexelLocation = (idxT * pTexture->m_Width + idxS) * pTexture->m_NumChannels;

	float OneOver255 = 1.0f / 255.0f;
	float R = (float)(pTexture->m_Data[TexelLocation++] * OneOver255);
	float G = (float)(pTexture->m_Data[TexelLocation++] * OneOver255);
	float B = (float)(pTexture->m_Data[TexelLocation++] * OneOver255);

	glm::vec3 Result = glm::vec3(R, G, B);

	return (Result);
}

int main(void)
{	
	std::vector<glm::vec3> FrameBuffer(ScreenWidth * ScreenHeight, glm::vec3(0, 0, 0));
	std::vector<float> DepthBuffer(ScreenWidth * ScreenHeight, FLT_MAX);

	std::vector<vertex_input> VertexBuffer;
	std::vector<uint32_t> IndexBuffer;

	std::vector<Mesh> Primitives;

	std::map<std::string, texture*> Textures; 

	char* Filename = "../assets/sponza.obj";

	InitializeSceneObjects(Filename, Primitives, VertexBuffer, IndexBuffer, Textures);

	// Build view & projection matrices (right-handed sysem)
	float NearPlane = 0.125f;
	float FarPlane = 5000.f;
	glm::vec3 CamPos(0, -8.5f, -5.0f);
	glm::vec3 CamTarget(20, 5, 1);
	glm::vec3 CamUp(0, 1, 0);
	
	glm::mat4 View = glm::lookAt(CamPos, CamTarget, CamUp);
	View = glm::rotate(View, glm::radians(-30.0f), glm::vec3(0, 1, 0));
	glm::mat4 Proj = glm::perspective(glm::radians(60.f), (float)ScreenWidth / (float)ScreenHeight, NearPlane, FarPlane);

	glm::mat4 MVP = Proj * View;

	for(uint32_t ObjectIndex = 0; ObjectIndex < Primitives.size(); ObjectIndex++)
	{
		Mesh mesh = Primitives[ObjectIndex];
		texture* pTexture = Textures[mesh.MDiffuseTexName];
		Assert(pTexture != 0);

		int32_t TriCount = mesh.MIdxCount / 3;

		for(int Idx = 0; Idx < TriCount; Idx++)
		{
			vertex_input& VI0 = VertexBuffer[IndexBuffer[mesh.MIdxOffset + (Idx * 3)]];
			vertex_input& VI1 = VertexBuffer[IndexBuffer[mesh.MIdxOffset + (Idx * 3 + 1)]];
			vertex_input& VI2 = VertexBuffer[IndexBuffer[mesh.MIdxOffset + (Idx * 3 + 2)]];

			fragment_input FI0;
			fragment_input FI1;
			fragment_input FI2;

			glm::vec4 V0Clip = VS(VI0, MVP, FI0);
			glm::vec4 V1Clip = VS(VI1, MVP, FI1);
			glm::vec4 V2Clip = VS(VI2, MVP, FI2);

			// Apply viewport transformation
			glm::vec4 V0Homogen = TO_RASTER(V0Clip);
			glm::vec4 V1Homogen = TO_RASTER(V1Clip);
			glm::vec4 V2Homogen = TO_RASTER(V2Clip);

			glm::mat3 M = 
			{
				{V0Homogen.x, V1Homogen.x, V2Homogen.x},
				{V0Homogen.y, V1Homogen.y, V2Homogen.y},
				{V0Homogen.w, V1Homogen.w, V2Homogen.w},
			};

			// If det(M) == 0.0f, we'd perform division by 0 when calculating the invert matrix,
			// whereas (det(M) > 0) implies a back-facing triangle
			float Det = glm::determinant(M);
			if (Det >= 0.0f)
				continue;

			M = glm::inverse(M);

			// Set up edge functions based on the vertex matrix
			// We also apply some scaling to edge functions to be more robust.
			// This is fine, as we are working with homogeneous coordinates and do not disturb the sign of these functions.
			glm::vec3 E0 = M[0] / (glm::abs(M[0].x) + glm::abs(M[0].y));
            glm::vec3 E1 = M[1] / (glm::abs(M[1].x) + glm::abs(M[1].y));
            glm::vec3 E2 = M[2] / (glm::abs(M[2].x) + glm::abs(M[2].y));

			// Calculate constant function to interpolate 1/w
			glm::vec3 C = M * glm::vec3(1, 1, 1);

			// Calculate z interpolation vector
            glm::vec3 Z = M * glm::vec3(V0Clip.z, V1Clip.z, V2Clip.z);

			// Calculate normal interpolation vector
			glm::vec3 PNX = M * glm::vec3(FI0.Normal.x, FI1.Normal.x, FI2.Normal.x);
            glm::vec3 PNY = M * glm::vec3(FI0.Normal.y, FI1.Normal.y, FI2.Normal.y);
            glm::vec3 PNZ = M * glm::vec3(FI0.Normal.z, FI1.Normal.z, FI2.Normal.z);

			glm::vec3 PUVS = M * glm::vec3(FI0.TexCoords.s, FI1.TexCoords.s, FI2.TexCoords.s);
			glm::vec3 PUVT = M * glm::vec3(FI0.TexCoords.t, FI1.TexCoords.t, FI2.TexCoords.t);

			for(int Y = 0; Y < ScreenHeight; Y++)
			{
				for(int X = 0; X < ScreenWidth; X++)
				{
					glm::vec3 SampleLocation = {X + 0.5f, Y + 0.5f, 1.0f};

					bool Inside0 = EdgeFunction(E0, SampleLocation);
					bool Inside1 = EdgeFunction(E1, SampleLocation);
					bool Inside2 = EdgeFunction(E2, SampleLocation);

					if(Inside0 && Inside1 && Inside2)
					{
						float OneOverW = (C.x * SampleLocation.x) + (C.y * SampleLocation.y) + C.z;
						float W = 1.0f / OneOverW; 

						float ZOverW = (Z.x * SampleLocation.x) + (Z.y * SampleLocation.y) + Z.z;
						float Z = ZOverW * W;

						if(Z <= DepthBuffer[Y*ScreenWidth + X])
						{
							DepthBuffer[Y*ScreenWidth + X] = Z;							

							float nxOverW = (PNX.x * SampleLocation.x) + (PNX.y * SampleLocation.y) + PNX.z;
							float nyOverW = (PNY.x * SampleLocation.x) + (PNY.y * SampleLocation.y) + PNY.z;
							float nzOverW = (PNZ.x * SampleLocation.x) + (PNZ.y * SampleLocation.y) + PNZ.z;
							glm::vec3 Normal = glm::vec3(nxOverW, nyOverW, nzOverW) * W;

							float uOverW = (PUVS.x * SampleLocation.x) + (PUVS.y * SampleLocation.y) + PUVS.z;
							float vOverW = (PUVT.x * SampleLocation.x) + (PUVT.y * SampleLocation.y) + PUVT.z;
							glm::vec2 TextureCoords = glm::vec2(uOverW, vOverW) * W;

							fragment_input FSInput = {Normal, TextureCoords};
							glm::vec3 OutputColor = FS(FSInput, pTexture);

							FrameBuffer[Y*ScreenWidth + X] = OutputColor;
						}
					}
				}
			}
		}	
	}

	OutputFrame(FrameBuffer, "../render_output.ppm");

	return(0);
}