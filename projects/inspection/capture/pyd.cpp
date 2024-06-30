#include "stdafx.h" 

static std::atomic_bool gs_open = false;
static std::atomic_bool gs_exit = false;
static HANDLE  gs_core_thread = nullptr;
static HANDLE gs_core_thread_event = nullptr;

PYD_API bool op_begin(void) {
	do {
		if (gs_open.load())
			break;
		gs_core_thread_event = CreateEventW(NULL, TRUE, FALSE, NULL);
		gs_core_thread = (HANDLE)_beginthreadex(NULL, 0,
			[](void* arg)->unsigned int {
				HANDLE hEvent = (HANDLE)arg;

				do {
					do {
						if (gs_open.load())
							break;
						if (!__gpCore)
							__gpCore = new Core(reinterpret_cast<HMODULE>(__gpHinstance));

						gs_open.store(__gpCore->Start());

						SetEvent(hEvent);
					} while (0);

					if (!gs_open.load())
						break;
					if (gs_exit.load()) {
						__gpCore->Stop();
						__gpCore->Release();
						break;
					}
					std::this_thread::sleep_for(std::chrono::milliseconds(1000));
				} while (1);
				return 0;
			}, (void*)gs_core_thread_event, NULL, NULL);
	} while (0);

	WaitForSingleObject(gs_core_thread_event, INFINITE);
	CloseHandle(gs_core_thread_event);
	return gs_open.load();
}
PYD_API void op_end(void) {

	do {
		if (!__gpCore || !gs_open.load())
			break;
		gs_exit.store(true);
		WaitForSingleObject(gs_core_thread, INFINITE);
		CloseHandle(gs_core_thread);
	} while (0);
}

PYD_API void dev_set_data_dir(const char* dir) {
	if (CoreGet())
		CoreGet()->SetDataDir(dir);
}

PYD_API tfWindow op_find_capture(const pybind11::object& window_type/*int*/, const pybind11::object& key) {
	tfWindow result = 0;
	try {
		do {
			if (!gs_open.load())
				break;
			tfWindowType _window_type = static_cast<tfWindowType>( window_type.is_none() ? 0 : window_type.cast<unsigned long long>());
			std::string _key = key.is_none() ? "" : key.cast<std::string>();
			result = reinterpret_cast<decltype(result)>(CoreGet()->FindElement(_key, _window_type));
		} while (0);
	}
	catch (const std::exception& e) {
		std::cout << e.what() << std::endl;
	}
	return result;
}
PYD_API int op_click(const pybind11::object& window/*unsigned long long*/, const pybind11::object& key) {
	int result = 0;
	try {
		do {
			if (!gs_open.load())
				break;
			tfWindow _window = window.is_none() ? 0 : window.cast<unsigned long long>();
			std::string _key = key.is_none() ? "" : key.cast<std::string>();
			result = CoreGet()->MouseLeftClick(_window, _key);
		} while (0);
	}
	catch (const std::exception& e) {
		std::cout << e.what() << std::endl;
	}
	return result;
}
PYD_API int op_right_click(const pybind11::object& window, const pybind11::object& key) {
	int result = 0;
	try {
		do {
			if (!gs_open.load())
				break;
			tfWindow _window = window.is_none() ? 0 : window.cast<unsigned long long>();
			std::string _key = key.is_none() ? "" : key.cast<std::string>();
			result = CoreGet()->MouseRightClick(_window, _key);
		} while (0);
	}
	catch (const std::exception& e) {
		std::cout << e.what() << std::endl;
	}
	return result;
}
PYD_API int op_double_click(const pybind11::object& window, const pybind11::object& key) {
	int result = 0;
	try {
		do {
			if (!gs_open.load())
				break;
			tfWindow _window = window.is_none() ? 0 : window.cast<unsigned long long>();
			std::string _key = key.is_none() ? "" : key.cast<std::string>();
			result = CoreGet()->MouseLeftDoubleClick(_window, _key);
		} while (0);
	}
	catch (const std::exception& e) {
		std::cout << e.what() << std::endl;
	}
	return result;
}
PYD_API int op_send_keys(const pybind11::object& window, const pybind11::object& key, const pybind11::object& text) {
	int result = 0;
	try {
		do {
			if (!gs_open.load())
				break;
			tfWindow _window = window.is_none() ? 0 : window.cast<unsigned long long>();
			std::string _key = key.is_none() ? "" : key.cast<std::string>();
			std::string _text = text.is_none() ? "" : text.cast<std::string>();
			result = CoreGet()->SendText(_window, _key, _text);
		} while (0);
	}
	catch (const std::exception& e) {
		std::cout << e.what() << std::endl;
	}
	return result;
}
PYD_API int op_mouse_drag_item2item(const pybind11::object& window1, const pybind11::object& key1, const pybind11::object& window2, const pybind11::object& key2) {
	int result = 0;
	try {
		do {
			if (!gs_open.load())
				break;
			tfWindow _window1 = window1.is_none() ? 0 : window1.cast<unsigned long long>();
			tfWindow _window2 = window2.is_none() ? 0 : window2.cast<unsigned long long>();
			std::string _key1 = key1.is_none() ? "" : key1.cast<std::string>();
			std::string _key2 = key2.is_none() ? "" : key2.cast<std::string>();
			result = CoreGet()->MouseDragged(_window1, _key1,_window2, _key2);
		} while (0);
	}
	catch (const std::exception& e) {
		std::cout << e.what() << std::endl;
	}
	return result;
}
PYD_API int op_mouse_move_to(const pybind11::object& window, const pybind11::object& key, const pybind11::object& delay) {
	int result = 0;
	try {
		do {
			if (!gs_open.load())
				break;
			tfWindow _window = window.is_none() ? 0 : window.cast<unsigned long long>();
			tfStep _delay = delay.is_none() ? 0 : delay.cast<unsigned int>();
			std::string _key = key.is_none() ? "" : key.cast<std::string>();
			result = CoreGet()->MouseMoveTo(_window, _key, _delay);
		} while (0);
	}
	catch (const std::exception& e) {
		std::cout << e.what() << std::endl;
	}
	return result;
}

PYD_API int op_show_desktop(const pybind11::object& window) {
	return CoreGet()->ShowDesktop() ? 1 : 0;
}


PYD_API int test(void) {
	do {
		if (!gs_open.load())
			break;

		CoreGet()->LoadData();
		CoreGet()->Test();



		auto sk = 0;
		/*auto dd = CoreGet()->FindElement("77f502057da1d39c74e2424aeaa93f35", WindowType::WINDOW_TYPE_PROGRAM);
		if (!dd)
			break;*/

			//43627b5837e9a2fe336a875dbccf1079

			//cbc5f4c264bbf6b3fcc01bb1285ef361
		//auto dd = CoreGet()->MouseLeftDoubleClick(1,"cbc5f4c264bbf6b3fcc01bb1285ef361");
		//if (!dd)
		//	break;
		//int r = CoreGet()->SendText(reinterpret_cast<unsigned long long>(dd), "468c6e0945ede688a14463d403def7f5", "wq vb wq vb 你好啊。。");


		auto sksks = 0;
		//window_type = 2, key = '77f502057da1d39c74e2424aeaa93f35')
		//# ret = capture.op_click(
		//	#    window = dd, key = '1ad05d98e70c31a976f7d657d08ff434')
		//ret = capture.op_send_keys(
		//	window = dd, key = "468c6e0945ede688a14463d403def7f5", text = "hello 你好")
	//auto r = CoreGet()->FindElement()
	} while (0);
	return 0;
}


namespace pybind = pybind11;

PYBIND11_MODULE(capture, m) {
	m.doc() = "PDRPA(R) from 'git@github.com:skstu' on pyd.";
	m.def("capture_device_version",
		[]() { return PYD_VERSION; },
		"A function return version");

	m.def("op_begin",
		&op_begin,
		"PYD_API bool op_begin(void)"
	);

	m.def("op_end",
		&op_end,
		"PYD_API void op_end(void)"
	);

	m.def("op_show_desktop",
		&op_show_desktop,
		"PYD_API bool op_show_desktop(void)"
	);

	m.def("op_find_capture",
		&op_find_capture,
		pybind::arg("window_type").none(true),
		pybind::arg("key").none(true),
		"PYD_API UINT op_find_capture(int window_type, std::string key)"
	);

	m.def("op_click",
		&op_click,
		pybind::arg("window").none(true),
		pybind::arg("key").none(true),
		"PYD_API bool op_click(UINT item_key)"
	);

	m.def("op_double_click",
		&op_double_click,
		pybind::arg("window").none(true),
		pybind::arg("key").none(true),
		"PYD_API bool op_double_click(UINT item_key)"
	);

	m.def("op_send_keys",
		&op_send_keys,
		pybind11::arg("window").none(true),
		pybind11::arg("key").none(true),
		pybind11::arg("text").none(true),
		"PYD_API bool op_send_keys(UINT item_key, std::string text)"
	);

	m.def("op_mouse_drag_item2item",
		&op_mouse_drag_item2item,
		pybind::arg("window1").none(true),
		pybind::arg("key1").none(true),
		pybind::arg("window2").none(true),
		pybind::arg("key2").none(true),
		"PYD_API bool op_mouse_drag_item2item(UINT drag_key, UINT drop_key)"
	);

	m.def("op_mouse_move_to",
		&op_mouse_move_to,
		pybind::arg("window").none(true),
		pybind::arg("key").none(true),
		pybind::arg("delay").none(true),
		"PYD_API bool op_mouse_move_to(UINT item_key, UINT delay)"
	);

	m.def("op_right_click",
		&op_right_click,
		pybind::arg("window").none(true),
		pybind::arg("key").none(true),
		"PYD_API bool op_right_click(UINT item_key)"
	);



}

