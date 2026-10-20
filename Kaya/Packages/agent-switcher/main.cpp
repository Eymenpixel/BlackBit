#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
static fs::path data_dir() {
    const char *p = std::getenv("KAYA_DATA_DIR");
    return p && *p ? fs::path(p) : fs::path(".agent-switcher-data");
}
static const std::vector<std::string> backends = {"forge", "apt", "dnf", "pacman"};
static std::string resolve(std::string name) { return name == "dpkg" ? "apt" : name; }
static bool known(std::string n) {
    n = resolve(n);
    for (auto &b : backends) if (b == n) return true;
    return false;
}
static bool select_backend(const std::string &backend) {
    std::error_code ec; fs::create_directories(data_dir(), ec);
    if (ec) { std::cerr << "Cannot access private app data.\n"; return false; }
    std::ofstream f(data_dir() / "selected-backend", std::ios::trunc);
    f << backend << '\n';
    return static_cast<bool>(f);
}
static void help() {
    std::cout << "agent-switcher --list\n"
                 "agent-switcher --use <forge|apt|dnf|pacman>\n"
                 "agent-switcher --install <manager>\n"
                 "dpkg is an alias for the APT backend.\n";
}
int main(int argc, char **argv) {
    if (argc == 1 || std::string(argv[1]) == "--help") { help(); return 0; }
    std::string cmd = argv[1];
    if (cmd == "--list") {
        std::ifstream f(data_dir() / "selected-backend");
        std::string selected; f >> selected;
        std::cout << "Available adapters:\n  forge (Blackbit built-in)\n  apt (Debian package manager)\n"
                     "  dnf (Fedora package manager)\n  pacman (Arch package manager)\n"
                     "  dpkg -> apt alias\n";
        if (!selected.empty()) std::cout << "Selected: " << selected << '\n';
        return 0;
    }
    if ((cmd == "--use" || cmd == "--install") && argc == 3) {
        std::string requested = argv[2], backend = resolve(requested);
        if (!known(requested)) { std::cerr << "Unsupported backend: " << requested << '\n'; help(); return 2; }
        if (cmd == "--use") {
            if (!select_backend(backend)) return 1;
            std::cout << "Selected " << backend << ".\n"; return 0;
        }
        if (backend == "forge") {
            std::cout << "forge is built into Blackbit; use: forge install <package>\n"; return 0;
        }
        std::cout << "Install request: " << requested << " maps to " << backend << ".\n"
                     "Kaya will run: forge install " << backend << "\nContinue? [y/N] ";
        std::string answer; std::getline(std::cin, answer);
        if (answer != "y" && answer != "Y") { std::cout << "Cancelled.\n"; return 0; }
        // backend is selected from the fixed allowlist above, so no user text
        // is interpolated into the command.
        const std::string command = "forge install " + backend;
        if (std::system(command.c_str()) != 0) {
            std::cerr << "Adapter installation failed.\n"; return 1;
        }
        if (!select_backend(backend)) return 1;
        std::cout << "Installed and selected " << backend << ".\n";
        return 0;
    }
    help(); return 2;
}
