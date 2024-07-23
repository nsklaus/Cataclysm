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
#include <curses.h>

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

std::string extract_disassembly_info(const std::string &info) {
	std::istringstream stream(info);
	std::string line;
	std::string result;
	while (std::getline(stream, line)) {
		if (line.find("- ") != std::string::npos) {
			result += line + "\n";
		}
	}
	return result;
}

std::vector<itype_id> load_item_ids_from_directory(const std::string &directory_path, std::ofstream &log_file) {
	std::vector<itype_id> item_ids;
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
							item_ids.push_back(item.get_string("id"));
						}
					}
				} else if (jsin.test_object()) {
					JsonObject json = jsin.get_object();
					if (json.has_array("items")) {
						JsonArray items = json.get_array("items");
						for (JsonObject item : items) {
							if (item.has_string("id")) {
								item_ids.push_back(item.get_string("id"));
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

	log_file << "Loading items..." << std::endl;

	std::vector<itype_id> item_ids;
	std::vector<std::string> directories = {"data/json/items/tool", "data/json/items/resources"};
	for (const auto &directory : directories) {
		std::vector<itype_id> ids_from_directory = load_item_ids_from_directory(directory, log_file);
		item_ids.insert(item_ids.end(), ids_from_directory.begin(), ids_from_directory.end());
	}

	for (const auto &id : item_ids) {
		const itype *e = item_controller->find_template(id);
		if (!e) {
			log_file << "Missing item definition: " << id << std::endl;
			continue;
		}
		if (e->phase == phase_id::PNULL) {
			continue;
		}
		if (e->gun || e->magazine || e->ammo || e->tool || e->armor || e->book || e->bionic || e->container || e->artifact || e->comestible || e->brewable) {
			all_items.emplace_back(e->get_id());
			// log_file << "Added item: " << e->get_id().c_str() << std::endl;
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



			const auto &uncraft_components = selected_item.get_uncraft_components();
			if (!uncraft_components.empty()) {
				mvwprintz(w, point(40, 7), c_white, "Disassembling info:");
				int line = 8;  // Start from the next line
				for (const auto &component : uncraft_components) {
					const itype *comp_itype = item::find_type(component.type);
					if (comp_itype) {
						mvwprintz(w, point(40, line), c_light_gray, "- %s x%d", strip_tags(comp_itype->nname(1)).c_str(), component.count);
					}
					line++;
				}
			}
			/*
			std::string description = selected_item.info(true);
			std::string disassembly_info = extract_disassembly_info(description);
			if (!disassembly_info.empty()) {
				line++;
				mvwprintz(w, point(40, 7), c_light_gray, "Disassembling info:");
				line++;
				line++;
				std::istringstream stream(disassembly_info);
				std::string word;
				int x = 40;

				while (stream >> word) {
					if (x + word.length() > static_cast<std::string::size_type>(TERMX - 1)) {
						x = 40;
						line++;
					}
					mvwprintz(w, point(x, line), c_light_gray, "%s ", word.c_str());
					x += word.length() + 1;
				}
				line++;
			}*/
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
