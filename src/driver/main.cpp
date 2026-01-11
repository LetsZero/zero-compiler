/**
 * @file main.cpp
 * @brief Zero Compiler — CLI Driver
 * 
 * Usage:
 *   zeroc <file.zero>           Compile and run
 *   zeroc --dump-ir <file.zero> Dump IR
 *   zeroc --help                Show help
 */

#include "source/source.hpp"
#include "lexer/lexer.hpp"
#include "parser/parser.hpp"
#include "sema/sema.hpp"
#include "ir/ir.hpp"
#include "ir/lowering.hpp"
#include "backend/interpreter.hpp"

#include <iostream>
#include <string>
#include <vector>
#include <fstream>

namespace {

void print_help() {
    std::cout << "Zero Compiler v0.1.0 (MPP)\n\n";
    std::cout << "Usage:\n";
    std::cout << "  zeroc <file.zero>           Compile and execute\n";
    std::cout << "  zeroc --dump-ir <file.zero> Dump IR\n";
    std::cout << "  zeroc --dump-ast <file.zero> Dump AST (placeholder)\n";
    std::cout << "  zeroc --help                Show this help\n";
    std::cout << "  zeroc --version             Show version\n";
}

void print_version() {
    std::cout << "zeroc 0.1.0 (Minimal Public Prototype)\n";
}

void print_error(const std::string& msg) {
    std::cerr << "\033[31merror:\033[0m " << msg << "\n";
}

bool file_exists(const std::string& path) {
    std::ifstream f(path);
    return f.good();
}

int compile_and_run(const std::string& filename, bool dump_ir) {
    using namespace zero;
    
    // ─────────────────────────────────────────────────────────────────────
    // 1. Load source
    // ─────────────────────────────────────────────────────────────────────
    source::SourceManager sm;
    source::SourceID src_id = sm.load(filename);
    
    if (src_id == source::INVALID_SOURCE_ID) {
        print_error("Failed to load file: " + filename);
        return 1;
    }
    
    // ─────────────────────────────────────────────────────────────────────
    // 2. Parse
    // ─────────────────────────────────────────────────────────────────────
    parser::Parser parser(sm, src_id);
    ast::Program prog = parser.parse();
    
    if (parser.had_error()) {
        print_error("Parse errors occurred");
        return 1;
    }
    
    // ─────────────────────────────────────────────────────────────────────
    // 3. Semantic analysis
    // ─────────────────────────────────────────────────────────────────────
    sema::Sema sema;
    sema.analyze(prog);
    
    if (sema.had_error()) {
        for (const auto& err : sema.errors()) {
            print_error(err.message);
        }
        return 1;
    }
    
    // ─────────────────────────────────────────────────────────────────────
    // 4. Lower to IR
    // ─────────────────────────────────────────────────────────────────────
    ir::Lowering lowering;
    ir::Module mod = lowering.lower(prog);
    
    // ─────────────────────────────────────────────────────────────────────
    // 5. Dump IR if requested
    // ─────────────────────────────────────────────────────────────────────
    if (dump_ir) {
        std::cout << ir::print_module(mod);
        return 0;
    }
    
    // ─────────────────────────────────────────────────────────────────────
    // 6. Execute
    // ─────────────────────────────────────────────────────────────────────
    backend::Interpreter interp;
    
    // Register print function
    interp.register_external("print", [](const std::vector<backend::RuntimeValue>& args) {
        for (const auto& arg : args) {
            if (arg.is_int()) {
                std::cout << arg.as_int();
            } else if (arg.is_float()) {
                std::cout << arg.as_float();
            }
        }
        std::cout << "\n";
        return backend::RuntimeValue{};
    });
    
    try {
        interp.execute(mod, "main");
        return interp.exit_code();
    } catch (const std::exception& e) {
        print_error(e.what());
        return 1;
    }
}

} // anonymous namespace

int main(int argc, char* argv[]) {
    std::vector<std::string> args(argv + 1, argv + argc);
    
    if (args.empty()) {
        print_help();
        return 0;
    }
    
    // Parse arguments
    std::string filename;
    bool dump_ir = false;
    
    for (size_t i = 0; i < args.size(); ++i) {
        const std::string& arg = args[i];
        
        if (arg == "--help" || arg == "-h") {
            print_help();
            return 0;
        }
        
        if (arg == "--version" || arg == "-v") {
            print_version();
            return 0;
        }
        
        if (arg == "--dump-ir") {
            dump_ir = true;
            continue;
        }
        
        if (arg == "--dump-ast") {
            // TODO: Implement AST dump
            std::cout << "AST dump not yet implemented\n";
            return 0;
        }
        
        if (arg[0] == '-') {
            print_error("Unknown option: " + arg);
            return 1;
        }
        
        filename = arg;
    }
    
    if (filename.empty()) {
        print_error("No input file specified");
        return 1;
    }
    
    if (!file_exists(filename)) {
        print_error("File not found: " + filename);
        return 1;
    }
    
    return compile_and_run(filename, dump_ir);
}
