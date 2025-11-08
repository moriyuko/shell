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
#include <sys/inotify.h>
#include <thread>
#include <atomic>
#include <mutex>

void handle_sighup(int) {
    std::cout << "Configuration reloaded" << std::endl;
}

const std::string USERS_DIR = "/opt/users";

bool dir_exists(const char* path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

void make_dir(const char* path) {
    mkdir(path, 0755);
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

        // Создаем каталог только для пользователей с shell, заканчивающимся на 'sh'
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
    // Проверяем, существует ли пользователь
    std::string checkCmd = "id " + username + " >/dev/null 2>&1";
    int userExists = system(checkCmd.c_str());
    
    if (userExists != 0) {
        std::string cmd = "adduser --disabled-password --gecos \"\" " + username + " >/dev/null 2>&1";
        system(cmd.c_str());
    }
}

// Удаление пользователя
void handle_deleted_user(const std::string& username) {
    std::string cmd = "userdel " + username + " >/dev/null 2>&1";
    system(cmd.c_str());
}

// Отслеживание изменений
void monitor_users_dir() {
    int fd = inotify_init();
    if (fd < 0) {
        perror("inotify_init");
        return;
    }

    int wd = inotify_add_watch(fd, USERS_DIR.c_str(), IN_CREATE | IN_DELETE);
    if (wd < 0) {
        perror("inotify_add_watch");
        close(fd);
        return;
    }

    char buffer[1024];
    while (true) {
        int length = read(fd, buffer, sizeof(buffer));
        if (length < 0) break;

        int i = 0;
        while (i < length) {
            struct inotify_event *event = (struct inotify_event *) &buffer[i];
            if (event->len > 0) {
                std::string name = event->name;
                // Проверяем, что это директория и не начинается с точки
                if (name[0] != '.') {
                    if (event->mask & IN_CREATE && (event->mask & IN_ISDIR)) {
                        // Небольшая задержка, чтобы каталог полностью создался
                        usleep(100000); // 100ms
                        handle_new_user(name);
                    } else if (event->mask & IN_DELETE && (event->mask & IN_ISDIR)) {
                        handle_deleted_user(name);
                    }
                }
            }
            i += sizeof(struct inotify_event) + event->len;
        }
    }

    close(fd);
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

    // Запускаем мониторинг в отдельном процессе
    pid_t pid = fork();
    if (pid == 0) {
        monitor_users_dir();  // дочерний процесс следит за изменениями (бесконечный цикл)
        exit(0);  // Этот код никогда не выполнится, но оставляем для ясности
    }

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
            handle_new_user(input.substr(9));
        } 
        if (input.rfind("\\deluser ", 0) == 0) {
            handle_deleted_user(input.substr(9));
        }

        // cd
        if (input.rfind("cd ", 0) == 0) {
            std::string path = input.substr(3);
            if (path.empty()) path = getenv("HOME");  // cd без аргументов -> домашняя директория

            if (chdir(path.c_str()) != 0) {
                perror("cd");  // выводит ошибку, если путь некорректный
            }
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
    return 0;
}
