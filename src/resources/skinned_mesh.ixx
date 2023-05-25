module;

#include <filesystem>
#include <memory>
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
//#include "render_manager.h"

export module SkinnedMesh;

import MDX;
import ResourceManager;
import GPUTexture;
import Shader;
import Hierarchy;
import Camera;
import SkeletalModelInstance;

namespace fs = std::filesystem;

export class SkinnedMesh : public Resource {
  public:
	struct MeshEntry {
		int vertices = 0;
		int indices = 0;
		int base_vertex = 0;
		int base_index = 0;

		int material_id = 0;
		mdx::Extent extent;

		mdx::GeosetAnimation* geoset_anim; // can be nullptr, often
	};

	std::shared_ptr<mdx::MDX> model;

	std::vector<MeshEntry> geosets;
	bool has_mesh; // ToDo remove when added support for meshless
	bool has_transparent_layers;

	GLuint vao;
	GLuint vertex_buffer;
	GLuint uv_buffer;
	GLuint normal_buffer;
	GLuint tangent_buffer;
	GLuint weight_buffer;
	GLuint index_buffer;
	GLuint layer_alpha;

	GLuint instance_ssbo;
	GLuint layer_colors_ssbo;
	GLuint bones_ssbo;
	GLuint bones_ssbo_colored;

	GLuint preskinned_vertex_ssbo;
	GLuint preskinned_tangent_light_direction_ssbo;

	int skip_count = 0;

	fs::path path;
	//	int mesh_id;
	std::vector<std::shared_ptr<GPUTexture>> textures;
	std::vector<glm::mat4> render_jobs;
	std::vector<glm::vec3> render_colors;
	std::vector<const SkeletalModelInstance*> skeletons;
	std::vector<glm::mat4> instance_bone_matrices;
	std::vector<glm::vec4> layer_colors;

	static constexpr const char* name = "SkinnedMesh";

	explicit SkinnedMesh(const fs::path& path, std::optional<std::pair<int, std::string>> replaceable_id_override) {
		if (path.extension() != ".mdx" && path.extension() != ".MDX") {
			throw;
		}

		BinaryReader reader = hierarchy.open_file(path);
		this->path = path;

		size_t vertices = 0;
		size_t indices = 0;
		size_t matrices = 0;

		model = std::make_shared<mdx::MDX>(reader);

		glGenVertexArrays(1, &vao);
		glBindVertexArray(vao);

		has_mesh = model->geosets.size();
		if (!has_mesh) {
			return;
		}

		for (const auto& i : geosets) {
			const auto& layer = model->materials[i.material_id].layers[0];
			if (layer.blend_mode != 0 && layer.blend_mode != 1) {
				has_transparent_layers = true;
				break;
			}
		}

		// Calculate required space
		for (const auto& i : model->geosets) {
			if (i.lod != 0) {
				continue;
			}
			vertices += i.vertices.size();
			indices += i.faces.size();
			matrices += i.matrix_groups.size();
		}

		// Allocate space
		glCreateBuffers(1, &vertex_buffer);
		glNamedBufferData(vertex_buffer, vertices * sizeof(glm::vec4), nullptr, GL_DYNAMIC_DRAW);

		glCreateBuffers(1, &uv_buffer);
		glNamedBufferData(uv_buffer, vertices * sizeof(glm::vec2), nullptr, GL_DYNAMIC_DRAW);

		glCreateBuffers(1, &normal_buffer);
		glNamedBufferData(normal_buffer, vertices * sizeof(glm::vec4), nullptr, GL_DYNAMIC_DRAW);

		glCreateBuffers(1, &tangent_buffer);
		glNamedBufferData(tangent_buffer, vertices * sizeof(glm::vec4), nullptr, GL_DYNAMIC_DRAW);

		glCreateBuffers(1, &weight_buffer);
		glNamedBufferData(weight_buffer, vertices * sizeof(glm::uvec2), nullptr, GL_DYNAMIC_DRAW);

		glCreateBuffers(1, &index_buffer);
		glNamedBufferData(index_buffer, indices * sizeof(uint16_t), nullptr, GL_DYNAMIC_DRAW);

		glCreateBuffers(1, &instance_ssbo);
		glCreateBuffers(1, &layer_colors_ssbo);
		glCreateBuffers(1, &bones_ssbo);
		glCreateBuffers(1, &bones_ssbo_colored);

		glCreateBuffers(1, &preskinned_vertex_ssbo);
		glCreateBuffers(1, &preskinned_tangent_light_direction_ssbo);

		// Buffer Data
		int base_vertex = 0;
		int base_index = 0;

		for (const auto& i : model->geosets) {
			if (i.lod != 0) {
				continue;
			}
			MeshEntry entry;
			entry.vertices = static_cast<int>(i.vertices.size());
			entry.base_vertex = base_vertex;

			entry.indices = static_cast<int>(i.faces.size());
			entry.base_index = base_index;

			entry.material_id = i.material_id;
			entry.geoset_anim = nullptr;
			entry.extent = i.extent;

			geosets.push_back(entry);

			// If the skin vector is empty then the model has SD bone weights and we convert them to the HD skin weights.
			// Technically SD supports infinite bones per vertex, but we limit it to 4 like HD does.
			// This could cause graphical inconsistensies with the game, but after more than 4 bones the contribution per bone is low enough that we don't care
			if (i.skin.empty()) {
				std::vector<glm::u8vec4> groups;
				std::vector<glm::u8vec4> weights;

				int bone_offset = 0;
				for (const auto& group_size : i.matrix_groups) {
					int bone_count = std::min(group_size, 4u);
					glm::uvec4 indices(0);
					glm::uvec4 weightss(0);

					int weight = 255 / bone_count;
					for (int j = 0; j < bone_count; j++) {
						indices[j] = i.matrix_indices[bone_offset + j];
						weightss[j] = weight;
					}

					int remainder = 255 - weight * bone_count;
					weightss[0] += remainder;

					groups.push_back(indices);
					weights.push_back(weightss);
					bone_offset += group_size;
				}

				std::vector<glm::u8vec4> skin_weights;
				skin_weights.reserve(entry.vertices * 2);
				for (const auto& vertex_group : i.vertex_groups) {
					skin_weights.push_back(groups[vertex_group]);
					skin_weights.push_back(weights[vertex_group]);
				}

				glNamedBufferSubData(weight_buffer, base_vertex * sizeof(glm::uvec2), entry.vertices * 8, skin_weights.data());
			} else {
				glNamedBufferSubData(weight_buffer, base_vertex * sizeof(glm::uvec2), entry.vertices * 8, i.skin.data());
			}

			std::vector<glm::vec4> vertices_vec4;
			for (const auto& j : i.vertices) {
				vertices_vec4.push_back(glm::vec4(j, 1.f));
			}

			std::vector<glm::vec4> normals_vec4;
			for (const auto& j : i.normals) {
				normals_vec4.push_back(glm::vec4(j, 1.f));
			}

			glNamedBufferSubData(vertex_buffer, base_vertex * sizeof(glm::vec4), entry.vertices * sizeof(glm::vec4), vertices_vec4.data());
			glNamedBufferSubData(uv_buffer, base_vertex * sizeof(glm::vec2), entry.vertices * sizeof(glm::vec2), i.texture_coordinate_sets.front().data());
			glNamedBufferSubData(normal_buffer, base_vertex * sizeof(glm::vec4), entry.vertices * sizeof(glm::vec4), normals_vec4.data());
			glNamedBufferSubData(tangent_buffer, base_vertex * sizeof(glm::vec4), entry.vertices * sizeof(glm::vec4), i.tangents.data());
			glNamedBufferSubData(index_buffer, base_index * sizeof(uint16_t), entry.indices * sizeof(uint16_t), i.faces.data());

			base_vertex += entry.vertices;
			base_index += entry.indices;
		}

		for (auto& i : geosets) {
			skip_count += model->materials[i.material_id].layers.size();
		}

		// animations geoset ids > geosets
		for (auto& i : model->animations) {
			if (i.geoset_id >= 0 && i.geoset_id < geosets.size()) {
				geosets[i.geoset_id].geoset_anim = &i;
			}
		}

		for (size_t i = 0; i < model->textures.size(); i++) {
			const mdx::Texture& texture = model->textures[i];

			if (texture.replaceable_id != 0) {
				// Figure out if this is an HD texture
				// Unfortunately replaceable ID textures don't have any additional information on whether they are diffuse/normal/orm
				// So we take a guess using the index
				std::string suffix("");
				bool found = false;
				for (const auto& material : model->materials) {
					for (const auto& layer : material.layers) {
						for (size_t j = 0; j < layer.texturess.size(); j++) {
							if (layer.texturess[j].id != i) {
								continue;
							}

							found = true;

							if (layer.hd) {
								switch (j) {
									case 0:
										suffix = "_diffuse";
										break;
									case 1:
										suffix = "_normal";
										break;
									case 2:
										suffix = "_orm";
										break;
									case 3:
										suffix = "_emmisive";
										break;
								}
							}
							break;
						}
						if (found) {
							break;
						}
					}
					if (found) {
						break;
					}
				}

				if (replaceable_id_override && texture.replaceable_id == replaceable_id_override->first) {
					textures.push_back(resource_manager.load<GPUTexture>(replaceable_id_override->second + suffix, std::to_string(texture.flags)));
				} else {
					textures.push_back(resource_manager.load<GPUTexture>(mdx::replacable_id_to_texture.at(texture.replaceable_id) + suffix, std::to_string(texture.flags)));
				}
			} else {
				textures.push_back(resource_manager.load<GPUTexture>(texture.file_name, std::to_string(texture.flags)));
			}
			glTextureParameteri(textures.back()->id, GL_TEXTURE_WRAP_S, texture.flags & 1 ? GL_REPEAT : GL_CLAMP_TO_EDGE);
			glTextureParameteri(textures.back()->id, GL_TEXTURE_WRAP_T, texture.flags & 2 ? GL_REPEAT : GL_CLAMP_TO_EDGE);
		}

		// glVertexArrayAttribFormat(vao, 0, 3, GL_FLOAT, GL_FALSE, 0);
		// glVertexArrayAttribFormat(vao, 1, 2, GL_FLOAT, GL_FALSE, 0);
		// glVertexArrayAttribFormat(vao, 2, 3, GL_FLOAT, GL_FALSE, 0);
		// glVertexArrayAttribFormat(vao, 3, 4, GL_FLOAT, GL_FALSE, 0);
		// glVertexArrayAttribIFormat(vao, 4, 2, GL_UNSIGNED_INT, 0);

		// glVertexArrayAttribBinding(vao, 0, 0);
		// glVertexArrayAttribBinding(vao, 1, 1);
		// glVertexArrayAttribBinding(vao, 2, 2);
		// glVertexArrayAttribBinding(vao, 3, 3);
		// glVertexArrayAttribBinding(vao, 4, 4);
		//
		// glVertexArrayVertexBuffer(vao, 0, vertex_buffer, 0, 0);
		// glVertexArrayVertexBuffer(vao, 1, uv_buffer, 0, 0);
		// glVertexArrayVertexBuffer(vao, 2, normal_buffer, 0, 0);
		// glVertexArrayVertexBuffer(vao, 3, tangent_buffer, 0, 0);
		// glVertexArrayVertexBuffer(vao, 4, weight_buffer, 0, 0);

		glEnableVertexArrayAttrib(vao, 0);
		glEnableVertexArrayAttrib(vao, 1);
		glEnableVertexArrayAttrib(vao, 2);
		glEnableVertexArrayAttrib(vao, 3);
		glEnableVertexArrayAttrib(vao, 4);

		glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer);
		glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 0, nullptr);

		glBindBuffer(GL_ARRAY_BUFFER, uv_buffer);
		glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, nullptr);

		glBindBuffer(GL_ARRAY_BUFFER, normal_buffer);
		glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, 0, nullptr);

		glBindBuffer(GL_ARRAY_BUFFER, tangent_buffer);
		glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, 0, nullptr);

		glBindBuffer(GL_ARRAY_BUFFER, weight_buffer);
		glVertexAttribIPointer(4, 2, GL_UNSIGNED_INT, 0, nullptr);

		glVertexArrayElementBuffer(vao, index_buffer);
	}

	~SkinnedMesh() {
		glDeleteBuffers(1, &vertex_buffer);
		glDeleteBuffers(1, &uv_buffer);
		glDeleteBuffers(1, &normal_buffer);
		glDeleteBuffers(1, &tangent_buffer);
		glDeleteBuffers(1, &weight_buffer);
		glDeleteBuffers(1, &index_buffer);
		glDeleteBuffers(1, &layer_alpha);
		glDeleteBuffers(1, &layer_colors_ssbo);
		glDeleteBuffers(1, &instance_ssbo);
		glDeleteBuffers(1, &bones_ssbo);
		glDeleteBuffers(1, &bones_ssbo_colored);

		glDeleteBuffers(1, &preskinned_vertex_ssbo);
		glDeleteBuffers(1, &preskinned_tangent_light_direction_ssbo);
	}

	//void render_queue(RenderManager& render_manager, const SkeletalModelInstance& skeleton, glm::vec3 color) {
	//	mdx::Extent& extent = model->sequences[skeleton.sequence_index].extent;
	//	if (!camera.inside_frustrum(skeleton.matrix * glm::vec4(extent.minimum, 1.f), skeleton.matrix * glm::vec4(extent.maximum, 1.f))) {
	//		return;
	//	}

	//	render_jobs.push_back(skeleton.matrix);
	//	render_colors.push_back(color);
	//	skeletons.push_back(&skeleton);

	//	// Register for opaque drawing
	//	if (render_jobs.size() == 1) {
	//		render_manager.skinned_meshes.push_back(this);
	//	}

	//	// Register for transparent drawing
	//	// If the mesh contains transparent parts then those need to be sorted and drawn on top/after all the opaque parts
	//	if (!has_mesh) {
	//		return;
	//	}

	//	if (has_transparent_layers) {
	//		RenderManager::SkinnedInstance t{
	//			.mesh = this,
	//			.instance_id = static_cast<int>(render_jobs.size() - 1),
	//			.distance = glm::distance(camera.position - camera.direction * camera.distance, glm::vec3(skeleton.matrix[3]))
	//		};

	//		render_manager.skinned_transparent_instances.push_back(t);
	//	}
	//}

	void upload_render_data() {
		if (!has_mesh) {
			return;
		}

		glNamedBufferData(instance_ssbo, render_jobs.size() * sizeof(glm::mat4), render_jobs.data(), GL_DYNAMIC_DRAW);

		for (int i = 0; i < render_jobs.size(); i++) {
			instance_bone_matrices.insert(instance_bone_matrices.end(), skeletons[i]->world_matrices.begin(), skeletons[i]->world_matrices.begin() + model->bones.size());
		}

		glNamedBufferData(bones_ssbo, instance_bone_matrices.size() * sizeof(glm::mat4), instance_bone_matrices.data(), GL_DYNAMIC_DRAW);

		layer_colors.clear();

		for (size_t k = 0; k < render_jobs.size(); k++) {
			for (const auto& i : geosets) {
				glm::vec3 geoset_color = render_colors[k];
				float geoset_anim_visibility = 1.0f;
				if (i.geoset_anim && skeletons[k]->sequence_index >= 0) {
					geoset_color *= skeletons[k]->get_geoset_animation_color(*i.geoset_anim);
					geoset_anim_visibility = skeletons[k]->get_geoset_animation_visiblity(*i.geoset_anim);
				}

				const auto& layers = model->materials[i.material_id].layers;
				for (auto& j : layers) {
					float layer_visibility = 1.0f;
					if (skeletons[k]->sequence_index >= 0) {
						layer_visibility = skeletons[k]->get_layer_visiblity(j);
					}
					layer_colors.push_back(glm::vec4(geoset_color, layer_visibility * geoset_anim_visibility));
				}
			}
		}

		glNamedBufferData(layer_colors_ssbo, layer_colors.size() * sizeof(glm::vec4), layer_colors.data(), GL_DYNAMIC_DRAW);

		size_t total_indices = 0;
		for (const auto& i : geosets) {
			total_indices += i.indices;
		}
		glNamedBufferData(preskinned_vertex_ssbo, total_indices * sizeof(glm::vec4) * render_jobs.size(), nullptr, GL_DYNAMIC_DRAW);
		glNamedBufferData(preskinned_tangent_light_direction_ssbo, total_indices * sizeof(glm::vec4) * render_jobs.size(), nullptr, GL_DYNAMIC_DRAW);
	}

	// Render all geometry and save the resulting vertices in a buffer
	void preskin_geometry() {
		if (!has_mesh) {
			return;
		}

		size_t instance_vertex_count = 0;
		for (const auto& i : geosets) {
			instance_vertex_count += i.indices;
		}

		glBindVertexArray(vao);

		glUniform1ui(1, render_jobs.size());
		glUniform1ui(2, instance_vertex_count);
		glUniform1ui(3, model->bones.size());

		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, instance_ssbo);
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, bones_ssbo);
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, vertex_buffer);
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, normal_buffer);
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 5, tangent_buffer);
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 6, weight_buffer);
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 7, preskinned_vertex_ssbo);
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 8, preskinned_tangent_light_direction_ssbo);

		glDispatchCompute(((instance_vertex_count + 63) / 64) * render_jobs.size(), 1, 1);
		glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

		// std::vector<glm::vec4> ssbo;
		// ssbo.resize(instance_vertex_count);
		// glGetNamedBufferSubData(preskinned_vertex_ssbo, 0, instance_vertex_count * sizeof(glm::vec4), ssbo.data());

		// for (const auto& i : ssbo) {
		//	std::cout << i.x << " " << i.y << " " << i.z << "\n";
		// }
		// std::cout << "\n";
	}

	void render_opaque(bool render_hd) {
		if (!has_mesh) {
			return;
		}

		glBindVertexArray(vao);

		size_t instance_vertex_count = 0;
		for (const auto& i : geosets) {
			instance_vertex_count += i.indices;
		}

		glUniform1i(4, skip_count);
		glUniform1ui(6, instance_vertex_count);

		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, layer_colors_ssbo);
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, preskinned_vertex_ssbo);
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, preskinned_tangent_light_direction_ssbo);

		int lay_index = 0;
		for (const auto& i : geosets) {
			const auto& layers = model->materials[i.material_id].layers;

			if (layers[0].blend_mode != 0 && layers[0].blend_mode != 1) {
				lay_index += layers.size();
				continue;
			}

			for (const auto& j : layers) {
				if (j.hd != render_hd) {
					lay_index += 1;
					continue;
				}

				glUniform1f(1, j.blend_mode == 1 ? 0.75f : -1.f);
				glUniform1i(5, lay_index);

				switch (j.blend_mode) {
					case 0:
					case 1:
						glBlendFunc(GL_ONE, GL_ZERO);
						break;
					case 2:
						glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
						break;
					case 3:
						glBlendFunc(GL_ONE, GL_ONE);
						break;
					case 4:
						glBlendFunc(GL_SRC_ALPHA, GL_ONE);
						break;
					case 5:
						glBlendFunc(GL_ZERO, GL_SRC_COLOR);
						break;
					case 6:
						glBlendFunc(GL_DST_COLOR, GL_SRC_COLOR);
						break;
				}

				if (j.shading_flags & 0x10) {
					glDisable(GL_CULL_FACE);
				} else {
					glEnable(GL_CULL_FACE);
				}

				if (j.shading_flags & 0x40) {
					glDisable(GL_DEPTH_TEST);
				} else {
					glEnable(GL_DEPTH_TEST);
				}

				if (j.shading_flags & 0x80) {
					glDepthMask(false);
				} else {
					glDepthMask(true);
				}

				for (size_t texture_slot = 0; texture_slot < j.texturess.size(); texture_slot++) {
					glBindTextureUnit(texture_slot, textures[j.texturess[texture_slot].id]->id);
				}

				glDrawElementsInstancedBaseVertex(GL_TRIANGLES, i.indices, GL_UNSIGNED_SHORT, reinterpret_cast<void*>(i.base_index * sizeof(uint16_t)), render_jobs.size(), i.base_vertex);
				lay_index += 1;
			}
		}
	}

	void render_transparent(int instance_id, bool render_hd) {
		if (!has_mesh) {
			return;
		}

		glBindVertexArray(vao);

		glm::mat4 MVP = camera.projection_view * render_jobs[instance_id];
		glUniformMatrix4fv(0, 1, false, &MVP[0][0]);
		if (render_hd) {
			glUniformMatrix4fv(5, 1, false, &render_jobs[instance_id][0][0]);
		}

		glUniform1i(3, model->bones.size());
		glUniform1i(4, instance_id);
		glUniform1i(6, skip_count);

		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, layer_colors_ssbo);
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, bones_ssbo);

		int lay_index = 0;
		for (const auto& i : geosets) {
			const auto& layers = model->materials[i.material_id].layers;

			if (layers[0].blend_mode == 0 || layers[0].blend_mode == 1) {
				lay_index += layers.size();
				continue;
			}

			for (auto& j : layers) {
				// We don't have to render fully transparent meshes
				if (layer_colors[instance_id * skip_count + lay_index].a <= 0.01f) {
					lay_index += 1;
					continue;
				}

				if (j.hd != render_hd) {
					lay_index += 1;
					continue;
				}

				glUniform1i(7, lay_index);

				switch (j.blend_mode) {
					case 2:
						glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
						break;
					case 3:
						glBlendFunc(GL_ONE, GL_ONE);
						break;
					case 4:
						glBlendFunc(GL_SRC_ALPHA, GL_ONE);
						break;
					case 5:
						glBlendFunc(GL_ZERO, GL_SRC_COLOR);
						break;
					case 6:
						glBlendFunc(GL_DST_COLOR, GL_SRC_COLOR);
						break;
				}

				if (j.shading_flags & 0x10) {
					glDisable(GL_CULL_FACE);
				} else {
					glEnable(GL_CULL_FACE);
				}

				if (j.shading_flags & 0x40) {
					glDisable(GL_DEPTH_TEST);
				} else {
					glEnable(GL_DEPTH_TEST);
				}

				for (size_t texture_slot = 0; texture_slot < j.texturess.size(); texture_slot++) {
					glBindTextureUnit(texture_slot, textures[j.texturess[texture_slot].id]->id);
				}

				glDrawElementsBaseVertex(GL_TRIANGLES, i.indices, GL_UNSIGNED_SHORT, reinterpret_cast<void*>(i.base_index * sizeof(uint16_t)), i.base_vertex);
				lay_index += 1;
			}
		}
	}

	void render_color_coded(const SkeletalModelInstance& skeleton, int id) {
		if (!has_mesh) {
			return;
		}

		glBindVertexArray(vao);

		glm::mat4 MVP = camera.projection_view * skeleton.matrix;
		glUniformMatrix4fv(0, 1, false, &MVP[0][0]);

		glUniform1i(3, model->bones.size());
		glUniform1i(7, id);

		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, bones_ssbo_colored);
		glNamedBufferData(bones_ssbo_colored, model->bones.size() * sizeof(glm::mat4), &skeleton.world_matrices[0][0][0], GL_DYNAMIC_DRAW);

		for (const auto& i : geosets) {
			glm::vec3 geoset_color(1.0f);
			float geoset_anim_visibility = 1.0f;
			if (i.geoset_anim && skeleton.sequence_index >= 0) {
				geoset_color = skeleton.get_geoset_animation_color(*i.geoset_anim);
				geoset_anim_visibility = skeleton.get_geoset_animation_visiblity(*i.geoset_anim);
			}

			for (auto& j : model->materials[i.material_id].layers) {
				float layer_visibility = 1.0f;
				if (skeleton.sequence_index >= 0) {
					layer_visibility = skeleton.get_layer_visiblity(j);
				}

				float final_visibility = layer_visibility * geoset_anim_visibility;
				if (final_visibility <= 0.001f) {
					continue;
				}

				glUniform3f(4, geoset_color.x, geoset_color.y, geoset_color.z);
				glUniform1f(5, final_visibility);

				// if (j.blend_mode == 0) {
				//	glUniform1f(1, -1.f);
				// } else if (j.blend_mode == 1) {
				//	glUniform1f(1, 0.75f);
				// }

				if (j.shading_flags & 0x40) {
					glDisable(GL_DEPTH_TEST);
				} else {
					glEnable(GL_DEPTH_TEST);
				}

				if (j.shading_flags & 0x80) {
					glDepthMask(false);
				} else {
					glDepthMask(true);
				}

				if (j.shading_flags & 0x10) {
					glDisable(GL_CULL_FACE);
				} else {
					glEnable(GL_CULL_FACE);
				}

				glDrawElementsBaseVertex(GL_TRIANGLES, i.indices, GL_UNSIGNED_SHORT, reinterpret_cast<void*>(i.base_index * sizeof(uint16_t)), i.base_vertex);
				break;
			}
		}
	}
};