#include <iostream>
#include <string>
#include <fstream>
#include <cstdlib>

int main() {
  std::cout << std::unitbuf;
  std::cerr << std::unitbuf;

  std::string input;
  
  std::string home = getenv("HOME"); //переменная окружения для поиска домашнего каталога
  std::string history_file = home + "/.kubsh_history";

  std::ofstream history_out(history_file, std::ios::app);
  if (!history_out.is_open()) {
    std::cerr << "History file unavailable!" << std::endl;
  }

  while (true) {

        std::cout << "kubsh$ ";

        if (!std::getline(std::cin, input)) {
            std::cout << "\nExiting...\n";
            break;
        }

        if (history_out.is_open()) {
            history_out << input << std::endl;
        }

        if (input.substr(0, 5) == "echo ") {
            std::cout << input.substr(5) << std::endl;
            continue; //добавила, чтоб не было вывода ": command not found"
        }

        if (input == "\\q") {
            std::cout << "Exiting...\n";
            break;
        }
        std::cout << input << ": command not found" << std::endl;
    }
    return 0;
}
