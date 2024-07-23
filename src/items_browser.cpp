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

/* works
// manual loading json file test
void parse_json_file(const std::string &file_path) {
	std::ifstream file(file_path);
	if (!file.is_open()) {
		std::cerr << "Failed to open file: " << file_path << std::endl;
		return;
	}

	JsonIn json(file);
	JsonArray array = json.get_array();

	while (array.has_more()) {
		JsonObject data = array.next_object();

		std::string name;
		if (data.has_string("name")) {
			name = data.get_string("name");
		} else if (data.has_object("name")) {
			JsonObject name_obj = data.get_object("name");
			name = name_obj.get_string("str", "Unknown");
		} else {
			name = "Unknown";
		}

		std::string category = data.get_string("category", "Unknown");
		std::string id = data.get_string("id", "Unknown");
		std::string type = data.get_string("type", "Unknown");

		// Handle material as an array or a single string
		std::string material;
		if (data.has_array("material")) {
			JsonArray materials = data.get_array("material");
			material = materials.get_string(0); // Assuming you want the first material
		} else if (data.has_string("material")) {
			material = data.get_string("material");
		} else {
			material = "Unknown";
		}

		std::cout << "Name: " << name << std::endl;
		std::cout << "Category: " << category << std::endl;
		std::cout << "Material: " << material << std::endl;
		std::cout << "ID: " << id << std::endl;
		std::cout << "Type: " << type << std::endl;
		std::cout << "-----------------------------" << std::endl;
	}

	file.close();
}
*/


/* simple, works */
/*
void parse_json_file(const std::string &file_path) {
	std::ifstream file(file_path);
	if (!file.is_open()) {
		std::cerr << "Failed to open file: " << file_path << std::endl;
		return;
	}

	JsonIn jsin(file);

	try {
		jsin.start_array();
		while (!jsin.end_array()) {
			JsonObject obj = jsin.get_object();

			if (obj.has_string("id")) {
				std::string id = obj.get_string("id");
				std::cout << "ID: " << id << std::endl;
			}

			if (obj.has_string("name")) {
				std::string name = obj.get_string("name");
				std::cout << "Name: " << name << std::endl;
			}

			if (obj.has_string("category")) {
				std::string category = obj.get_string("category");
				std::cout << "Category: " << category << std::endl;
			}

			if (obj.has_string("material")) {
				std::string material = obj.get_string("material");
				std::cout << "Material: " << material << std::endl;
			}

			if (obj.has_string("weight")) {
				std::string weight = obj.get_string("weight");
				std::cout << "Weight: " << weight << std::endl;
			}

			if (obj.has_string("volume")) {
				std::string volume = obj.get_string("volume");
				std::cout << "Volume: " << volume << std::endl;
			}

			if (obj.has_string("description")) {
				std::string description = obj.get_string("description");
				std::cout << "Description: " << description << std::endl;
			}
		}
	} catch (const JsonError &e) {
		std::cerr << "Error parsing JSON: " << e.what() << std::endl;
	}
}
*/


std::vector<itype_id> load_item_ids_from_directory(const std::string &directory_path ){ //, std::ofstream &log_file) {
	std::vector<itype_id> item_ids;
	DIR *dir = opendir(directory_path.c_str());
	// fprintf(stderr,"dir: %s\n", directory_path.c_str());
	if (dir == nullptr) {
		//log_file << "Failed to open directory: " << directory_path << std::endl;
		return item_ids;
	}
	//int entry_count = 0;
	struct dirent *entry;
	while ((entry = readdir(dir)) != nullptr) {
		std::string file_name = entry->d_name;
		if (file_name.size() > 5 && file_name.substr(file_name.size() - 5) == ".json") {
			std::string full_path = directory_path + "/" + file_name;
			std::ifstream file(full_path);
			if (!file.is_open()) {
				//log_file << "Failed to open JSON file: " << full_path << std::endl;
				fprintf(stderr,"error opening file: %s\n", full_path.c_str());
				continue;
			} // else { fprintf(stderr,"opening file: %s\n", full_path.c_str()); }
			JsonIn jsin(file);
			try {
				if (jsin.test_array()) {
					JsonArray items = jsin.get_array();
					for (JsonObject item : items) {
						if (item.has_string("id")) {
							item_ids.push_back(item.get_string("id"));
							// fprintf(stderr,"case jsonarray, ID: %s\n", item.get_string("id").c_str());
							// entry_count++;
						}
					}
				} else if (jsin.test_object()) {
					JsonObject json = jsin.get_object();
					if (json.has_array("items")) {
						JsonArray items = json.get_array("items");
						for (JsonObject item : items) {
							if (item.has_string("id")) {
								item_ids.push_back(item.get_string("id"));
								// fprintf(stderr,"case jsonobject, ID: %s\n", item.get_string("id").c_str());
								// entry_count++;
							}
						}
					}
				}
			} catch (const JsonError &err) {
				//log_file << "Error parsing JSON file: " << full_path << " - " << err.what() << std::endl;
				fprintf(stderr, "Error parsing JSON file: %s", err.what());
			}
			file.close();
		}
	}

	// fprintf(stderr,"total ID count: %d\n", entry_count);
	closedir(dir);
	return item_ids;
}

void game::items_browser() {
	// std::ofstream log_file("items_browser.log", std::ios::trunc);
	// if (!log_file.is_open()) {
	// 	std::cerr << "Failed to open log file for writing." << std::endl;
	// 	return;
	// }
	catacurses::window w = catacurses::newwin(TERMY, TERMX, point_zero);
	input_context ctxt("ITEMS_BROWSER");
	ctxt.register_action("QUIT");
	ctxt.register_action("UP");
	ctxt.register_action("DOWN");
	ctxt.register_action("FILTER");

	std::vector<item> all_items;

	//log_file << "Loading items..." << std::endl;
	// parse_json_file("data/json/items/resources/wood.json");
	//parse_json_file("data/json/recipes/weapon/explosive.json");

	std::vector<itype_id> item_ids;
	std::vector<std::string> directories = {"data/json/items/tool", "data/json/items/resources", "data/json/items/melee", "data/json/items/generic"};
	//std::vector<std::string> directories = {"data/json/items/resources"};
	//std::vector<std::string> directories = {"data/json/recipes/weapon"};

	for (const auto &directory : directories) {
		std::vector<itype_id> ids_from_directory = load_item_ids_from_directory(directory); //, log_file);
		item_ids.insert(item_ids.end(), ids_from_directory.begin(), ids_from_directory.end());
	}

	int entry_count_all = 0;
	int entry_count_filtered = 0;
	for (const auto &id : item_ids) {
		const itype *e = item_controller->find_template(id);
		//fprintf(stderr," adding: %s\n", id.c_str());
		if (!e) {
			//log_file << "Missing item definition: " << id << std::endl;
			continue;
		}
		if (e->phase == phase_id::PNULL) {
			continue;
		}
		//if (e->gun || e->magazine || e->ammo || e->tool || e->armor || e->book || e->bionic || e->container || e->artifact || e->comestible || e->brewable ) {
		entry_count_all++;
			all_items.emplace_back(e->get_id());
			//fprintf(stderr," adding: %s\n", id.c_str());
			// log_file << "Added item: " << e->get_id().c_str() << std::endl;
		//}
	}

	//log_file << "Total items loaded: " << all_items.size() << std::endl;
	std::vector<item> filtered_items = all_items;
	std::string filter_text;
	size_t selected_index = 0;
	size_t top_line = 0;

	while (true) {
		werase(w);
		if (!filter_text.empty()) {
			entry_count_filtered = 0;
			filtered_items.clear();
			for (const item &it : all_items) {
				//if (strip_tags(it.tname()).find(filter_text) != std::string::npos) {
				if (it.tname().find(filter_text) != std::string::npos) {
					entry_count_filtered++;
					filtered_items.push_back(it);
				}
			}
		} else {
			entry_count_filtered = entry_count_all;
			filtered_items = all_items;
		}
		const int num_lines = TERMY - 2;

		mvwprintz(w, point(1, 1 ), c_white, "item count: %d",entry_count_filtered);
		for (size_t i = top_line; i < filtered_items.size() && i < top_line + num_lines; ++i) {
			nc_color color = (i == selected_index) ? h_white : c_white;
			// mvwprintz(w, point(1, 1 + i - top_line), color, strip_tags(filtered_items[i].tname()).c_str());

			mvwprintz(w, point(1, 3 + i - top_line), color, filtered_items[i].tname().c_str());
		}

		if (!filtered_items.empty()) {
			const item &selected_item = filtered_items[selected_index];
			mvwprintz(w, point(40, 3), c_light_blue, "Details:");
			mvwprintz(w, point(40, 4), c_light_gray, "Name: %s", selected_item.tname().c_str());
			mvwprintz(w, point(40, 5), c_light_gray, "Weight: %s", weight_to_string(selected_item.weight()).c_str());
			mvwprintz(w, point(40, 6), c_light_gray, "Volume: %s", volume_to_string(selected_item.volume()).c_str());



			const auto &uncraft_components = selected_item.get_uncraft_components();
			if (!uncraft_components.empty()) {
				mvwprintz(w, point(40, 8), c_white, "Disassembling info:");
				int line = 9;  // Start from the next line
				for (const auto &component : uncraft_components) {
					const itype *comp_itype = item::find_type(component.type);
					if (comp_itype) {
						mvwprintz(w, point(40, line), c_light_gray, "- %s x%d", comp_itype->nname(1).c_str(), component.count);
					}
					line++;
				}
			}

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
	//log_file.close();
}
