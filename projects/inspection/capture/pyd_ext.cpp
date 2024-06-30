#include "stdafx.h"

#if 0
#define PYD_VERSION "24.625.2055.0"

namespace pybind = pybind11;

static void add(const unsigned long long& window,const pybind11::object& key,const pybind11::object& text) {
	std::cout <<"window=" << window << std::endl;
	std::cout << "key=" << (key.is_none() ? "None" : key.cast<std::string>()) << std::endl;
	std::cout << "text=" << (text.is_none() ? "None" : text.cast<std::string>()) << std::endl;


}
PYBIND11_MODULE(capture, _module) {
 _module.doc() = "PDRPA(R) from 'git@github.com:skstu' on pyd.";
 _module.def("capture_device_version", 
	 []() { return PYD_VERSION; },
	 "A function return version");

 _module.def("add",
	 &add,
	 pybind::arg("window"),
	 pybind::arg("key").none(true),
	 pybind::arg("text").none(true),
	 "PYD_API bool op_begin(void)"
 );

 _module.def("op_begin",
	 &op_begin,
	 "PYD_API bool op_begin(void)"
 );

 _module.def("op_end",
	 &op_end,
	 "PYD_API void op_end(void)"
 );

 _module.def("op_find_capture",
  &op_find_capture,
  pybind::arg("window_type"),
  pybind::arg("key").none(true),
  "PYD_API UINT op_find_capture(int window_type, std::string key)"
 );

 _module.def("op_click",
	 &op_click,
	 pybind::arg("window"),
	 pybind::arg("key").none(true),
	 "PYD_API bool op_click(UINT item_key)"
 );

 _module.def("op_double_click",
	 &op_double_click,
	 pybind::arg("window"),
	 pybind::arg("key").none(true),
	 "PYD_API bool op_double_click(UINT item_key)"
 );

 /*_module.def("op_send_keys", &op_send_keys, "A function that sends keys to a window",
	 pybind11::arg("window"), pybind11::arg("key"), pybind11::arg("text"));*/


 _module.def("op_send_keys",
	 &op_send_keys,
	 pybind11::arg("window"), 
	 pybind11::arg("key").none(true), 
	 pybind11::arg("text").none(true),
	 "PYD_API bool op_send_keys(UINT item_key, std::string text)"
 );

 _module.def("op_mouse_drag_item2item",
	 &op_mouse_drag_item2item,
	 pybind::arg("window1"),
	 pybind::arg("key1").none(true),
	 pybind::arg("window2"),
	 pybind::arg("key2").none(true),
	 "PYD_API bool op_mouse_drag_item2item(UINT drag_key, UINT drop_key)"
 );

 _module.def("op_mouse_move_to",
	 &op_mouse_move_to,
	 pybind::arg("window"),
	 pybind::arg("key").none(true),
	 pybind::arg("delay"),
	 "PYD_API bool op_mouse_move_to(UINT item_key, UINT delay)"
 );

 _module.def("op_right_click",
	 &op_right_click,
	 pybind::arg("window"),
	 pybind::arg("key").none(true),
	 "PYD_API bool op_right_click(UINT item_key)"
 );

 _module.def("op_show_desktop",
	 &op_show_desktop,
	 "PYD_API bool op_show_desktop(void)"
 );

#endif
#if 0
 _module.def("op_release_capture",
  &op_release_capture,
  pybind::arg("co_obj"),
  pybind::arg("item_key"),
  "A function release the item"
 );

 _module.def("op_set_top",
  &op_set_top,
  pybind::arg("co_obj"),
  pybind::arg("item_key"),
  "A function set the item's window top"
 );

 _module.def("op_draw_border",
  &op_draw_border,
  pybind::arg("co_obj"),
  pybind::arg("item_key"),
  "A function draw the item's border"
 );
 _module.def("op_click",
  &op_click,
  pybind::arg("co_obj"),
  pybind::arg("item_key"),
  "A function click the item"
 );
 _module.def("op_double_click",
  &op_double_click,
  pybind::arg("co_obj"),
  pybind::arg("item_key"),
  "A function double click the item"
 );
 _module.def("op_right_click",
  &op_right_click,
  pybind::arg("co_obj"),
  pybind::arg("item_key"),
  "A function right click the item"
 );
 _module.def("op_set_focus",
  &op_set_focus,
  pybind::arg("co_obj"),
  pybind::arg("item_key"),
  "A function set the item focus"
 );
 _module.def("op_mouse_hover",
  &op_mouse_hover,
  pybind::arg("co_obj"),
  pybind::arg("item_key"),
  "A function mouse hover the item"
 );
 _module.def("op_get_rect",
  &op_get_rect,
  pybind::arg("co_obj"),
  pybind::arg("item_key"),
  pybind::arg("x"),
  pybind::arg("y"),
  pybind::arg("width"),
  pybind::arg("height"),
  "A function get rect of item"
 );
 _module.def("op_send_keys",
  &op_send_keys,
  pybind::arg("co_obj"),
  pybind::arg("item_key"),
  pybind::arg("keys"),
  "A function send keys to item"
 );
 _module.def("op_mouse_move_to",
  &op_mouse_move_to,
  pybind::arg("co_obj"),
  pybind::arg("item_key"),
  pybind::arg("delay"),
  "A function move mouse to item"
 );
 _module.def("op_mouse_drag_item2item",
  &op_mouse_drag_item2item,
  pybind::arg("co_obj"),
  pybind::arg("drag_key"),
  pybind::arg("drop_key"),
  "A function drag item to item"
 );
 _module.def("op_mouse_drag_point2item",
  &op_mouse_drag_point2item,
  pybind::arg("co_obj"),
  pybind::arg("src_x"),
  pybind::arg("src_y"),
  pybind::arg("drop_key"),
  "A function drag point to item"
 );
 _module.def("op_mouse_drag_item2point",
  &op_mouse_drag_item2point,
  pybind::arg("co_obj"),
  pybind::arg("drag_key"),
  pybind::arg("dest_x"),
  pybind::arg("dest_y"),
  "A function drag item to point"
 );
 _module.def("op_mouse_drag_point2point",
  &op_mouse_drag_point2point,
  pybind::arg("co_obj"),
  pybind::arg("src_x"),
  pybind::arg("src_y"),
  pybind::arg("dest_x"),
  pybind::arg("dest_y"),
  "A function drag point to point"
 );

 _module.def("op_mouse_drag_point2item_percent",
  &op_mouse_drag_point2item_percent,
  pybind::arg("co_obj"),
  pybind::arg("src_x"),
  pybind::arg("src_y"),
  pybind::arg("drop_key"),
  "A function drag point to item"
 );
 _module.def("op_mouse_drag_item2point_percent",
  &op_mouse_drag_item2point_percent,
  pybind::arg("co_obj"),
  pybind::arg("drag_key"),
  pybind::arg("dest_x"),
  pybind::arg("dest_y"),
  "A function drag item to point"
 );
 _module.def("op_mouse_drag_point2point_percent",
  &op_mouse_drag_point2point_percent,
  pybind::arg("co_obj"),
  pybind::arg("src_x"),
  pybind::arg("src_y"),
  pybind::arg("dest_x"),
  pybind::arg("dest_y"),
  "A function drag point to point"
 );

}
#endif