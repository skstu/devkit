#pragma once
#include <iostream>

namespace stl {
	LIBSTL_INLINE MainProc::MainProc(const MainProcCb& callback) : callback_(callback) {
		if (callback_) {
			Proc();
		}
		else {
			std::cerr << "Callback function is not valid!" << std::endl;
		}
	}
	LIBSTL_INLINE void MainProc::Proc() {
		std::string input;
		while (std::getline(std::cin, input)) {
			if (input.empty()) {
				continue;
			}
			bool exit_flag = false;
			callback_(input, exit_flag);
			if (exit_flag) {
				break;
			}
		}
	}

} // namespace stl
