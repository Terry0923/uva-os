#include <cstdlib>
#include <iostream>
#include <string>
#include <sstream>
#include <vector>
#include <sys/types.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <regex>
#include <set>

struct CommandObj {
    pid_t pid;
    std::vector<std::string> tokens;
    std::string redirect_in;
    std::string redirect_out;
    int pipe_to = 0;
    int pipe_from = 0;
};

void parse_and_run_command(const std::string &command) {
    // spacing the special shell token ">" "<" "|"
    // std::string format_cmd = std::regex_replace(command, std::regex("(>|<|\\|)"), " $1 ");

    std::string format_cmd = command;
    std::istringstream ss (format_cmd);

    std::vector<CommandObj> cmds {CommandObj ()};

    std::string token;
    std::set<std::string> special_chars ({">", "<", "|"});

    while (ss >> token) {
        CommandObj& cmd = cmds.back();

        if (token == "<") {
            std::string target;
            if (!(ss >> target) || special_chars.count(target)) {
                std::cerr << "Invalid command\n";
                return;
            }

            cmd.redirect_in = target;
        } else if (token == ">") {
            std::string target;
            if (!(ss >> target) || special_chars.count(target)) {
                std::cerr << "Invalid command\n";
                return;
            }

            cmd.redirect_out = target;
        } else if (token == "|") {
            if (!cmd.tokens.size()) {
                std::cerr << "Invalid command\n";
                return;
            }

            cmd.pipe_to = 1;
            CommandObj new_cmd;
            new_cmd.pipe_from = 1;
            cmds.push_back(new_cmd);
        } else {
            cmd.tokens.push_back(token);
        }
    }

    for (auto i = cmds.begin(); i != cmds.end(); i++) {
        if (!(i->tokens.size())) {
            std::cerr << "Invalid command\n";
            return;
        }
    }

    int pipefd[2];

    for (auto cmdPtr = cmds.begin(); cmdPtr != cmds.end(); cmdPtr++) {
        CommandObj& cmd = *cmdPtr;

        if (cmd.tokens[0] == "exit") {
            exit(0);
        }

        if (cmd.pipe_from) {
            cmd.pipe_from = pipefd[0];
        }
        if (cmd.pipe_to) {
            if (pipe(pipefd) == -1) {
                std::cerr << "create pipe error\n";
                exit(1);
            }
            cmd.pipe_to = pipefd[1];
        }

        cmd.pid = fork();

        if (cmd.pid < 0) {
            std::cerr << "fork command error\n";
            return;
        } else if (cmd.pid == 0) {
            if (cmd.redirect_in != "") {
                int in_f = open(cmd.redirect_in.c_str(), O_RDONLY);
                if (in_f == -1) {
                    std::cerr << "Cannot read file " << cmd.redirect_in << "\n";
                    exit(errno);
                }
                dup2(in_f, STDIN_FILENO);
            }

            if (cmd.redirect_out != "") {
                int out_f = open(cmd.redirect_out.c_str(), O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
                if (out_f == -1) {
                    std::cerr << "Cannot write file " << cmd.redirect_out << "\n";
                    exit(errno);
                }
                dup2(out_f, STDOUT_FILENO);
            }

            if (cmd.pipe_from) {
                // pipe_to has already been closed in main process of previous iteration
                dup2(cmd.pipe_from, STDIN_FILENO);
            }

            if (cmd.pipe_to) {
                close(pipefd[0]);
                dup2(cmd.pipe_to, STDOUT_FILENO);
            }

            std::vector<char*> charTokens;
            for (auto i = cmd.tokens.begin(); i != cmd.tokens.end(); i++) {
                char* str = &(*i)[0];
                charTokens.push_back(str);
            }
            charTokens.push_back(NULL);

            execv(charTokens[0], charTokens.data());

            std::cerr << "Cannot execute " << charTokens[0] << "\n";
            exit(errno);
        } else {
            if (cmd.pipe_to) {
                close(cmd.pipe_to);
            }
            if (cmd.pipe_from) {
                close(cmd.pipe_from);
            }
        }
    }

    for (auto cmdPtr = cmds.begin(); cmdPtr != cmds.end(); cmdPtr++) {
        int status;
        waitpid(cmdPtr->pid, &status, 0);
        std::cout << "> Exit status: " << WEXITSTATUS(status) << "\n";
    }
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
