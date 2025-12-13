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
#include <set>
#include <cerrno>

void handle_sighup(int) {
    std::cout << "Configuration reloaded" << std::endl;
}

const std::string USERS_DIR = "/opt/users";

bool dir_exists(const char* path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

void make_dir(const char* path) {
    if (mkdir(path, 0755) != 0 && errno != EEXIST) {
        perror("mkdir");
    }
}

std::vector<std::string> split(const std::string& str, char delim) {
    std::vector<std::string> parts;
    std::string temp;
    for (char ch : str) {
        if (ch == delim) {
            parts.push_back(temp);
            temp.clear();
        } else temp += ch;
    }
    parts.push_back(temp);
    return parts;
}

// Создание структуры пользователей
void create_vfs() {
    if (!dir_exists(USERS_DIR.c_str())) {
        make_dir(USERS_DIR.c_str());
    }

    std::ifstream passwd("/etc/passwd");
    std::string line;
    while (std::getline(passwd, line)) {
        auto parts = split(line, ':');
        if (parts.size() < 7) continue;

        std::string username = parts[0];
        std::string uid = parts[2];
        std::string home = parts[5];
        std::string shell = parts[6];

        if (shell.length() < 2 || shell.substr(shell.length() - 2) != "sh") {
            continue;
        }

        std::string user_dir = USERS_DIR + "/" + username;

        if (!dir_exists(user_dir.c_str())) {
            make_dir(user_dir.c_str());

            std::ofstream(user_dir + "/id") << uid;
            std::ofstream(user_dir + "/home") << home;
            std::ofstream(user_dir + "/shell") << shell;
        }
    }
}

// Добавление нового пользователя
void handle_new_user(const std::string& username) {
    std::string checkCmd = "id " + username + " >/dev/null 2>&1";
    int userExists = system(checkCmd.c_str());
    
    if (userExists != 0) {
        std::string cmd = "useradd -m -s /bin/bash " + username + " 2>/dev/null";
        int result = system(cmd.c_str());
        
        if (result == 0) {
            std::ifstream passwd_file("/etc/passwd");
            std::string line;
            while (std::getline(passwd_file, line)) {
                auto parts = split(line, ':');
                if (parts.size() >= 7 && parts[0] == username) {
                    std::string user_dir = USERS_DIR + "/" + username;
                    
                    std::ofstream id_file(user_dir + "/id");
                    if (id_file.is_open()) id_file << parts[2];
                    id_file.close();
                    
                    std::ofstream home_file(user_dir + "/home");
                    if (home_file.is_open()) home_file << parts[5];
                    home_file.close();
                    
                    std::ofstream shell_file(user_dir + "/shell");
                    if (shell_file.is_open()) shell_file << parts[6];
                    shell_file.close();
                    
                    break;
                }
            }
            passwd_file.close();
        }
    } else {
        std::ifstream passwd_file("/etc/passwd");
        std::string line;
        while (std::getline(passwd_file, line)) {
            auto parts = split(line, ':');
            if (parts.size() >= 7 && parts[0] == username) {
                std::string user_dir = USERS_DIR + "/" + username;
                
                std::ofstream id_file(user_dir + "/id");
                if (id_file.is_open()) id_file << parts[2];
                id_file.close();
                
                std::ofstream home_file(user_dir + "/home");
                if (home_file.is_open()) home_file << parts[5];
                home_file.close();
                
                std::ofstream shell_file(user_dir + "/shell");
                if (shell_file.is_open()) shell_file << parts[6];
                shell_file.close();
                
                break;
            }
        }
        passwd_file.close();
    }
}

// Удаление пользователя
void handle_deleted_user(const std::string& username) {
    std::string cmd = "userdel -r " + username + " >/dev/null 2>&1";
    system(cmd.c_str());
}

// Отслеживание изменений
void monitor_users_dir() {
    std::set<std::string> known_users;
    
    if (!dir_exists(USERS_DIR.c_str())) {
        make_dir(USERS_DIR.c_str());
    }
    
    while (true) {
        std::set<std::string> current_users;
        
        DIR* dir = opendir(USERS_DIR.c_str());
        if (dir) {
            struct dirent* entry;
            while ((entry = readdir(dir)) != nullptr) {
                if (entry->d_type == DT_DIR && 
                    entry->d_name[0] != '.' &&
                    std::string(entry->d_name) != "." &&
                    std::string(entry->d_name) != "..") {
                    
                    std::string username = entry->d_name;
                    current_users.insert(username);
                    
                    if (known_users.find(username) == known_users.end()) {
                        handle_new_user(username);
                    }
                }
            }
            closedir(dir);
        }
        
        // Проверяем удалённых пользователей
        for (const auto& username : known_users) {
            if (current_users.find(username) == current_users.end()) {
                handle_deleted_user(username);
            }
        }
        
        known_users = current_users;
        
        usleep(100000);
    }
}

int main() {
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;

    std::string input;
    
    const char* homeEnv = getenv("HOME");
    if (!homeEnv) {
        homeEnv = "/opt"; // fallback для тестов
    }
    std::string home = homeEnv;
    std::string history_file = home + "/.kubsh_history";

    std::ofstream history_out(history_file, std::ios::app);
    if (!history_out.is_open()) {
        std::cerr << "History file unavailable!" << std::endl;
    }

    create_vfs();

    signal(SIGHUP, handle_sighup);

    // Запускаем мониторинг
    pid_t monitor_pid = fork();
    if (monitor_pid == 0) {
        monitor_users_dir();
        exit(0);
    } else if (monitor_pid < 0) {
        perror("fork");
    }

    while (true) {
        std::cout << "kubsh$ ";

        // Выход
        if (!std::getline(std::cin, input)) {
            std::cout << "\nExiting...\n";
            break;
        }

        // История
        if (history_out.is_open()) {
            history_out << input << std::endl;
        }

        // Эхо
        if (input.substr(0, 5) == "echo ") {
            std::string echoArg = input.substr(5);
            
            if (echoArg.length() >= 2 && echoArg[0] == '\'' && echoArg[echoArg.length()-1] == '\'') {
                echoArg = echoArg.substr(1, echoArg.length()-2);
            } else if (echoArg.length() >= 2 && echoArg[0] == '"' && echoArg[echoArg.length()-1] == '"') {
                echoArg = echoArg.substr(1, echoArg.length()-2);
            }
            std::cout << echoArg << std::endl;
            continue;
        }
        
        // Обработка команды debug (для тестов)
        if (input.substr(0, 6) == "debug ") {
            std::string debugArg = input.substr(6);

            if (debugArg.length() >= 2 && debugArg[0] == '\'' && debugArg[debugArg.length()-1] == '\'') {
                debugArg = debugArg.substr(1, debugArg.length()-2);
            } else if (debugArg.length() >= 2 && debugArg[0] == '"' && debugArg[debugArg.length()-1] == '"') {
                debugArg = debugArg.substr(1, debugArg.length()-2);
            }
            std::cout << std::endl << debugArg << std::endl;
            continue;
        }

        // Эхо переменной окружения
        if (input.rfind("\\e $", 0) == 0) {
            std::string var = input.substr(4);
            const char* value = std::getenv(var.c_str());
            if (value) {
                std::string valueStr(value);
                // Если переменная PATH, выводим каждый путь на отдельной строке
                if (var == "PATH" && valueStr.find(':') != std::string::npos) {
                    std::istringstream pathStream(valueStr);
                    std::string pathItem;
                    while (std::getline(pathStream, pathItem, ':')) {
                        if (!pathItem.empty()) {
                            std::cout << pathItem << std::endl;
                        }
                    }
                } else {
                    std::cout << std::endl << valueStr << std::endl;
                }
            }
            else {
                std::cout << std::endl << "Переменная не найдена" << std::endl;
            }
            continue;
        }

        // Команды юзеров
        if (input.rfind("\\adduser ", 0) == 0) {
            std::string username = input.substr(9);
            std::string user_dir = USERS_DIR + "/" + username;
            // Создаём директорию, мониторинг её подхватит
            make_dir(user_dir.c_str());
            continue;
        } 
        if (input.rfind("\\deluser ", 0) == 0) {
            std::string username = input.substr(9);
            std::string user_dir = USERS_DIR + "/" + username;
            // Удаляем директорию
            std::string cmd = "rm -rf " + user_dir;
            system(cmd.c_str());
            continue;
        }

        // cd
        if (input.rfind("cd ", 0) == 0) {
            std::string path = input.substr(3);
            if (path.empty()) path = getenv("HOME");

            if (chdir(path.c_str()) != 0) {
                perror("cd");
            }
            continue;
        }

        // Выход
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

        if (tokens.empty()) {
            continue;
        }

        std::vector<char*> args;
        for (auto& t : tokens) args.push_back(const_cast<char*>(t.c_str()));
        args.push_back(nullptr);

        pid_t pid = fork();
        if (pid == 0) {
            execvp(args[0], args.data());
            std::cout << args[0] << ": command not found" << std::endl;
            exit(1);
        } else if (pid > 0) {
            int status;
            waitpid(pid, &status, 0);
        } else {
            perror("fork");
        }
    }
    
    // Останавливаем мониторинг
    if (monitor_pid > 0) {
        kill(monitor_pid, SIGTERM);
        waitpid(monitor_pid, nullptr, 0);
    }
    
    return 0;
}