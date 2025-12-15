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

const bool testflag = true;
string file;
ofstream outFile;
string users_dir = "/opt/users";

map<string, vector<string>> user_database;
atomic<bool> stop_monitoring(false);
void c_vfs_mon() {
    while (!stop_monitoring) {
        DIR* dir = opendir(users_dir.c_str());
        if (dir) {
            struct dirent* entry;
            while ((entry = readdir(dir)) != nullptr) {
                if (entry->d_type == DT_DIR && string(entry->d_name) != "." && string(entry->d_name) != "..") {
                    string username = entry->d_name;
                    if (user_database.find(username) == user_database.end()) {
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
                                    user_database[username] = fields;
                                    break;
                                }
                            }
                            passwd_file.close();
                        
                            string user_path = users_dir + "/" + username;
                            ofstream id_file(user_path + "/id");
                            if (id_file.is_open()) id_file << user_database[username][2];
                            id_file.close();
                            
                            ofstream home_file(user_path + "/home");
                            if (home_file.is_open()) home_file << user_database[username][5];
                            home_file.close();
                            
                            ofstream shell_file(user_path + "/shell");
                            if (shell_file.is_open()) shell_file << user_database[username][6];
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

void sign(int sig) {
    if (sig == SIGHUP) {
        cout << "Configuration reloaded" << endl;
    }
}

vector<string> split(const string& s, char delimiter) {
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

string trim(const string& str) {
    size_t start = str.find_first_not_of(" \t\n\r");
    size_t end = str.find_last_not_of(" \t\n\r");
    if (start == string::npos || end == string::npos) {
        return "";
    }
    return str.substr(start, end - start + 1);
}

string homedir() {
    const char* home = getenv("HOME");
    if (home) return string(home);
    
    struct passwd* pw = getpwuid(getuid());
    if (pw) return string(pw->pw_dir);
    
    return ".";
}

void history(const string& command) {
    if (!command.empty() && command != "\\q" && outFile.is_open()) {
        outFile << command << endl;
        outFile.flush();
    }
}

void debug(const string& command) {
    string text = command.substr(5);
    text = trim(text);
    
    if (text.length() >= 2 && 
        ((text[0] == '\'' && text[text.length()-1] == '\'') ||
         (text[0] == '"' && text[text.length()-1] == '"'))) {
        text = text.substr(1, text.length() - 2);
    }
    
    cout << text << endl;
}

void env(const string& command) {
    size_t dollar = command.find('$');
    if (dollar != string::npos) {
        string name = command.substr(dollar + 1);
        name = trim(name);
        
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
                vector<string> paths = split(value, ':');
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

void execute(const string& command) {
    vector<string> args = split(command, ' ');
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

bool check_sign(const string& device) {
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

void part_table(const string& device) {
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

void list_part(const string& device) {
    cout << "Checking MBR signature on " << device << "..." << endl;
    
    if (check_sign(device)) {
        cout << "Partition table for " << device << ":" << endl;
        part_table(device);
    } else {
        cout << "No valid MBR found or device cannot be accessed" << endl;
        cout << "Trying alternative method..." << endl;
        string command = "lsblk " + device + " 2>/dev/null";
        system(command.c_str());
    }
}

void load_user() {
    user_database.clear();
    ifstream passwd_file("/etc/passwd");
    string line;
    
    while (getline(passwd_file, line)) {
        vector<string> fields = split(line, ':');
        if (fields.size() >= 7) {
            user_database[fields[0]] = fields;
        }
    }
    passwd_file.close();
}

void vfsWithPass() {
    mkdir(users_dir.c_str(), 493);
    
    for (const auto& [username, fields] : user_database) {
        if (fields.size() >= 7) {
            string shell = fields[6];
            if (shell.length() >= 2 && shell.substr(shell.length() - 2) == "sh") {
                string user_dir = users_dir + "/" + username;
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

void init_user() {
    users_dir = "/opt/users";
    
    load_user();
    
    if (user_database.find("root") == user_database.end()) {
        vector<string> root_fields = {"root", "x", "0", "0", "root", "/root", "/bin/bash"};
        user_database["root"] = root_fields;
    }
    
    mkdir(users_dir.c_str(), 493);
    vfsWithPass();
    
    cout << "Users VFS mounted at: " << users_dir << endl;
}

int main() {
    string home_dir = homedir();
    file = home_dir + "/.kubsh_history";
    outFile.open(file, ios::app);
    
    signal(SIGHUP, sign);
    
    init_user();
    thread monitor_thread(c_vfs_mon);
    monitor_thread.detach();
    
    string input;
    
    while(true) {
        if (!testflag) {
            cout << "$ ";
            cout.flush();
        }
        
        if (!getline(cin, input)) {
            break;
        }
        
        string trinput = trim(input);
        
        if (trinput.empty()) {
            continue;
        }
        
        history(trinput);
        
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
            vector<string> parts = split(trinput, ' ');
            if (parts.size() >= 2) {
                list_part(parts[1]);
            } else {
                cout << "Usage: \\l <device>" << endl;
            }
            if (testflag) exit(0);
        }
        else {
            cout << trinput << ": command not found" << endl;
            if (testflag) exit(0);
        }
    }
    
    stop_monitoring = true;
    
    if (outFile.is_open()) {
        outFile.close();
    }
    
    return 0;
}