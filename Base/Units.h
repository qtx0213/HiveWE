﻿#pragma once

#include <vector>
#include <unordered_map>

#include "Quadtree.h"
#include "BinaryReader.h"
#include "StaticMesh.h"
#include "Terrain.h"

struct Unit {
	static int auto_increment;

	std::string id;
	int variation = 0;
	glm::vec3 position;
	float angle;
	glm::vec3 scale;

	std::string skin_id;

	uint8_t flags = 2;
	int player = 0;

	uint8_t unknown1 = 0;
	uint8_t unknown2 = 0;
	int health = -1;
	int mana = -1;

	int item_table_pointer = 0xFFFF;
	std::vector<ItemSet> item_sets;

	int gold = 12500;
	float target_acquisition = -1;

	int level = 1;
	int strength = 0;
	int agility = 0;
	int intelligence = 0;

	// Slot, ID
	std::vector<std::pair<uint32_t, std::string>> items;

	// ID, autocast, ability level
	std::vector<std::tuple<std::string, uint32_t, uint32_t>> abilities;

	int random_type = 0;
	std::vector<uint8_t> random;

	int custom_color = -1;
	int waygate = -1;
	int creation_number;

	glm::mat4 matrix = glm::mat4(1.f);
	std::shared_ptr<StaticMesh> mesh;

	Unit() {
		creation_number = ++auto_increment;
	}

	void update();
};

class Units {
	std::unordered_map<std::string, std::shared_ptr<StaticMesh>> id_to_mesh;

	static constexpr int write_version = 8;
	static constexpr int write_subversion = 11;

	static constexpr int mod_table_write_version = 2;
public:
	std::vector<Unit> units;
	std::vector<Unit> items;
	QuadTree<Unit> tree; // ToDo remove

	bool load(BinaryReader& reader, Terrain& terrain);
	void save() const;
	void load_unit_modifications(BinaryReader& reader);
	void load_item_modifications(BinaryReader& reader);
	void save_unit_modifications();
	void save_item_modifications();
	void update_area(const QRect& area);
	void create();
	void render() const;

	Unit& add_unit(std::string id, glm::vec3 position);
	Unit& add_unit(Unit unit);

	void remove_unit(Unit* unit);

	std::vector<Unit*> query_area(const QRectF& area);
	void remove_units(const std::vector<Unit*>& list);

	std::shared_ptr<StaticMesh> get_mesh(const std::string& id);
};

// Undo/redo structures
class UnitAddAction : public TerrainUndoAction {
public:
	std::vector<Unit> units;

	void undo() override;
	void redo() override;
};

class UnitDeleteAction : public TerrainUndoAction {
public:
	std::vector<Unit> units;

	void undo() override;
	void redo() override;
};

class UnitStateAction : public TerrainUndoAction {
public:
	std::vector<Unit> old_units;
	std::vector<Unit> new_units;

	void undo() override;
	void redo() override;
};