#include <cvpp.h>
#include <stl.h>


#pragma comment(lib,"dl.lib")

cvpp::ICvpp* __gpCvpp = nullptr;
int main(int argc, char* argv[]) {
    __gpCvpp = cvpp::ICvpp::Create("cvpp.dll");

    __gpCvpp->Start();

    std::string input;
    do {
        input.clear();
        std::getline(std::cin, input);
        if (input == "q" || std::cin.eof()) {
            __gpCvpp->Stop();
            break;
        }
    } while (1);
    cvpp::ICvpp::Destroy(&__gpCvpp);
    return 0;
}
