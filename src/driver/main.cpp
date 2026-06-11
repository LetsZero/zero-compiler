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
#include <sstream>

namespace {

// Render an F32 tensor for `print`. 1-D -> [a, b, c]; 2-D -> [[a, b], [c, d]];
// anything else falls back to a flat list plus a shape suffix. Without this,
// `print(some_tensor)` produced nothing — you could not observe the one thing
// the language exists to compute from the CLI.
std::string format_tensor(const zero::Tensor& t) {
    std::ostringstream os;
    const float* d = static_cast<const float*>(t.data);
    if (d == nullptr) return "<tensor: null data>";

    if (t.ndim == 1) {
        os << "[";
        for (int64_t i = 0; i < t.shape[0]; ++i) os << (i ? ", " : "") << d[i];
        os << "]";
    } else if (t.ndim == 2) {
        os << "[";
        for (int64_t r = 0; r < t.shape[0]; ++r) {
            os << (r ? ", " : "") << "[";
            for (int64_t c = 0; c < t.shape[1]; ++c)
                os << (c ? ", " : "") << d[r * t.shape[1] + c];
            os << "]";
        }
        os << "]";
    } else {
        // Rank-0 or rank>2: flat values + shape tag.
        os << "[";
        for (int64_t i = 0; i < t.numel(); ++i) os << (i ? ", " : "") << d[i];
        os << "] (shape ";
        for (int8_t i = 0; i < t.ndim; ++i) os << (i ? "x" : "") << t.shape[i];
        os << ")";
    }
    return os.str();
}

// Render any RuntimeValue (recurses into struct fields).
std::string format_value(const zero::backend::RuntimeValue& v) {
    if (v.is_int())    return std::to_string(v.as_int());
    if (v.is_float())  { std::ostringstream os; os << v.as_float(); return os.str(); }
    if (v.is_str())    return v.as_str();
    if (v.is_tensor()) return format_tensor(*v.as_tensor());
    if (v.is_struct()) {
        std::ostringstream os;
        os << "{ ";
        const auto& sv = *v.as_struct();
        for (size_t i = 0; i < sv.fields.size(); ++i) {
            os << (i ? ", " : "") << format_value(sv.fields[i]);
        }
        os << " }";
        return os.str();
    }
    return "";
}

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
        for (const auto& err : parser.errors()) {
            auto [line, col] = sm.get_line_col(err.span);
            std::cerr << "\033[31merror:\033[0m " << err.message
                      << " (line " << line << ", col " << col << ")\n";
        }
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
            std::cout << format_value(arg);
        }
        std::cout << "\n";
        return backend::RuntimeValue{};
    });
    
    // Register log function (with color support)
    interp.register_external("log", [](const std::vector<backend::RuntimeValue>& args) {
        // Find message and color arguments
        std::string message;
        std::string color;
        
        for (const auto& arg : args) {
            if (arg.is_str()) {
                if (message.empty()) {
                    message = arg.as_str();
                } else {
                    color = arg.as_str();
                }
            }
        }
        
        // ANSI color codes
        std::string ansi_code = "\033[0m"; // default/reset
        if (color == "red") ansi_code = "\033[31m";
        else if (color == "green") ansi_code = "\033[32m";
        else if (color == "yellow") ansi_code = "\033[33m";
        else if (color == "blue") ansi_code = "\033[34m";
        else if (color == "magenta") ansi_code = "\033[35m";
        else if (color == "cyan") ansi_code = "\033[36m";
        
        std::cout << ansi_code << message << "\033[0m\n";
        return backend::RuntimeValue{};
    });
    
    // Spec 010: give the interpreter the source manager so runtime errors
    // render as Frame & Focus diagnostics with source-line context.
    interp.set_source_manager(&sm);

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
