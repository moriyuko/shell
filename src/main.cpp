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

void syncSystemUsers() {
    const char* home = getenv("HOME");
    std::string usersPath;
    
    // Проверяем, существует ли /opt/users (для тестов)
    struct stat optStat;
    if (stat("/opt/users", &optStat) == 0 && S_ISDIR(optStat.st_mode)) {
        usersPath = "/opt/users";
    } else if (home) {
        usersPath = std::string(home) + "/users";
    } else {
        usersPath = "/opt/users";
    }

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

        if (shell.empty() || shell == "/usr/sbin/nologin" || shell == "/bin/false" || shell == "/sbin/nologin") {
            continue;
        }
        
        if (shell.length() < 2 || shell.substr(shell.length() - 2) != "sh") {
            continue;
        }

        int uid = std::stoi(uid_str);

        std::string userDir = usersPath + "/" + username;
        struct stat st = {0};
        // Создаем каталог, если его нет, или обновляем файлы, если они отсутствуют
        if (stat(userDir.c_str(), &st) == -1) {
            mkdir(userDir.c_str(), 0755);
        }
        
        // Создаем или обновляем файлы
        std::ofstream idFile(userDir + "/id");
        idFile << uid;
        idFile.close();
        
        std::ofstream homeFile(userDir + "/home");
        homeFile << homeDir;
        homeFile.close();
        
        std::ofstream shellFile(userDir + "/shell");
        shellFile << shell;
        shellFile.close();
    }
}

void createUsersDir() {
    const char* home = getenv("HOME");
    std::string usersPath;
    
    // Проверяем, существует ли /opt/users (для тестов)
    struct stat optStat;
    if (stat("/opt/users", &optStat) == 0 && S_ISDIR(optStat.st_mode)) {
        usersPath = "/opt/users";
    } else if (home) {
        usersPath = std::string(home) + "/users";
    } else {
        usersPath = "/opt/users";
    }

    struct stat st = {0};
    if (stat(usersPath.c_str(), &st) == -1) {
        mkdir(usersPath.c_str(), 0755);
    }

    syncSystemUsers();
}

void addUser(const std::string& username) {
    const char* home = getenv("HOME");
    std::string usersPath;
    
    // Проверяем, существует ли /opt/users (для тестов)
    struct stat optStat;
    if (stat("/opt/users", &optStat) == 0 && S_ISDIR(optStat.st_mode)) {
        usersPath = "/opt/users";
    } else if (home) {
        usersPath = std::string(home) + "/users";
    } else {
        usersPath = "/opt/users";
    }
    std::string userDir = usersPath + "/" + username;

    if (mkdir(userDir.c_str(), 0755) == 0) {
        std::ofstream(userDir + "/id") << getuid();
        std::ofstream(userDir + "/home") << "/home/" << username;
        std::ofstream(userDir + "/shell") << "/bin/bash";

        std::string cmd = "adduser --disabled-password --gecos \"\" " + username;
        system(cmd.c_str());
    } else {
        std::cerr << "Ошибка: каталог уже существует или нет прав." << std::endl;
    }
}

void delUser(const std::string& username) {
    const char* home = getenv("HOME");
    std::string usersPath;
    
    struct stat optStat;
    if (stat("/opt/users", &optStat) == 0 && S_ISDIR(optStat.st_mode)) {
        usersPath = "/opt/users";
    } else if (home) {
        usersPath = std::string(home) + "/users";
    } else {
        usersPath = "/opt/users";
    }
    std::string userDir = usersPath + "/" + username;

    unlink((userDir + "/id").c_str());
    unlink((userDir + "/home").c_str());
    unlink((userDir + "/shell").c_str());
    rmdir(userDir.c_str());

    std::string cmd = "userdel " + username;
    system(cmd.c_str());
}

void processNewUserDir(const std::string& usersPath, const std::string& username) {
    std::string userDir = usersPath + "/" + username;
    
    std::string idFile = userDir + "/id";
    std::string homeFile = userDir + "/home";
    std::string shellFile = userDir + "/shell";
    
    struct stat idStat, homeStat, shellStat;
    bool hasId = (stat(idFile.c_str(), &idStat) == 0);
    bool hasHome = (stat(homeFile.c_str(), &homeStat) == 0);
    bool hasShell = (stat(shellFile.c_str(), &shellStat) == 0);
    
    if (!hasId || !hasHome || !hasShell) {
        // Проверяем, существует ли пользователь в системе
        std::string checkCmd = "id " + username + " >/dev/null 2>&1";
        int userExists = system(checkCmd.c_str());
        
        // Если пользователь не существует, создаем его
        if (userExists != 0) {
            std::string cmd = "adduser --disabled-password --gecos \"\" " + username + " >/dev/null 2>&1";
            int result = system(cmd.c_str());
            
            usleep(200000); // 200ms
            
            int checkResult = system(("id " + username + " >/dev/null 2>&1").c_str());
            if (checkResult != 0 && (result == 0 || result == 256)) {
                // Если пользователь все еще не существует, пробуем еще раз
                system(cmd.c_str());
                usleep(200000);
            }
            
            if (result == 0 || result == 256) {
                // Получаем информацию о пользователе из /etc/passwd
                std::ifstream passwdFile("/etc/passwd");
                std::string line;
                bool found = false;
                while (std::getline(passwdFile, line)) {
                    if (line.find(username + ":") == 0) {
                        std::stringstream ss(line);
                        std::string u, x, uid_str, gid_str, info, homeDir, shell;
                        std::getline(ss, u, ':');
                        std::getline(ss, x, ':');
                        std::getline(ss, uid_str, ':');
                        std::getline(ss, gid_str, ':');
                        std::getline(ss, info, ':');
                        std::getline(ss, homeDir, ':');
                        std::getline(ss, shell, ':');
                        
                        std::ofstream(idFile) << uid_str;
                        std::ofstream(homeFile) << homeDir;
                        std::ofstream(shellFile) << shell;
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    // Если не нашли в /etc/passwd, создаем файлы с дефолтными значениями
                    std::ofstream(idFile) << getuid();
                    std::ofstream(homeFile) << "/home/" << username;
                    std::ofstream(shellFile) << "/bin/bash";
                }
            } else {
                // Если adduser не сработал, создаем файлы с дефолтными значениями
                std::ofstream(idFile) << getuid();
                std::ofstream(homeFile) << "/home/" << username;
                std::ofstream(shellFile) << "/bin/bash";
            }
        } else {
            // Пользователь уже существует, просто создаем файлы
            std::ifstream passwdFile("/etc/passwd");
            std::string line;
            while (std::getline(passwdFile, line)) {
                if (line.find(username + ":") == 0) {
                    std::stringstream ss(line);
                    std::string u, x, uid_str, gid_str, info, homeDir, shell;
                    std::getline(ss, u, ':');
                    std::getline(ss, x, ':');
                    std::getline(ss, uid_str, ':');
                    std::getline(ss, gid_str, ':');
                    std::getline(ss, info, ':');
                    std::getline(ss, homeDir, ':');
                    std::getline(ss, shell, ':');
                    
                    std::ofstream(idFile) << uid_str;
                    std::ofstream(homeFile) << homeDir;
                    std::ofstream(shellFile) << shell;
                    break;
                }
            }
        }
    }
}

void checkNewUserDirs() {
    const char* home = getenv("HOME");
    std::string usersPath;
    
    // Проверяем, существует ли /opt/users
    struct stat optStat;
    if (stat("/opt/users", &optStat) == 0 && S_ISDIR(optStat.st_mode)) {
        usersPath = "/opt/users";
    } else if (home) {
        usersPath = std::string(home) + "/users";
    } else {
        usersPath = "/opt/users";
    }
    
    DIR* dir = opendir(usersPath.c_str());
    if (!dir) return;
    
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        if (entry->d_name[0] == '.') continue;
        
        std::string userDir = usersPath + "/" + entry->d_name;
        struct stat st;
        if (stat(userDir.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
            processNewUserDir(usersPath, entry->d_name);
        }
    }
    closedir(dir);
}

// Глобальные переменные для inotify
std::atomic<bool> inotify_running(false);
int inotify_fd = -1;
int inotify_wd = -1;
std::string inotify_users_path;

void inotifyWatcher() {
    char buffer[4096];
    
    while (inotify_running) {
        ssize_t length = read(inotify_fd, buffer, sizeof(buffer));
        if (length < 0) {
            if (errno == EINTR || errno == EAGAIN) {
                usleep(50000); // 50ms задержка при отсутствии событий
                continue;
            }
            break;
        }
        
        if (length == 0) {
            usleep(50000);
            continue;
        }
        
        char* ptr = buffer;
        while (ptr < buffer + length) {
            struct inotify_event* event = (struct inotify_event*)ptr;
            
            if (event->mask & (IN_CREATE | IN_MOVED_TO)) {
                if (event->mask & IN_ISDIR) {
                    // Создан новый каталог
                    std::string username(event->name);
                    if (username.length() > 0 && username[0] != '.') {
                        // Небольшая задержка, чтобы каталог полностью создался
                        usleep(100000); // 100ms
                        processNewUserDir(inotify_users_path, username);
                    }
                }
            }
            
            ptr += sizeof(struct inotify_event) + event->len;
        }
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

  createUsersDir();

  signal(SIGHUP, handle_sighup);

  // Инициализируем inotify для отслеживания создания каталогов
  std::string usersPath;
  struct stat optStat;
  if (stat("/opt/users", &optStat) == 0 && S_ISDIR(optStat.st_mode)) {
      usersPath = "/opt/users";
  } else if (homeEnv) {
      usersPath = std::string(homeEnv) + "/users";
  } else {
      usersPath = "/opt/users";
  }
  inotify_users_path = usersPath;
  
  inotify_fd = inotify_init1(IN_NONBLOCK);
  if (inotify_fd >= 0) {
      inotify_wd = inotify_add_watch(inotify_fd, usersPath.c_str(), IN_CREATE | IN_MOVED_TO);
      if (inotify_wd >= 0) {
          inotify_running = true;
          std::thread watcher_thread(inotifyWatcher);
          watcher_thread.detach();
      } else {
          close(inotify_fd);
          inotify_fd = -1;
      }
  }
  
  checkNewUserDirs();
  
  usleep(100000); // 100ms

  while (true) {
        checkNewUserDirs();

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
            addUser(input.substr(9));
        } 
        if (input.rfind("\\deluser ", 0) == 0) {
            delUser(input.substr(9));
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
    
    // Останавливаем inotify
    inotify_running = false;
    if (inotify_fd >= 0) {
        if (inotify_wd >= 0) {
            inotify_rm_watch(inotify_fd, inotify_wd);
        }
        close(inotify_fd);
    }

    return 0;
}
