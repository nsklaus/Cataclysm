#include "item_factory.h"
#include "output.h"
#include "input.h"
#include "game.h"
#include "item.h"
// #include "catacharset.h"
#include "units.h"
#include "string_formatter.h"
#include "material.h"
#include "string_input_popup.h"
#include "json.h"
#include <vector>
#include <string>
// #include <map>
// #include <iostream>
#include <fstream>
#include <dirent.h>
// #include <sstream>
#include <curses.h>
#include <set>

std::string weight_to_string(const units::mass &weight) {
	return string_format("%d g", to_gram(weight));
}

std::string volume_to_string(const units::volume &volume) {
	return string_format("%d ml", to_milliliter(volume));
}

std::set<std::string> unique_names;
std::vector<itype_id> item_ids;

std::vector<itype_id> load_item_ids_from_directory(const std::string &directory_path ){
	std::vector<itype_id> item_ids;
	DIR *dir = opendir(directory_path.c_str());
	if (dir == nullptr) {
		return item_ids;
	}
	//int entry_count = 0;
	struct dirent *entry;
	std::set<itype_id> unique_ids;
	while ((entry = readdir(dir)) != nullptr) {
		std::string file_name = entry->d_name;
		if (file_name.size() > 5 && file_name.substr(file_name.size() - 5) == ".json") {
			std::string full_path = directory_path + "/" + file_name;
			std::ifstream file(full_path);
			if (!file.is_open()) {
				fprintf(stderr,"error opening file: %s\n", full_path.c_str());
				continue;
			}
//			std::set<itype_id> unique_ids;

			JsonIn jsin(file);
//			try {
//				if (jsin.test_array()) {
//					JsonArray items = jsin.get_array();
//					for (JsonObject item : items) {
//						if (item.has_string("id") )  {
//							std::string id = item.get_string("name");
//							if (unique_ids.insert(id).second) {
//							item_ids.push_back(item.get_string("id"));
//							fprintf(stderr,"json array item ID: %s\n", item.get_string("id").c_str());
//							}
//						}
//					}
//				}

try {
    if (jsin.test_array()) {
        JsonArray items = jsin.get_array();
        for (JsonObject item : items) {
            if (item.has_string("id") && item.has_object("name")) {
                JsonObject name_obj = item.get_object("name");
                if (name_obj.has_string("str")) {
                    std::string name = name_obj.get_string("str");
                    if (unique_names.insert(name).second) {
                        item_ids.push_back(item.get_string("id"));
                        //fprintf(stderr, "json array item ID: %s\n", item.get_string("id").c_str());
                    }
                }
            }
        }
    }
//}

			//	else if (jsin.test_object()) {
			//		JsonObject json = jsin.get_object();
			//		if (json.has_array("items")) {
			//			JsonArray items = json.get_array("items");
			//			for (JsonObject item : items) {
			//				if (item.has_string("id")) {
			//					item_ids.push_back(item.get_string("id"));
			//					fprintf(stderr,"json object item ID: %s\n", item.get_string("id").c_str());
			//				}
			//			}
			//		}
			//	}
			} catch (const JsonError &err) {
				fprintf(stderr, "Error parsing JSON file: %s", err.what());
			}
			file.close();
		}
	}
	closedir(dir);
	return item_ids;
}


void game::items_browser() {

	catacurses::window w = catacurses::newwin(TERMY, TERMX, point_zero);
	input_context ctxt("ITEMS_BROWSER");
	ctxt.register_action("QUIT");
	ctxt.register_action("UP");
	ctxt.register_action("DOWN");
	ctxt.register_action("FILTER");


	std::vector<itype_id> item_ids;
	std::vector<item> all_items;

	std::vector<std::string> directories = {"data/json/items/tool", "data/json/items/resources", "data/json/items/melee", "data/json/items/generic"};
	//std::vector<std::string> directories = {"data/json/items/resources"};
	//std::vector<std::string> directories = {"data/json/recipes/weapon"};

	for (const auto &directory : directories) {
		std::vector<itype_id> ids_from_directory = load_item_ids_from_directory(directory); //, log_file);
		item_ids.insert(item_ids.end(), ids_from_directory.begin(), ids_from_directory.end());
	}

	int entry_count_all = 0;
	int entry_count_filtered = 0;

	//std::set<itype_id> unique_ids;
	for (const auto &id : item_ids) {
		//if (unique_ids.insert(id).second) {
			const itype *e = item_controller->find_template(id);
			if (e != nullptr) {
				entry_count_all++;
				all_items.emplace_back(e->get_id());
			}
		//}
	}

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

				if (it.tname().find(filter_text) != std::string::npos) {
					entry_count_filtered++;
					filtered_items.push_back(it);
				}
			}
		} else {
			entry_count_filtered = entry_count_all;
			filtered_items = all_items;
		}
		const int num_lines = TERMY - 4;

		mvwprintz(w, point(1, 1 ), c_white, "item count: %d",entry_count_filtered);
		for (size_t i = top_line; i < filtered_items.size() && i < top_line + num_lines; ++i) {
			nc_color color = (i == selected_index) ? h_white : c_white;
			mvwprintz(w, point(1, 3 + i - top_line), color, filtered_items[i].tname().c_str());
			//fprintf(stderr," filtered_items[i].tname() = %s\n", filtered_items[i].tname().c_str());
		}

		if (!filtered_items.empty()) {
			const item &selected_item = filtered_items[selected_index];
			mvwprintz(w, point(50, 3), c_light_blue, "Details:");
			mvwprintz(w, point(50, 4), c_light_gray, "Name: %s", selected_item.tname().c_str());
			mvwprintz(w, point(50, 5), c_light_gray, "Weight: %s", weight_to_string(selected_item.weight()).c_str());
			mvwprintz(w, point(50, 6), c_light_gray, "Volume: %s", volume_to_string(selected_item.volume()).c_str());



			const auto &uncraft_components = selected_item.get_uncraft_components();
			if (!uncraft_components.empty()) {
				mvwprintz(w, point(50, 8), c_white, "Disassembling info:");
				int line = 9;  // Start from the next line
				for (const auto &component : uncraft_components) {
					const itype *comp_itype = item::find_type(component.type);
					if (comp_itype) {
						mvwprintz(w, point(50, line), c_light_gray, "- %s x%d", comp_itype->nname(1).c_str(), component.count);
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
}
