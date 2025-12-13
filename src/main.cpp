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

const bool testflag = true;
std::string file;
std::ofstream outFile;
std::string USERS_DIR = "/opt/users";

std::map<std::string, std::vector<std::string>> user_database;
std::atomic<bool> stop_monitoring(false);

void monitor_users_dir() {
    while (!stop_monitoring) {
        DIR* dir = opendir(USERS_DIR.c_str());
        if (dir) {
            struct dirent* entry;
            while ((entry = readdir(dir)) != nullptr) {
                if (entry->d_type == DT_DIR && std::string(entry->d_name) != "." && std::string(entry->d_name) != "..") {
                    std::string username = entry->d_name;
                    if (user_database.find(username) == user_database.end()) {
                        std::string cmd = "useradd -m -s /bin/bash " + username + " 2>/dev/null";
                        int result = system(cmd.c_str());
                        
                        if (result == 0) {
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
                                    user_database[username] = fields;
                                    break;
                                }
                            }
                            passwd_file.close();
                        
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
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

void handle_sighup(int sig) {
    if (sig == SIGHUP) {
        std::cout << "Configuration reloaded" << std::endl;
    }
}

std::vector<std::string> split(const std::string& s, char delimiter) {
    std::vector<std::string> tokens;
    std::string token;
    std::istringstream tokenStream(s);
    while (std::getline(tokenStream, token, delimiter)) {
        if (!token.empty()) {
            tokens.push_back(token);
        }
    }
    return tokens;
}

std::string trim(const std::string& str) {
    size_t start = str.find_first_not_of(" \t\n\r");
    size_t end = str.find_last_not_of(" \t\n\r");
    if (start == std::string::npos || end == std::string::npos) {
        return "";
    }
    return str.substr(start, end - start + 1);
}

std::string homedir() {
    const char* home = getenv("HOME");
    if (home) return std::string(home);
    
    struct passwd* pw = getpwuid(getuid());
    if (pw) return std::string(pw->pw_dir);
    
    return ".";
}

void load_user() {
    user_database.clear();
    std::ifstream passwd_file("/etc/passwd");
    std::string line;
    
    while (std::getline(passwd_file, line)) {
        std::vector<std::string> fields = split(line, ':');
        if (fields.size() >= 7) {
            user_database[fields[0]] = fields;
        }
    }
    passwd_file.close();
}

void vfsWithPass() {
    mkdir(USERS_DIR.c_str(), 0755);
    
    for (const auto& [username, fields] : user_database) {
        if (fields.size() >= 7) {
            std::string shell = fields[6];
            if (shell.length() >= 2 && shell.substr(shell.length() - 2) == "sh") {
                std::string user_dir = USERS_DIR + "/" + username;
                mkdir(user_dir.c_str(), 0755);
                
                std::ofstream id_file(user_dir + "/id");
                if (id_file.is_open()) id_file << fields[2];
                id_file.close();
                
                std::ofstream home_file(user_dir + "/home");
                if (home_file.is_open()) home_file << fields[5];
                home_file.close();
                
                std::ofstream shell_file(user_dir + "/shell");
                if (shell_file.is_open()) shell_file << fields[6];
                shell_file.close();
            }
        }
    }
}

void create_vfs() {
    USERS_DIR = "/opt/users";
    
    load_user();
    
    if (user_database.find("root") == user_database.end()) {
        std::vector<std::string> root_fields = {"root", "x", "0", "0", "root", "/root", "/bin/bash"};
        user_database["root"] = root_fields;
    }
    
    mkdir(USERS_DIR.c_str(), 0755);
    vfsWithPass();
}

void debug(const std::string& command) {
    std::string text = command.substr(5);
    text = trim(text);
    
    if (text.length() >= 2 && 
        ((text[0] == '\'' && text[text.length()-1] == '\'') ||
         (text[0] == '"' && text[text.length()-1] == '"'))) {
        text = text.substr(1, text.length() - 2);
    }
    
    std::cout << text << std::endl;
}

void env(const std::string& command) {
    size_t dollar = command.find('$');
    if (dollar != std::string::npos) {
        std::string name = command.substr(dollar + 1);
        name = trim(name);
        
        std::string clear;
        for (char c : name) {
            if (isalnum(c) || c == '_') {
                clear += c;
            } else {
                break;
            }
        }
        
        if (const char* value = getenv(clear.c_str())) {
            if (clear == "PATH") {
                std::vector<std::string> paths = split(value, ':');
                std::vector<std::string> unique;
                for (const auto& path : paths) {
                    if (!path.empty() && std::find(unique.begin(), unique.end(), path) == unique.end()) {
                        unique.push_back(path);
                    }
                }
                for (const auto& path : unique) {
                    std::cout << path << std::endl;
                }
            } else {
                std::cout << value << std::endl;
            }
        }
    }
}

void execute(const std::string& command) {
    std::vector<std::string> args = split(command, ' ');
    if (args.empty()) return;
    
    std::vector<char*> c_args;
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

bool check_sign(const std::string& device) {
    std::ifstream disk(device, std::ios::binary);
    if (!disk) {
        std::cout << "Error: cannot open device " << device << std::endl;
        return false;
    }
    
    char mbr[512];
    disk.read(mbr, 512);
    
    if (disk.gcount() < 512) {
        std::cout << "Error: cannot read full MBR (only " << disk.gcount() << " bytes read)" << std::endl;
        return false;
    }
    
    unsigned char byte510 = static_cast<unsigned char>(mbr[510]);
    unsigned char byte511 = static_cast<unsigned char>(mbr[511]);
    
    std::cout << "MBR signature bytes: " << std::hex << (int)byte510 << " " << (int)byte511 << std::dec << std::endl;
    
    if (byte510 == 0x55 && byte511 == 0xAA) {
        std::cout << "Valid MBR signature (55AA) found" << std::endl;
        return true;
    } else {
        std::cout << "Invalid MBR signature" << std::endl;
        return false;
    }
}

void part_table(const std::string& device) {
    std::ifstream disk(device, std::ios::binary);
    if (!disk) return;
    
    char mbr[512];
    disk.read(mbr, 512);
    if (disk.gcount() < 512) return;
    
    for (int i = 0; i < 4; i++) {
        int offset = 446 + i * 16;
        
        unsigned char status = static_cast<unsigned char>(mbr[offset]);
        unsigned char partition_type = static_cast<unsigned char>(mbr[offset + 4]);
        
        if (partition_type != 0x00) {
            std::cout << "Partition " << (i + 1) << ": ";
            std::cout << (status == 0x80 ? "Active" : "Inactive") << ", ";
            std::cout << "Type: 0x" << std::hex << (int)partition_type << std::dec;
            
            unsigned int size_sectors = 
                (static_cast<unsigned char>(mbr[offset + 12]) << 24) |
                (static_cast<unsigned char>(mbr[offset + 13]) << 16) |
                (static_cast<unsigned char>(mbr[offset + 14]) << 8) |
                (static_cast<unsigned char>(mbr[offset + 15]));
            
            std::cout << ", Size: " << (size_sectors * 512 / 1024 / 1024) << " MB" << std::endl;
        }
    }
}

void list_part(const std::string& device) {
    std::cout << "Checking MBR signature on " << device << "..." << std::endl;
    
    if (check_sign(device)) {
        std::cout << "Partition table for " << device << ":" << std::endl;
        part_table(device);
    } else {
        std::cout << "No valid MBR found or device cannot be accessed" << std::endl;
        std::cout << "Trying alternative method..." << std::endl;
        std::string command = "lsblk " + device + " 2>/dev/null";
        system(command.c_str());
    }
}

int main() {
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;
    
    std::string home_dir = homedir();
    file = home_dir + "/.kubsh_history";
    outFile.open(file, std::ios::app);
    
    signal(SIGHUP, handle_sighup);
    
    create_vfs();
    std::thread monitor_thread(monitor_users_dir);
    monitor_thread.detach();
    
    std::string input;
    
    while(true) {
        if (!testflag) {
            std::cout << "kubsh$ ";
            std::cout.flush();
        }
        
        if (!std::getline(std::cin, input)) {
            break;
        }
        
        std::string trinput = trim(input);
        
        if (trinput.empty()) {
            continue;
        }
        
        if (outFile.is_open()) {
            outFile << trinput << std::endl;
            outFile.flush();
        }
        
        if (trinput == "\\q") {
            break;
        }
        else if (trinput.find("debug") == 0) {
            debug(trinput);
            if (testflag) exit(0);
        }
        else if (trinput.find("\\e") == 0) {
            env(trinput);
            if (testflag) exit(0);
        }
        else if (trinput.find("cat") == 0) {
            execute(trinput);
            if (testflag) exit(0);
        }
        else if (trinput.find("\\l") == 0) {
            std::vector<std::string> parts = split(trinput, ' ');
            if (parts.size() >= 2) {
                list_part(parts[1]);
            } else {
                std::cout << "Usage: \\l <device>" << std::endl;
            }
            if (testflag) exit(0);
        }
        else if (trinput.rfind("\\adduser ", 0) == 0) {
            std::string username = trinput.substr(9);
            std::string user_dir = USERS_DIR + "/" + username;
            mkdir(user_dir.c_str(), 0755);
        }
        else if (trinput.rfind("\\deluser ", 0) == 0) {
            std::string username = trinput.substr(9);
            user_database.erase(username);
            std::string cmd = "userdel -r " + username + " >/dev/null 2>&1";
            system(cmd.c_str());
            std::string user_dir = USERS_DIR + "/" + username;
            std::string rmcmd = "rm -rf " + user_dir;
            system(rmcmd.c_str());
        }
        else if (trinput.rfind("cd ", 0) == 0) {
            std::string path = trinput.substr(3);
            if (path.empty()) path = getenv("HOME");
            if (chdir(path.c_str()) != 0) {
                perror("cd");
            }
        }
        else {
            std::cout << trinput << ": command not found" << std::endl;
            if (testflag) exit(0);
        }
    }
    
    stop_monitoring = true;
    
    if (outFile.is_open()) {
        outFile.close();
    }
    
    return 0;
}