#include <string>
#include <vector>
#include <cstdint>

#include "glm/vec3.hpp"
#include "glm/vec4.hpp"
#include "glm/mat4x4.hpp"
#include "glm/gtc/matrix_transform.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define TINYOBJLOADER_IMPLEMENTATION
#include "tiny_obj_loader.h"

#define Assert(Expression) if(!(Expression)) { *(int *)0 = 0; }

struct vertex_input
{
	glm::vec3 Pos;
	glm::vec3 Normal;
	glm::vec2 TexCoords;
};

struct fragment_input
{
	glm::vec3 Normal;
	glm::vec2 TexCoords;
};

struct texture
{
	stbi_uc* m_Data = 0;
	int32_t m_Width = -1;
	int32_t m_Height = -1;
	int32_t m_NumChannels = -1;
};

struct Mesh
{
	// Offset into the global index buffer
	uint32_t MIdxOffset = 0;

	// How many indices this mesh contains. Number of triangles therefore equals (m_IdxCount / 3)
	uint32_t MIdxCount = 0;

	// Texture map from material
	std::string MDiffuseTexName;
};


static void
InitializeSceneObjects(char* Filename, std::vector<Mesh>& MeshBuffer, std::vector<vertex_input>& VertexBuffer, 
					   std::vector<uint32_t>& IndexBuffer, std::map<std::string, texture*>& Textures)
{
	tinyobj::attrib_t attribs;
	std::vector<tinyobj::shape_t> shapes;
	std::vector<tinyobj::material_t> materials;
	std::string err = "";
	std::string warn = "";

	bool ret = tinyobj::LoadObj(&attribs, &shapes, &materials, &warn, &err, Filename, "../assets/", true /*triangulate*/);
	if (ret)
	{
		// Process materials to load images
		{
			for (unsigned i = 0; i < materials.size(); i++)
			{
				const tinyobj::material_t& m = materials[i];

				std::string diffuseTexName = m.diffuse_texname;
				//Assert(!diffuseTexName.empty() && "Mesh missing texture!");

				if (Textures.find(diffuseTexName) == Textures.end())
				{
					texture* pAlbedo = new texture();

					pAlbedo->m_Data = stbi_load(("../assets/" + diffuseTexName).c_str(), &pAlbedo->m_Width, &pAlbedo->m_Height, &pAlbedo->m_NumChannels, 0);
					//Assert(pAlbedo->m_Data != nullptr && "Failed to load image!");

					Textures[diffuseTexName] = pAlbedo;
				}
			}
		}

		// Process vertices
		{
			// POD of indices of vertex data provided by tinyobjloader, used to map unique vertex data to indexed primitive
			struct IndexedPrimitive
			{
				uint32_t PosIdx;
				uint32_t NormalIdx;
				uint32_t UVIdx;

				bool operator<(const IndexedPrimitive& other) const
				{
					return memcmp(this, &other, sizeof(IndexedPrimitive)) > 0;
				}
			};

			std::map<IndexedPrimitive, uint32_t> indexedPrims;
			for (size_t s = 0; s < shapes.size(); s++)
			{
				const tinyobj::shape_t& shape = shapes[s];

				uint32_t meshIdxBase = IndexBuffer.size();
				for (size_t i = 0; i < shape.mesh.indices.size(); i++)
				{
					auto index = shape.mesh.indices[i];

					// Fetch indices to construct an IndexedPrimitive to first look up existing unique vertices
					int vtxIdx = index.vertex_index;
					Assert(vtxIdx != -1);

					bool hasNormals = index.normal_index != -1;
					bool hasUV = index.texcoord_index != -1;

					int normalIdx = index.normal_index;
					int uvIdx = index.texcoord_index;

					IndexedPrimitive prim;
					prim.PosIdx = vtxIdx;
					prim.NormalIdx = hasNormals ? normalIdx : UINT32_MAX;
					prim.UVIdx = hasUV ? uvIdx : UINT32_MAX;

					auto res = indexedPrims.find(prim);
					if (res != indexedPrims.end())
					{
						// Vertex is already defined in terms of POS/NORMAL/UV indices, just append index data to index buffer
						IndexBuffer.push_back(res->second);
					}
					else
					{
						// New unique vertex found, get vertex data and append it to vertex buffer and update indexed primitives
						auto newIdx = VertexBuffer.size();
						indexedPrims[prim] = newIdx;
						IndexBuffer.push_back(newIdx);

						auto vx = attribs.vertices[3 * index.vertex_index];
						auto vy = attribs.vertices[3 * index.vertex_index + 1];
						auto vz = attribs.vertices[3 * index.vertex_index + 2];

						glm::vec3 pos(vx, vy, vz);

						glm::vec3 normal(0.f);
						if (hasNormals)
						{
							auto nx = attribs.normals[3 * index.normal_index];
							auto ny = attribs.normals[3 * index.normal_index + 1];
							auto nz = attribs.normals[3 * index.normal_index + 2];

							normal.x = nx;
							normal.y = ny;
							normal.z = nz;
						}

						glm::vec2 uv(0.f);
						if (hasUV)
						{
							auto ux = attribs.texcoords[2 * index.texcoord_index];
							auto uy = 1.f - attribs.texcoords[2 * index.texcoord_index + 1];

							uv.s = glm::abs(ux);
							uv.t = glm::abs(uy);
						}

						vertex_input uniqueVertex = { pos, normal, uv };
						VertexBuffer.push_back(uniqueVertex);
					}
				}

				// Push new mesh to be rendered in the scene 
				Mesh mesh;
				mesh.MIdxOffset = meshIdxBase;
				mesh.MIdxCount = shape.mesh.indices.size();

				Assert((shape.mesh.material_ids[0] != -1) && "Mesh missing a material!");
				mesh.MDiffuseTexName = materials[shape.mesh.material_ids[0]].diffuse_texname; // No per-face material but fixed one

				MeshBuffer.push_back(mesh);
			}
		}
	}
	else
	{
		printf("ERROR: %s\n", err.c_str());
		Assert(false && "Failed to load .OBJ file, check file paths!");
	}
}