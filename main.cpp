#include <iostream>
#include <string>
#include <fstream>
#include <sstream>
#include <pwd.h>
#include <unistd.h>

std::string expandHome(const std::string& path) {
    if (path.empty() || path[0] != '~') return path;
    struct passwd *pw = getpwuid(getuid());
    if (!pw) return path;
    return std::string(pw->pw_dir) + path.substr(1);
}

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

        if (input.rfind("\\e", 0) == 0) {
            std::string var = input.substr(3); // всё после "\e "
            if (!var.empty() && var[0] == '$') {
                var = var.substr(1);
                const char* val = getenv(var.c_str());
                if (val) {
                    std::string value = val;
                    if (value.find(':') != std::string::npos) {
                        std::stringstream ss(value);
                        std::string token;
                        while (std::getline(ss, token, ':')) {
                            std::cout << token << std::endl;
                        }
                    } else {
                        std::cout << value << std::endl;
                    }
                } else {
                    std::cout << "Переменная не найдена" << std::endl;
                }
            } else {
                std::cout << "Формат: \\e $PATH" << std::endl;
            }
            continue;
        }

        if (input == "\\q") {
            std::cout << "Exiting...\n";
            break;
        }
        std::cout << input << ": command not found" << std::endl;
    }
    
    if (history_out.is_open()) history_out.close();
    return 0;
}
