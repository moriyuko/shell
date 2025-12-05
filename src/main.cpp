#define FUSE_USE_VERSION 31

#include <fuse3/fuse.h>
#include <iostream>
#include <string>
#include <fstream>
#include <cstdlib>
#include <sstream>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <pwd.h>
#include <csignal>
#include <vector>
#include <map>
#include <mutex>
#include <cstring>
#include <dirent.h>

// Глобальные переменные
std::string MOUNT_POINT;
std::mutex fs_mutex;

// Структура для хранения информации о пользователе
struct UserInfo {
    std::string username;
    uid_t uid;
    std::string home;
    std::string shell;
};

// Кеш пользователей
std::map<std::string, UserInfo> user_cache;

void handle_sighup(int) {
    std::cout << "Configuration reloaded" << std::endl;
}

// Получить список пользователей системы
void refresh_user_cache() {
    std::lock_guard<std::mutex> lock(fs_mutex);
    user_cache.clear();
    
    setpwent();
    struct passwd* pw;
    while ((pw = getpwent()) != nullptr) {
        std::string shell = pw->pw_shell;
        // Только пользователи с shell, заканчивающимся на 'sh'
        if (shell.length() >= 2 && shell.substr(shell.length() - 2) == "sh") {
            UserInfo info;
            info.username = pw->pw_name;
            info.uid = pw->pw_uid;
            info.home = pw->pw_dir;
            info.shell = pw->pw_shell;
            user_cache[info.username] = info;
        }
    }
    endpwent();
}

// Создание нового пользователя
int create_user(const std::string& username) {
    std::string cmd = "useradd -m -s /bin/bash " + username + " 2>&1";
    int result = system(cmd.c_str());
    
    if (result == 0 || WEXITSTATUS(result) == 9) {
        // Ждем пока пользователь появится в системе
        for (int i = 0; i < 100; i++) {
            usleep(10000);
            struct passwd* pw = getpwnam(username.c_str());
            if (pw != nullptr) {
                refresh_user_cache();
                return 0;
            }
        }
    }
    return -1;
}

// Удаление пользователя
int delete_user(const std::string& username) {
    std::string cmd = "userdel -r " + username + " >/dev/null 2>&1";
    system(cmd.c_str());
    refresh_user_cache();
    return 0;
}

// FUSE операции
static int vfs_getattr(const char* path, struct stat* stbuf, struct fuse_file_info*) {
    memset(stbuf, 0, sizeof(struct stat));
    
    std::string spath(path);
    
    // Корневая директория
    if (spath == "/") {
        stbuf->st_mode = S_IFDIR | 0755;
        stbuf->st_nlink = 2;
        stbuf->st_uid = getuid();
        stbuf->st_gid = getgid();
        return 0;
    }
    
    // Парсим путь
    std::vector<std::string> parts;
    std::istringstream ss(spath.substr(1));
    std::string part;
    while (std::getline(ss, part, '/')) {
        if (!part.empty()) parts.push_back(part);
    }
    
    if (parts.empty()) return -ENOENT;
    
    std::lock_guard<std::mutex> lock(fs_mutex);
    
    // Директория пользователя
    if (parts.size() == 1) {
        auto it = user_cache.find(parts[0]);
        if (it != user_cache.end()) {
            stbuf->st_mode = S_IFDIR | 0755;
            stbuf->st_nlink = 2;
            stbuf->st_uid = getuid();
            stbuf->st_gid = getgid();
            return 0;
        }
        return -ENOENT;
    }
    
    // Файлы внутри директории пользователя
    if (parts.size() == 2) {
        auto it = user_cache.find(parts[0]);
        if (it == user_cache.end()) return -ENOENT;
        
        if (parts[1] == "id" || parts[1] == "home" || parts[1] == "shell") {
            stbuf->st_mode = S_IFREG | 0644;
            stbuf->st_nlink = 1;
            stbuf->st_uid = getuid();
            stbuf->st_gid = getgid();
            
            // Размер файла
            if (parts[1] == "id") {
                stbuf->st_size = std::to_string(it->second.uid).length() + 1;
            } else if (parts[1] == "home") {
                stbuf->st_size = it->second.home.length() + 1;
            } else if (parts[1] == "shell") {
                stbuf->st_size = it->second.shell.length() + 1;
            }
            return 0;
        }
        return -ENOENT;
    }
    
    return -ENOENT;
}

static int vfs_readdir(const char* path, void* buf, fuse_fill_dir_t filler,
                       off_t offset, struct fuse_file_info* fi,
                       enum fuse_readdir_flags flags) {
    (void) offset;
    (void) fi;
    (void) flags;
    
    std::string spath(path);
    
    filler(buf, ".", NULL, 0, (fuse_fill_dir_flags)0);
    filler(buf, "..", NULL, 0, (fuse_fill_dir_flags)0);
    
    // Корневая директория - список пользователей
    if (spath == "/") {
        std::lock_guard<std::mutex> lock(fs_mutex);
        for (const auto& pair : user_cache) {
            filler(buf, pair.first.c_str(), NULL, 0, (fuse_fill_dir_flags)0);
        }
        return 0;
    }
    
    // Директория пользователя - список файлов
    std::vector<std::string> parts;
    std::istringstream ss(spath.substr(1));
    std::string part;
    while (std::getline(ss, part, '/')) {
        if (!part.empty()) parts.push_back(part);
    }
    
    if (parts.size() == 1) {
        std::lock_guard<std::mutex> lock(fs_mutex);
        auto it = user_cache.find(parts[0]);
        if (it != user_cache.end()) {
            filler(buf, "id", NULL, 0, (fuse_fill_dir_flags)0);
            filler(buf, "home", NULL, 0, (fuse_fill_dir_flags)0);
            filler(buf, "shell", NULL, 0, (fuse_fill_dir_flags)0);
            return 0;
        }
        return -ENOENT;
    }
    
    return -ENOENT;
}

static int vfs_open(const char* path, struct fuse_file_info*) {
    std::string spath(path);
    
    std::vector<std::string> parts;
    std::istringstream ss(spath.substr(1));
    std::string part;
    while (std::getline(ss, part, '/')) {
        if (!part.empty()) parts.push_back(part);
    }
    
    if (parts.size() != 2) return -ENOENT;
    
    std::lock_guard<std::mutex> lock(fs_mutex);
    auto it = user_cache.find(parts[0]);
    if (it == user_cache.end()) return -ENOENT;
    
    if (parts[1] != "id" && parts[1] != "home" && parts[1] != "shell") {
        return -ENOENT;
    }
    
    return 0;
}

static int vfs_read(const char* path, char* buf, size_t size, off_t offset,
                    struct fuse_file_info*) {
    std::string spath(path);
    
    std::vector<std::string> parts;
    std::istringstream ss(spath.substr(1));
    std::string part;
    while (std::getline(ss, part, '/')) {
        if (!part.empty()) parts.push_back(part);
    }
    
    if (parts.size() != 2) return -ENOENT;
    
    std::lock_guard<std::mutex> lock(fs_mutex);
    auto it = user_cache.find(parts[0]);
    if (it == user_cache.end()) return -ENOENT;
    
    std::string content;
    if (parts[1] == "id") {
        content = std::to_string(it->second.uid) + "\n";
    } else if (parts[1] == "home") {
        content = it->second.home + "\n";
    } else if (parts[1] == "shell") {
        content = it->second.shell + "\n";
    } else {
        return -ENOENT;
    }
    
    size_t len = content.length();
    if (offset < (off_t)len) {
        if (offset + size > len) {
            size = len - offset;
        }
        memcpy(buf, content.c_str() + offset, size);
    } else {
        size = 0;
    }
    
    return size;
}

static int vfs_mkdir(const char* path, mode_t) {
    std::string spath(path);
    if (spath.length() <= 1) return -EINVAL;
    
    std::string username = spath.substr(1);
    if (username.find('/') != std::string::npos) {
        return -EINVAL; // Вложенные директории не разрешены
    }
    
    // Создаем пользователя
    if (create_user(username) == 0) {
        return 0;
    }
    return -EIO;
}

static int vfs_rmdir(const char* path) {
    std::string spath(path);
    if (spath.length() <= 1) return -EINVAL;
    
    std::string username = spath.substr(1);
    if (username.find('/') != std::string::npos) {
        return -EINVAL;
    }
    
    std::lock_guard<std::mutex> lock(fs_mutex);
    auto it = user_cache.find(username);
    if (it == user_cache.end()) return -ENOENT;
    
    // Удаляем пользователя
    delete_user(username);
    return 0;
}

static struct fuse_operations vfs_oper = {};

void init_fuse_operations() {
    memset(&vfs_oper, 0, sizeof(vfs_oper));
    vfs_oper.getattr = vfs_getattr;
    vfs_oper.mkdir = vfs_mkdir;
    vfs_oper.rmdir = vfs_rmdir;
    vfs_oper.open = vfs_open;
    vfs_oper.read = vfs_read;
    vfs_oper.readdir = vfs_readdir;
}

// Поток для FUSE
void* fuse_thread(void*) {
    init_fuse_operations();
    
    char* argv[] = {
        (char*)"kubsh_vfs",
        (char*)"-f",
        (char*)"-s",
        (char*)MOUNT_POINT.c_str(),
        NULL
    };
    
    fuse_main(4, argv, &vfs_oper, NULL);
    return nullptr;
}

int main() {
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;
    
    std::string input;
    
    const char* homeEnv = getenv("HOME");
    if (!homeEnv) {
        homeEnv = "/tmp";
    }
    std::string home = homeEnv;
    std::string history_file = home + "/.kubsh_history";
    
    MOUNT_POINT = home + "/users";
    
    std::ofstream history_out(history_file, std::ios::app);
    if (!history_out.is_open()) {
        std::cerr << "History file unavailable!" << std::endl;
    }
    
    // Создаем точку монтирования
    struct stat st;
    if (stat(MOUNT_POINT.c_str(), &st) != 0) {
        if (mkdir(MOUNT_POINT.c_str(), 0755) != 0) {
            std::cerr << "Failed to create mount point: " << MOUNT_POINT << std::endl;
            return 1;
        }
    }
    
    // Инициализируем кеш пользователей
    refresh_user_cache();
    
    // Запускаем FUSE в отдельном потоке
    pthread_t fuse_tid;
    pthread_create(&fuse_tid, nullptr, fuse_thread, nullptr);
    
    // Ждем пока FUSE смонтируется
    sleep(1);
    
    signal(SIGHUP, handle_sighup);
    
    std::cout << "VFS mounted at: " << MOUNT_POINT << std::endl;
    
    while (true) {
        std::cout << "kubsh$ ";
        
        if (!std::getline(std::cin, input)) {
            std::cout << "\nExiting...\n";
            break;
        }
        
        if (history_out.is_open()) {
            history_out << input << std::endl;
        }
        
        // echo
        if (input.rfind("echo ", 0) == 0) {
            std::string echoArg = input.substr(5);
            if (echoArg.length() >= 2 && echoArg[0] == '\'' && echoArg[echoArg.length()-1] == '\'') {
                echoArg = echoArg.substr(1, echoArg.length()-2);
            } else if (echoArg.length() >= 2 && echoArg[0] == '"' && echoArg[echoArg.length()-1] == '"') {
                echoArg = echoArg.substr(1, echoArg.length()-2);
            }
            std::cout << echoArg << std::endl;
            continue;
        }
        
        // debug
        if (input.rfind("debug ", 0) == 0) {
            std::string debugArg = input.substr(6);
            if (debugArg.length() >= 2 && debugArg[0] == '\'' && debugArg[debugArg.length()-1] == '\'') {
                debugArg = debugArg.substr(1, debugArg.length()-2);
            } else if (debugArg.length() >= 2 && debugArg[0] == '"' && debugArg[debugArg.length()-1] == '"') {
                debugArg = debugArg.substr(1, debugArg.length()-2);
            }
            std::cout << std::endl << debugArg << std::endl;
            continue;
        }
        
        // echo переменной окружения
        if (input.rfind("\\e $", 0) == 0) {
            std::string var = input.substr(4);
            const char* value = std::getenv(var.c_str());
            if (value) {
                std::string valueStr(value);
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
            } else {
                std::cout << std::endl << "Переменная не найдена" << std::endl;
            }
            continue;
        }
        
        // Команды пользователей (теперь через VFS)
        if (input.rfind("\\adduser ", 0) == 0) {
            std::string username = input.substr(9);
            std::string user_dir = MOUNT_POINT + "/" + username;
            if (mkdir(user_dir.c_str(), 0755) == 0) {
                std::cout << "User created: " << username << std::endl;
            } else {
                std::cerr << "Failed to create user: " << username << std::endl;
            }
            continue;
        }
        
        if (input.rfind("\\deluser ", 0) == 0) {
            std::string username = input.substr(9);
            std::string user_dir = MOUNT_POINT + "/" + username;
            if (rmdir(user_dir.c_str()) == 0) {
                std::cout << "User deleted: " << username << std::endl;
            } else {
                std::cerr << "Failed to delete user: " << username << std::endl;
            }
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
        
        // выход
        if (input == "\\q") {
            std::cout << "Exiting...\n";
            break;
        }
        
        // lsblk
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
        
        // Обычные команды
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
    
    // Размонтируем FUSE
    std::string umount_cmd = "fusermount -u " + MOUNT_POINT;
    system(umount_cmd.c_str());
    
    return 0;
}