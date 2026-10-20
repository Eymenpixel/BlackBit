#include <array>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
static fs::path data_dir() {
    const char *p = std::getenv("KAYA_DATA_DIR");
    return p && *p ? fs::path(p) : fs::path(".nexus-data");
}
static void help() {
    std::cout << "Nexus portable bytecode VM\n"
                 "nexus --init <name>   create a sample .nvm program\n"
                 "nexus --run <name>    execute <name>.nvm from private app data\n"
                 "nexus --help\n"
                 "Instructions: SET ADD SUB MUL DIV PRINT JMP JZ HALT\n";
}
static bool safe_name(const std::string &s) {
    if (s.empty() || s.size() > 48) return false;
    for (unsigned char c : s) if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                                    (c >= '0' && c <= '9') || c == '-' || c == '_')) return false;
    return true;
}
static int run_vm(const fs::path &path) {
    std::ifstream file(path);
    if (!file) { std::cerr << "Cannot open VM program: " << path.filename().string() << '\n'; return 1; }
    std::vector<std::vector<std::string>> code;
    std::string line;
    while (std::getline(file, line)) {
        auto hash = line.find('#'); if (hash != std::string::npos) line.resize(hash);
        std::istringstream in(line); std::vector<std::string> words; std::string w;
        while (in >> w) words.push_back(w);
        if (!words.empty()) code.push_back(std::move(words));
        if (code.size() > 4096) { std::cerr << "Program exceeds 4096 instructions.\n"; return 2; }
    }
    std::array<int64_t,8> r{}; size_t pc = 0; unsigned steps = 0;
    auto reg = [&](const std::string &s, int64_t &v) {
        if (s.size() == 2 && s[0] == 'r' && s[1] >= '0' && s[1] <= '7') { v = r[s[1]-'0']; return true; }
        try { size_t n; v = std::stoll(s, &n); return n == s.size(); } catch (...) { return false; }
    };
    while (pc < code.size() && steps++ < 100000) {
        const auto &a = code[pc]; const std::string &op = a[0];
        if (op == "HALT") break;
        if (op == "SET" && a.size() == 3 && a[1].size() == 2 && a[1][0] == 'r' && a[1][1] >= '0' && a[1][1] <= '7') {
            int64_t v; if (!reg(a[2],v)) return 2; r[a[1][1]-'0'] = v;
        } else if ((op == "ADD" || op == "SUB" || op == "MUL" || op == "DIV") && a.size() == 3 && a[1].size() == 2 && a[1][0] == 'r' && a[1][1] >= '0' && a[1][1] <= '7') {
            int64_t v; if (!reg(a[2],v)) return 2; auto &dst = r[a[1][1]-'0'];
            int64_t result = 0;
            if (op == "ADD") {
                if (__builtin_add_overflow(dst, v, &result)) { std::cerr << "VM integer overflow.\n"; return 2; }
            } else if (op == "SUB") {
                if (__builtin_sub_overflow(dst, v, &result)) { std::cerr << "VM integer overflow.\n"; return 2; }
            } else if (op == "MUL") {
                if (__builtin_mul_overflow(dst, v, &result)) { std::cerr << "VM integer overflow.\n"; return 2; }
            } else {
                if (!v) { std::cerr << "VM division by zero.\n"; return 2; }
                if (dst == INT64_MIN && v == -1) { std::cerr << "VM integer overflow.\n"; return 2; }
                result = dst / v;
            }
            dst = result;
        } else if (op == "PRINT" && a.size() == 2) {
            int64_t v; if (!reg(a[1],v)) return 2; std::cout << v << '\n';
        } else if (op == "JMP" && a.size() == 2) {
            size_t dest; try { dest = std::stoul(a[1]); } catch (...) { return 2; }
            if (dest >= code.size()) return 2;
            pc = dest;
            continue;
        } else if (op == "JZ" && a.size() == 3) {
            int64_t v; if (!reg(a[1],v)) return 2;
            if (!v) { size_t dest; try { dest = std::stoul(a[2]); } catch (...) { return 2; } if (dest >= code.size()) return 2; pc = dest; continue; }
        } else { std::cerr << "Invalid instruction at " << pc << ": " << op << '\n'; return 2; }
        ++pc;
    }
    if (steps >= 100000) { std::cerr << "VM instruction limit reached.\n"; return 3; }
    return 0;
}
int main(int argc, char **argv) {
    if (argc == 1 || std::string(argv[1]) == "--help") { help(); return 0; }
    std::string cmd = argv[1];
    if ((cmd == "--init" || cmd == "--run") && argc == 3 && safe_name(argv[2])) {
        fs::path root = data_dir();
        if (cmd == "--init") {
            std::error_code ec; fs::create_directories(root, ec); if (ec) return 1;
            fs::path p = root / (std::string(argv[2]) + ".nvm");
            if (fs::exists(p)) { std::cerr << "VM program already exists.\n"; return 1; }
            std::ofstream f(p);
            f << "# Nexus VM example: calculate 6 * 7\nSET r0 6\nMUL r0 7\nPRINT r0\nHALT\n";
            if (!f) return 1;
            std::cout << "Created VM program " << p.filename().string() << '\n'; return 0;
        }
        return run_vm(root / (std::string(argv[2]) + ".nvm"));
    }
    help(); return 2;
}
