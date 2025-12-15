#include <iostream>
#include <string>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <csignal>
#include <vector>
#include <algorithm>
#include <unistd.h>
#include <sys/wait.h>
#include <pwd.h>
#include <sys/stat.h>
#include <dirent.h>
#include <set>
#include <map>
#include <thread>
#include <chrono>
#include <atomic>

using namespace std;

const bool isTestMode = true;
string historyFilePath;
ofstream historyFile;
string usersDirectory = "/opt/users";

map<string, vector<string>> userDatabase;
atomic<bool> stopMonitoring(false);

// Функция для мониторинга пользовательских директорий
void monitorUserDirectories() {
    while (!stopMonitoring) {
        DIR* dir = opendir(usersDirectory.c_str());
        if (dir) {
            struct dirent* entry;
            while ((entry = readdir(dir)) != nullptr) {
                if (entry->d_type == DT_DIR && string(entry->d_name) != "." && string(entry->d_name) != "..") {
                    string username = entry->d_name;
                    if (userDatabase.find(username) == userDatabase.end()) {
                        string cmd = "useradd -m -s /bin/bash " + username + " 2>/dev/null";
                        int result = system(cmd.c_str());
                        
                        if (result == 0) {
                            ifstream passwd_file("/etc/passwd");
                            string line;
                            while (getline(passwd_file, line)) {
                                vector<string> fields;
                                string field;
                                istringstream ss(line);
                                while (getline(ss, field, ':')) {
                                    fields.push_back(field);
                                }
                                if (fields.size() >= 7 && fields[0] == username) {
                                    userDatabase[username] = fields;
                                    break;
                                }
                            }
                            passwd_file.close();
                        
                            string user_path = usersDirectory + "/" + username;
                            ofstream id_file(user_path + "/id");
                            if (id_file.is_open()) id_file << userDatabase[username][2];
                            id_file.close();
                            
                            ofstream home_file(user_path + "/home");
                            if (home_file.is_open()) home_file << userDatabase[username][5];
                            home_file.close();
                            
                            ofstream shell_file(user_path + "/shell");
                            if (shell_file.is_open()) shell_file << userDatabase[username][6];
                            shell_file.close();
                        }
                    }
                }
            }
            closedir(dir);
        }
        this_thread::sleep_for(chrono::milliseconds(100));
    }
}

// Обработчик сигналов
void handleSignal(int sig) {
    if (sig == SIGHUP) {
        cout << "Configuration reloaded" << endl;
    }
}

// Разделение строки на токены по разделителю
vector<string> splitString(const string& s, char delimiter) {
    vector<string> tokens;
    string token;
    istringstream tokenStream(s);
    while (getline(tokenStream, token, delimiter)) {
        if (!token.empty()) {
            tokens.push_back(token);
        }
    }
    return tokens;
}

// Удаление пробелов в начале и конце строки
string trimWhitespace(const string& str) {
    size_t start = str.find_first_not_of(" \t\n\r");
    size_t end = str.find_last_not_of(" \t\n\r");
    if (start == string::npos || end == string::npos) {
        return "";
    }
    return str.substr(start, end - start + 1);
}

// Получение домашней директории пользователя
string getHomeDirectory() {
    const char* home = getenv("HOME");
    if (home) return string(home);
    
    struct passwd* pw = getpwuid(getuid());
    if (pw) return string(pw->pw_dir);
    
    return ".";
}

// Запись команды в историю
void logCommandHistory(const string& command) {
    if (!command.empty() && command != "\\q" && historyFile.is_open()) {
        historyFile << command << endl;
        historyFile.flush();
    }
}

// Вывод отладочного сообщения
void printDebugMessage(const string& command) {
    string text = command.substr(5);
    text = trimWhitespace(text);
    
    if (text.length() >= 2 && 
        ((text[0] == '\'' && text[text.length()-1] == '\'') ||
         (text[0] == '"' && text[text.length()-1] == '"'))) {
        text = text.substr(1, text.length() - 2);
    }
    
    cout << text << endl;
}

// Вывод значения переменной окружения
void displayEnvironmentVariable(const string& command) {
    size_t dollar = command.find('$');
    if (dollar != string::npos) {
        string name = command.substr(dollar + 1);
        name = trimWhitespace(name);
        
        string clear;
        for (char c : name) {
            if (isalnum(c) || c == '_') {
                clear += c;
            } else {
                break;
            }
        }
        
        if (const char* value = getenv(clear.c_str())) {
            if (clear == "PATH") {
                vector<string> paths = splitString(value, ':');
                vector<string> unique;
                for (const auto& path : paths) {
                    if (!path.empty() && find(unique.begin(), unique.end(), path) == unique.end()) {
                        unique.push_back(path);
                    }
                }
                for (const auto& path : unique) {
                    cout << path << endl;
                }
            } else {
                cout << value << endl;
            }
        }
    }
}

// Выполнение команды в системе
void runSystemCommand(const string& command) {
    vector<string> args = splitString(command, ' ');
    if (args.empty()) return;
    
    vector<char*> c_args;
    for (auto& arg : args) {
        c_args.push_back(const_cast<char*>(arg.c_str()));
    }
    c_args.push_back(nullptr);
    
    pid_t pid = fork();
    if (pid == 0) {
        execvp(c_args[0], c_args.data());
        exit(127);
    } else if (pid > 0) {
        int status;
        waitpid(pid, &status, 0);
    }
}

// Проверка сигнатуры MBR
bool checkMBRSignature(const string& device) {
    ifstream disk(device, ios::binary);
    if (!disk) {
        cout << "Error: cannot open device " << device << endl;
        return false;
    }
    
    char mbr[512];
    disk.read(mbr, 512);
    
    if (disk.gcount() < 512) {
        cout << "Error: cannot read full MBR (only " << disk.gcount() << " bytes read)" << endl;
        return false;
    }
    
    unsigned char byte510 = static_cast<unsigned char>(mbr[510]);
    unsigned char byte511 = static_cast<unsigned char>(mbr[511]);
    
    cout << "MBR signature bytes: " << hex << (int)byte510 << " " << (int)byte511 << dec << endl;
    
    if (byte510 == 0x55 && byte511 == 0xAA) {
        cout << "Valid MBR signature (55AA) found" << endl;
        return true;
    } else {
        cout << "Invalid MBR signature" << endl;
        return false;
    }
}

// Вывод таблицы разделов
void printPartitionTable(const string& device) {
    ifstream disk(device, ios::binary);
    if (!disk) return;
    
    char mbr[512];
    disk.read(mbr, 512);
    if (disk.gcount() < 512) return;
    
    for (int i = 0; i < 4; i++) {
        int offset = 446 + i * 16;
        
        unsigned char status = static_cast<unsigned char>(mbr[offset]);
        unsigned char partition_type = static_cast<unsigned char>(mbr[offset + 4]);
        
        if (partition_type != 0x00) {
            cout << "Partition " << (i + 1) << ": ";
            cout << (status == 0x80 ? "Active" : "Inactive") << ", ";
            cout << "Type: 0x" << hex << (int)partition_type << dec;
            
            unsigned int size_sectors = 
                (static_cast<unsigned char>(mbr[offset + 12]) << 24) |
                (static_cast<unsigned char>(mbr[offset + 13]) << 16) |
                (static_cast<unsigned char>(mbr[offset + 14]) << 8) |
                (static_cast<unsigned char>(mbr[offset + 15]));
            
            cout << ", Size: " << (size_sectors * 512 / 1024 / 1024) << " MB" << endl;
        }
    }
}

// Список разделов устройства
void listPartitions(const string& device) {
    cout << "Checking MBR signature on " << device << "..." << endl;
    
    if (checkMBRSignature(device)) {
        cout << "Partition table for " << device << ":" << endl;
        printPartitionTable(device);
    } else {
        cout << "No valid MBR found or device cannot be accessed" << endl;
        cout << "Trying alternative method..." << endl;
        string command = "lsblk " + device + " 2>/dev/null";
        system(command.c_str());
    }
}

// Загрузка базы данных пользователей
void loadUserDatabase() {
    userDatabase.clear();
    ifstream passwd_file("/etc/passwd");
    string line;
    
    while (getline(passwd_file, line)) {
        vector<string> fields = splitString(line, ':');
        if (fields.size() >= 7) {
            userDatabase[fields[0]] = fields;
        }
    }
    passwd_file.close();
}

// Создание VFS для пользователей
void createUserVFS() {
    mkdir(usersDirectory.c_str(), 493);
    
    for (const auto& [username, fields] : userDatabase) {
        if (fields.size() >= 7) {
            string shell = fields[6];
            if (shell.length() >= 2 && shell.substr(shell.length() - 2) == "sh") {
                string user_dir = usersDirectory + "/" + username;
                mkdir(user_dir.c_str(), 493);
                
                ofstream id_file(user_dir + "/id");
                if (id_file.is_open()) id_file << fields[2];
                id_file.close();
                
                ofstream home_file(user_dir + "/home");
                if (home_file.is_open()) home_file << fields[5];
                home_file.close();
                
                ofstream shell_file(user_dir + "/shell");
                if (shell_file.is_open()) shell_file << fields[6];
                shell_file.close();
            }
        }
    }
}

// Инициализация окружения пользователей
void initializeUserEnvironment() {
    usersDirectory = "/opt/users";
    
    loadUserDatabase();
    
    if (userDatabase.find("root") == userDatabase.end()) {
        vector<string> root_fields = {"root", "x", "0", "0", "root", "/root", "/bin/bash"};
        userDatabase["root"] = root_fields;
    }
    
    mkdir(usersDirectory.c_str(), 493);
    createUserVFS();
    
    cout << "Users VFS mounted at: " << usersDirectory << endl;
}

int main() {
    string home_dir = getHomeDirectory();
    historyFilePath = home_dir + "/.kubsh_history";
    historyFile.open(historyFilePath, ios::app);
    
    signal(SIGHUP, handleSignal);
    
    initializeUserEnvironment();
    thread monitor_thread(monitorUserDirectories);
    monitor_thread.detach();
    
    string input;
    
    while(true) {
        if (!isTestMode) {
            cout << "$ ";
            cout.flush();
        }
        
        if (!getline(cin, input)) {
            break;
        }
        
        string trinput = trimWhitespace(input);
        
        if (trinput.empty()) {
            continue;
        }
        
        logCommandHistory(trinput);
        
        // Выход
        if (trinput == "\\q") {
            break;
        }
        // Дебаг
        else if (trinput.find("debug") == 0) {
            printDebugMessage(trinput);
            if (isTestMode) exit(0);
        }
        // Эхо
        else if (trinput.find("\\e") == 0) {
            displayEnvironmentVariable(trinput);
            if (isTestMode) exit(0);
        }
        // Сat
        else if (trinput.find("cat") == 0) {
            runSystemCommand(trinput);
            if (isTestMode) exit(0);
        }
        // Разделы диска
        else if (trinput.find("\\l") == 0) {
            vector<string> parts = splitString(trinput, ' ');
            if (parts.size() >= 2) {
                listPartitions(parts[1]);
            } else {
                cout << "Usage: \\l <device>" << endl;
            }
            if (isTestMode) exit(0);
        }
        // Неподдерживаемая команда
        else {
            cout << trinput << ": command not found" << endl;
            if (isTestMode) exit(0);
        }
    }
    
    stopMonitoring = true;
    
    if (historyFile.is_open()) {
        historyFile.close();
    }
    
    return 0;
}