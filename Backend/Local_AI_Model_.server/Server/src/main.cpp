#include <cstdlib>
#include <iostream>
#include <string_view>

namespace
{
void print_usage(std::string_view executable)
{
    std::cout << "Server Application\n"
              << "Usage: " << executable << " [options]\n\n"
              << "Options:\n"
              << "  -h, --help    Show this help message and exit\n";
}
}

int main(int argc, char** argv)
{
    const std::string_view executable = argc > 0 ? argv[0] : "server_app";

    if (argc <= 1)
    {
        print_usage(executable);
        return EXIT_SUCCESS;
    }

    const std::string_view first_arg{argv[1]};
    if (first_arg == "-h" || first_arg == "--help")
    {
        print_usage(executable);
        return EXIT_SUCCESS;
    }

    std::cerr << "Unknown option: " << first_arg << "\n";
    print_usage(executable);
    return EXIT_SUCCESS;
}
