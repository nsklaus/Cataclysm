#include "item_factory.h"
#include "output.h"
#include "input.h"
#include "game.h"
#include "item.h"
#include "material.h"
#include "string_input_popup.h"
#include "json.h"
#include <vector>
#include <string>
#include <fstream>
#include <dirent.h>
#include <curses.h>
#include <set>




std::vector<itype_id> load_item_ids_from_directory(const std::string &directory_path ){

	std::set<std::string> unique_names;
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

			JsonIn jsin(file);

			try {
			    if (jsin.test_array()) {
			        JsonArray items = jsin.get_array();
			        for (JsonObject item : items) {
			            if (item.has_string("id") && item.has_object("name")) {
			                JsonObject name_obj = item.get_object("name");
			                if (name_obj.has_string("str")) {
			                    std::string name = name_obj.get_string("str");
								// don't add duplicates
			                    if (unique_names.insert(name).second) {
                                    itype_id id(item.get_string("id"));
                                    item_ids.push_back(id);
			                        //item_ids.push_back(item.get_string("id"));
			                        //fprintf(stderr, "json array item ID: %s\n", item.get_string("id").c_str());
			                    } else {
									// print duplicates on stderr
									// fprintf(stderr, "json array item ID: %s\n", item.get_string("id").c_str());
								}
			                }
			                //name_obj.allow_omitted_members();
                            name_obj.allow_omitted_members();
                            
			            }
                        item.allow_omitted_members();
			        }
			    }
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
	//std::vector<std::string> directories = {"data/json/items"};

	for (const auto &directory : directories) {
		std::vector<itype_id> ids_from_directory = load_item_ids_from_directory(directory); //, log_file);
		item_ids.insert(item_ids.end(), ids_from_directory.begin(), ids_from_directory.end());
	}

	int entry_count_all = 0;
	int entry_count_filtered = 0;

	//for (const auto &id : item_ids) {
	//	const itype *e = item_controller->find_template(id);
	//	if (e != nullptr) {
	//		entry_count_all++;
	//		all_items.emplace_back(e->get_id());
	//	}
	//}

	for (const auto &id : item_ids) {
        for (const auto &item : item_controller->all()) {
            if (item->get_id() == id) {
                entry_count_all++;
                all_items.emplace_back(item->get_id());
            }
        }
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

        center_print(w, 0, c_white, "CODEX (work in progress)");
        //mvwprintz(w, point(0,0), c_white, "CODEX (work in progress)");
		mvwprintz(w, point(1, 2 ), c_white, "item count: %d",entry_count_filtered);
		for (size_t i = top_line; i < filtered_items.size() && i < top_line + num_lines; ++i) {
			nc_color color = (i == selected_index) ? h_white : c_white;
			mvwprintz(w, point(1, 4 + i - top_line), color, filtered_items[i].tname().c_str());
		}

		int line = 3;  // Start from the next line
		mvwprintz(w, point(50, 2), c_light_blue, "Details:");
		if (!filtered_items.empty()) {
			const item &selected_item = filtered_items[selected_index];

			mvwprintz(w, point(50, ++line), c_light_gray, "Name: %s", selected_item.tname().c_str());
			const std::string capacity = selected_item.get_property_string("weight").c_str();
            //fprintf(stderr,"weight=%s\n",capacity.c_str());
            mvwprintz(w, point(50, ++line), c_light_gray, "Weight: %s",capacity.c_str());// get_weight_string().c_str());
			//mvwprintz(w, point(50, ++line), c_light_gray, "Volume: %s", selected_item.get_volume_string().c_str());
			// if (selected_item.components.empty()) {
			// 	mvwprintz(w, point(50, ++line), c_light_gray, "Components: No components");
			// } else {
			// 	mvwprintz(w, point(50, ++line), c_light_gray, "Components: %s", selected_item.components_to_string().c_str());
			// }


			line+=2;

            std::string bleh =  selected_item.components_to_string().c_str();  //.disassembly_requirements();
            //fprintf(stderr,"bleh=%s\n",bleh.c_str());
            mvwprintz(w, point(50, line), c_light_gray, "- %s",bleh);
            /*
			const auto &uncraft_components = selected_item.get_uncraft_components();

			if (!uncraft_components.empty()) {
				mvwprintz(w, point(50, line++), c_white, "Disassembling info:");

				for (const auto &component : uncraft_components) {
					const itype *comp_itype = item::find_type(component.type);
					if (comp_itype) {
						mvwprintz(w, point(50, line), c_light_gray, "- %s x%d", comp_itype->nname(1).c_str(), component.count);
					}
					line++;
				}
			}
			*/


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
