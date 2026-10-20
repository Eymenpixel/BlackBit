#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace fs = std::filesystem;
struct Theme { std::string background = "000000", text = "FFFFFF"; };

static fs::path data_dir() {
    const char *p = std::getenv("KAYA_DATA_DIR");
    return p && *p ? fs::path(p) : fs::path(".small-creatures-data");
}
static bool valid_color(std::string s) {
    if (s.size() == 7 && s[0] == '#') s.erase(0, 1);
    if (s.size() != 6) return false;
    for (unsigned char c : s) if (!std::isxdigit(c)) return false;
    return true;
}
static std::string normalize(std::string s) {
    if (!s.empty() && s[0] == '#') s.erase(0, 1);
    for (char &c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}
static fs::path current_path() { return data_dir() / "current.theme"; }
static Theme load_theme() {
    Theme t;
    std::ifstream f(current_path());
    std::string b, x;
    if (f >> b >> x && valid_color(b) && valid_color(x)) { t.background = normalize(b); t.text = normalize(x); }
    return t;
}
static bool save_theme(const fs::path &p, const Theme &t) {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    if (ec) return false;
    std::ofstream f(p, std::ios::trunc);
    f << t.background << ' ' << t.text << '\n';
    return static_cast<bool>(f);
}
static void apply_theme(const Theme &t, bool reset = false) {
    if (reset) { std::cout << "\033[0m"; return; }
    std::cout << "\033[48;2;" << std::stoi(t.background.substr(0,2),nullptr,16) << ';'
              << std::stoi(t.background.substr(2,2),nullptr,16) << ';' << std::stoi(t.background.substr(4,2),nullptr,16)
              << "m\033[38;2;" << std::stoi(t.text.substr(0,2),nullptr,16) << ';'
              << std::stoi(t.text.substr(2,2),nullptr,16) << ';' << std::stoi(t.text.substr(4,2),nullptr,16) << 'm';
}
static void help() {
    std::cout << "small-creatures --customize <bg HEX|E> <text HEX|E>\n"
                 "small-creatures --save <number>\n"
                 "small-creatures --load <number>\n"
                 "small-creatures --reset\n"
                 "small-creatures --help | --about\n";
}
static bool slot_number(const std::string &n) {
    return !n.empty() && n.size() <= 4 && n != "0" &&
           n.find_first_not_of("0123456789") == std::string::npos;
}
static int save_or_load(bool save, const std::string &n) {
    if (!slot_number(n)) { std::cerr << "Save number must be 1..9999.\n"; return 2; }
    fs::path slot = data_dir() / ("save-" + n + ".theme");
    if (save) {
        if (!save_theme(slot, load_theme())) { std::cerr << "Could not save theme.\n"; return 1; }
        std::cout << "Saved theme " << n << ".\n"; return 0;
    }
    std::ifstream f(slot); Theme t;
    if (!(f >> t.background >> t.text) || !valid_color(t.background) || !valid_color(t.text)) {
        std::cerr << "No valid saved theme " << n << ".\n"; return 1;
    }
    t.background = normalize(t.background); t.text = normalize(t.text);
    if (!save_theme(current_path(), t)) return 1;
    apply_theme(t); std::cout << "Loaded theme " << n << ".\n"; return 0;
}
static int menu() {
    std::cout << "\nCustomize               Customize your CLI\n"
                 "Saves                    Save or load themes\n"
                 "Reset                    Go back to the original default\n"
                 "Help                     Learn the commands/arguments\n"
                 "About                    About small-creatures\n\nChoice: ";
    std::string choice; if (!std::getline(std::cin, choice)) return 0;
    if (choice == "1" || choice == "Customize") {
        std::string bg, fg; std::cout << "Background HEX (E keeps current): "; std::getline(std::cin, bg);
        std::cout << "Text HEX (E keeps current): "; std::getline(std::cin, fg);
        Theme t = load_theme();
        if (bg != "E" && !valid_color(bg)) { std::cerr << "Invalid background HEX color.\n"; return 2; }
        if (fg != "E" && !valid_color(fg)) { std::cerr << "Invalid text HEX color.\n"; return 2; }
        if (bg != "E") t.background = normalize(bg);
        if (fg != "E") t.text = normalize(fg);
        if (!save_theme(current_path(), t)) return 1;
        apply_theme(t); std::cout << "Theme applied.\n"; return 0;
    }
    if (choice == "2" || choice == "Saves") {
        std::string action, n; std::cout << "Save or load? [s/l]: "; std::getline(std::cin, action);
        std::cout << "Save number: "; std::getline(std::cin, n);
        return save_or_load(action == "s" || action == "S", n);
    }
    if (choice == "3" || choice == "Reset") {
        Theme t; if (!save_theme(current_path(), t)) return 1;
        apply_theme(t, true); std::cout << "Default theme restored.\n"; return 0;
    }
    if (choice == "4" || choice == "Help") help();
    else if (choice == "5" || choice == "About") std::cout << "small-creatures: private terminal theme profiles for Kaya.\n";
    else std::cout << "Choose 1-5.\n";
    return 0;
}
int main(int argc, char **argv) {
    if (argc == 1) return menu();
    const std::string cmd = argv[1];
    if (cmd == "--help" || cmd == "help") { help(); return 0; }
    if (cmd == "--about") { std::cout << "small-creatures 1.0.0\n"; return 0; }
    if (cmd == "--reset") { Theme t; if (!save_theme(current_path(), t)) return 1; apply_theme(t, true); std::cout << "Theme reset.\n"; return 0; }
    if ((cmd == "--save" || cmd == "--load") && argc == 3) {
        return save_or_load(cmd == "--save", argv[2]);
    }
    if (cmd == "--customize" && argc == 4) {
        Theme t = load_theme();
        std::string bg = argv[2], fg = argv[3];
        if (bg != "E" && !valid_color(bg)) { std::cerr << "Invalid background HEX color.\n"; return 2; }
        if (fg != "E" && !valid_color(fg)) { std::cerr << "Invalid text HEX color.\n"; return 2; }
        if (bg != "E") t.background = normalize(bg);
        if (fg != "E") t.text = normalize(fg);
        if (!save_theme(current_path(), t)) { std::cerr << "Could not save theme.\n"; return 1; }
        std::cout << "Theme saved: background #" << t.background << ", text #" << t.text << "\n";
        apply_theme(t); std::cout << " Kaya terminal theme applied.\n";
        return 0;
    }
    help(); return 2;
}
