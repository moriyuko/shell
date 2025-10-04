#include <iostream>
#include <string>
#include <fstream>
#include <cstdlib>
#include <sstream>
#include <unistd.h>
#include <sys/wait.h>
#include <csignal>

void handle_sighup(int) {
    std::cout << "Configuration reloaded" << std::endl;
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

  signal(SIGHUP, handle_sighup);

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

        if (input.rfind("\\e $", 0) == 0) {
            std::string var = input.substr(4);
            const char* value = std::getenv(var.c_str());
            if (value) {
                std::cout << value << std::endl;
            }
            else {
                std::cout << "Переменная не найдена" << std::endl;
            }
            continue;
        }

        if (input == "\\q") {
            std::cout << "Exiting...\n";
            break;
        }

        std::istringstream iss(input);
        std::vector<std::string> tokens;
        std::string token;
        while (iss >> token) tokens.push_back(token);

        if (tokens.empty()) continue;

        // превращаем в массив char* для execvp
        std::vector<char*> args;
        for (auto& t : tokens) args.push_back(const_cast<char*>(t.c_str()));
        args.push_back(nullptr);

        pid_t pid = fork();
        if (pid == 0) {
            execvp(args[0], args.data()); // ищет в $PATH и запускает
            std::cerr << args[0] << ": command not found" << std::endl;
            exit(1);
        } else if (pid > 0) {
            int status;
            waitpid(pid, &status, 0);
        } else {
            perror("fork");
        }
    } 

    return 0;
}
