#include "tooling/lsp/server.hpp"

#include <iostream>

int main() {
    ahfl::lsp::LspServer server(std::cin, std::cout);
    server.run();
    return 0;
}
