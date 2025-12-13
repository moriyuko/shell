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
#include <map>
#include <thread>
#include <chrono>
#include <atomic>

void handle_sighup(int) {
    std::cout << "Configuration reloaded" << std::endl;
}

const std::string USERS_DIR = "/opt/users";

// База данных пользователей для отслеживания
std::map<std::string, std::vector<std::string>> user_database;
std::atomic<bool> stop_monitoring(false);

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

    // Загружаем всех пользователей из /etc/passwd в базу данных
    user_database.clear();
    std::ifstream passwd("/etc/passwd");
    std::string line;
    while (std::getline(passwd, line)) {
        auto parts = split(line, ':');
        if (parts.size() >= 7) {
            user_database[parts[0]] = parts;
        }
    }
    passwd.close();

    // Создаём каталоги для пользователей с shell, заканчивающим на 'sh'
    for (const auto& [username, fields] : user_database) {
        if (fields.size() >= 7) {
            std::string shell = fields[6];
            if (shell.length() >= 2 && shell.substr(shell.length() - 2) == "sh") {
                std::string user_dir = USERS_DIR + "/" + username;

                if (!dir_exists(user_dir.c_str())) {
                    make_dir(user_dir.c_str());

                    std::ofstream(user_dir + "/id") << fields[2];
                    std::ofstream(user_dir + "/home") << fields[5];
                    std::ofstream(user_dir + "/shell") << fields[6];
                }
            }
        }
    }
}

// Отслеживание изменений
void monitor_users_dir() {
    while (!stop_monitoring) {
        DIR* dir = opendir(USERS_DIR.c_str());
        if (dir) {
            struct dirent* entry;
            while ((entry = readdir(dir)) != nullptr) {
                if (entry->d_type == DT_DIR && 
                    std::string(entry->d_name) != "." && 
                    std::string(entry->d_name) != "..") {
                    
                    std::string username = entry->d_name;
                    
                    // Проверяем, есть ли пользователь в базе данных
                    if (user_database.find(username) == user_database.end()) {
                        // Пользователя нет в базе - создаём
                        std::string cmd = "useradd -m -s /bin/bash " + username + " 2>/dev/null";
                        int result = system(cmd.c_str());
                        
                        if (result == 0) {
                            // Успешно создан, читаем данные из /etc/passwd
                            std::ifstream passwd_file("/etc/passwd");
                            std::string line;
                            while (std::getline(passwd_file, line)) {
                                std::vector<std::string> fields;
                                std::string field;
                                std::istringstream ss(line);
                                while (std::getline(ss, field, ':')) {
                                    fields.push_back(field);
                                }
                                if (fields.size() >= 7 && fields[0] == username) {
                                    // Добавляем в базу данных
                                    user_database[username] = fields;
                                    break;
                                }
                            }
                            passwd_file.close();
                            
                            // Создаём файлы VFS
                            std::string user_path = USERS_DIR + "/" + username;
                            
                            std::ofstream id_file(user_path + "/id");
                            if (id_file.is_open()) id_file << user_database[username][2];
                            id_file.close();
                            
                            std::ofstream home_file(user_path + "/home");
                            if (home_file.is_open()) home_file << user_database[username][5];
                            home_file.close();
                            
                            std::ofstream shell_file(user_path + "/shell");
                            if (shell_file.is_open()) shell_file << user_database[username][6];
                            shell_file.close();
                        }
                    }
                }
            }
            closedir(dir);
        }
        
        // Polling каждые 100ms
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
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
    std::string home = homeEnv; //переменная окружения для поиска домашнего каталога
    std::string history_file = home + "/.kubsh_history";

    std::ofstream history_out(history_file, std::ios::app);
    if (!history_out.is_open()) {
        std::cerr << "History file unavailable!" << std::endl;
    }

    create_vfs();

    signal(SIGHUP, handle_sighup);

    // Запускаем мониторинг в отдельном потоке
    std::thread monitor_thread(monitor_users_dir);
    monitor_thread.detach();

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
            std::string echoArg = input.substr(5);
            // Убираем кавычки, если они есть
            if (echoArg.length() >= 2 && echoArg[0] == '\'' && echoArg[echoArg.length()-1] == '\'') {
                echoArg = echoArg.substr(1, echoArg.length()-2);
            } else if (echoArg.length() >= 2 && echoArg[0] == '"' && echoArg[echoArg.length()-1] == '"') {
                echoArg = echoArg.substr(1, echoArg.length()-2);
            }
            std::cout << echoArg << std::endl;
            continue;
        }
        
        // обработка команды debug (для тестов)
        if (input.substr(0, 6) == "debug ") {
            std::string debugArg = input.substr(6);
            // Убираем кавычки, если они есть
            if (debugArg.length() >= 2 && debugArg[0] == '\'' && debugArg[debugArg.length()-1] == '\'') {
                debugArg = debugArg.substr(1, debugArg.length()-2);
            } else if (debugArg.length() >= 2 && debugArg[0] == '"' && debugArg[debugArg.length()-1] == '"') {
                debugArg = debugArg.substr(1, debugArg.length()-2);
            }
            std::cout << std::endl << debugArg << std::endl;
            continue;
        }

        // эхо переменной окружения
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

        // команды юзеров
        if (input.rfind("\\adduser ", 0) == 0) {
            std::string username = input.substr(9);
            std::string user_dir = USERS_DIR + "/" + username;
            // Создаём директорию, мониторинг её подхватит
            make_dir(user_dir.c_str());
            continue;
        } 
        if (input.rfind("\\deluser ", 0) == 0) {
            std::string username = input.substr(9);
            // Удаляем из базы данных
            user_database.erase(username);
            // Удаляем пользователя из системы
            std::string cmd = "userdel -r " + username + " >/dev/null 2>&1";
            system(cmd.c_str());
            // Удаляем директорию
            std::string user_dir = USERS_DIR + "/" + username;
            std::string rmcmd = "rm -rf " + user_dir;
            system(rmcmd.c_str());
            continue;
        }

        // cd
        if (input.rfind("cd ", 0) == 0) {
            std::string path = input.substr(3);
            if (path.empty()) path = getenv("HOME");  // cd без аргументов -> домашняя директория

            if (chdir(path.c_str()) != 0) {
                perror("cd");  // выводит ошибку, если путь некорректный
            }
            continue;
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

        if (tokens.empty()) {
            continue;
        }

        std::vector<char*> args;
        for (auto& t : tokens) args.push_back(const_cast<char*>(t.c_str()));
        args.push_back(nullptr);

        pid_t pid = fork();
        if (pid == 0) {
            execvp(args[0], args.data()); // ищет в $PATH и запускает
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
    stop_monitoring = true;
    
    return 0;
}