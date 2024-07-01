#include <stl.h>
#include <ossl.h>

int main(int argc, char* argv[]) {

	std::string md5str = ossl::Ssl::MD5("");

	stl::File::Create(R"(C:\\TEST\\AAA.TEXT)");
	std::string input;
	do {
		input.clear();
		std::getline(std::cin, input);
		if (input == "q" || std::cin.eof()) {

			break;
		}
	} while (1);
	return 0;
}