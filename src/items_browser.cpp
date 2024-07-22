#include "game.h"
#include "input.h"
#include "output.h"
#include "item.h"
#include "item_factory.h"
#include "item_group.h"
#include "catacharset.h"
#include "material.h"
#include <vector>
#include <string>
#include "string_input_popup.h"
// #include <curses.h>

// Helper function to convert weight to a printable string
std::string weight_to_string(const units::mass &weight) {
	return string_format("%d g", to_gram(weight));
}

// Helper function to convert volume to a printable string
std::string volume_to_string(const units::volume &volume) {
	return string_format("%d ml", to_milliliter(volume));
}

void game::items_browser()
{
	catacurses::window w = new_centered_win( TERMY, TERMX );

	input_context ctxt( "ITEMS_BROWSER" );
	ctxt.register_cardinal();
	ctxt.register_action( "CONFIRM" );
	ctxt.register_action( "QUIT" );
	ctxt.register_action("FILTER");
	ctxt.register_action("SELECT");

	// Load 5 items from wood.json
	std::vector<item> all_items;
	all_items.emplace_back("splinter");
	all_items.emplace_back("2x4");
	all_items.emplace_back("log");
	all_items.emplace_back("stick");
	all_items.emplace_back("long_pole");

	// Filtered items
	std::vector<item> filtered_items = all_items;
	std::string filter_text;
	size_t selected_index = 0;

	std::string bleh = "my items browser";

	while( true ) {
		werase( w );

		// Apply filter
		if (!filter_text.empty()) {
			filtered_items.clear();
			for (const item& it : all_items) {
				if (it.tname().find(filter_text) != std::string::npos) {
					filtered_items.push_back(it);
				}
			}
		} else {
			filtered_items = all_items;
		}


		mvwprintz( w, point( 2, 2 ), c_light_red, bleh );
		//wrefresh( w_ibrowse );

		// Print the list of items
		for (size_t i = 0; i < filtered_items.size(); ++i) {
			nc_color color = (i == selected_index) ? h_white : c_white;
			mvwprintz(w, point(5, 5 + i), color, filtered_items[i].tname());
		}

		// Print item details
		if (!filtered_items.empty()) {
			const item &selected_item = filtered_items[selected_index];
			mvwprintz(w, point(40, 5), c_light_blue, "Details:");
			mvwprintz(w, point(40, 6), c_light_gray, "Name: %s", selected_item.tname().c_str());
			mvwprintz(w, point(40, 7), c_light_gray, "Weight: %s", weight_to_string(selected_item.weight()).c_str());
			mvwprintz(w, point(40, 8), c_light_gray, "Volume: %s", volume_to_string(selected_item.volume()).c_str());
			mvwprintz(w, point(40, 9), c_light_gray, "Material: %s", selected_item.get_base_material().name().c_str());
		}

		// Refresh the window
		wrefresh(w);

		const std::string action = ctxt.handle_input();

		if( action == "QUIT" ) {
			break;

		} else if (action == "UP" && !filtered_items.empty()) {
			if (selected_index > 0) {
				selected_index--;
			}
		} else if (action == "DOWN" && !filtered_items.empty()) {
			if (selected_index < filtered_items.size() - 1) {
				selected_index++;
			}
		} else if (action == "FILTER") {
			filter_text = string_input_popup().title("Filter:").query_string();
			selected_index = 0; // Reset selection index after filtering
		}
		else if( action == "FILTER" ) {
			bleh = "pressed filter";
		}
		else if( action == "SELECT" ) {
			bleh = "pressed select";
		}
	}
	refresh_all();
}
