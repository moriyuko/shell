#include <iostream>
#include <string>

int main() {
  std::cout << std::unitbuf;
  std::cerr << std::unitbuf;

  std::string input;
  while (true) {
        std::cout << "kubsh$ ";
        if (!std::getline(std::cin, input)) {
            std::cout << "\nExiting...\n";
            break;
        }
        if (input == "\\q") {
            std::cout << "Exiting...\n";
            break;
        }
        std::cout << input << ": command not found" << std::endl;
    }
    return 0;
}
