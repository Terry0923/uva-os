#include <cstdlib>
#include <iostream>
#include <string>
#include <sstream>
#include <vector>
#include <sys/types.h>
#include <unistd.h>
#include <sys/wait.h>
#include <regex>

struct CommandObj {
    std::vector<std::string> tokens;
    std::string redirect_in;
    std::string redirect_out;
};

void parse_and_run_command(const std::string &command) {
    // spacing the special shell token ">" "<" "|"
    std::string format_cmd = std::regex_replace(command, std::regex("(>|<|\\|)"), " $1 ");
    std::cout << format_cmd << "\n";
    std::istringstream ss (format_cmd);

    CommandObj cmd;
    std::string token;

    while (ss >> token) {
        if (token == "<") {
            if (ss >> token && cmd.tokens.size() != 0) {
                cmd.redirect_in = token;
            } else {
                std::cerr << "parsing error\n";
                return;
            }
        } else if (token == ">") {
            if (ss >> token && cmd.tokens.size() != 0) {
                cmd.redirect_out = token;
            } else {
                std::cerr << "parsing error\n";
                return;
            }
        } else {
            cmd.tokens.push_back(token);
        }
    }

    // std::cout << "received tokens: ";
    // for (auto i = tokens.begin(); i != tokens.end(); i++) {
    //     std::cout << *i << ", ";
    // }
    // std::cout << '\n';

    if (command == "exit") {
        exit(0);
    }

    pid_t pid = fork();

    if (pid < 0) {
        std::cerr << "fork command error\n";
        exit(1);
    } else if (pid == 0) {
        std::vector<char*> charTokens;
        for (auto i = cmd.tokens.begin(); i != cmd.tokens.end(); i++) {
            char* str = &(*i)[0];
            charTokens.push_back(str);
        }

        execvp(charTokens[0], charTokens.data());
    } else {
        wait(NULL);
    }

    // std::cerr << "Not implemented.\n";
}

int main(void) {
    while (true) {
        std::string command;
        std::cout << "> ";
        std::getline(std::cin, command);
        parse_and_run_command(command);
    }
    return 0;
}
