#include "stdafx.h"

namespace pybind = pybind11;


PYBIND11_MODULE(capture, _module) {
 _module.doc() = "PDRPA(R) from UIAutomation on pyd.";
 _module.def("capture_device_version", []() { return "0.1.5"; }, "A function return version");

 _module.def("capture_start",
	 &rpa_win32_start,
	 "A function create CaptureOperator"
 );

 _module.def("rpa_win32_stop",
	 &rpa_win32_stop,
	 "A function create CaptureOperator"
 );



 _module.def("op_init",
  &op_init,
  "A function create CaptureOperator"
 );

 _module.def("op_show_desktop", &op_show_desktop, "Quick show desktop.");

 _module.def("op_release",
  &op_release,
  pybind::arg("co_obj"),
  "A function release CaptureOperaotr"
 );

 _module.def("op_find_capture",
  &op_find_capture,
  pybind::arg("co_obj"),
  pybind::arg("json_str"),
  "A function find a capture item"
 );

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