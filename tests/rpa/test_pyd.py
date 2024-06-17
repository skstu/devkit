import zbot
import zbot.win32.capture as capture
import time
import atexit

b = capture.rpa_win32_start()

capture.rpa_win32_stop()


co_obj = zbot.win32.capture.op_init()
zbot.win32.capture.op_release(co_obj=co_obj)


'''

file = zbot.win32.capture.op_find_capture(co_obj=co_obj, json_str='{"code":0,"msg":"ok","data":{"Rect":[150,1279,224,1351],"FrameworkID":"Win32","ControlType":50007,"WebText":"test.py","ProcessName":"","ProcessPath":"","ProcessId":11652,"hWnd":65822,"ancestors":[{"Level":0,"Rect":[150,1279,224,1351],"hWnd":0,"FrameworkID":"Win32","ControlType":50007,"ProcessId":11652,"WebText":"test.py"},{"Level":1,"Rect":[150,1279,224,1351],"hWnd":65822,"WndClass":"SysListView32","WndText":"FolderVie","AutomationID":"1","FrameworkID":"Win32","ControlType":50008,"ProcessId":11652,"WebClass":"SysListView32","WebText":"桌面"},{"Level":2,"Rect":[150,1279,224,1351],"hWnd":65820,"WndClass":"SHELLDLL_DefView","FrameworkID":"Win32","ControlType":50033,"ProcessId":11652,"WebClass":"SHELLDLL_DefView"},{"Level":3,"Rect":[150,1279,224,1351],"hWnd":65818,"WndClass":"Progman","WndText":"Program Manage","FrameworkID":"Win32","ControlType":50033,"ProcessId":11652,"WebClass":"Progman","WebText":"Program Manager"}]},"name":"test.py"}')
input = zbot.win32.capture.op_find_capture(co_obj=co_obj, json_str='{"code":0,"msg":"ok","data":{"Rect":[503,988,1109,1048],"ControlType":50004,"WebText":"文件传输助手","ProcessName":"","ProcessPath":"","ProcessId":4564,"hWnd":3212974,"ancestors":[{"Level":0,"Rect":[503,988,1109,1048],"hWnd":0,"ControlType":50004,"ProcessId":4564,"WebText":"文件传输助手"},{"Level":1,"Rect":[503,988,1109,1048],"hWnd":0,"ControlType":50033,"ProcessId":4564},{"Level":2,"Rect":[503,988,1109,1048],"hWnd":0,"ControlType":50033,"ProcessId":4564},{"Level":3,"Rect":[503,988,1109,1048],"hWnd":0,"ControlType":50033,"ProcessId":4564},{"Level":4,"Rect":[503,988,1109,1048],"hWnd":0,"ControlType":50033,"ProcessId":4564},{"Level":5,"Rect":[503,988,1109,1048],"hWnd":0,"ControlType":50033,"ProcessId":4564},{"Level":6,"Rect":[503,988,1109,1048],"hWnd":0,"ControlType":50033,"ProcessId":4564},{"Level":7,"Rect":[503,988,1109,1048],"hWnd":0,"ControlType":50033,"ProcessId":4564},{"Level":8,"Rect":[503,988,1109,1048],"hWnd":0,"ControlType":50033,"ProcessId":4564},{"Level":9,"Rect":[503,988,1109,1048],"hWnd":0,"ControlType":50033,"ProcessId":4564},{"Level":10,"Rect":[503,988,1109,1048],"hWnd":0,"ControlType":50033,"ProcessId":4564},{"Level":11,"Rect":[503,988,1109,1048],"hWnd":0,"ControlType":50033,"ProcessId":4564},{"Level":12,"Rect":[503,988,1109,1048],"hWnd":0,"ControlType":50033,"ProcessId":4564},{"Level":13,"Rect":[503,988,1109,1048],"hWnd":3212974,"WndClass":"WeChatMainWndForPC","WndText":"微","FrameworkID":"Win32","ControlType":50032,"ProcessId":4564,"WebClass":"WeChatMainWndForPC","WebText":"微信"}]},"name":"文件传输助手"}')

capture.op_set_focus(co_obj, input)
# capture.op_draw_border(co_obj, input)
time.sleep(3.0)
# capture.op_mouse_move_to(co, input, 100)
# capture.op_click(co, input)
capture.op_set_top(co_obj, file)
capture.op_set_focus(co_obj, file)
# capture.op_click(co, file)
# capture.op_draw_border(co_obj, file)
capture.op_mouse_move_to(co_obj, file, 10)

ret = zbot.win32.capture.op_mouse_drag_item2item(
    co_obj=co_obj, drag_key=file, drop_key=input)


sendfile = capture.op_find_capture(co_obj, TEST_SEND_FILE)
if sendfile == 0:
    print("查找发送文件按钮失败", "错误")


atexit.register(capture.op_release_capture, co_obj, sendfile)

capture.op_set_focus(co_obj, sendfile)
# capture.op_draw_border(co_obj, sendfile)
capture.op_click(co_obj, sendfile)

zbot.win32.capture.op_release(co_obj=co_obj)
'''
