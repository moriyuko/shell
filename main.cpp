#include <iostream>
#include <string>
#include <fstream>
#include <cstdlib>
#include <sstream>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <pwd.h>
#include <csignal>
#include <vector>

void handle_sighup(int) {
    std::cout << "Configuration reloaded" << std::endl;
}

void syncSystemUsers() {
    const char* home = getenv("HOME");
    std::string usersPath = std::string(home) + "/users";

    std::ifstream passwdFile("/etc/passwd");
    std::string line;

    while (std::getline(passwdFile, line)) {
        std::stringstream ss(line);
        std::string username, x, uid_str, gid_str, info, homeDir, shell;

        std::getline(ss, username, ':');
        std::getline(ss, x, ':');
        std::getline(ss, uid_str, ':');
        std::getline(ss, gid_str, ':');
        std::getline(ss, info, ':');
        std::getline(ss, homeDir, ':');
        std::getline(ss, shell, ':');

        int uid = std::stoi(uid_str);

        // фильтруем системных пользователей
        if (uid < 1000) continue;

        std::string userDir = usersPath + "/" + username;
        struct stat st = {0};
        if (stat(userDir.c_str(), &st) == -1) {
            mkdir(userDir.c_str(), 0755);

            std::ofstream(userDir + "/id") << uid;
            std::ofstream(userDir + "/home") << homeDir;
            std::ofstream(userDir + "/shell") << shell;
        }
    }

    std::cout << "Каталоги пользователей синхронизированы с системой." << std::endl;
}

void createUsersDir() {
    const char* home = getenv("HOME");
    std::string usersPath = std::string(home) + "/users";

    struct stat st = {0};
    if (stat(usersPath.c_str(), &st) == -1) {
        mkdir(usersPath.c_str(), 0755);
        std::cout << "Создан каталог " << usersPath << std::endl;
    }

    syncSystemUsers();
}

void addUser(const std::string& username) {
    const char* home = getenv("HOME");
    std::string userDir = std::string(home) + "/users/" + username;

    if (mkdir(userDir.c_str(), 0755) == 0) {
        std::ofstream(userDir + "/id") << getuid();
        std::ofstream(userDir + "/home") << "/home/" << username;
        std::ofstream(userDir + "/shell") << "/bin/bash";

        std::string cmd = "sudo adduser --disabled-password --gecos \"\" " + username;
        system(cmd.c_str());
    } else {
        std::cerr << "Ошибка: каталог уже существует или нет прав." << std::endl;
    }
}

void delUser(const std::string& username) {
    const char* home = getenv("HOME");
    std::string userDir = std::string(home) + "/users/" + username;

    unlink((userDir + "/id").c_str());
    unlink((userDir + "/home").c_str());
    unlink((userDir + "/shell").c_str());
    rmdir(userDir.c_str());

    std::string cmd = "sudo userdel " + username;
    system(cmd.c_str());
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

  createUsersDir();

  signal(SIGHUP, handle_sighup);

  while (true) {

        std::cout << "kubsh$ ";

        // выход
        if (!std::getline(std::cin, input)) {
            std::cout << "\nExiting...\n";
            break;
        }

        // история
        if (history_out.is_open()) {
            history_out << input << std::endl;
        }

        // эхо
        if (input.substr(0, 5) == "echo ") {
            std::cout << input.substr(5) << std::endl;
            continue; //добавила, чтоб не было вывода ": command not found"
        }

        // эхо переменной окружения
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

        // команды юзеров
        if (input.rfind("\\adduser ", 0) == 0) {
            addUser(input.substr(9));
        } 
        if (input.rfind("\\deluser ", 0) == 0) {
            delUser(input.substr(9));
        }

        //выход
        if (input == "\\q") {
            std::cout << "Exiting...\n";
            break;
        }

        if (input.rfind("\\l ", 0) == 0) {
            std::string device = input.substr(3);
            if (device.empty()) {
                std::cout << "Usage: \\l <device>\n";
                continue;
            }

            pid_t pid = fork();
            if (pid == 0) {
                execlp("lsblk", "lsblk", device.c_str(), "-o", "NAME,SIZE,TYPE,MOUNTPOINT", nullptr);
                perror("lsblk failed");
                exit(1);
            } else if (pid > 0) {
                int status;
                waitpid(pid, &status, 0);
            } else {
                perror("fork failed");
            }
            continue;
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
