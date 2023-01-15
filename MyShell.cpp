#include <vector>
#include <string>
#include <cstring>
#include <unordered_map>
#include <algorithm>
#include <sstream>
#include <fstream>

#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <ctime>
#include <unistd.h>
#include <dirent.h>

using namespace std;

/* ---------- 宏定义 ---------- */

#define BUFFER_SIZE 1024
#define INVALID_PID (-1)

#define YELLOW "\e[1;33m"
#define WHITE "\e[0;37m"
#define RED "\e[1;31m"
#define GREEN "\e[1;32m"
#define BLUE "\e[1;34m"
#define CLEAR "\e[1;1H\e[2J"

/* ---------- 全局变量 ---------- */

namespace Global {

    // 储存当前指令
    string command;
    vector<string> command_tokens; // 切割后的指令段

    // 环境变量
    string host; // 主机名
    string user; // 用户名
    string home_path; // 用户主目录路径
    string shell_path; // MyShell 路径
    string manual_path; // 帮助手册路径
    string pwd; // 当前工作目录
    pid_t sub_pid = INVALID_PID; // 子进程号，默认为-1

    // 作业表
    constexpr unsigned MAX_WORK = 1024; // 最大子进程数
    bool is_backend = false; // 是否是后台指令
    unordered_map<pid_t, int> jobs; // 子进程 pid
    vector<int> work_id_list; // 已分配的作业号，用于计算分配

    /* 子进程状态
     * BACKEND - 子进程在后台执行
     * SUSPEND - 子进程被挂起
     */
    typedef enum {
        BACKEND, SUSPEND
    } JobStatus;

    unordered_map<pid_t, JobStatus> state; // 子进程状态
    unordered_map<pid_t, string> sub_commands; // 子进程执行的命令

    // 命令行参数
    unsigned argc = 0; // 命令行参数个数
    vector<string> argv; // 命令行参数字符串

    // 是否是批处理文件
    bool is_batch_file = false;
}

/* ---------- 辅助函数 ---------- */

// 初始化，获得主机名、用户名等
void Initialization(int argc, char**&argv);

// 显示命令提示符，包含当前路径，用户名和主机名
void DisplayPrompt();

// 处理组合键如 Ctrl+C Ctrl+Z 输入
void SignalHandle(int signal);

// 分割命令
vector<string> SpiltCommand(const string& cmd);

// 向后台进程表中添加进程
void AddJob(pid_t pid,Global::JobStatus stat,const string& sub_cmd);

// 后台进程表项格式化为字符串
string FormatJobMsg(pid_t pid, bool finish);

// 解析'$'开头的变量
string Parse2Value(const string&cmd_token);

// 格式化输出目录里的内容
void FormatPrintDir(DIR *dir, char *path);

/* ---------- 指令解释执行 ---------- */

// 第一阶段解析，处理后台执行字符'&'
void EvaluationEntry();

// 第二阶段解析，处理由管道组成的多条命令
void EvaluationOfPipe(vector<string>&cmd_token);

// 第三阶段解析，处理重定向
void EvaluationOfRedirect(const vector<string>&cmd_token);

// 第四阶段，执行指令
void Execute(const vector<string>&cmd_token);

/* ---------- 内建命令 ---------- */

// cd: 改变目录
void cd(const vector<string>&cmd_token);

// clr: 清屏
void clear(const vector<string>&cmd_token);

// echo: 显示文本并换行
void echo(const vector<string>&cmd_token);

// pwd: 显示当前目录路径
void pwd(const vector<string>&cmd_token);

// exit: 退出 MyShell
void exit(const vector<string>&cmd_token);

// time: 显示当前时间
void time(const vector<string>&cmd_token);

// umask: 显示当前掩码或修改掩码
void umask(const vector<string>&cmd_token);

// dir: 列出目录内容
void dir(const vector<string>&cmd_token);

// exec: 使用指定命令替换 MyShell
void exec(const vector<string>&cmd_token);

// help: 显示用户手册
void help(const vector<string>&cmd_token);

// set: 设置环境变量的值，没有参数则列出所有环境变量
void set(const vector<string>&cmd_token);

// test: 进行字符串、数字的比较
void test(const vector<string>&cmd_token);

// bg: 将前台命令转移到后台执行
void bg(const vector<string>&cmd_token);

// fg: 将后台命令转移到前台执行
void fg(const vector<string>&cmd_token);

// jobs: 打印作业表
void jobs(const vector<string>&cmd_token);

/* ---------- main 函数 ---------- */

int main(int argc, char * argv[]) {
    Initialization(argc, argv);

    bool not_eof = true; // 是否执行到批文件的末尾
    char buffer[BUFFER_SIZE] = {0};

    while (not_eof) {
        // 显示提示
        DisplayPrompt();

        // 循环读入，直到读到 EOF 或者换行为止
        int i = 0;
        while (true) {
            // 从批文件中加载时，读到 EOF 结束
            if (read(STDIN_FILENO, buffer + i, 1) <= 0) {
                not_eof = false;
                break;
            }
            // 从命令行读入时，读到换行符结束
            if (buffer[i] == '\n') {
                break;
            }
            i++;
        }

        buffer[i] = '\0';
        Global::command = string(buffer); // 从 buffer 中转存到 command 中

        // 进行指令的切割
        Global::command_tokens = SpiltCommand(Global::command);

        // 指令解释入口
        EvaluationEntry();
    }
}

/* ---------- 辅助函数实现 ---------- */

void Initialization(int argc, char**&argv) {

    char buf[BUFFER_SIZE] = {0};
    int fd = -1; // 文件描述符

    // 拷贝命令行参数信息
    Global::argc = argc;
    for (int i = 0; i < argc; i++) {
        Global::argv.emplace_back(argv[i]);
    }

    if (argc >= 2) { // 给出的批文件数量多于一个
        fd = open(argv[1], O_RDONLY);

        if (fd < 0) {
            // 文件打开失败，退出并提示
            sprintf(buf, "MyShell: fail to access %s\n", argv[1]);
            fprintf(stderr, RED "%s", buf);
            exit(-1);
        }

        // 重定向输入
        dup2(fd, STDIN_FILENO);
        close(fd);

        Global::is_batch_file = true; // 设置批文件标记
    }

    gethostname(buf, BUFFER_SIZE); // 得到主机名
    Global::host = string(buf);

    Global::user = getenv("USERNAME"); // 得到用户名

    Global::home_path = getenv("HOME"); // 得到主目录路径

    Global::pwd = getenv("PWD"); // 得到当前工作目录路径

    Global::sub_pid = INVALID_PID; // 初始时没有子进程，为-1

    // 得到 MyShell 路径
    buf[readlink("/proc/self/exe", buf, BUFFER_SIZE)] = '\0';
    Global::shell_path = string(buf);
    // 覆盖当前 shell 路径
    setenv("SHELL", Global::shell_path.c_str(), 1);

    Global::manual_path = Global::pwd + "/manual"; // 得到帮助手册路径

    // 设置父进程路径
    setenv("PARENT", "\\bin\\bash", 1);

    // 设置中断信号处理函数
    signal(SIGINT, SignalHandle);
    signal(SIGTSTP, SignalHandle);
}

void DisplayPrompt() {
    // 控制颜色，输出命令提示符到终端
    // 若为批文件，不输出
    if (!Global::is_batch_file) {
        string prompt = string(YELLOW) + Global::user
                        + "@" + Global::host
                        + string(WHITE) + ":"
                        + string(BLUE) + Global::pwd
                        + string(WHITE) + "$ ";

        fprintf(stdout, "%s", prompt.c_str());
        fflush(stdout);
    }
}

void SignalHandle(int signal) {

    if (signal == SIGINT) { // 中断
        fprintf(stdout, "\n");
        kill(getpid(), SIGKILL);
    }
    else if (signal == SIGTSTP) { // 挂起
        fprintf(stdout, "\n");
        if (Global::sub_pid != INVALID_PID) {
            setpgid(Global::sub_pid, 0);
            kill(Global::sub_pid, SIGTSTP); // 挂起

            // 添加到jobs表中
            try {
                AddJob(Global::sub_pid, Global::JobStatus::SUSPEND, Global::command);
            }
            catch (const char *s) {
                fprintf(stderr, RED"%s", s);
            }
            fprintf(stdout, WHITE"%s", FormatJobMsg(Global::sub_pid, false).c_str());
            Global::sub_pid = INVALID_PID;
        }
    }
}

vector<string> SpiltCommand(const string& cmd) {
    stringstream stm;
    stm << cmd;

    vector<string> tokens; // 指令切割结果
    int i = 0;
    while (true) {
        tokens.emplace_back("");
        stm >> tokens[i]; // 储存切割结果
        if (tokens[i].empty()) break;
        i++;
    }

    tokens.pop_back(); // 弹出最后一个空字符串
    return tokens;
}

void AddJob(pid_t pid,Global::JobStatus stat,const string& sub_cmd) {
    if (Global::jobs.size() == Global::MAX_WORK) {
        throw "MyShell: job list is full\n";
    }

    // 添加作业号，为空时添加1
    if (Global::work_id_list.empty()) {
        Global::jobs.insert(pair<pid_t, int>(pid, 1));
        Global::work_id_list.push_back(1);
    }
        // 否则作业号为当前最大作业号+1
    else {
        Global::jobs.insert(pair<pid_t, int>(pid, *Global::work_id_list.rbegin() + 1));
        Global::work_id_list.push_back(*Global::work_id_list.rbegin() + 1);
    }
    Global::state.insert(pair<pid_t, Global::JobStatus>(pid, stat)); // 添加后台进程状态
    Global::sub_commands.insert(pair<pid_t, string>(pid, sub_cmd)); // 添加后台进程执行的指令信息
}

string FormatJobMsg(pid_t pid, bool finish) {
    if (Global::state[pid] != Global::BACKEND && Global::state[pid] != Global::SUSPEND) {
        return "";
    }
    else {
        return "[" + to_string(Global::jobs[pid]) + "]\t\t" +
               to_string(pid) + "\t\t" +
               ((finish) ? "Done" : (Global::state[pid] == Global::BACKEND) ? "Running" : "Suspend") + "\t\t" +
               Global::sub_commands[pid] + "\n";
    }
}

string Parse2Value(const string&cmd_token) {
    // '$'后是数字，需要返回命令行参数的值
    if (*cmd_token.begin() == '$') {
        if (cmd_token[1] >= '0' && cmd_token[1] <= '9') {
            return Global::argv[(int) cmd_token[1]];
        }
            // 命令行参数个数
        else if (cmd_token == "$#") {
            return to_string(Global::argc - 1);
        }
        else {
            // getenv 获得变量的值
            char *val = getenv(cmd_token.substr(1).c_str());
            // 环境变量存在
            if (val != nullptr) {
                return val;
            }
            else {
                return "";
            }
        }
    }
        // 被''或者""引用的字符串返回值
    else if (*cmd_token.begin() == '\'' || *cmd_token.begin() == '\"') {
        return cmd_token.substr(1, cmd_token.size() - 2);
    }
    else {
        return cmd_token;
    }
}

void FormatPrintDir(DIR *dir, char*path) {
    struct stat file_info{}; // 文件状态指针
    struct dirent *p;

    // 将 readdir(dir) 的返回值赋值给 p，并在其不为 nullptr 的情况下循环读取
    while ((p = readdir(dir)) != nullptr) {
        //除了. 和 ..，将目录下的其他内容输出
        if (strcmp(p->d_name, ".") != 0 && strcmp(p->d_name, "..") != 0) {
            string abs_path(string(path) + "/");
            abs_path += p->d_name;

            stat(abs_path.c_str(), &file_info); // 获取文件状态

            // 是否为目录文件
            if (S_ISDIR(file_info.st_mode)) {
                fprintf(stdout, BLUE "%s\t", p->d_name);
            }
                // 是否为可执行文件
            else if (access(p->d_name, X_OK) != -1) {
                fprintf(stdout, GREEN "%s\t", p->d_name);
            }
                // 其他文件
            else {
                fprintf(stdout, WHITE "%s\t", p->d_name);
            }
        }
    }
    fprintf(stdout, WHITE"\n");
}

/* ---------- 指令解释执行实现 ---------- */

void EvaluationEntry() {

    // 在执行新的指令前，先检查后台进程作业表是否有指令完成，
    // 若已经完成，打印返回信息
    for (auto &job: Global::jobs) {
        if (waitpid(job.first, nullptr, WNOHANG) == job.first) {
            fprintf(stdout, WHITE"%s", FormatJobMsg(job.first, true).c_str());

            // 更新 work_list
            Global::work_id_list.erase(find(Global::work_id_list.begin(), Global::work_id_list.end(),job.second));
            Global::jobs.erase(job.first);
            Global::state.erase(job.first);
        }
    }

    // 命令为空，返回
    if (Global::command_tokens.empty()) {
        return;
    }

    // 先处理后台运行字符'&'
    if (*Global::command.crbegin() == '&') {
        pid_t pid = fork(); // 创建子进程
        Global::is_backend = true;

        if (pid != 0) { // 父进程

            try {
                AddJob(pid, Global::BACKEND, Global::command); // 添加子进程
            }
            catch (const char *s) {
                fprintf(stderr, RED "%s", s);
            }

            Global::command[Global::command.find('&')] = ' '; // 将原指令中的'&'字符去掉
            Global::command_tokens.pop_back();

            // 打印子进程表
            fprintf(stdout, "%s", FormatJobMsg(pid, false).c_str());

        }
            // 子进程执行命令
        else {
            setpgid(0, 0); // 使子进程单独成为一个进程组，后台进程组自动忽略 Ctrl+Z, Ctrl+C 等信号

            Global::command[Global::command.find('&')] = ' '; // 将原指令中的'&'字符去掉
            Global::command_tokens.pop_back();

            try {
                EvaluationOfPipe(Global::command_tokens);
                exit(0);
            }
            catch (const char *s) {
                fprintf(stderr, RED "%s", s);
            }
        }
    }
        // 没有'&'，前台运行
    else {
        Global::is_backend = false;
        try {
            EvaluationOfPipe(Global::command_tokens);
        }
        catch (const char *s) {
            fprintf(stderr, RED "%s", s);
        }
    }
}

void EvaluationOfPipe(vector<string>& cmd_tokens) {
    bool has_pipe = false; // 是否有管道符

    if (find(cmd_tokens.begin(),
             cmd_tokens.end(),
             "|") != cmd_tokens.end()) {
        has_pipe = true;
    }

    // 没有管道符，直接进入第三步
    if (!has_pipe) {
        try {
            EvaluationOfRedirect(cmd_tokens);
        }
        catch (const char *s) {
            fprintf(stderr, RED "%s", s);
        }
    }
    else {
        // 生成父进程（自己）的拷贝
        if (!Global::is_backend) Global::sub_pid = fork();
        // 父进程等待子进程
        if (!Global::is_backend && Global::sub_pid) {
            while (Global::sub_pid != INVALID_PID && !waitpid(Global::sub_pid, nullptr, 0));
            Global::sub_pid = INVALID_PID;
        }
        else { // 子进程
            // 将信号处理函数恢复至系统默认
            signal(SIGINT, SIG_DFL);
            signal(SIGTSTP, SIG_DFL);

            int pipe_fd1[2]{STDIN_FILENO, -1}, pipe_fd2[2]; // 管道描述符
            vector<pid_t> pid_list; // 子进程 pid 列表

            cmd_tokens.emplace_back("|"); // 在末尾临时添加一个管道符便于判断
            int last_pipe = -1; // 标记上一个管道符的位置

            for (int i = 0; i < cmd_tokens.size(); i++) {
                if (cmd_tokens[i] == "|") {
                    if (last_pipe == -1) { // 第一个管道符
                        pipe(pipe_fd2);
                    }
                    else {
                        pipe_fd1[0] = pipe_fd2[0];
                        pipe_fd1[1] = pipe_fd2[1];
                        close(pipe_fd1[1]); // 关闭管道写端口

                        if (i == cmd_tokens.size() - 1) { // 最后一个临时管道符
                            pipe_fd2[0] = -1;
                            pipe_fd2[1] = fileno(stdout);
                        }
                        else { // 命令中间的管道符
                            pipe(pipe_fd2);
                        }
                    }

                    pid_list.push_back(fork()); // 创建执行命令的子进程

                    if (*pid_list.rbegin() == 0) {
                        // 设置信号处理函数
                        signal(SIGINT, SIG_IGN);
                        signal(SIGTSTP, SIG_DFL);

                        // 重定向命令输入到管道读端口，同时关闭写端口
                        dup2(pipe_fd1[0], STDIN_FILENO);
                        close(pipe_fd1[1]);
                        // 重定向命令输出到管道写端口，同时关闭读端口
                        dup2(pipe_fd2[1], STDOUT_FILENO);
                        close(pipe_fd2[0]);

                        try {
                            // 进入第三步分析
                            EvaluationOfRedirect(vector<string>(cmd_tokens.begin() + last_pipe + 1,
                                                                cmd_tokens.begin() + i));
                            exit(0);
                        }
                        catch (const char *s) {
                            fprintf(stderr, RED"%s", s);
                        }
                    }
                    // 更新记录上一个管道符变量位置
                    last_pipe = i;
                }
            }

            // 后一个子进程等待前一个子进程完成
            for (auto &pid: pid_list) {
                waitpid(pid, nullptr, 0);
            }
            exit(0);
        }
    }
}

void EvaluationOfRedirect(const vector<string>&cmd_token) {

    // 输入、输出、错误输出重定向的文件名
    string input, output, error;
    // 备份三个标准输入、输出、错误流
    auto old_input_fd = dup(STDIN_FILENO), old_output_fd = dup(STDOUT_FILENO), old_error_fd = dup(STDERR_FILENO);
    int input_fd, output_fd, err_fd; // 新的文件描述符
    unsigned last = cmd_token.size();
    char err[BUFFER_SIZE]{0}; // 错误信息

    // 搜索重定向符号
    for (auto pos = cmd_token.rbegin(); pos != cmd_token.rend(); pos++) {
        // 输入重定向
        if (*pos == "<" || *pos == "0<") {
            input = *(pos - 1); // 输入流文件在重定向符号'<'后
            input_fd = open(input.c_str(), O_RDONLY);
            // 打开失败
            if (input_fd == -1) {
                sprintf(err, "MyShell: cannot access %s\n", input.c_str());
                throw err;
            }
            else {
                dup2(input_fd, STDIN_FILENO);
                close(input_fd);
                last = distance(pos, cmd_token.rend()) - 1;
            }
        }
            // 输出重定向（覆盖）
        else if (*pos == ">" || *pos == "1>") {
            output = *(pos - 1);
            output_fd = open(output.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
            if (output_fd == -1) {
                sprintf(err, "MyShell: cannot access %s\n", output.c_str());
                throw err;
            }
            else {
                dup2(output_fd, STDOUT_FILENO);
                close(output_fd);
                last = distance(pos, cmd_token.rend()) - 1;
            }
        }
            // 输出重定向（追加）
        else if (*pos == ">>" || *pos == "1>>") {
            output = *(pos - 1);
            output_fd = open(output.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0666);
            if (output_fd == -1) {
                sprintf(err, "MyShell: cannot access %s\n", output.c_str());
                throw err;
            }
            else {
                dup2(output_fd, STDOUT_FILENO);
                close(output_fd);
                last = distance(pos, cmd_token.rend()) - 1;
            }
        }
            // 错误输出重定向（覆盖）
        else if (*pos == "2>") {
            error = *(pos - 1);
            err_fd = open(error.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
            if (err_fd == -1) {
                sprintf(err, "MyShell: cannot access %s\n", error.c_str());
                throw err;
            }
            else {
                dup2(err_fd, STDERR_FILENO);
                close(err_fd);
                last = distance(pos, cmd_token.rend()) - 1;
            }
        }
            // 错误输出重定向（追加）
        else if (*pos == "2>") {
            error = *(pos - 1);
            err_fd = open(error.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0666);
            if (err_fd == -1) {
                sprintf(err, "MyShell: cannot access %s\n", error.c_str());
                throw err;
            }
            else {
                dup2(err_fd, STDERR_FILENO);
                close(err_fd);
                last = distance(pos, cmd_token.rend()) - 1;
            }
        }
    }

    // 最后一步，直接执行
    Execute(vector<string>(cmd_token.begin(), cmd_token.begin() + last));

    // 恢复标准输入输出
    dup2(old_input_fd, STDIN_FILENO);
    close(old_input_fd);
    dup2(old_output_fd, STDOUT_FILENO);
    close(old_output_fd);
    dup2(old_error_fd, STDERR_FILENO);
    close(old_error_fd);
}

void Execute(const vector<string>&cmd_token) {

    /* 单条指令直接执行 */
    if (*cmd_token.begin() == "cd") {
        try {
            cd(cmd_token);
        }
        catch (const char *s) {
            fprintf(stderr, RED "%s", s);
        }
    }
    else if (*cmd_token.begin() == "clr") {
        try {
            clear(cmd_token);
        }
        catch (const char *s) {
            fprintf(stderr, RED "%s", s);
        }
    }
    else if (*cmd_token.begin() == "echo") {
        try {
            echo(cmd_token);
        }
        catch (const char *s) {
            fprintf(stderr, RED "%s", s);
        }
    }
    else if (*cmd_token.begin() == "pwd") {
        try {
            pwd(cmd_token);
        }
        catch (const char *s) {
            fprintf(stderr, RED "%s", s);
        }
    }
    else if (*cmd_token.begin() == "exit") {
        exit(cmd_token);
    }
    else if (*cmd_token.begin() == "time") {
        try {
            time(cmd_token);
        }
        catch (const char *s) {
            fprintf(stderr, RED "%s", s);
        }
    }
    else if (*cmd_token.begin() == "umask") {
        try {
            umask(cmd_token);
        }
        catch (const char *s) {
            fprintf(stderr, RED "%s", s);
        }
    }
    else if (*cmd_token.begin() == "dir") {
        try {
            dir(cmd_token);
        }
        catch (const char *s) {
            fprintf(stderr, RED "%s", s);
        }
    }
    else if (*cmd_token.begin() == "exec") {
        try {
            exec(cmd_token);
        }
        catch (const char *s) {
            fprintf(stderr, RED "%s", s);
        }
    }
    else if (*cmd_token.begin() == "set") {
        try {
            set(cmd_token);
        }
        catch (const char *s) {
            fprintf(stderr, RED "%s", s);
        }
    }
    else if (*cmd_token.begin() == "test") {
        try {
            test(cmd_token);
        }
        catch (const char *s) {
            fprintf(stderr, RED "%s", s);
        }
    }
    else if (*cmd_token.begin() == "help") {
        try {
            help(cmd_token);
        }
        catch (const char *s) {
            fprintf(stderr, RED "%s", s);
        }
    }
    else if (*cmd_token.begin() == "bg") {
        try {
            bg(cmd_token);
        }
        catch (const char *s) {
            fprintf(stderr, RED "%s", s);
        }
    }
    else if (*cmd_token.begin() == "fg") {
        try {
            fg(cmd_token);
        }
        catch (const char *s) {
            fprintf(stderr, RED "%s", s);
        }
    }
    else if (*cmd_token.begin() == "jobs") {
        try {
            jobs(cmd_token);
        }
        catch (const char *s) {
            fprintf(stderr, RED "%s", s);
        }
    }
    else {
        vector<string> modified_cmd(cmd_token);
        modified_cmd.insert(modified_cmd.begin(), "exec");
        if (!Global::is_backend) {
            Global::sub_pid = fork();
            if (Global::sub_pid == 0) {
                setenv("PARENT", Global::shell_path.c_str(), 1);
                try {
                    exec(modified_cmd);
                    exit(0);
                }
                catch (const char *s) {
                    fprintf(stderr, RED "%s", s);
                }
            }
                // 父进程等待子进程完成
            else {
                while (Global::sub_pid != INVALID_PID && !waitpid(Global::sub_pid, nullptr, WNOHANG));
                Global::sub_pid = INVALID_PID;
            }
        }
        else {
            setenv("PARENT", Global::shell_path.c_str(), 1);
            try {
                exec(modified_cmd);
            }
            catch (const char *s) {
                fprintf(stderr, RED "%s", s);
            }
        }
    }
}

/* ---------- 内建命令实现 ---------- */

void cd(const vector<string>&cmd_token) {
    // 没有显式的指明路径或者路径为"~"
    if (cmd_token.size() == 1 || (cmd_token.size() == 2 && cmd_token[1] == "~")) {
        chdir(Global::home_path.c_str());
        Global::pwd = Global::home_path;
        setenv("PWD", Global::pwd.c_str(), 1); // 更新 pwd 环境变量
    }
    else if (cmd_token.size() == 2) { // 直接调用chdir改变路径
        char buf[BUFFER_SIZE];
        // chdir 失败
        if (chdir(cmd_token[1].c_str())) {
            sprintf(buf, "cd: %s: no such file or directory\n", cmd_token[1].c_str());
            char err[BUFFER_SIZE] = {0};
            strcpy(err, buf);
            throw err;
        }
        else {
            getcwd(buf, BUFFER_SIZE);
            Global::pwd = string(buf); // 更新 pwd
            setenv("PWD", Global::pwd.c_str(), 1); // 更新 pwd 环境变量
        }
    }
        // 参数过多
    else {
        throw "cd: too many arguments\n";
    }
}

void clear(const vector<string>&cmd_token) {
    // 参数多于一个，报错
    if (cmd_token.size() > 1) {
        throw "clr: too many arguments\n";
    }
    else {
        // 直接清屏
        fprintf(stdout, CLEAR);
    }
}

void echo(const vector<string>&cmd_token) {
    // 解析给出的参数并输出
    for (int i = 1; i < cmd_token.size(); i++) {
        fprintf(stdout, "%s ", Parse2Value(cmd_token[i]).c_str());
    }
    fprintf(stdout, "%s", "\n");
}

void pwd(const vector<string>&cmd_token) {
    // 参数多于一个，报错
    if (cmd_token.size() > 1) {
        throw "pwd: too many arguments\n";
    }
    else {
        // 输出 pwd
        fprintf(stdout, WHITE"%s\n", Global::pwd.c_str());
    }
}

void exit(const vector<string>&cmd_token) {
    // 直接退出
    exit(0);
}

void time(const vector<string>&cmd_token) {
    // 参数多于一个，报错
    if (cmd_token.size() > 1) {
        throw "time: too many arguments\n";
    }
    else {
        // 定义类型为 time_t 的变量 now
        time_t now;
        struct tm *t;
        // 存放星期的信息
        static const char *day[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
        time(&now); // time() 函数返回从公元1979年1月1日的 UTC 时间从0时0分0秒起到现在经过的秒数
        t = localtime(&now); // localtime() 函数将参数所指的 time_t 结构中的信息转换成真实世界所使用的时间日期表示方法
        // 格式化输出时间
        fprintf(stdout, "%d-%d-%d %s %02d:%02d:%02d\n",
                (1900 + t->tm_year), (1 + t->tm_mon), t->tm_mday, day[t->tm_wday],
                t->tm_hour, t->tm_min, t->tm_sec);
    }
}

void umask(const vector<string>&cmd_token) {
    // 没有输入参数，显示 umask 的值
    if (cmd_token.size() == 1) {
        mode_t cur_mask = umask(0);
        umask(cur_mask);
        fprintf(stdout, "%04d\n", cur_mask);
    }
        // 输入一个参数，设置 umask 的值
    else if (cmd_token.size() == 2) {
        // 数字多于四位，报错
        if (cmd_token[1].size() > 4) {
            throw "umask: octal number out of range\n";
        }
        else {
            string octal = cmd_token[1]; // 8进制数
            // 对齐到四位
            while (octal.length() < 4) {
                octal = "0" + octal;
            }

            // 检查是否是8进制数
            for (auto &digit: octal) {
                if (!(digit >= '0' && digit <= '7')) {
                    throw "umask: octal number out of range\n";
                }
            }

            // 合法，设置 umask 的值
            unsigned new_mask = ((octal[0] - '0') << 9) | ((octal[1] - '0') << 6)
                                | ((octal[2] - '0') << 3) | (octal[3] - '0');

            umask(new_mask);
        }
    }
    else {
        throw "umask: too many arguments\n";
    }
}

void dir(const vector<string>&cmd_token) {
    DIR *dir; // DIR 类型指针
    char buf[BUFFER_SIZE]{0};

    // 用户没有输入目录参数（默认列出当前目录内容）或只输入了一个参数
    if (cmd_token.size() <= 2) {

        if (cmd_token.size() == 1) {
            getcwd(buf, BUFFER_SIZE); // 获取当前路径
        }
        else {
            strcpy(buf, cmd_token[1].c_str()); // 获取输入的一个参数
        }

        // 打开 buf 所指的目录，返回值保存到 dir 中
        // 打开失败则报错
        if (!(dir = opendir(buf))) {
            char err[2 * BUFFER_SIZE]{0};
            sprintf(err, "dir: cannot access \'%s\': no such file or directory\n", buf);
            throw err;
        }

        FormatPrintDir(dir, buf); // 格式化输出目录内容
        closedir(dir);
    }
        // 输入了多个目录
    else {
        for (int i = 1; i < cmd_token.size(); i++) {
            strcpy(buf, cmd_token[i].c_str());

            if (!(dir = opendir(buf))) {
                char err[2 * BUFFER_SIZE]{0};
                sprintf(err, "dir: cannot access \'%s\': no such file or directory\n", buf);
                throw err;
            }

            fprintf(stdout, WHITE"%s: \n", buf); // 输出待列出的目录
            FormatPrintDir(dir, buf);
            fprintf(stdout, "\n");
            closedir(dir);
        }
    }
}

void exec(const vector<string>&cmd_token) {
    // 没有给出参数，报错
    if (cmd_token.size() == 1) {
        throw "exec: lack of parameter\n";
    }
    else {
        // exec 所需参数
        char *args[cmd_token.size()];
        for (int i = 1; i < cmd_token.size(); i++) {
            args[i - 1] = const_cast<char *>(cmd_token[i].c_str());
        }
        args[cmd_token.size() - 1] = nullptr;
        execvp(cmd_token[1].c_str(), args);

        // execvp 执行成功后会退出源程序，如果执行到这里说明执行出错
        throw "exec: cannot find the command\n";
    }
}

void set(const vector<string>&cmd_token) {
    // 没有输入变量，显示当前所有环境变量
    if (cmd_token.size() == 1) {
        extern char **environ; // 环境变量表，系统预定义
        for (int i = 0; environ[i] != nullptr; i++) {
            fprintf(stdout, "%s\n", environ[i]);
        }
    }
        // 正确输入变量名以及值
    else if (cmd_token.size() == 3) {
        // 环境变量不存在
        string var = Parse2Value(cmd_token[1]);
        if (!getenv(var.c_str())) {
            char err[BUFFER_SIZE]{0};
            sprintf(err, "set: cannot access `%s`: no such environment variable\n", cmd_token[1].c_str());
            throw err;
        }
        else {
            // 设置环境变量的值
            setenv(var.c_str(), Parse2Value(cmd_token[2]).c_str(), 1);
        }
    }
        // 参数数量不正确
    else {
        throw "set: unexpected parameters\n";
    }
}

void test(const vector<string>&cmd_token) {
    if (cmd_token.size() <= 2) {
        throw "test: lack of parameter\n";
    }
        // 一元运算符
    else if (cmd_token.size() == 3) {
        const string &option = cmd_token[1]; // 选项
        string val = Parse2Value(cmd_token[2]); // 文件名、字符串或是$开头的变量

        // 文件是否存在
        if (option == "-e") {
            if (access(val.c_str(), F_OK) == 0) {
                fprintf(stdout, "true\n");
            }
            else {
                fprintf(stdout, "false\n");
            }
        }
            // 文件存在且是否可读
        else if (option == "-r") {
            if (access(val.c_str(), R_OK) == 0) {
                fprintf(stdout, "true\n");
            }
            else {
                fprintf(stdout, "false\n");
            }
        }
            // 文件存在且是否可写
        else if (option == "-w") {
            if (access(val.c_str(), W_OK) == 0) {
                fprintf(stdout, "true\n");
            }
            else {
                fprintf(stdout, "false\n");
            }
        }
            // 文件存在且是否可执行
        else if (option == "-x") {
            if (access(val.c_str(), X_OK) == 0) {
                fprintf(stdout, "true\n");
            }
            else {
                fprintf(stdout, "false\n");
            }
        }
            // 文件存在且不为空
        else if (option == "-s") {
            struct stat file_info{};
            stat(val.c_str(), &file_info);
            if (file_info.st_size) {
                fprintf(stdout, "true\n");
            }
            else {
                fprintf(stdout, "false\n");
            }
        }
            // 文件存在且为目录
        else if (option == "-d") {
            struct stat file_info{};
            stat(val.c_str(), &file_info);
            if (S_ISDIR(file_info.st_mode)) {
                fprintf(stdout, "true\n");
            }
            else {
                fprintf(stdout, "false\n");
            }
        }
            // 文件存在且为普通文件
        else if (option == "-f") {
            struct stat file_info{};
            stat(val.c_str(), &file_info);
            if (S_ISREG(file_info.st_mode)) {
                fprintf(stdout, "true\n");
            }
            else {
                fprintf(stdout, "false\n");
            }
        }
            // 文件存在且为字符型特殊文件
        else if (option == "-c") {
            struct stat file_info{};
            stat(val.c_str(), &file_info);
            if (S_ISCHR(file_info.st_mode)) {
                fprintf(stdout, "true\n");
            }
            else {
                fprintf(stdout, "false\n");
            }
        }
            // 文件存在且为块特殊文件
        else if (option == "-b") {
            struct stat file_info{};
            stat(val.c_str(), &file_info);
            if (S_ISBLK(file_info.st_mode)) {
                fprintf(stdout, "true\n");
            }
            else {
                fprintf(stdout, "false\n");
            }
        }
            // 文件存在且为符号链接
        else if (option == "-h" || option == "-L") {
            struct stat file_info{};
            stat(val.c_str(), &file_info);
            if (S_ISLNK(file_info.st_mode)) {
                fprintf(stdout, "true\n");
            }
            else {
                fprintf(stdout, "false\n");
            }
        }
            // 文件存在且为命名管道
        else if (option == "-p") {
            struct stat file_info{};
            stat(val.c_str(), &file_info);
            if (S_ISFIFO(file_info.st_mode)) {
                fprintf(stdout, "true\n");
            }
            else {
                fprintf(stdout, "false\n");
            }
        }
            // 文件存在且为嵌套字
        else if (option == "-S") {
            struct stat file_info{};
            stat(val.c_str(), &file_info);
            if (S_ISSOCK(file_info.st_mode)) {
                fprintf(stdout, "true\n");
            }
            else {
                fprintf(stdout, "false\n");
            }
        }
            // 字符串长度不为0
        else if (option == "-n") {
            if (!val.empty()) {
                fprintf(stdout, "true\n");
            }
            else {
                fprintf(stdout, "false\n");
            }
        }
            // 字符串长度为0
        else if (option == "-z") {
            if (val.empty()) {
                fprintf(stdout, "true\n");
            }
            else {
                fprintf(stdout, "false\n");
            }
        }
        else {
            // 不支持的选项报错
            char err[BUFFER_SIZE]{0};
            sprintf(err, "test: %s: invalid option\n", option.c_str());
            throw (const char *) err;
        }
    }
        // 二元运算符
    else if (cmd_token.size() == 4) {
        const string &option = cmd_token[2];
        string val1 = Parse2Value(cmd_token[1]), val2 = Parse2Value(cmd_token[3]);

        // 字符串相等
        if (option == "=") {
            if (val1 == val2) {
                fprintf(stdout, "true\n");
            }
            else {
                fprintf(stdout, "false\n");
            }
        }
            // 字符串不等
        else if (option == "!=") {
            if (val1 != val2) {
                fprintf(stdout, "true\n");
            }
            else {
                fprintf(stdout, "false\n");
            }
        }
            // 整数相等
        else if (option == "-eq") {
            if (strtol(val1.c_str(), nullptr, 10) == strtol(val2.c_str(), nullptr, 10)) {
                fprintf(stdout, "true\n");
            }
            else {
                fprintf(stdout, "false\n");
            }
        }
            // 整数大于等于
        else if (option == "-ge") {
            if (strtol(val1.c_str(), nullptr, 10) >= strtol(val2.c_str(), nullptr, 10)) {
                fprintf(stdout, "true\n");
            }
            else {
                fprintf(stdout, "false\n");
            }
        }
            // 整数大于
        else if (option == "-gt") {
            if (strtol(val1.c_str(), nullptr, 10) > strtol(val2.c_str(), nullptr, 10)) {
                fprintf(stdout, "true\n");
            }
            else {
                fprintf(stdout, "false\n");
            }
        }
            // 整数小于等于
        else if (option == "-le") {
            if (strtol(val1.c_str(), nullptr, 10) <= strtol(val2.c_str(), nullptr, 10)) {
                fprintf(stdout, "true\n");
            }
            else {
                fprintf(stdout, "false\n");
            }
        }
            // 整数小于
        else if (option == "-lt") {
            if (strtol(val1.c_str(), nullptr, 10) < strtol(val2.c_str(), nullptr, 10)) {
                fprintf(stdout, "true\n");
            }
            else {
                fprintf(stdout, "false\n");
            }
        }
            // 整数不等于
        else if (option == "-ne") {
            if (strtol(val1.c_str(), nullptr, 10) != strtol(val2.c_str(), nullptr, 10)) {
                fprintf(stdout, "true\n");
            }
            else {
                fprintf(stdout, "false\n");
            }
        }
        else {
            // 不支持的选项报错
            char err[BUFFER_SIZE]{0};
            sprintf(err, "test: %s: invalid option\n", option.c_str());
            throw (const char *) err;
        }
    }
        // 参数过多
    else {
        throw "test: too many arguments\n";
    }
}

void help(const vector<string>&cmd_token) {

    if (cmd_token.size() <= 2) {
        fstream fp(Global::manual_path); // 初始化

        if (!fp.is_open()) {
            throw "help: cannot access manual\n";
        }
        else {
            // 没有参数，显示全局帮助手册
            // 有参数，显示对应指令的帮助手册
            string item, line, target;
            // 寻找的命令名字
            target = (cmd_token.size() == 1) ? "* manual *" : cmd_token[1];
            while (!fp.eof()) {
                getline(fp, line);
                if (!item.empty()) {
                    // 指令帮助内容已经打印完
                    if (*line.begin() == '*') {
                        break;
                    }
                        // 打印内容
                    else {
                        fprintf(stdout, WHITE"%s\n", line.c_str());
                    }
                }
                // 找到对应的命令帮助手册
                if (*line.begin() == '*' && line.find(target) != string::npos) {
                    item = line;
                    fprintf(stdout, WHITE"%s\n", item.c_str());
                }
            }
        }
        fp.close();
    }
        // 参数过多
    else {
        throw "help: too many arguments\n";
    }
}

void bg(const vector<string>&cmd_token) {
    // 没有给出参数，输出后台作业表的所有信息
    if (cmd_token.size() == 1) {
        // 没有后台进程
        if (Global::jobs.empty()) {
            throw "bg: current: no such job\n";
        }
            // 提示目前后台进程的数量
        else {
            char msg[BUFFER_SIZE]{0};
            sprintf(msg, "bg: job %lu already in background\n", Global::jobs.size());
            fprintf(stdout, WHITE"%s", msg);
        }
    }

    else if (cmd_token.size() == 2) {
        auto id = strtol(cmd_token[1].c_str(), nullptr, 10);
        // 没有找到该作业号
        if (Global::jobs.find(id) == Global::jobs.end()) {
            char err[BUFFER_SIZE]{0};
            sprintf(err, "bg: %ld: no such job\n", id);
            throw err;
        }
        // 已经是后台进程
        else if (Global::state[id] == Global::BACKEND) {
            char err[BUFFER_SIZE]{0};
            sprintf(err, "bg: %ld: already at backend\n", id);
            throw err;
        }
        else {
            // 改变该进程的状态为 BACKEND
            Global::state.insert(pair<pid_t, Global::JobStatus>(id, Global::BACKEND));
            // 向该进程发送SIGCONT信号，使其继续运行
            kill(id, SIGCONT);
        }
    }
        // 参数过多
    else {
        throw "bg: too many arguments\n";
    }
}

void fg(const vector<string>&cmd_token) {
    if (cmd_token.size() == 1) {
        // 没有后台进程
        if (Global::jobs.empty()) {
            throw "fg: current: no such job\n";
        }
        else {
            throw "fg: lack of parameter\n";
        }
    }

    else if (cmd_token.size() == 2) {
        auto id = strtol(cmd_token[1].c_str(), nullptr, 10);
        // 没有找到该作业号
        if (Global::jobs.find(id) == Global::jobs.end()) {
            char err[BUFFER_SIZE]{0};
            sprintf(err, "fg: %ld: no such job\n", id);
            throw err;
        }
        else {
            // 更新当前指令和指令 token
            Global::command = Global::sub_commands[id];
            Global::command_tokens = SpiltCommand(Global::command);

            // 更新作业表
            Global::state.erase(id);
            Global::sub_pid = id;
            Global::jobs.erase(id);

            setpgid(Global::sub_pid, getpid()); // 设置进程组，使子进程进入前台进程组
            kill(Global::sub_pid, SIGCONT);

            // 阻塞主进程，等待子进程完成
            while (Global::sub_pid != -1 && !waitpid(Global::sub_pid, nullptr, 0));
            Global::sub_pid = -1;
        }
    }
    else {
        throw "fg: too many arguments\n";
    }
}

void jobs(const vector<string>&cmd_token) {
    // 输出作业表
    if (cmd_token.size() == 1) {
        for (auto &job: Global::jobs) {
            fprintf(stdout, "%s", FormatJobMsg(job.first, false).c_str());
        }
    }
    else {
        throw "jobs: too many arguments\n";
    }
}