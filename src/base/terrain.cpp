#include <set>
#include <bitset>
#include <iostream>

#include "Terrain.h"

#include "globals.h"
#include <map_global.h>
#include <glad/glad.h>

import Hierarchy;
import Camera;
import BinaryReader;
import BinaryWriter;
import OpenGLUtilities;
import Physics;
import SLK;

using namespace std::literals::string_literals;

float Corner::final_ground_height() const {
	return height + layer_height - 2.0;
}

float Corner::final_water_height() const {
	return water_height + map->terrain.water_offset;
}

Terrain::~Terrain() {
	glDeleteTextures(1, &ground_height);
	glDeleteTextures(1, &ground_corner_height);
	glDeleteTextures(1, &ground_texture_data);
	glDeleteTextures(1, &ground_exists);
	glDeleteTextures(1, &cliff_texture_array);

	glDeleteTextures(1, &water_texture_array);
	glDeleteTextures(1, &water_exists);
	glDeleteTextures(1, &water_height);

	//map->physics.dynamicsWorld->removeRigidBody(collision_body);
	//delete collision_body;
	//delete collision_shape;
}

bool Terrain::load() {
	BinaryReader reader = hierarchy.map_file_read("war3map.w3e");

	const std::string magic_number = reader.read_string(4);
	if (magic_number != "W3E!") {
		std::cout << "Invalid war3map.w3e file: Magic number is not W3E!" << std::endl;
		return false;
	}
	
	reader.advance(4); // Version

	tileset = reader.read<char>();
	reader.advance(4); // Custom tileset

	const uint32_t tileset_textures = reader.read<uint32_t>();
	for (size_t i = 0; i < tileset_textures; i++) {
		tileset_ids.push_back(reader.read_string(4));
	}

	const int cliffset_textures = reader.read<uint32_t>();
	for (int i = 0; i < cliffset_textures; i++) {
		cliffset_ids.push_back(reader.read_string(4));
	}

	width = reader.read<uint32_t>();
	height = reader.read<uint32_t>();

	offset = reader.read<glm::vec2>();

	// Parse all tilepoints
	corners.resize(width, std::vector<Corner>(height));
	for (int j = 0; j < height; j++) {
		for (int i = 0; i < width; i++) {
			Corner& corner = corners[i][j];

			corners[i][j].height = (reader.read<uint16_t>() - 8192.f) / 512.f;

			const uint16_t water_and_edge = reader.read<uint16_t>();
			corners[i][j].water_height = ((water_and_edge & 0x3FFF) - 8192.f) / 512.f;
			corner.map_edge = water_and_edge & 0x4000;

			const uint8_t texture_and_flags = reader.read<uint8_t>();
			corner.ground_texture = texture_and_flags & 0b00001111;

			corner.ramp = texture_and_flags & 0b00010000;
			corner.blight = texture_and_flags & 0b00100000;
			corner.water = texture_and_flags & 0b01000000;
			corner.boundary = texture_and_flags & 0b10000000;

			const uint8_t variation = reader.read<uint8_t>();
			corner.ground_variation = variation & 0b00011111;
			corner.cliff_variation = (variation & 0b11100000) >> 5;

			const uint8_t misc = reader.read<uint8_t>();
			corner.cliff_texture = (misc & 0b11110000) >> 4;
			corner.layer_height = misc & 0b00001111;
		}
	}

	create();

	return true;
}

void Terrain::create() {
	// Determine if cliff
	for (int i = 0; i < width - 1; i++) {
		for (int j = 0; j < height - 1; j++) {
			Corner& bottom_left = corners[i][j];
			Corner& bottom_right = corners[i + 1][j];
			Corner& top_left = corners[i][j + 1];
			Corner& top_right = corners[i + 1][j + 1];

			bottom_left.cliff = bottom_left.layer_height != bottom_right.layer_height
				|| bottom_left.layer_height != top_left.layer_height
				|| bottom_left.layer_height != top_right.layer_height;
		}
	}
	// Done parsing

	hierarchy.tileset = tileset;

	terrain_slk.load("TerrainArt/Terrain.slk");
	cliff_slk.load("TerrainArt/CliffTypes.slk");
	const slk::SLK water_slk("TerrainArt/Water.slk");

	// Water Textures and Colours

	water_offset = water_slk.data<float>("height", tileset + "Sha"s);
	water_textures_nr = water_slk.data<int>("numtex", tileset + "Sha"s);
	animation_rate = water_slk.data<int>("texrate", tileset + "Sha"s);

	int red = water_slk.data<int>("smin_r", tileset + "Sha"s);
	int green = water_slk.data<int>("smin_g", tileset + "Sha"s);
	int blue = water_slk.data<int>("smin_b", tileset + "Sha"s);
	int alpha = water_slk.data<int>("smin_a", tileset + "Sha"s);

	shallow_color_min = { red, green, blue, alpha };
	shallow_color_min /= 255.f;

	red = water_slk.data<int>("smax_r", tileset + "Sha"s);
	green = water_slk.data<int>("smax_g", tileset + "Sha"s);
	blue = water_slk.data<int>("smax_b", tileset + "Sha"s);
	alpha = water_slk.data<int>("smax_a", tileset + "Sha"s);

	shallow_color_max = { red, green, blue, alpha };
	shallow_color_max /= 255.f;

	red = water_slk.data<int>("dmin_r", tileset + "Sha"s);
	green = water_slk.data<int>("dmin_g", tileset + "Sha"s);
	blue = water_slk.data<int>("dmin_b", tileset + "Sha"s);
	alpha = water_slk.data<int>("dmin_a", tileset + "Sha"s);

	deep_color_min = { red, green, blue, alpha };
	deep_color_min /= 255.f;

	red = water_slk.data<int>("dmax_r", tileset + "Sha"s);
	green = water_slk.data<int>("dmax_g", tileset + "Sha"s);
	blue = water_slk.data<int>("dmax_b", tileset + "Sha"s);
	alpha = water_slk.data<int>("dmax_a", tileset + "Sha"s);

	deep_color_max = { red, green, blue, alpha };
	deep_color_max /= 255.f;

	// Cliff Meshes
	slk::SLK cliffs_variation_slk("Data/Warcraft/Cliffs.slk", true);
	for (size_t i = 0; i < cliffs_variation_slk.rows(); i++) {
		for (int j = 0; j < cliffs_variation_slk.data<int>("variations", i) + 1; j++) {
			std::string file_name = "Doodads/Terrain/Cliffs/Cliffs" + cliffs_variation_slk.index_to_row.at(i) + std::to_string(j) + ".mdx";
			cliff_meshes.push_back(resource_manager.load<CliffMesh>(file_name));
			path_to_cliff.emplace(cliffs_variation_slk.index_to_row.at(i) + std::to_string(j), static_cast<int>(cliff_meshes.size()) - 1);
		}
		cliff_variations.emplace(cliffs_variation_slk.index_to_row.at(i), cliffs_variation_slk.data<int>("variations", i));
	}

	// Ground textures
	for (const auto& tile_id : tileset_ids) {
		ground_textures.push_back(resource_manager.load<GroundTexture>(terrain_slk.data("dir", tile_id) + "/" + terrain_slk.data("file", tile_id)));
		ground_texture_to_id.emplace(tile_id, static_cast<int>(ground_textures.size() - 1));
	}
	blight_texture = static_cast<int>(ground_textures.size());
	ground_texture_to_id.emplace("blight", blight_texture);
	ground_textures.push_back(resource_manager.load<GroundTexture>(world_edit_data.data("TileSets", std::string(1, tileset), 1)));

	// Cliff Textures
	for (auto&& cliff_id : cliffset_ids) {
		cliff_textures.push_back(resource_manager.load<Texture>(cliff_slk.data("texdir", cliff_id) + "/" + cliff_slk.data("texfile", cliff_id)));
		cliff_texture_size = std::max(cliff_texture_size, cliff_textures.back()->width);
		cliff_to_ground_texture.push_back(ground_texture_to_id[cliff_slk.data("groundtile", cliff_id)]);
	}

	// prepare GPU data
	ground_heights.resize(width * height);
	ground_corner_heights.resize(width * height);
	ground_texture_list.resize((width - 1) * (height - 1));
	ground_exists_data.resize(width * height);

	water_heights.resize(width * height);
	water_exists_data.resize(width * height);

	for (int i = 0; i < width; i++) {
		for (int j = 0; j < height; j++) {
			ground_corner_heights[j * width + i] = corners[i][j].final_ground_height();
			water_exists_data[j * width + i] = corners[i][j].water;
			ground_heights[j * width + i] = corners[i][j].height;
			water_heights[j * width + i] = corners[i][j].water_height;
		}
	}

	// Ground
	glCreateTextures(GL_TEXTURE_2D, 1, &ground_height);
	glTextureStorage2D(ground_height, 1, GL_R16F, width, height);
	glTextureSubImage2D(ground_height, 0, 0, 0, width, height, GL_RED, GL_FLOAT, ground_heights.data());
	glTextureParameteri(ground_height, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTextureParameteri(ground_height, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	glCreateTextures(GL_TEXTURE_2D, 1, &ground_corner_height);
	glTextureStorage2D(ground_corner_height, 1, GL_R16F, width, height);
	glTextureSubImage2D(ground_corner_height, 0, 0, 0, width, height, GL_RED, GL_FLOAT, ground_corner_heights.data());
	glTextureParameteri(ground_corner_height, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTextureParameteri(ground_corner_height, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	// Cliff
	glCreateTextures(GL_TEXTURE_2D_ARRAY, 1, &cliff_texture_array);
	glTextureStorage3D(cliff_texture_array, log2(cliff_texture_size) + 1, GL_RGBA8, cliff_texture_size, cliff_texture_size, cliff_textures.size());
	glTextureParameteri(cliff_texture_array, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	int sub = 0;
	for (const auto& i : cliff_textures) {
		glTextureSubImage3D(cliff_texture_array, 0, 0, 0, sub, i->width, i->height, 1, i->channels == 4 ? GL_RGBA : GL_RGB, GL_UNSIGNED_BYTE, i->data.data());
		sub += 1;
	}
	glGenerateTextureMipmap(cliff_texture_array);

	glCreateTextures(GL_TEXTURE_2D, 1, &ground_texture_data);
	glTextureStorage2D(ground_texture_data, 1, GL_RGBA16UI, width - 1, height - 1);
	glTextureParameteri(ground_texture_data, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTextureParameteri(ground_texture_data, GL_TEXTURE_MIN_FILTER, GL_NEAREST);

	glCreateTextures(GL_TEXTURE_2D, 1, &ground_exists);
	glTextureStorage2D(ground_exists, 1, GL_R8, width, height);

	// Water
	glCreateTextures(GL_TEXTURE_2D, 1, &water_height);
	glTextureStorage2D(water_height, 1, GL_R16F, width, height);
	glTextureSubImage2D(water_height, 0, 0, 0, width, height, GL_RED, GL_FLOAT, water_heights.data());

	glCreateTextures(GL_TEXTURE_2D, 1, &water_exists);
	glTextureStorage2D(water_exists, 1, GL_R8, width, height);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glTextureSubImage2D(water_exists, 0, 0, 0, width, height, GL_RED, GL_UNSIGNED_BYTE, water_exists_data.data());
	glPixelStorei(GL_UNPACK_ALIGNMENT, 4);

	// Water textures
	glCreateTextures(GL_TEXTURE_2D_ARRAY, 1, &water_texture_array);
	glTextureStorage3D(water_texture_array, std::log(128) + 1, GL_RGBA8, 128, 128, water_textures_nr);
	glTextureParameteri(water_texture_array, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTextureParameteri(water_texture_array, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	const std::string file_name = water_slk.data("texfile", tileset + "Sha"s);
	for (int i = 0; i < water_textures_nr; i++) {
		const auto texture = resource_manager.load<Texture>(file_name + (i < 10 ? "0" : "") + std::to_string(i));

		if (texture->width != 128 || texture->height != 128) {
			std::cout << "Odd water texture size detected of " << texture->width << " wide and " << texture->height << " high\n";
		}
		glTextureSubImage3D(water_texture_array, 0, 0, 0, i, texture->width, texture->height, 1, GL_RGB, GL_UNSIGNED_BYTE, texture->data.data());
	}
	glGenerateTextureMipmap(water_texture_array);

	update_cliff_meshes({ 0, 0, width - 1, height - 1 });
	update_ground_textures({ 0, 0, width - 1, height - 1 });
	update_ground_heights({ 0, 0, width - 1, height - 1 });

	ground_shader = resource_manager.load<Shader>({ "Data/Shaders/terrain.vs", "Data/Shaders/terrain.fs" });
	cliff_shader = resource_manager.load<Shader>({ "Data/Shaders/cliff.vs", "Data/Shaders/cliff.fs" });
	water_shader = resource_manager.load<Shader>({ "Data/Shaders/water.vs", "Data/Shaders/water.fs" });

	collision_shape = new btHeightfieldTerrainShape(width, height, ground_corner_heights.data(), 0, -16.f, 16.f, 2 /*z*/, PHY_FLOAT, false);
	if (collision_shape == nullptr) {
		std::cout << "Error creating Bullet collision shape\n";
	}

	collision_body = new btRigidBody(0, new btDefaultMotionState(), collision_shape);
	collision_body->getWorldTransform().setOrigin(btVector3(width / 2.f - 0.5f, height / 2.f - 0.5f, 0.f)); // Bullet centers the collision mesh automatically, we need to decenter it and place it under the player
	collision_body->setCollisionFlags(collision_body->getCollisionFlags() | btCollisionObject::CF_STATIC_OBJECT);
	map->physics.dynamicsWorld->addRigidBody(collision_body, 32, 32);
	
	emit minimap_changed(minimap_image());
}

void Terrain::save() const {
	BinaryWriter writer;
	writer.write_string("W3E!");
	writer.write(write_version);
	writer.write(tileset);
	writer.write(1);
	writer.write<uint32_t>(tileset_ids.size());
	writer.write_vector(tileset_ids);
	writer.write<uint32_t>(cliffset_ids.size());
	writer.write_vector(cliffset_ids);
	writer.write(width);
	writer.write(height);
	writer.write(offset);

	for (int j = 0; j < height; j++) {
		for (int i = 0; i < width; i++) {
			const Corner& corner = corners[i][j];

			writer.write<uint16_t>(corner.height * 512.f + 8192.f);

			uint16_t water_and_edge = corner.water_height * 512.f + 8192.f;
			water_and_edge += corner.map_edge << 14;
			writer.write(water_and_edge);

			uint8_t texture_and_flags = corner.ground_texture;
			texture_and_flags |= corner.ramp << 4;

			texture_and_flags |= corner.blight << 5;
			texture_and_flags |= corner.water << 6;
			texture_and_flags |= corner.boundary << 7;
			writer.write(texture_and_flags);

			uint8_t variation = corner.ground_variation;
			variation += corner.cliff_variation << 5;
			writer.write(variation);

			uint8_t misc = corner.cliff_texture << 4;
			misc += corner.layer_height;
			writer.write(misc);
		}
	}

	hierarchy.map_file_write("war3map.w3e", writer.buffer);
}

void Terrain::render_ground(bool render_pathing, bool render_lighting) const {
	// Render tiles
	ground_shader->use();

	glDisable(GL_BLEND);
	glEnable(GL_CULL_FACE);

	glUniformMatrix4fv(1, 1, GL_FALSE, &camera.projection_view[0][0]);
	glUniform1i(2, render_pathing);
	glUniform1i(3, render_lighting);
	glUniform3fv(4, 1, &map->light_direction.x);

	glBindTextureUnit(0, ground_height);
	glBindTextureUnit(1, ground_corner_height);
	glBindTextureUnit(2, ground_texture_data);
	glBindTextureUnit(22, ground_exists);

	for (size_t i = 0; i < ground_textures.size(); i++) {
		glBindTextureUnit(3 + i, ground_textures[i]->id);
	}
	glBindTextureUnit(20, map->pathing_map.texture_static);
	glBindTextureUnit(21, map->pathing_map.texture_dynamic);

	glEnableVertexAttribArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, shapes.vertex_buffer);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);

	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, shapes.index_buffer);
	glDrawElementsInstanced(GL_TRIANGLES, shapes.quad_indices.size() * 3, GL_UNSIGNED_INT, nullptr, (width - 1) * (height - 1));

	glDisableVertexAttribArray(0);

	glEnable(GL_BLEND);

	// Render cliffs
	for (const auto& i : cliffs) {
		const Corner& bottom_left = corners[i.x][i.y];
		const Corner& bottom_right = corners[i.x + 1][i.y];
		const Corner& top_left = corners[i.x][i.y + 1];
		const Corner& top_right = corners[i.x + 1][i.y + 1];

		const float min = std::min({ bottom_left.layer_height - 2,	bottom_right.layer_height - 2,
									top_left.layer_height - 2,		top_right.layer_height - 2 });

		if (bottom_left.special_doodad) {
			continue;
		}

		cliff_meshes[i.z]->render_queue({ i.x, i.y, min, bottom_left.cliff_texture });
	}

	cliff_shader->use();

	// WC3 models are 128x too large
	glUniformMatrix4fv(0, 1, GL_FALSE, &camera.projection_view[0][0]);
	glUniform1i(1, render_pathing);
	glUniform1i(2, render_lighting);
	glUniform3fv(3, 1, &map->light_direction.x);

	glBindTextureUnit(0, cliff_texture_array);
	glBindTextureUnit(1, ground_height);
	glBindTextureUnit(2, map->pathing_map.texture_static);
	for (const auto& i : cliff_meshes) {
		i->render();
	}
}

void Terrain::render_water() const {
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glDepthMask(false);

	water_shader->use();

	glUniformMatrix4fv(0, 1, GL_FALSE, &camera.projection_view[0][0]);
	glUniform4fv(1, 1, &shallow_color_min[0]);
	glUniform4fv(2, 1, &shallow_color_max[0]);
	glUniform4fv(3, 1, &deep_color_min[0]);
	glUniform4fv(4, 1, &deep_color_max[0]);
	glUniform1f(5, water_offset);
	glUniform1i(6, current_texture);

	glBindTextureUnit(0, water_height);
	glBindTextureUnit(1, ground_corner_height);
	glBindTextureUnit(2, water_exists);
	glBindTextureUnit(3, water_texture_array);

	glEnableVertexAttribArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, shapes.vertex_buffer);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);

	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, shapes.index_buffer);
	glDrawElementsInstanced(GL_TRIANGLES, shapes.quad_indices.size() * 3, GL_UNSIGNED_INT, nullptr, (width - 1) * (height - 1));

	glDisableVertexAttribArray(0);
	glDepthMask(true);
}

void Terrain::change_tileset(const std::vector<std::string>& new_tileset_ids, std::vector<int> new_to_old) {
	tileset_ids = new_tileset_ids;

	// Blight
	new_to_old.push_back(new_tileset_ids.size());

	// Map old ids to the new ids
	for (auto& i : corners) {
		for (auto& j : i) {
			j.ground_texture = new_to_old[j.ground_texture];
		}
	}

	// Reload tile textures
	ground_textures.clear();	// ToDo Clear them after loading new ones?
	ground_texture_to_id.clear();

	for (const auto& tile_id : tileset_ids) {
		ground_textures.push_back(resource_manager.load<GroundTexture>(terrain_slk.data("dir", tile_id) + "/" + terrain_slk.data("file", tile_id) + (hierarchy.hd ? "_diffuse.dds" : ".dds")));
		ground_texture_to_id.emplace(tile_id, static_cast<int>(ground_textures.size() - 1));
	}
	blight_texture = static_cast<int>(ground_textures.size());
	ground_texture_to_id.emplace("blight", blight_texture);
	ground_textures.push_back(resource_manager.load<GroundTexture>(world_edit_data.data("TileSets", std::string(1, tileset), 1) + (hierarchy.hd ? "_diffuse.dds" : ".dds")));

	cliff_to_ground_texture.clear();
	for (const auto& cliff_id : cliffset_ids) {
		cliff_to_ground_texture.push_back(ground_texture_to_id[cliff_slk.data("groundtile", cliff_id)]);
	}

	update_ground_textures({ 0, 0, width, height });
}

/// The texture of the tilepoint which is influenced by its surroundings. nearby cliff/ramp > blight > regular texture
int Terrain::real_tile_texture(const int x, const int y) const {
	for (int i = -1; i < 1; i++) {
		for (int j = -1; j < 1; j++) {
			if (x + i >= 0 && x + i < width && y + j >= 0 && y + j < height) {
				if (corners[x + i][y + j].cliff) {
					if (x + i < width - 1 && y + j < height - 1) {
						const Corner& bottom_left = corners[x + i][y + j];
						const Corner& bottom_right = corners[x + i + 1][y + j];
						const Corner& top_left = corners[x + i][y + j + 1];
						const Corner& top_right = corners[x + i + 1][y + j + 1];

						if (bottom_left.ramp && top_left.ramp && bottom_right.ramp && top_right.ramp && !bottom_left.romp && !bottom_right.romp && !top_left.romp && !top_right.romp) {
							goto out_of_loop;
						}
					}
				}

				if (corners[x + i][y + j].romp || corners[x + i][y + j].cliff) {
					int texture = corners[x + i][y + j].cliff_texture;
					// Number 15 seems to be something
					if (texture == 15) {
						texture -= 14;
					}

					return cliff_to_ground_texture[texture];
				}
			}
		}
	}
out_of_loop:

	if (corners[x][y].blight) {
		return blight_texture;
	}

	return corners[x][y].ground_texture;
}

/// The subtexture of a groundtexture to use.
int Terrain::get_tile_variation(const int ground_texture, const int variation) const {
	if (ground_textures[ground_texture]->extended) {
		if (variation <= 15) {
			return 16 + variation;
		} else if (variation == 16) {
			return 15;
		} else {
			return 0;
		}
	} else {
		if (variation == 0) {
			return 0;
		} else {
			return 15;
		}
	}
}

/// The 4 ground textures of the tilepoint. First 5 bits are which texture array to use and the next 5 bits are which subtexture to use
glm::u16vec4 Terrain::get_texture_variations(const int x, const int y) const {
	const int bottom_left = real_tile_texture(x, y);
	const int bottom_right = real_tile_texture(x + 1, y);
	const int top_left = real_tile_texture(x, y + 1);
	const int top_right = real_tile_texture(x + 1, y + 1);

	std::set<int> set({ bottom_left, bottom_right, top_left, top_right });
	glm::u16vec4 tiles(17); // 17 is a black transparent texture
	int component = 1;

	tiles.x = *set.begin() + (get_tile_variation(*set.begin(), corners[x][y].ground_variation) << 5);
	set.erase(set.begin());

	std::bitset<4> index;
	for (auto&& texture : set) {
		index[0] = bottom_right == texture;
		index[1] = bottom_left == texture;
		index[2] = top_right == texture;
		index[3] = top_left == texture;

		tiles[component++] = texture + (index.to_ulong() << 5);
	}
	return tiles;
}

float Terrain::interpolated_height(float x, float y) const {
	x = std::clamp(x, 0.f, width - 1.01f);
	y = std::clamp(y, 0.f, height - 1.01f);

	// Biliniear interpolation
	float xx = glm::mix(corners[x][y].final_ground_height(), corners[std::ceil(x)][y].final_ground_height(), x - floor(x));
	float yy = glm::mix(corners[x][std::ceil(y)].final_ground_height(), corners[std::ceil(x)][std::ceil(y)].final_ground_height(), x - floor(x));
	return glm::mix(xx, yy, y - floor(y));
}

bool Terrain::is_corner_ramp_entrance(int x, int y) {
	if (x == width || y == height) {
		return false;
	}

	Corner& bottom_left = corners[x][y];
	Corner& bottom_right = corners[x + 1][y];
	Corner& top_left = corners[x][y + 1];
	Corner& top_right = corners[x + 1][y + 1];

	return bottom_left.ramp && top_left.ramp && bottom_right.ramp && top_right.ramp && !(bottom_left.layer_height == top_right.layer_height && top_left.layer_height == bottom_right.layer_height);
}

/// Constructs a minimap image with tile, cliff, and water colors. Other objects such as doodads will not be added here
Texture Terrain::minimap_image() {
	Texture new_minimap_image;

	new_minimap_image.width = width;
	new_minimap_image.height = height;
	new_minimap_image.channels = 4;
	new_minimap_image.data.resize(width * height * 4);

	for (int j = 0; j < height; j++) {
		for (int i = 0; i < width; i++) {
			glm::vec4 color;

			if (corners[i][j].cliff || (i > 0 && corners[i - 1][j].cliff) || (j > 0 && corners[i][j - 1].cliff) || (i > 0 && j > 0 && corners[i - 1][j - 1].cliff)) {
				color = glm::vec4(128.f, 128.f, 128.f, 255.f);
			} else {
				color = ground_textures[real_tile_texture(i, j)]->minimap_color;
			}

			if (corners[i][j].water && corners[i][j].final_water_height() > corners[i][j].final_ground_height()) {
				if (corners[i][j].final_water_height() - corners[i][j].final_ground_height() > 0.5f) {
					color *= 0.5625f;
					color += glm::vec4(0, 0, 80, 112);
				} else {
					color *= 0.75f;
					color += glm::vec4(0, 0, 48, 64);
				}
			}

			int index = (height - 1 - j) * (width * 4) + i * 4;
			new_minimap_image.data[index + 0] = color.r;
			new_minimap_image.data[index + 1] = color.g;
			new_minimap_image.data[index + 2] = color.b;
			new_minimap_image.data[index + 3] = color.a;

		}
	}

	return new_minimap_image;
}

/// Backups the old corners for a new undo group
void Terrain::new_undo_group() {
	old_corners = corners;
}

/// Adds the undo to the current undo group
void Terrain::add_undo(const QRect& area, undo_type type) {
	auto undo_action = std::make_unique<TerrainGenericAction>();

	undo_action->area = area;
	undo_action->undo_type = type;

	// Copy old corners
	undo_action->old_corners.reserve(area.width() * area.height());
	for (int j = area.top(); j <= area.bottom(); j++) {
		for (int i = area.left(); i <= area.right(); i++) {
			undo_action->old_corners.push_back(old_corners[i][j]);
		}
	}

	// Copy new corners
	undo_action->new_corners.reserve(area.width() * area.height());
	for (int j = area.top(); j <= area.bottom(); j++) {
		for (int i = area.left(); i <= area.right(); i++) {
			undo_action->new_corners.push_back(corners[i][j]);
		}
	}

	map->terrain_undo.add_undo_action(std::move(undo_action));
}

void Terrain::upload_ground_heights() const {
	glTextureSubImage2D(ground_height, 0, 0, 0, width, height, GL_RED, GL_FLOAT, ground_heights.data());
}

void Terrain::upload_corner_heights() const {
	glTextureSubImage2D(ground_corner_height, 0, 0, 0, width, height, GL_RED, GL_FLOAT, ground_corner_heights.data());
}

void Terrain::upload_ground_texture() const {
	glTextureSubImage2D(ground_texture_data, 0, 0, 0, width - 1, height - 1, GL_RGBA_INTEGER, GL_UNSIGNED_SHORT, ground_texture_list.data());
}

void Terrain::upload_ground_exists() const {
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glTextureSubImage2D(ground_exists, 0, 0, 0, width, height, GL_RED, GL_UNSIGNED_BYTE, ground_exists_data.data());
	glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
}

void Terrain::upload_water_exists() const {
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glTextureSubImage2D(water_exists, 0, 0, 0, width, height, GL_RED, GL_UNSIGNED_BYTE, water_exists_data.data());
	glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
}

void Terrain::upload_water_heights() const {
	glTextureSubImage2D(water_height, 0, 0, 0, width, height, GL_RED, GL_FLOAT, water_heights.data());
}

void Terrain::update_ground_heights(const QRect& area) {
	for (int j = area.y(); j < area.y() + area.height(); j++) {
		for (int i = area.x(); i < area.x() + area.width(); i++) {
			ground_heights[j * width + i] = corners[i][j].height; // todo 15.998???

			float ramp_height = 0.f;
			// Check if in one of the configurations the bottom_left is a ramp
			for (int x_offset = -1; x_offset <= 0; x_offset++) {
				for (int y_offset = -1; y_offset <= 0; y_offset++) {
					if (i + x_offset >= 0 && i + x_offset < width - 1 && j + y_offset >= 0 && j + y_offset < height - 1) {
						const Corner& bottom_left = corners[i + x_offset][j + y_offset];
						const Corner& bottom_right = corners[i + 1 + x_offset][j + y_offset];
						const Corner& top_left = corners[i + x_offset][j + 1 + y_offset];
						const Corner& top_right = corners[i + 1 + x_offset][j + 1 + y_offset];

						const int base = std::min({ bottom_left.layer_height, bottom_right.layer_height, top_left.layer_height, top_right.layer_height });
						if (corners[i][j].layer_height != base) {
							continue;
						}

						if (is_corner_ramp_entrance(i + x_offset, j + y_offset)) {
							ramp_height = 0.5f;
							goto exit_loop;
						}
					}
				}
			}
		exit_loop:

			ground_corner_heights[j * width + i] = corners[i][j].final_ground_height() + ramp_height;
		}
	}

	upload_ground_heights();
	upload_corner_heights();
}

/// Updates the ground texture variation information and uploads it to the GPU
void Terrain::update_ground_textures(const QRect& area) {
	const QRect update_area = area.adjusted(-1, -1, 1, 1).intersected({ 0, 0, width - 1, height - 1 });

	for (int j = update_area.top(); j <= update_area.bottom(); j++) {
		for (int i = update_area.left(); i <= update_area.right(); i++) {
			ground_texture_list[j * (width - 1) + i] = get_texture_variations(i, j);
		}
	}

	upload_ground_texture();
}

void Terrain::update_ground_exists(const QRect& area) {
	QRect update_area = area.adjusted(-1, -1, 1, 1).intersected({ 0, 0, width - 1, height - 1 });

	for (int j = update_area.top(); j <= update_area.bottom(); j++) {
		for (int i = update_area.left(); i <= update_area.right(); i++) {
			ground_exists_data[j * (width - 1) + i] = !(((corners[i][j].cliff || corners[i][j].romp) && !is_corner_ramp_entrance(i, j)) || corners[i][j].special_doodad);
		}
	}

	upload_ground_exists();
}

/// Updates and uploads the water data for the GPU
void Terrain::update_water(const QRect& area) {
	for (int i = area.x(); i < area.x() + area.width(); i++) {
		for (int j = area.y(); j < area.y() + area.height(); j++) {
			map->terrain.water_exists_data[j * width + i] = corners[i][j].water;
			map->terrain.water_heights[j * width + i] = corners[i][j].water_height;
		}
	}
	upload_water_exists();
	upload_water_heights();
}

/// ToDo clean
/// Function is a bit of a mess
/// Updates the cliff and ramp meshes for an area
void Terrain::update_cliff_meshes(const QRect& area) {
	// Remove all existing cliff meshes in area
	for (size_t i = cliffs.size(); i-- > 0;) {
		glm::ivec3& pos = cliffs[i];
		if (area.contains(pos.x, pos.y)) {
			cliffs.erase(cliffs.begin() + i);
		}
	}

	for (int i = area.x(); i < area.right(); i++) {
		for (int j = area.y(); j < area.bottom(); j++) {
			corners[i][j].romp = false;
		}
	}

	QRect ramp_area = area.adjusted(-2, -2, 2, 2).intersected({ 0, 0, width, height });

	// Add new cliff meshes
	for (int i = ramp_area.x(); i < ramp_area.right(); i++) {
		for (int j = ramp_area.y(); j < ramp_area.bottom(); j++) {
			Corner& bottom_left = corners[i][j];
			Corner& bottom_right = corners[i + 1][j];
			Corner& top_left = corners[i][j + 1];
			Corner& top_right = corners[i + 1][j + 1];

			// Vertical ramps
			if (j < height - 2) {
				const Corner& top_top_left = corners[i][j + 2];
				const Corner& top_top_right = corners[i + 1][j + 2];
				const int ae = std::min(bottom_left.layer_height, top_top_left.layer_height);
				const int cf = std::min(bottom_right.layer_height, top_top_right.layer_height);

				if (top_left.layer_height == ae && top_right.layer_height == cf) {
					int base = std::min(ae, cf);
					if (bottom_left.ramp == top_left.ramp 
						&& bottom_left.ramp == top_top_left.ramp 
						&& bottom_right.ramp == top_right.ramp 
						&& bottom_right.ramp == top_top_right.ramp 
						&& bottom_left.ramp != bottom_right.ramp) {

						std::string file_name = ""s
							+ char((top_top_left.ramp ? 'L' : 'A') + (top_top_left.layer_height - base) * (top_top_left.ramp ? -4 : 1))
							+ char((top_top_right.ramp ? 'L' : 'A') + (top_top_right.layer_height - base) * (top_top_right.ramp ? -4 : 1))
							+ char((bottom_right.ramp ? 'L' : 'A') + (bottom_right.layer_height - base) * (bottom_right.ramp ? -4 : 1))
							+ char((bottom_left.ramp ? 'L' : 'A') + (bottom_left.layer_height - base) * (bottom_left.ramp ? -4 : 1));

						file_name = "doodads/terrain/clifftrans/clifftrans" + file_name + "0.mdx";
						if (hierarchy.file_exists(file_name)) {
							if (!path_to_cliff.contains(file_name)) {
								cliff_meshes.push_back(resource_manager.load<CliffMesh>(file_name));
								path_to_cliff.emplace(file_name, static_cast<int>(cliff_meshes.size()) - 1);
							}

							cliffs.emplace_back(i, j, path_to_cliff[file_name]);
							bottom_left.romp = true;
							top_left.romp = true;

							continue;
						}
					}
				}
			}

			// Horizontal ramps
			if (i < width - 2) {
				const Corner& bottom_right_right = corners[i + 2][j];
				const Corner& top_right_right = corners[i + 2][j + 1];
				const int ae = std::min(bottom_left.layer_height, bottom_right_right.layer_height);
				const int bf = std::min(top_left.layer_height, top_right_right.layer_height);

				if (bottom_right.layer_height == ae && top_right.layer_height == bf) {
					int base = std::min(ae, bf);
					if (bottom_left.ramp == bottom_right.ramp 
						&& bottom_left.ramp == bottom_right_right.ramp 
						&& top_left.ramp == top_right.ramp 
						&& top_left.ramp == top_right_right.ramp 
						&& bottom_left.ramp != top_left.ramp) {

						std::string file_name = ""s
							+ char((top_left.ramp ? 'L' : 'A') + (top_left.layer_height - base) * (top_left.ramp ? -4 : 1))
							+ char((top_right_right.ramp ? 'L' : 'A') + (top_right_right.layer_height - base) * (top_right_right.ramp ? -4 : 1))
							+ char((bottom_right_right.ramp ? 'L' : 'A') + (bottom_right_right.layer_height - base) * (bottom_right_right.ramp ? -4 : 1))
							+ char((bottom_left.ramp ? 'L' : 'A') + (bottom_left.layer_height - base) * (bottom_left.ramp ? -4 : 1));

						file_name = "doodads/terrain/clifftrans/clifftrans" + file_name + "0.mdx";
						if (hierarchy.file_exists(file_name)) {
							if (!path_to_cliff.contains(file_name)) {
								cliff_meshes.push_back(resource_manager.load<CliffMesh>(file_name));
								path_to_cliff.emplace(file_name, static_cast<int>(cliff_meshes.size()) - 1);
							}

							cliffs.emplace_back(i, j, path_to_cliff[file_name]);
							bottom_left.romp = true;
							bottom_right.romp = true;

							continue;
						}
					}
				}
			}

			if (!bottom_left.cliff || bottom_left.romp) {
				continue;
			}

			if (is_corner_ramp_entrance(i, j)) {
				continue;
			}

			const int base = std::min({bottom_left.layer_height, bottom_right.layer_height, top_left.layer_height, top_right.layer_height});

			// Cliff model path
			std::string file_name = ""s + char('A' + top_left.layer_height - base)
				+ char('A' + top_right.layer_height - base)
				+ char('A' + bottom_right.layer_height - base)
				+ char('A' + bottom_left.layer_height - base);

			if (file_name == "AAAA") {
				continue;
			}

			// Clamp to within max variations
			file_name += std::to_string(std::clamp(bottom_left.cliff_variation, 0, cliff_variations[file_name]));

			cliffs.emplace_back(i, j, path_to_cliff[file_name]);
		}
	}

	update_ground_exists(ramp_area);
}

void Terrain::resize(size_t new_width, size_t new_height) {
	glDeleteTextures(1, &ground_height);
	glDeleteTextures(1, &ground_corner_height);
	glDeleteTextures(1, &ground_texture_data);
	glDeleteTextures(1, &ground_exists);

	glDeleteTextures(1, &water_exists);
	glDeleteTextures(1, &water_height);

	width = new_width;
	height = new_height;

	//offset = 

	auto t = corners[0][0];
	corners.clear();
	corners.resize(width, std::vector<Corner>(height, t));

	ground_heights.resize(width * height);
	ground_corner_heights.resize(width * height);
	ground_texture_list.resize((width - 1) * (height - 1));
	ground_exists_data.resize(width * height);

	water_heights.resize(width * height);
	water_exists_data.resize(width * height);

	for (int i = 0; i < width; i++) {
		for (int j = 0; j < height; j++) {
			ground_corner_heights[j * width + i] = corners[i][j].final_ground_height();
			water_exists_data[j * width + i] = corners[i][j].water;
			ground_heights[j * width + i] = corners[i][j].height;
			water_heights[j * width + i] = corners[i][j].water_height;
		}
	}

	for (int i = 0; i < width - 1; i++) {
		for (int j = 0; j < height - 1; j++) {
			Corner& bottom_left = corners[i][j];
			Corner& bottom_right = corners[i + 1][j];
			Corner& top_left = corners[i][j + 1];
			Corner& top_right = corners[i + 1][j + 1];

			bottom_left.cliff = bottom_left.layer_height != bottom_right.layer_height || bottom_left.layer_height != top_left.layer_height || bottom_left.layer_height != top_right.layer_height;
		}
	}

	glCreateTextures(GL_TEXTURE_2D, 1, &ground_height);
	glTextureStorage2D(ground_height, 1, GL_R16F, width, height);
	glTextureSubImage2D(ground_height, 0, 0, 0, width, height, GL_RED, GL_FLOAT, ground_heights.data());
	glTextureParameteri(ground_height, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTextureParameteri(ground_height, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	glCreateTextures(GL_TEXTURE_2D, 1, &ground_corner_height);
	glTextureStorage2D(ground_corner_height, 1, GL_R16F, width, height);
	glTextureSubImage2D(ground_corner_height, 0, 0, 0, width, height, GL_RED, GL_FLOAT, ground_corner_heights.data());
	glTextureParameteri(ground_corner_height, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTextureParameteri(ground_corner_height, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	glCreateTextures(GL_TEXTURE_2D, 1, &ground_texture_data);
	glTextureStorage2D(ground_texture_data, 1, GL_RGBA16UI, width - 1, height - 1);
	glTextureParameteri(ground_texture_data, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTextureParameteri(ground_texture_data, GL_TEXTURE_MIN_FILTER, GL_NEAREST);

	glCreateTextures(GL_TEXTURE_2D, 1, &ground_exists);
	glTextureStorage2D(ground_exists, 1, GL_R8, width, height);

	// Water
	glCreateTextures(GL_TEXTURE_2D, 1, &water_height);
	glTextureStorage2D(water_height, 1, GL_R16F, width, height);
	glTextureSubImage2D(water_height, 0, 0, 0, width, height, GL_RED, GL_FLOAT, water_heights.data());

	glCreateTextures(GL_TEXTURE_2D, 1, &water_exists);
	glTextureStorage2D(water_exists, 1, GL_R8, width, height);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glTextureSubImage2D(water_exists, 0, 0, 0, width, height, GL_RED, GL_UNSIGNED_BYTE, water_exists_data.data());
	glPixelStorei(GL_UNPACK_ALIGNMENT, 4);

	update_cliff_meshes({ 0, 0, width - 1, height - 1 });
	update_ground_textures({ 0, 0, width - 1, height - 1 });
	update_ground_heights({ 0, 0, width - 1, height - 1 });

	map->physics.dynamicsWorld->removeRigidBody(collision_body);
	delete collision_body;
	delete collision_shape;

	collision_shape = new btHeightfieldTerrainShape(width, height, ground_corner_heights.data(), 0, -16.f, 16.f, 2 /*z*/, PHY_FLOAT, false);
	if (collision_shape == nullptr) {
		std::cout << "Error creating Bullet collision shape\n";
	}
	collision_body = new btRigidBody(0, new btDefaultMotionState(), collision_shape);
	collision_body->getWorldTransform().setOrigin(btVector3(width / 2.f - 0.5f, height / 2.f - 0.5f, 0.f)); // Bullet centers the collision mesh automatically, we need to decenter it and place it under the player
	collision_body->setCollisionFlags(collision_body->getCollisionFlags() | btCollisionObject::CF_STATIC_OBJECT);
	map->physics.dynamicsWorld->addRigidBody(collision_body, 32, 32);
}

void Terrain::update_minimap() {
	emit minimap_changed(minimap_image());
}

void TerrainGenericAction::undo() {
	for (int j = area.top(); j <= area.bottom(); j++) {
		for (int i = area.left(); i <= area.right(); i++) {
			map->terrain.corners[i][j] = old_corners[(j - area.top()) * area.width() + i - area.left()];
		}
	}

	if (undo_type == Terrain::undo_type::height) {
		map->terrain.update_ground_heights(area);
	}

	if (undo_type == Terrain::undo_type::texture) {
		map->terrain.update_ground_textures(area);
	}

	if (undo_type == Terrain::undo_type::cliff) {
		map->terrain.update_ground_heights(area);
		map->terrain.update_cliff_meshes(area);
		map->terrain.update_ground_textures(area);
		map->terrain.update_water(area);
	}

	map->terrain.update_minimap();
	map->units.update_area(area);
}

void TerrainGenericAction::redo() {
	for (int j = area.top(); j <= area.bottom(); j++) {
		for (int i = area.left(); i <= area.right(); i++) {
			map->terrain.corners[i][j] = new_corners[(j - area.top()) * area.width() + i - area.left()];
		}
	}

	if (undo_type == Terrain::undo_type::height) {
		map->terrain.update_ground_heights(area);
	}

	if (undo_type == Terrain::undo_type::texture) {
		map->terrain.update_ground_textures(area);
	}

	if (undo_type == Terrain::undo_type::cliff) {
		map->terrain.update_ground_heights(area);
		map->terrain.update_cliff_meshes(area);
		map->terrain.update_ground_textures(area);
		map->terrain.update_water(area);
	}

	map->terrain.update_minimap();
	map->units.update_area(area);
}