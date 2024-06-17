#include "stdafx.h"

PYD_API ULLPTR op_init() {
	ULLPTR result = 0;

	if (!__gpCore) {
		__gpCore = new Core(reinterpret_cast<HMODULE>(__gpHinstance));
	}

	if (!__gpCore->Ready())
		SK_DELETE_PTR(__gpCore);

	result = reinterpret_cast<ULLPTR>(__gpCore);

	LOG_OUTPUT(R"(
--- function: {},
--- result: {}
)", __FUNCTION__, result);
	return result;
}
PYD_API void op_release(ULLPTR co_obj) {
	LOG_OUTPUT(R"(
--- function: {}
)", __FUNCTION__);
	SK_DELETE_PTR(__gpCore);
}

PYD_API bool rpa_win32_start() {
	bool result = false;
	do {
		if (!__gpCore)
			break;
		result = __gpCore->Start();
	} while (0);
	return result;
}
PYD_API void rpa_win32_stop() {
	do {
		if (!__gpCore)
			break;
		__gpCore->Stop();
	} while (0);
}


PYD_API UINT op_find_capture(ULLPTR co_obj, const std::string& strJson) {
	return 0;
}
PYD_API bool op_show_desktop(ULLPTR co_obj) {
	return false;
}
PYD_API bool op_release_capture(ULLPTR co_obj, UINT item_key) {
	return false;
}
PYD_API bool op_set_top(ULLPTR co_obj, UINT item_key) {
	return false;
}
PYD_API bool op_draw_border(ULLPTR co_obj, UINT item_key) {
	return false;
}
PYD_API bool op_click(ULLPTR co_obj, UINT item_key) {
	return false;
}
PYD_API bool op_double_click(ULLPTR co_obj, UINT item_key) {
	return false;
}
PYD_API  bool op_right_click(ULLPTR co_obj, UINT item_key) {
	return false;
}
PYD_API bool op_set_focus(ULLPTR co_obj, UINT item_key) {
	return false;
}
PYD_API bool op_mouse_hover(ULLPTR co_obj, UINT item_key) {
	return false;
}
PYD_API  bool op_get_rect(ULLPTR co_obj, UINT item_key, int& pt_x, int& pt_y, int& width, int& height) {
	return false;
}
PYD_API  bool op_send_keys(ULLPTR co_obj, UINT item_key, const std::string& strValue) {
	return false;
}
PYD_API  bool op_mouse_move_to(ULLPTR co_obj, UINT item_key, UINT delay /*= 100*/) {
	return false;
}
PYD_API  bool op_mouse_drag_item2item(ULLPTR co_obj, UINT drag_key, UINT drop_key) {
	return false;
}
PYD_API  bool op_mouse_drag_point2item(ULLPTR co_obj, long ptSrcX, long ptSrcY, UINT drop_key) {
	return false;
}
PYD_API  bool op_mouse_drag_item2point(ULLPTR co_obj, UINT drag_key, long ptDestX, long ptDestY) {
	return false;
}
PYD_API  bool op_mouse_drag_point2point(ULLPTR co_obj, long ptSrcX, long ptSrcY, long ptDestX, long ptDestY) {
	return false;
}
PYD_API  bool op_mouse_drag_point2item_percent(ULLPTR co_obj, float ptSrcPercentX, float ptSrcPercentY, UINT drop_key) {
	return false;
}
PYD_API bool op_mouse_drag_item2point_percent(ULLPTR co_obj, UINT drag_key, float ptDestPercentX, float ptDestPercentY) {
	return false;
}
PYD_API bool op_mouse_drag_point2point_percent(ULLPTR co_obj, float ptSrcPercentX, float ptSrcPercentY, float ptDestPercentX, float ptDestPercentY) {
	return false;
}

