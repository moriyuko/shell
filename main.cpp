#include <iostream>
#include <string>
#include <fstream>
#include <sstream>
#include <vector>
#include <algorithm>
#include <unistd.h>
#include <sys/wait.h>
#include <csignal>
#include <cstdlib>

using namespace std;

const bool testflag = true;
string file;
ofstream outFile;

string trim(const string& str) {
    size_t start = str.find_first_not_of(" \t\n\r");
    size_t end = str.find_last_not_of(" \t\n\r");
    if (start == string::npos || end == string::npos) {
        return "";
    }
    return str.substr(start, end - start + 1);
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

void history(const string& command) {
    if (!command.empty() && command != "\\q" && outFile.is_open()) {
        outFile << command << endl;
        outFile.flush();
    }
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

void sign(int sig) {
    if (sig == SIGHUP) {
        cout << "Configuration reloaded" << endl;
    }
}

int main() {
    file = ".kubsh_history";
    outFile.open(file, ios::app);
    
    signal(SIGHUP, sign);
    
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
            if (testflag) break;
        }
        else if (trinput.find("echo") == 0) {
            debug(trinput);
            if (testflag) break;
        }
        else if (trinput.find("\\e") == 0) {
            env(trinput);
            if (testflag) break;
        }
        else if (trinput.find("cat") == 0) {
            execute(trinput);
            if (testflag) break;
        }
        else {
            cout << trinput << ": command not found" << endl;
            if (testflag) break;
        }
    }
    
    if (outFile.is_open()) {
        outFile.close();
    }
    
    return 0;
}

