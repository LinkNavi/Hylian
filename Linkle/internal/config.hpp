#ifndef HYLIAN_STD_LINKLE_CONFIG_HPP
#define HYLIAN_STD_LINKLE_CONFIG_HPP

#include <string>
#include <vector>
#include <unordered_map>
#include <fstream>
#include <sstream>
#include <cctype>
#include "../../runtime/std/errors.hpp"

// ─── Data structures ──────────────────────────────────────────────────────────

struct LinkleProject {
    std::string name;
    std::string version;
    std::string author;
};

struct LinkleBuild {
    std::string src    = "src";
    std::string main   = "main";
    std::string out    = "build";
    std::string bin;
    std::string std_ver = "c++17";
    std::vector<std::string> flags;
    std::vector<std::string> libs;
};

struct LinkleTarget {
    std::string name;
    std::vector<std::string> commands; // each exec("...") arg in order
};

struct LinkleConfig {
    LinkleProject project;
    LinkleBuild   build;
    std::vector<LinkleTarget> targets;

    // Returns nullptr if target not found
    const LinkleTarget* get_target_ptr(const std::string& name) const {
        for (auto& t : targets)
            if (t.name == name) return &t;
        return nullptr;
    }

    // Returns target by value (empty target if not found)
    LinkleTarget get_target(const std::string& name) const {
        for (auto& t : targets)
            if (t.name == name) return t;
        return LinkleTarget{};
    }

    bool has_target(const std::string& name) const {
        return get_target_ptr(name) != nullptr;
    }

    // Returns a list of all target names
    std::vector<std::string> target_names() const {
        std::vector<std::string> names;
        for (auto& t : targets)
            names.push_back(t.name);
        return names;
    }
};

// ─── Flat accessors (for Hylian single-level member access) ──────────────────

inline std::string cfg_project_name(const LinkleConfig& c)    { return c.project.name; }
inline std::string cfg_project_version(const LinkleConfig& c) { return c.project.version; }
inline std::string cfg_project_author(const LinkleConfig& c)  { return c.project.author; }

inline std::string cfg_build_src(const LinkleConfig& c)     { return c.build.src; }
inline std::string cfg_build_main(const LinkleConfig& c)    { return c.build.main; }
inline std::string cfg_build_out(const LinkleConfig& c)     { return c.build.out; }
inline std::string cfg_build_bin(const LinkleConfig& c)     { return c.build.bin; }
inline std::string cfg_build_std(const LinkleConfig& c)     { return c.build.std_ver; }
inline std::vector<std::string> cfg_build_flags(const LinkleConfig& c) { return c.build.flags; }
inline std::vector<std::string> cfg_build_libs(const LinkleConfig& c)  { return c.build.libs; }

inline std::string target_name(const LinkleTarget& t)                  { return t.name; }
inline std::vector<std::string> target_commands(const LinkleTarget& t) { return t.commands; }

// ─── Lexer ────────────────────────────────────────────────────────────────────

enum class TokKind {
    Ident,        // project, build, target, exec, name, ...
    String,       // "hello"
    LBrace,       // {
    RBrace,       // }
    LParen,       // (
    RParen,       // )
    LBracket,     // [
    RBracket,     // ]
    Colon,        // :
    Comma,        // ,
    Semicolon,    // ;
    Eof,
};

struct Token {
    TokKind     kind;
    std::string value;  // for Ident and String
    int         line;
};

class Lexer {
public:
    Lexer(const std::string& src) : _src(src), _pos(0), _line(1) {}

    std::vector<Token> tokenize(std::string& err_out) {
        std::vector<Token> tokens;
        while (_pos < _src.size()) {
            skip_whitespace_and_comments();
            if (_pos >= _src.size()) break;

            char c = _src[_pos];

            if (c == '"') {
                Token t;
                t.line = _line;
                t.kind = TokKind::String;
                auto s = read_string(err_out);
                if (!err_out.empty()) return {};
                t.value = s;
                tokens.push_back(t);
                continue;
            }

            if (std::isalpha(c) || c == '_') {
                Token t;
                t.line = _line;
                t.kind = TokKind::Ident;
                t.value = read_ident();
                tokens.push_back(t);
                continue;
            }

            Token t;
            t.line = _line;
            t.value = std::string(1, c);
            switch (c) {
                case '{': t.kind = TokKind::LBrace;    break;
                case '}': t.kind = TokKind::RBrace;    break;
                case '(': t.kind = TokKind::LParen;    break;
                case ')': t.kind = TokKind::RParen;    break;
                case '[': t.kind = TokKind::LBracket;  break;
                case ']': t.kind = TokKind::RBracket;  break;
                case ':': t.kind = TokKind::Colon;     break;
                case ',': t.kind = TokKind::Comma;     break;
                case ';': t.kind = TokKind::Semicolon; break;
                default:
                    err_out = "line " + std::to_string(_line) +
                              ": unexpected character '" + c + "'";
                    return {};
            }
            _pos++;
            tokens.push_back(t);
        }
        Token eof;
        eof.kind = TokKind::Eof;
        eof.line = _line;
        tokens.push_back(eof);
        return tokens;
    }

private:
    const std::string& _src;
    size_t _pos;
    int    _line;

    void skip_whitespace_and_comments() {
        while (_pos < _src.size()) {
            // whitespace
            if (_src[_pos] == '\n') { _line++; _pos++; continue; }
            if (std::isspace(_src[_pos])) { _pos++; continue; }
            // single-line comment
            if (_pos + 1 < _src.size() && _src[_pos] == '/' && _src[_pos+1] == '/') {
                while (_pos < _src.size() && _src[_pos] != '\n') _pos++;
                continue;
            }
            break;
        }
    }

    std::string read_ident() {
        size_t start = _pos;
        while (_pos < _src.size() && (std::isalnum(_src[_pos]) || _src[_pos] == '_' || _src[_pos] == '.'))
            _pos++;
        return _src.substr(start, _pos - start);
    }

    std::string read_string(std::string& err_out) {
        _pos++; // skip opening "
        std::string result;
        while (_pos < _src.size()) {
            char c = _src[_pos];
            if (c == '"') { _pos++; return result; }
            if (c == '\\' && _pos + 1 < _src.size()) {
                _pos++;
                char esc = _src[_pos++];
                switch (esc) {
                    case 'n':  result += '\n'; break;
                    case 't':  result += '\t'; break;
                    case '"':  result += '"';  break;
                    case '\\': result += '\\'; break;
                    default:   result += '\\'; result += esc; break;
                }
                continue;
            }
            if (c == '\n') _line++;
            result += c;
            _pos++;
        }
        err_out = "line " + std::to_string(_line) + ": unterminated string literal";
        return {};
    }
};

// ─── Parser ───────────────────────────────────────────────────────────────────

class ConfigParser {
public:
    Error* parse(const std::vector<Token>& tokens, LinkleConfig& out) {
        _tokens = &tokens;
        _pos    = 0;

        while (!at_eof()) {
            Token t = peek();
            if (t.kind != TokKind::Ident) {
                return Err("line " + std::to_string(t.line) +
                           ": expected block keyword, got '" + t.value + "'");
            }

            if (t.value == "project") {
                advance();
                auto err = parse_project(out.project);
                if (err) return err;
            } else if (t.value == "build") {
                advance();
                auto err = parse_build(out.build);
                if (err) return err;
            } else if (t.value == "target") {
                advance();
                LinkleTarget target;
                auto err = parse_target(target);
                if (err) return err;
                out.targets.push_back(target);
            } else {
                return Err("line " + std::to_string(t.line) +
                           ": unknown block '" + t.value + "'");
            }
        }
        return nullptr;
    }

private:
    const std::vector<Token>* _tokens = nullptr;
    size_t _pos = 0;

    Token peek() const { return (*_tokens)[_pos]; }
    Token advance()    { return (*_tokens)[_pos++]; }
    bool at_eof() const {
        return _pos >= _tokens->size() || (*_tokens)[_pos].kind == TokKind::Eof;
    }

    Error* expect(TokKind kind, const std::string& what) {
        Token t = peek();
        if (t.kind != kind)
            return Err("line " + std::to_string(t.line) +
                       ": expected " + what + ", got '" + t.value + "'");
        advance();
        return nullptr;
    }

    // Parse a string value token and store it in out.
    Error* expect_string(std::string& out) {
        Token t = peek();
        if (t.kind != TokKind::String)
            return Err("line " + std::to_string(t.line) +
                       ": expected string value, got '" + t.value + "'");
        out = t.value;
        advance();
        return nullptr;
    }

    // Parse a key: value pair where value is a string.
    // Returns false (no error) if the key doesn't match expected.
    Error* parse_string_field(const std::string& key, std::string& out, bool& matched) {
        Token t = peek();
        if (t.kind != TokKind::Ident || t.value != key) { matched = false; return nullptr; }
        advance(); // consume key
        auto err = expect(TokKind::Colon, "':'");
        if (err) return err;
        err = expect_string(out);
        if (err) return err;
        matched = true;
        // optional trailing comma
        if (peek().kind == TokKind::Comma) advance();
        return nullptr;
    }

    // Parse ["a", "b", ...] into a vector of strings.
    Error* parse_string_array(std::vector<std::string>& out) {
        auto err = expect(TokKind::LBracket, "'['");
        if (err) return err;
        while (peek().kind != TokKind::RBracket && !at_eof()) {
            if (peek().kind != TokKind::String)
                return Err("line " + std::to_string(peek().line) +
                           ": expected string in array, got '" + peek().value + "'");
            out.push_back(peek().value);
            advance();
            if (peek().kind == TokKind::Comma) advance();
        }
        return expect(TokKind::RBracket, "']'");
    }

    // ── project { ... } ───────────────────────────────────────────────────────

    Error* parse_project(LinkleProject& out) {
        auto err = expect(TokKind::LBrace, "'{'");
        if (err) return err;

        while (peek().kind != TokKind::RBrace && !at_eof()) {
            Token key = peek();
            if (key.kind != TokKind::Ident)
                return Err("line " + std::to_string(key.line) +
                           ": expected field name in project block");

            bool matched = false;
            if ((err = parse_string_field("name",    out.name,    matched)) != nullptr) return err;
            if (!matched && (err = parse_string_field("version", out.version, matched)) != nullptr) return err;
            if (!matched && (err = parse_string_field("author",  out.author,  matched)) != nullptr) return err;
            if (!matched) {
                return Err("line " + std::to_string(key.line) +
                           ": unknown field '" + key.value + "' in project block");
            }
        }

        return expect(TokKind::RBrace, "'}'");
    }

    // ── build { ... } ─────────────────────────────────────────────────────────

    Error* parse_build(LinkleBuild& out) {
        auto err = expect(TokKind::LBrace, "'{'");
        if (err) return err;

        while (peek().kind != TokKind::RBrace && !at_eof()) {
            Token key = peek();
            if (key.kind != TokKind::Ident)
                return Err("line " + std::to_string(key.line) +
                           ": expected field name in build block");

            std::string k = key.value;

            // string fields
            if (k == "src" || k == "main" || k == "out" || k == "bin" || k == "std") {
                advance(); // consume key name
                err = expect(TokKind::Colon, "':'");
                if (err) return err;
                std::string val;
                err = expect_string(val);
                if (err) return err;
                if (peek().kind == TokKind::Comma) advance();

                if      (k == "src")  out.src     = val;
                else if (k == "main") out.main     = val;
                else if (k == "out")  out.out      = val;
                else if (k == "bin")  out.bin      = val;
                else if (k == "std")  out.std_ver  = val;
                continue;
            }

            // array fields
            if (k == "flags" || k == "libs") {
                advance(); // consume key name
                err = expect(TokKind::Colon, "':'");
                if (err) return err;
                std::vector<std::string> arr;
                err = parse_string_array(arr);
                if (err) return err;
                if (peek().kind == TokKind::Comma) advance();

                if      (k == "flags") out.flags = arr;
                else if (k == "libs")  out.libs  = arr;
                continue;
            }

            return Err("line " + std::to_string(key.line) +
                       ": unknown field '" + k + "' in build block");
        }

        return expect(TokKind::RBrace, "'}'");
    }

    // ── target name() { exec("..."); ... } ────────────────────────────────────

    Error* parse_target(LinkleTarget& out) {
        // read target name
        Token name = peek();
        if (name.kind != TokKind::Ident)
            return Err("line " + std::to_string(name.line) +
                       ": expected target name");
        out.name = name.value;
        advance();

        // ()
        auto err = expect(TokKind::LParen, "'('");
        if (err) return err;
        err = expect(TokKind::RParen, "')'");
        if (err) return err;

        // { body }
        err = expect(TokKind::LBrace, "'{'");
        if (err) return err;

        while (peek().kind != TokKind::RBrace && !at_eof()) {
            Token stmt = peek();
            if (stmt.kind != TokKind::Ident)
                return Err("line " + std::to_string(stmt.line) +
                           ": expected statement in target body");

            if (stmt.value == "exec") {
                advance(); // consume "exec"
                err = expect(TokKind::LParen, "'('");
                if (err) return err;
                std::string cmd;
                err = expect_string(cmd);
                if (err) return err;
                err = expect(TokKind::RParen, "')'");
                if (err) return err;
                // optional semicolon
                if (peek().kind == TokKind::Semicolon) advance();
                out.commands.push_back(cmd);
            } else {
                return Err("line " + std::to_string(stmt.line) +
                           ": unknown statement '" + stmt.value + "' in target body");
            }
        }

        return expect(TokKind::RBrace, "'}'");
    }
};

// ─── Public API ───────────────────────────────────────────────────────────────

// Parse a linkle.hy file from a path. Returns Error* on failure.
inline Error* linkle_parse_file(const std::string& path, LinkleConfig& out) {
    std::ifstream f(path);
    if (!f.is_open())
        return Err("linkle: cannot open config file '" + path + "'");

    std::ostringstream ss;
    ss << f.rdbuf();
    std::string src = ss.str();

    // Lex
    Lexer lexer(src);
    std::string lex_err;
    auto tokens = lexer.tokenize(lex_err);
    if (!lex_err.empty())
        return Err("linkle: " + path + ": " + lex_err);

    // Parse
    ConfigParser parser;
    Error* err = parser.parse(tokens, out);
    if (err)
        return Err("linkle: " + path + ": " + err->message());

    return nullptr;
}

// Parse a linkle.hy string directly (useful for testing).
inline Error* linkle_parse_str(const std::string& src, LinkleConfig& out) {
    Lexer lexer(src);
    std::string lex_err;
    auto tokens = lexer.tokenize(lex_err);
    if (!lex_err.empty())
        return Err("linkle: " + lex_err);

    ConfigParser parser;
    return parser.parse(tokens, out);
}

// ─── Debug helper ─────────────────────────────────────────────────────────────

inline void linkle_print_config(const LinkleConfig& cfg) {
    printf("project:\n");
    printf("  name:    %s\n", cfg.project.name.c_str());
    printf("  version: %s\n", cfg.project.version.c_str());
    printf("  author:  %s\n", cfg.project.author.c_str());
    printf("build:\n");
    printf("  src:     %s\n", cfg.build.src.c_str());
    printf("  main:    %s\n", cfg.build.main.c_str());
    printf("  out:     %s\n", cfg.build.out.c_str());
    printf("  bin:     %s\n", cfg.build.bin.c_str());
    printf("  std:     %s\n", cfg.build.std_ver.c_str());
    printf("  flags:   [");
    for (int i = 0; i < (int)cfg.build.flags.size(); i++)
        printf("%s%s", i ? ", " : "", cfg.build.flags[i].c_str());
    printf("]\n");
    printf("  libs:    [");
    for (int i = 0; i < (int)cfg.build.libs.size(); i++)
        printf("%s%s", i ? ", " : "", cfg.build.libs[i].c_str());
    printf("]\n");
    for (auto& t : cfg.targets) {
        printf("target %s():\n", t.name.c_str());
        for (auto& cmd : t.commands)
            printf("  exec(\"%s\")\n", cmd.c_str());
    }
}

#endif // HYLIAN_STD_LINKLE_CONFIG_HPP