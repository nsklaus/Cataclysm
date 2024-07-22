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
	catacurses::window w_ibrowse = new_centered_win( TERMY, TERMX );

	input_context ctxt( "ITEMS_BROWSER" );
	ctxt.register_cardinal();
	ctxt.register_action( "CONFIRM" );
	ctxt.register_action( "QUIT" );
	ctxt.register_action("FILTER");
	ctxt.register_action("SELECT");

	// Load 5 items from wood.json
	std::vector<item> items;
	items.emplace_back("splinter");
	items.emplace_back("2x4");
	items.emplace_back("log");
	items.emplace_back("stick");
	items.emplace_back("long_pole");

	size_t selected_index = 0;
	std::string bleh = "my items browser";

	while( true ) {
		werase( w_ibrowse );
		mvwprintz( w_ibrowse, point( 2, 2 ), c_light_red, bleh );
		//wrefresh( w_ibrowse );

		// Print the list of items
		for (size_t i = 0; i < items.size(); ++i) {
			mvwprintz(w_ibrowse, point(6, 4 + i), c_white, items[i].tname());
		}

		// Print item details
		const item &selected_item = items[selected_index];
		mvwprintz(w_ibrowse, point(40, 1), c_light_blue, "Details:");
		mvwprintz(w_ibrowse, point(40, 2), c_light_gray, "Name: %s", selected_item.tname().c_str());
		mvwprintz(w_ibrowse, point(40, 3), c_light_gray, "Weight: %s", weight_to_string(selected_item.weight()).c_str());
		mvwprintz(w_ibrowse, point(40, 4), c_light_gray, "Volume: %s", volume_to_string(selected_item.volume()).c_str());
		mvwprintz(w_ibrowse, point(40, 5), c_light_gray, "Material: %s", selected_item.get_base_material().name().c_str());


		// Refresh the window
		wrefresh(w_ibrowse);

		const std::string action = ctxt.handle_input();

		if( action == "QUIT" ) {
			break;
		}
		else if( action == "UP" ) {
			if (selected_index > 0) {
				selected_index--;
				bleh = "pressed up";
			}

		}
		else if( action == "DOWN" ) {
			if (selected_index < items.size() - 1) {
				selected_index++;
				bleh = "pressed down";
			}
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
