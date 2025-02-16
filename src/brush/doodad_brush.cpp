#include "doodad_brush.h"

#include <random>
#include <memory>

#define GLM_FORCE_CXX17
#define GLM_FORCE_RADIANS
#define GLM_FORCE_SILENT_WARNINGS
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "globals.h"
#include <map_global.h>

import Hierarchy;
import Texture;
import TerrainUndo;
import Camera;
import OpenGLUtilities;

DoodadBrush::DoodadBrush()
	: Brush() {
	uv_offset_granularity = 2;
	brush_offset = { 0.25f, 0.25f };
}

/// Gets a random variation from the possible_variation list
int DoodadBrush::get_random_variation() {
	if (possible_variations.size() == 0) {
		return 0;
	}

	std::mt19937 rng;
	rng.seed(std::random_device()());
	std::uniform_int_distribution<std::mt19937::result_type> dist(0, possible_variations.size() - 1);

	auto it = possible_variations.begin();
	std::advance(it, dist(rng));
	return *it;
}

void DoodadBrush::set_shape(const Shape new_shape) {
	shape = new_shape;

	if (pathing_texture) {
		const int div_w = (((int)glm::degrees(rotation) + 90) % 180) ? pathing_texture->height : pathing_texture->width;
		const int div_h = (((int)glm::degrees(rotation) + 90) % 180) ? pathing_texture->width : pathing_texture->height;

		for (int i = 0; i < size; i++) {
			for (int j = 0; j < size; j++) {
				brush[j * size + i] = { 0, 0, 0, 0 };
			}
		}

		for (int i = 0; i < pathing_texture->width; i++) {
			for (int j = 0; j < pathing_texture->height; j++) {
				int x = i;
				int y = j;

				switch (((int)glm::degrees(rotation) + 90) % 360) {
					case 90:
						x = pathing_texture->height - 1 - j;
						y = i;
						break;
					case 180:
						x = pathing_texture->width - 1 - i;
						y = pathing_texture->height - 1 - j;
						break;
					case 270:
						x = j;
						y = pathing_texture->width - 1 - i;
						break;
				}

				const int in = ((pathing_texture->height - 1 - j) * pathing_texture->width + i) * pathing_texture->channels;
				const int index = (y + std::max(0, div_w - div_h) / 2) * size + x + std::max(0, div_h - div_w) / 2;

				// Have to check for > 250 because sometimes the pathing textures are not properly thresholded
				glm::vec4 color = { pathing_texture->data[in + 2] > 250 ? 255 : 0,
									pathing_texture->data[in + 1] > 250 ? 255 : 0,
									pathing_texture->data[in] > 250 ? 255 : 0,
									128 };

				if (color.r || color.g || color.b) {
					brush[index] = color;
				}
			}
		}
	}

	glBindTexture(GL_TEXTURE_2D, brush_texture);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, size, size, 0, GL_BGRA, GL_UNSIGNED_BYTE, brush.data());
}

void DoodadBrush::key_press_event(QKeyEvent* event) {
	if (event->modifiers() & Qt::KeypadModifier) {
		if (action == Action::none) {
			start_action(Action::move);
		}

		bool left = event->key() == Qt::Key_1 || event->key() == Qt::Key_4 || event->key() == Qt::Key_7;
		bool right = event->key() == Qt::Key_3 || event->key() == Qt::Key_6 || event->key() == Qt::Key_9;
		bool down = event->key() == Qt::Key_7 || event->key() == Qt::Key_8 || event->key() == Qt::Key_9;
		bool up = event->key() == Qt::Key_1 || event->key() == Qt::Key_2 || event->key() == Qt::Key_3;

		bool free_movement = true;
		for (const auto& i : selections) {
			free_movement = free_movement && !i->pathing;
		}

		float x_displacement;
		float y_displacement;
		if (free_movement) {
			x_displacement = -0.25f * left + 0.25f * right;
			y_displacement = -0.25f * up + 0.25f * down;
		} else {
			x_displacement = -0.5f * left + 0.5f * right;
			y_displacement = -0.5f * up + 0.5f * down;
		}

		for (const auto& i : selections) {
			i->position.x += x_displacement;
			i->position.y += y_displacement;
			if (!lock_doodad_z) {
				i->position.z = map->terrain.interpolated_height(i->position.x, i->position.y);
			}
			i->update();
		}
		emit position_changed();

		map->doodads.update_doodad_pathing(selections);
	}

	if (event->modifiers() & Qt::ControlModifier) {
		switch (event->key()) {
			case Qt::Key_A:
				selections.clear();
				selections.reserve(map->doodads.doodads.size());
				for (auto& i : map->doodads.doodads) {
					selections.emplace(&i);
				}

				emit selection_changed();
				break;
			case Qt::Key_PageUp:
				if (action == Action::none) {
					start_action(Action::move);
				}
				for (const auto& i : selections) {
					i->position.z += 0.1f;
					i->update();
				}
				emit position_changed();
				break;
			case Qt::Key_PageDown:
				if (action == Action::none) {
					start_action(Action::move);
				}
				for (const auto& i : selections) {
					i->position.z -= 0.1f;
					i->update();
				}
				emit position_changed();
				break;
			default:
				Brush::key_press_event(event);
		}
	} else {
		switch (event->key()) {
			case Qt::Key_PageUp:
				if (action == Action::none) {
					start_action(Action::move);
				}
				for (const auto& i : selections) {
					i->scale.z += 0.1f;
					i->update();
				}
				emit scale_changed();
				break;
			case Qt::Key_PageDown:
				if (action == Action::none) {
					start_action(Action::move);
				}
				for (const auto& i : selections) {
					i->scale.z -= 0.1f;
					i->update();
				}
				emit scale_changed();
				break;
			default:
				Brush::key_press_event(event);
		}
	}
}

void DoodadBrush::key_release_event(QKeyEvent* event) {
	if (event->isAutoRepeat()) {
		return;
	}

	if (action == Action::move) {
		end_action();
	}
}

void DoodadBrush::mouse_press_event(QMouseEvent* event, double frame_delta) {
	// The mouse.y check is needed as sometimes it is negative for unknown reasons
	if (event->button() == Qt::LeftButton && input_handler.mouse.y > 0.f) {
		if (mode == Mode::selection) {
			if (event->modifiers() & Qt::KeyboardModifier::ShiftModifier) {
				auto id = map->render_manager.pick_doodad_id_under_mouse(map->doodads, input_handler.mouse);
				if (id) {
					if (selections.contains(&map->doodads.doodads[id.value()])) {
						selections.erase(&map->doodads.doodads[id.value()]);
					} else {
						selections.emplace(&map->doodads.doodads[id.value()]);
					}
					return;
				}
			}

			if (!event->modifiers()) {
				auto id = map->render_manager.pick_doodad_id_under_mouse(map->doodads, input_handler.mouse);
				if (id) {
					Doodad& doodad = map->doodads.doodads[id.value()];

					drag_start = input_handler.mouse_world;
					dragging = true;

					// If the current index is already in a selection then we want to drag the entire group
					if (std::find(selections.begin(), selections.end(), &doodad) != selections.end()) {
						drag_offsets.clear();
						for (const auto& i : selections) {
							drag_offsets.push_back(input_handler.mouse_world - i->position);
						}
					} else {
						selections = { &doodad };
						drag_offsets = { input_handler.mouse_world - doodad.position };
						emit selection_changed();
					}
					return;
				}
			}
		}
	}
	Brush::mouse_press_event(event, frame_delta);
}

void DoodadBrush::mouse_move_event(QMouseEvent* event, double frame_delta) {
	Brush::mouse_move_event(event, frame_delta);

	if (event->buttons() == Qt::LeftButton) {
		if (mode == Mode::selection) {
			if (dragging) {
				if (action == Action::none) {
					start_action(Action::drag);
				}

				bool free_movement = true;
				for (const auto& i : selections) {
					free_movement = free_movement && !i->pathing;
				}

				glm::vec3 offset;
				if (free_movement) {
					offset = input_handler.mouse_world - drag_start;
				} else {
					offset = glm::round((input_handler.mouse_world) * 2.f + 0.5f) / 2.f - 0.25f;
					offset -= glm::round((drag_start)*2.f + 0.5f) / 2.f - 0.25f;
				}
				offset.z = 0;

				if (!free_movement && offset.x == 0.f && offset.y == 0.f) {
					return;
				}
				drag_start = input_handler.mouse_world;

				for (const auto& doodad : selections) {
					doodad->position += offset;
					if (!lock_doodad_z) {
						doodad->position.z = map->terrain.interpolated_height(doodad->position.x, doodad->position.y);
					}
					doodad->update();
				}
				emit position_changed();
				map->doodads.update_doodad_pathing(selections);
			} else if (event->modifiers() & Qt::ControlModifier) {
				if (action == Action::none) {
					start_action(Action::rotate);
				}

				for (auto&& i : selections) {
					float target_rotation = std::atan2(input_handler.mouse_world.y - i->position.y, input_handler.mouse_world.x - i->position.x);
					if (target_rotation < 0) {
						target_rotation += 2.f * glm::pi<float>();
					}

					i->angle = Doodad::acceptable_angle(i->id, i->pathing, i->angle, target_rotation);
					i->update();
				}
				emit angle_changed();

				map->doodads.update_doodad_pathing(selections);
			} else if (mode == Mode::selection && selection_started) {
				const glm::vec2 size = glm::vec2(input_handler.mouse_world) - selection_start;

				auto query = map->doodads.query_area({ selection_start.x, selection_start.y, size.x, size.y });
				if (event->modifiers() & Qt::KeyboardModifier::ShiftModifier) {
					selections.insert(query.begin(), query.end());
				} else if (event->modifiers() & Qt::KeyboardModifier::AltModifier) {
					for (const auto& i : query) {
						selections.erase(i);
					}
				} else {
					selections.clear();
					selections.insert(query.begin(), query.end());
				}

				emit selection_changed();
			}
		}
	}
}

void DoodadBrush::mouse_release_event(QMouseEvent* event) {
	dragging = false;

	if (event->button() == Qt::LeftButton) {
		if (action == Action::drag || action == Action::rotate) {
			end_action();
		}
	}

	Brush::mouse_release_event(event);
}

void DoodadBrush::delete_selection() {
	if (!selections.size()) {
		return;
	}

	QRectF update_pathing_area;
	// Undo/redo
	auto action = std::make_unique<DoodadDeleteAction>();
	for (const auto& i : selections) {
		action->doodads.push_back(*i);

		if (update_pathing_area.width() == 0 || update_pathing_area.height() == 0) {
			update_pathing_area = { i->position.x, i->position.y, 1.f, 1.f };
		}
		update_pathing_area |= { i->position.x, i->position.y, 1.f, 1.f };
	}
	map->terrain_undo.new_undo_group();
	map->terrain_undo.add_undo_action(std::move(action));

	map->doodads.remove_doodads(selections);
	map->doodads.update_doodad_pathing(update_pathing_area);

	selections.clear();
	emit selection_changed();
}

void DoodadBrush::copy_selection() {
	clipboard.clear();

	// Mouse position is average location
	clipboard_free_placement = true;
	glm::vec3 average_position = {};
	for (const auto& i : selections) {
		if (i->pathing) {
			clipboard_free_placement = false;
		}
		clipboard.push_back(*i);
		average_position += i->position;
	}
	clipboard_mouse_offset = average_position / static_cast<float>(clipboard.size());
}

void DoodadBrush::cut_selection() {
	copy_selection();
	// Delete selection will add to the undo/redo tree
	delete_selection();
}

void DoodadBrush::clear_selection() {
	selections.clear();
	emit selection_changed();
}

void DoodadBrush::place_clipboard() {
	apply_begin();
	for (const auto& i : clipboard) {
		Doodad& new_doodad = map->doodads.add_doodad(i);
		new_doodad.creation_number = ++Doodad::auto_increment;
		glm::vec3 final_position;
		if (clipboard_free_placement) {
			final_position = glm::vec3(glm::vec2(input_handler.mouse_world + i.position) - clipboard_mouse_offset, i.position.z);
		} else {
			final_position = glm::round((input_handler.mouse_world) * 2.f + 0.5f) / 2.f - 0.25f + i.position - (glm::round((glm::vec3(clipboard_mouse_offset, 0)) * 2.f + 0.5f) / 2.f - 0.25f);
		}
		if (!lock_doodad_z) {
			final_position.z = map->terrain.interpolated_height(final_position.x, final_position.y);
		}

		new_doodad.position = final_position;
		new_doodad.update();
		doodad_undo->doodads.push_back(new_doodad);

		if (new_doodad.pathing) {
			map->pathing_map.blit_pathing_texture(new_doodad.position, glm::degrees(rotation) + 90, new_doodad.pathing);
		}
	}
	map->pathing_map.upload_dynamic_pathing();
	apply_end();
}

void DoodadBrush::apply_begin() {
	doodad_undo = std::make_unique<DoodadAddAction>();
}

void DoodadBrush::apply(double frame_delta) {
	if (id == "") {
		return;
	}

	glm::vec3 doodad_position;
	if (free_placement) {
		doodad_position = input_handler.mouse_world;
	} else {
		doodad_position = glm::vec3(glm::vec2(position) + glm::vec2(uv_offset) * 0.25f + size * 0.125f, input_handler.mouse_world.z);
	}

	Doodad& doodad = map->doodads.add_doodad(id, variation, doodad_position);
	doodad.scale = glm::vec3(scale);
	doodad.angle = rotation;
	doodad.state = state;
	doodad.update();

	doodad_undo->doodads.push_back(doodad);

	if (pathing_texture) {
		map->pathing_map.blit_pathing_texture(doodad_position, glm::degrees(rotation) + 90, pathing_texture);
		map->pathing_map.upload_dynamic_pathing();
	}

	if (random_rotation) {
		set_random_rotation();
		set_shape(shape);
	}

	if (random_variation) {
		set_random_variation();
	}

	if (random_scale) {
		std::random_device rd;
		std::mt19937 gen(rd());
		std::uniform_real_distribution dist(min_scale, max_scale);
		scale = dist(gen);
	}
}

void DoodadBrush::apply_end() {
	if (doodad_undo->doodads.empty()) {
		return;
	}
	map->terrain_undo.new_undo_group();
	map->terrain_undo.add_undo_action(std::move(doodad_undo));
}

void DoodadBrush::render_brush() {
	if (pathing_texture) {
		Brush::render_brush();
	}

	if (!mesh) {
		return;
	}

	glm::vec3 base_scale = glm::vec3(1.f);
	if (doodads_slk.row_headers.contains(id)) {
		base_scale = glm::vec3(doodads_slk.data<float>("defscale", id));
	}

	glm::vec3 final_position;
	if (free_placement) {
		final_position = input_handler.mouse_world;
	} else {
		final_position = glm::vec3(glm::vec2(position) + glm::vec2(uv_offset) * 0.25f + size * 0.125f, input_handler.mouse_world.z);
	}

	skeleton.update_location(final_position, rotation, (base_scale * scale) / 128.f);
	skeleton.update(0.016f);

	//mesh->render_queue(skeleton, glm::vec3(1.f));
	map->render_manager.render_queue(*mesh, skeleton, glm::vec3(1.f));
}

// Quads are drawn and then in the fragment shader fragments are discarded to form a circle
void DoodadBrush::render_selection() const {
	glDisable(GL_DEPTH_TEST);
	selection_circle_shader->use();
	glEnableVertexAttribArray(0);

	for (const auto& i : selections) {
		float selection_scale = 1.f;
		if (i->mesh->model->sequences.empty()) {
			selection_scale = i->mesh->model->extent.bounds_radius / 128.f;
		} else {
			selection_scale = i->mesh->model->sequences[i->skeleton.sequence_index].extent.bounds_radius / 128.f;
		}
		if (selection_scale < 0.1f) { // hack, what is the correct approach?
			selection_scale = i->mesh->model->extent.bounds_radius / 128.f;
		}

		glm::mat4 model(1.f);
		model = glm::translate(model, i->position - glm::vec3(selection_scale * 0.5f, selection_scale * 0.5f, 0.f));
		model = glm::scale(model, glm::vec3(selection_scale));

		model = camera.projection_view * model;
		glUniformMatrix4fv(1, 1, GL_FALSE, &model[0][0]);

		glBindBuffer(GL_ARRAY_BUFFER, shapes.vertex_buffer);
		glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);

		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, shapes.index_buffer);
		glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, nullptr);
	}

	glDisableVertexAttribArray(0);
	glEnable(GL_DEPTH_TEST);
}

void DoodadBrush::render_clipboard() {
	for (auto& i : clipboard) {
		glm::vec3 base_scale = glm::vec3(1.f);
		if (doodads_slk.row_headers.contains(i.id)) {
			base_scale = glm::vec3(doodads_slk.data<float>("defscale", i.id));
		}

		glm::vec3 final_position;
		if (clipboard_free_placement) {
			final_position = glm::vec3(glm::vec2(input_handler.mouse_world + i.position) - clipboard_mouse_offset, i.position.z);
		} else {
			final_position = glm::round((input_handler.mouse_world) * 2.f + 0.5f) / 2.f - 0.25f + i.position - (glm::round((glm::vec3(clipboard_mouse_offset, 0)) * 2.f + 0.5f) / 2.f - 0.25f);
		}

		if (!lock_doodad_z) {
			final_position.z = map->terrain.interpolated_height(final_position.x, final_position.y);
		}

		i.skeleton.update_location(final_position, i.angle, (base_scale * i.scale) / 128.f);
		i.skeleton.update(0.016f);

		//i.mesh->render_queue(i.skeleton, glm::vec3(1.f));
		map->render_manager.render_queue(*i.mesh, i.skeleton, glm::vec3(1.f));
	}
}

bool DoodadBrush::can_place() {
	if (!pathing_texture) {
		return true;
	}

	glm::vec3 doodad_position;
	if (free_placement) {
		doodad_position = input_handler.mouse_world;
	} else {
		doodad_position = glm::vec3(glm::vec2(position) + glm::vec2(uv_offset) * 0.25f + size * 0.125f, input_handler.mouse_world.z);
	}

	return map->pathing_map.is_area_free(doodad_position, glm::degrees(rotation) + 90, pathing_texture, PathingMap::Flags::unwalkable | PathingMap::Flags::unflyable | PathingMap::Flags::unbuildable);
}

void DoodadBrush::set_random_variation() {
	variation = get_random_variation();
	context->makeCurrent();
	mesh = map->doodads.get_mesh(id, variation);
	skeleton = SkeletalModelInstance(mesh->model);
}

void DoodadBrush::set_random_rotation() {
	std::random_device rd;
	std::mt19937 gen(rd());

	bool fixed_rotation = false;
	if (doodads_slk.row_headers.contains(id)) {
		fixed_rotation = doodads_slk.data<int>("fixedrot", id) > 0;
	} else {
		fixed_rotation = destructibles_slk.data<int>("fixedrot", id) > 0;
	}

	std::uniform_real_distribution dist(0.f, glm::pi<float>() * 2.f);
	float target_rotation = dist(gen);
	if (pathing_texture && pathing_texture->width == pathing_texture->height) {
		if (pathing_texture->homogeneous) {
			rotation = target_rotation;
		} else {
			rotation = (static_cast<int>((target_rotation + glm::pi<float>() * 0.25f) / (glm::pi<float>() * 0.5f)) % 4) * glm::pi<float>() * 0.5f;
		}
	} else {
		rotation = (static_cast<int>((target_rotation + glm::pi<float>() * 0.25f) / (glm::pi<float>() * 0.5f)) % 4) * glm::pi<float>() * 0.5f;
	}
}

void DoodadBrush::add_variation(int variation) {
	possible_variations.insert(variation);
}

void DoodadBrush::erase_variation(int variation) {
	possible_variations.erase(variation);
	if (this->variation == variation) {
		this->variation = get_random_variation();
	}
}

void DoodadBrush::set_doodad(const std::string& id) {
	this->id = id;

	const bool is_doodad = doodads_slk.row_headers.contains(id);
	const slk::SLK& slk = is_doodad ? doodads_slk : destructibles_slk;

	min_scale = slk.data<float>("minscale", id);
	max_scale = slk.data<float>("maxscale", id);

	std::string maxRoll = doodads_slk.data("maxroll", id);
	if (!maxRoll.empty()) {
		roll = -std::stof(maxRoll);
	} else {
		roll = 0;
	}

	if (is_doodad) {
		scale = slk.data<float>("defscale", id);
	}

	if (slk.data<int>("fixedrot", id) < 0) {
		rotation = glm::pi<float>() * 1.5f;
	} else {
		rotation = glm::radians(slk.data<float>("fixedrot", id));
	}

	pathing_texture.reset();
	std::string pathing_texture_path = slk.data("pathtex", id);
	if (hierarchy.file_exists(pathing_texture_path)) {
		free_placement = false;
		pathing_texture = resource_manager.load<PathingTexture>(pathing_texture_path);

		set_size(std::max(pathing_texture->width, pathing_texture->height));

		free_rotation = pathing_texture->width == pathing_texture->height;
		free_rotation = free_rotation && pathing_texture->homogeneous;
		free_rotation = free_rotation && slk.data<float>("fixedrot", id) < 0.f;
	} else {
		free_placement = true;
		free_rotation = true;
	}

	possible_variations.clear();
	int variation_count = slk.data<int>("numvar", id);
	for (int i = 0; i < variation_count; i++) {
		possible_variations.insert(i);
	}
	set_random_variation();
}

void DoodadBrush::start_action(Action new_action) {
	action = new_action;
	map->terrain_undo.new_undo_group();
	doodad_state_undo = std::make_unique<DoodadStateAction>();
	for (const auto& i : selections) {
		doodad_state_undo->old_doodads.push_back(*i);
	}
}

void DoodadBrush::end_action() {
	for (const auto& i : selections) {
		doodad_state_undo->new_doodads.push_back(*i);
	}
	map->terrain_undo.add_undo_action(std::move(doodad_state_undo));
	action = Action::none;
}

void DoodadBrush::set_selection_angle(float angle) {
	start_action(Action::rotate);
	for (auto& i : selections) {
		i->angle = Doodad::acceptable_angle(i->id, i->pathing, i->angle, angle);
		i->update();
	}
	end_action();
}

void DoodadBrush::set_selection_absolute_height(float height) {
	start_action(Action::move);
	for (auto& i : selections) {
		i->position.z = height;
		i->update();
	}
	end_action();
}

void DoodadBrush::set_selection_relative_height(float height) {
	start_action(Action::move);
	for (auto& i : selections) {
		i->position.z = map->terrain.interpolated_height(i->position.x, i->position.y) + height;
		i->update();
	}
	end_action();
}

void DoodadBrush::set_selection_scale_component(int component, float scale) {
	start_action(Action::scale);
	for (auto& i : selections) {
		bool is_doodad = doodads_slk.row_headers.contains(i->id);
		slk::SLK& slk = is_doodad ? doodads_slk : destructibles_slk;

		float min_scale = slk.data<float>("minscale", i->id);
		float max_scale = slk.data<float>("maxscale", i->id);

		if (!is_doodad) {
			i->scale = glm::vec3(std::clamp(scale, min_scale, max_scale));
		} else {
			i->scale[component] = std::clamp(scale, min_scale, max_scale);
		}
		i->update();
	}
	end_action();
}