#include "INI.h"

#include <sstream>

#include "Hierarchy.h"
#include "Utilities.h"

namespace ini {
	INI::INI(const fs::path& path) {
		load(path);
	}

	void INI::load(const fs::path& path) {
		std::stringstream file;
		file.write((char*)hierarchy.open_file(path).buffer.data(), hierarchy.open_file(path).buffer.size());

		// Strip byte order marking
		char a, b, c;
		a = file.get();
		b = file.get();
		c = file.get();
		if (a != (char)0xEF || b != (char)0xBB || c != (char)0xBF) {
			file.seekg(0);
		}

		std::string line;
		std::string current_section;
		while (std::getline(file, line)) {
			// Normaly ini files use ; for comments, but Blizzard uses //
			if (line.substr(0, 2) == "//" || line.empty() || line.front() == ';') {
				continue;
			}

			if (line.front() == '[') {
				std::string key = line.substr(1, line.find(']') - 1);

				// If the segment already exists
				if (ini_data.contains(key)) {
					continue;
				}
				ini_data[key] = std::map<std::string, std::vector<std::string>>();
				current_section = key;
			} else {
				size_t found = line.find_first_of('=');
				if (found == std::string::npos) {
					continue;
				}

				std::string key = line.substr(0, found);

				// Fix some upper/lowercase issues that appeared in 1.32. Hopefully temporary 29/02/2020
				if (key == "minscale") {
					key = "minScale";
				} else if (key == "maxscale") {
					key = "maxScale";
				} else if (key == "texid") {
					key = "texID";
				} else if (key == "fixedrot") {
					key = "fixedRot";
				} else if (key == "fixedrot") {
					key = "fixedRot";
				}

				std::string value = line.substr(found + 1);

				if (key.empty() || value.empty()) {
					continue;
				}

				if (value.back() == '\r') {
					value.pop_back();
				}

				auto parts = split(value, ',');
				// Strip off quotes at the front/back
				for (auto&& i : parts) {
					if (i.size() < 2) {
						continue;
					}
					if (i.front() == '\"') {
						i.erase(i.begin());
					}
					if (i.back() == '\"') {
						i.pop_back();
					}
				}
				ini_data[current_section][key] = parts;
			}
		}
	}

	/// Replaces all values (not keys) which match one of the keys in substitution INI
	void INI::substitute(const INI& ini, const std::string& section) {
		for (auto&& [section_key, section_value] : ini_data) {
			for (auto&& [key, value] : section_value) {
				for (auto&& part : value) {
					std::string westring = ini.data(section, part);
					if (!westring.empty()) {
						part = westring;
					}
				}
			}
		}
	}

	std::map<std::string, std::vector<std::string>> INI::section(const std::string& section) const {
		if (ini_data.contains(section)) {
			return ini_data.at(section);
		} else {
			return {};
		}
	}

	void INI::set_whole_data(const std::string& section, const std::string& key, std::string value) {
		ini_data[section][key] = { value };
	}

	std::vector<std::string> INI::whole_data(const std::string& section, const std::string& key) const {
		if (ini_data.contains(section) && ini_data.at(section).contains(key)) { // ToDo C++20 contains
			return ini_data.at(section).at(key);
		} else {
			return {};
		}
	}

	bool INI::key_exists(const std::string& section, const std::string& key) const {
		return ini_data.contains(section) && ini_data.at(section).contains(key);
	}

	bool INI::section_exists(const std::string& section) const {
		return ini_data.contains(section);
	}
}