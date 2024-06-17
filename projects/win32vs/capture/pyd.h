#ifndef CAPTURE_PYD_H
#define CAPTURE_PYD_H

#ifdef _DEBUG
#define PYD_API extern "C" __declspec(dllexport)
#else
#define PYD_API
#endif
PYD_API bool rpa_win32_start();
PYD_API void rpa_win32_stop();
PYD_API ULLPTR op_init();
PYD_API void op_release(ULLPTR co_obj);
PYD_API UINT op_find_capture(ULLPTR co_obj, const std::string& strJson);
PYD_API bool op_show_desktop(ULLPTR co_obj);
PYD_API bool op_release_capture(ULLPTR co_obj, UINT item_key);
PYD_API bool op_set_top(ULLPTR co_obj, UINT item_key);
PYD_API bool op_draw_border(ULLPTR co_obj, UINT item_key);
PYD_API bool op_click(ULLPTR co_obj, UINT item_key);
PYD_API bool op_double_click(ULLPTR co_obj, UINT item_key);
PYD_API bool op_right_click(ULLPTR co_obj, UINT item_key);
PYD_API bool op_set_focus(ULLPTR co_obj, UINT item_key);
PYD_API bool op_mouse_hover(ULLPTR co_obj, UINT item_key);
PYD_API bool op_get_rect(ULLPTR co_obj, UINT item_key, int& pt_x, int& pt_y, int& width, int& height);
PYD_API bool op_send_keys(ULLPTR co_obj, UINT item_key, const std::string& strValue);
PYD_API bool op_mouse_move_to(ULLPTR co_obj, UINT item_key, UINT delay = 100);
PYD_API bool op_mouse_drag_item2item(ULLPTR co_obj, UINT drag_key, UINT drop_key);
PYD_API bool op_mouse_drag_point2item(ULLPTR co_obj, long ptSrcX, long ptSrcY, UINT drop_key);
PYD_API bool op_mouse_drag_item2point(ULLPTR co_obj, UINT drag_key, long ptDestX, long ptDestY);
PYD_API bool op_mouse_drag_point2point(ULLPTR co_obj, long ptSrcX, long ptSrcY, long ptDestX, long ptDestY);
PYD_API bool op_mouse_drag_point2item_percent(ULLPTR co_obj, float ptSrcPercentX, float ptSrcPercentY, UINT drop_key);
PYD_API bool op_mouse_drag_item2point_percent(ULLPTR co_obj, UINT drag_key, float ptDestPercentX, float ptDestPercentY);
PYD_API bool op_mouse_drag_point2point_percent(ULLPTR co_obj, float ptSrcPercentX, float ptSrcPercentY, float ptDestPercentX, float ptDestPercentY);

#endif //CAPTURE_PYD_H
