#include "item_factory.h"
#include "output.h"
#include "input.h"
#include "game.h"
#include "item.h"
#include "catacharset.h"
#include "units.h"
#include "string_formatter.h"
#include "material.h"
#include "string_input_popup.h"
#include "json.h"
#include <vector>
#include <string>
#include <map>
#include <iostream>
#include <fstream>
#include <dirent.h>
#include <sstream>

// Function to strip color tags and `|| ` tags
std::string strip_tags(const std::string &str) {
	std::string result;
	bool inside_tag = false;
	for (char ch : str) {
		if (ch == '<') {
			inside_tag = true;
		} else if (ch == '>') {
			inside_tag = false;
		} else if (!inside_tag) {
			result += ch;
		}
	}

	// Remove any `|| ` tags
	size_t pos = 0;
	while ((pos = result.find("|| ")) != std::string::npos) {
		result.erase(pos, 3);
	}

	// Remove leading spaces
	if (!result.empty() && result[0] == ' ') {
		result.erase(0, 1);
	}

	return result;
}

std::string weight_to_string(const units::mass &weight) {
	return string_format("%d g", to_gram(weight));
}

std::string volume_to_string(const units::volume &volume) {
	return string_format("%d ml", to_milliliter(volume));
}

std::vector<std::pair<itype_id, std::string>> load_item_ids_from_directory(const std::string &directory_path, std::ofstream &log_file) {
	std::vector<std::pair<itype_id, std::string>> item_ids;
	DIR *dir = opendir(directory_path.c_str());
	if (dir == nullptr) {
		log_file << "Failed to open directory: " << directory_path << std::endl;
		return item_ids;
	}
	struct dirent *entry;
	while ((entry = readdir(dir)) != nullptr) {
		std::string file_name = entry->d_name;
		if (file_name.size() > 5 && file_name.substr(file_name.size() - 5) == ".json") {
			std::string full_path = directory_path + "/" + file_name;
			std::ifstream file(full_path);
			if (!file.is_open()) {
				log_file << "Failed to open JSON file: " << full_path << std::endl;
				continue;
			}
			JsonIn jsin(file);
			try {
				if (jsin.test_array()) {
					JsonArray items = jsin.get_array();
					for (JsonObject item : items) {
						if (item.has_string("id")) {
							item_ids.emplace_back(item.get_string("id"), full_path);
						}
					}
				} else if (jsin.test_object()) {
					JsonObject json = jsin.get_object();
					if (json.has_array("items")) {
						JsonArray items = json.get_array("items");
						for (JsonObject item : items) {
							if (item.has_string("id")) {
								item_ids.emplace_back(item.get_string("id"), full_path);
							}
						}
					}
				}
			} catch (const JsonError &err) {
				log_file << "Error parsing JSON file: " << full_path << " - " << err.what() << std::endl;
			}
			file.close();
		}
	}
	closedir(dir);
	return item_ids;
}

void game::items_browser() {
	std::ofstream log_file("items_browser.log", std::ios::trunc);
	if (!log_file.is_open()) {
		std::cerr << "Failed to open log file for writing." << std::endl;
		return;
	}
	catacurses::window w = catacurses::newwin(TERMY, TERMX, point_zero);
	input_context ctxt("ITEMS_BROWSER");
	ctxt.register_action("QUIT");
	ctxt.register_action("UP");
	ctxt.register_action("DOWN");
	ctxt.register_action("FILTER");

	std::vector<item> all_items;

	log_file << "Loading items from data/json/items/tool/..." << std::endl;
	std::vector<std::pair<itype_id, std::string>> item_ids = load_item_ids_from_directory("data/json/items/tool", log_file);

	for (const auto &id_file_pair : item_ids) {
		const itype_id &id = id_file_pair.first;
		const std::string &file_path = id_file_pair.second;
		const itype *e = item_controller->find_template(id);
		if (!e) {
			log_file << "Missing item definition: " << id << ", in file: " << file_path << std::endl;
			continue;
		}
		if (e->phase == phase_id::PNULL || id == "NULL") {
			log_file << "Ignoring item with NULL definition: " << id << ", in file: " << file_path << std::endl;
			continue;
		}
		if (e->gun || e->magazine || e->ammo || e->tool || e->armor || e->book || e->bionic || e->container) {
			all_items.emplace_back(e->get_id());
		}
	}

	log_file << "Total items loaded: " << all_items.size() << std::endl;
	std::vector<item> filtered_items = all_items;
	std::string filter_text;
	size_t selected_index = 0;
	size_t top_line = 0;

	while (true) {
		werase(w);
		if (!filter_text.empty()) {
			filtered_items.clear();
			for (const item &it : all_items) {
				if (strip_tags(it.tname()).find(filter_text) != std::string::npos) {
					filtered_items.push_back(it);
				}
			}
		} else {
			filtered_items = all_items;
		}
		const int num_lines = TERMY - 2;

		for (size_t i = top_line; i < filtered_items.size() && i < top_line + num_lines; ++i) {
			nc_color color = (i == selected_index) ? h_white : c_white;
			mvwprintz(w, point(1, 1 + i - top_line), color, strip_tags(filtered_items[i].tname()).c_str());
		}

		if (!filtered_items.empty()) {
			const item &selected_item = filtered_items[selected_index];
			mvwprintz(w, point(40, 1), c_light_blue, "Details:");
			mvwprintz(w, point(40, 2), c_light_gray, "Name: %s", strip_tags(selected_item.tname()).c_str());
			mvwprintz(w, point(40, 3), c_light_gray, "Weight: %s", weight_to_string(selected_item.weight()).c_str());
			mvwprintz(w, point(40, 4), c_light_gray, "Volume: %s", volume_to_string(selected_item.volume()).c_str());
			mvwprintz(w, point(40, 5), c_light_gray, "Material: %s", strip_tags(selected_item.get_base_material().name().c_str()).c_str());
		}
		wrefresh(w);
		const std::string action = ctxt.handle_input();

		if (action == "QUIT") {
			break;
		} else if (action == "UP" && !filtered_items.empty()) {
			if (selected_index > 0) {
				selected_index--;
				if (selected_index < top_line) {
					top_line--;
				}
			}
		} else if (action == "DOWN" && !filtered_items.empty()) {
			if (selected_index < filtered_items.size() - 1) {
				selected_index++;
				if (selected_index >= top_line + num_lines) {
					top_line++;
				}
			}
		} else if (action == "FILTER") {
			filter_text = string_input_popup()
			.title("Filter")
			.width(30)
			.text(filter_text)
			.query_string();
			selected_index = 0;
			top_line = 0;
		}
	}
	log_file.close();
}
