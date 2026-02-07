#include "lexer.h"
#include <cctype>

namespace vexel {

Lexer::Lexer(const std::string& src, const std::string& fname)
    : source(src), filename(fname), pos(0), line(1), column(1) {}

char Lexer::peek(int offset) {
    if (pos + offset >= source.size()) return '\0';
    char c = source[pos + offset];
    ensure_ascii(c);
    return c;
}

char Lexer::advance() {
    if (pos >= source.size()) return '\0';
    char c = source[pos++];
    ensure_ascii(c);
    if (c == '\n') {
        line++;
        column = 1;
    } else {
        column++;
    }
    return c;
}

bool Lexer::match(char expected) {
    if (peek() == expected) {
        advance();
        return true;
    }
    return false;
}

void Lexer::skip_whitespace() {
    while (isspace(peek())) {
        advance();
    }
}

void Lexer::skip_comment() {
    if (peek() == '/' && peek(1) == '/') {
        while (peek() != '\n' && peek() != '\0') {
            advance();
        }
    }
}

SourceLocation Lexer::current_location() {
    return SourceLocation(filename, line, column);
}

char Lexer::read_escape() {
    advance(); // skip backslash
    char c = advance();
    if (c == '\0') {
        throw CompileError("Unterminated escape sequence at end of file", current_location());
    }
    switch (c) {
        case 'n': return '\n';
        case 'r': return '\r';
        case 't': return '\t';
        case '\\': return '\\';
        case '\'': return '\'';
        case '"': return '"';
        case 'x': {
            // hex escape
            char h1 = advance();
            char h2 = advance();
            if (h1 == '\0' || h2 == '\0') {
                throw CompileError("Unterminated hex escape sequence at end of file", current_location());
            }
            if (!isxdigit(h1) || !isxdigit(h2)) {
                throw CompileError("Invalid hex escape sequence", current_location());
            }
            int val = (isdigit(h1) ? h1-'0' : tolower(h1)-'a'+10) * 16 +
                      (isdigit(h2) ? h2-'0' : tolower(h2)-'a'+10);
            return (char)val;
        }
        default:
            if (c >= '0' && c <= '3') {
                // octal escape
                int val = c - '0';
                char o1 = peek();
                if (o1 >= '0' && o1 <= '7') {
                    advance();
                    val = val * 8 + (o1 - '0');
                    char o2 = peek();
                    if (o2 >= '0' && o2 <= '7') {
                        advance();
                        val = val * 8 + (o2 - '0');
                    }
                }
                return (char)val;
            }
            throw CompileError(std::string("Invalid escape sequence: \\") + c, current_location());
    }
}

void Lexer::ensure_ascii(char c) {
    if (static_cast<unsigned char>(c) > 0x7F) {
        throw CompileError("Non-ASCII character detected (source must be ASCII-7)", current_location());
    }
}

Token Lexer::read_number() {
    SourceLocation loc = current_location();
    std::string num;
    auto ensure_no_identifier_tail = [&](char trailing) {
        if (std::isalpha(static_cast<unsigned char>(trailing)) || trailing == '_') {
            throw CompileError("Identifier cannot start with a digit (found '" + num + trailing + "')", loc);
        }
    };

    if (peek() == '0' && (peek(1) == 'x' || peek(1) == 'X')) {
        // hex
        num += advance();
        num += advance();
        if (!isxdigit(peek())) {
            throw CompileError("Invalid hexadecimal literal: must have at least one hex digit after 0x", loc);
        }
        while (isxdigit(peek())) {
            num += advance();
        }
        ensure_no_identifier_tail(peek());
        try {
            uint64_t val = std::stoull(num, nullptr, 16);
            Token t(TokenType::IntLiteral, num, loc);
            t.value = val;
            return t;
        } catch (const std::exception& e) {
            throw CompileError("Hex integer literal overflow: " + num, loc);
        }
    }

    while (isdigit(peek())) {
        num += advance();
    }
    ensure_no_identifier_tail(peek());

    if (peek() == '.' && isdigit(peek(1))) {
        // float
        num += advance();
        while (isdigit(peek())) {
            num += advance();
        }
        // Optional exponent part
        if (peek() == 'e' || peek() == 'E') {
            num += advance();
            if (peek() == '+' || peek() == '-') {
                num += advance();
            }
            if (!isdigit(peek())) {
                throw CompileError("Invalid float literal exponent", loc);
            }
            while (isdigit(peek())) {
                num += advance();
            }
        }
        ensure_no_identifier_tail(peek());
        try {
            double val = std::stod(num);
            Token t(TokenType::FloatLiteral, num, loc);
            t.value = val;
            return t;
        } catch (const std::exception& e) {
            throw CompileError("Float literal overflow: " + num, loc);
        }
    }

    // integer
    try {
        int64_t val = std::stoll(num);
        Token t(TokenType::IntLiteral, num, loc);
        t.value = val;
        return t;
    } catch (const std::exception& e) {
        throw CompileError("Integer literal overflow: " + num, loc);
    }
}

Token Lexer::read_identifier() {
    SourceLocation loc = current_location();
    std::string id;
    while (isalnum(peek()) || peek() == '_') {
        id += advance();
    }
    if (id == "true") {
        Token t(TokenType::IntLiteral, id, loc);
        t.value = static_cast<int64_t>(1);
        return t;
    }
    if (id == "false") {
        Token t(TokenType::IntLiteral, id, loc);
        t.value = static_cast<int64_t>(0);
        return t;
    }
    if (id == "mut") {
        return Token(TokenType::Mut, id, loc);
    }
    return Token(TokenType::Identifier, id, loc);
}

Token Lexer::read_string() {
    SourceLocation loc = current_location();
    advance(); // skip opening quote
    std::string str;
    while (peek() != '"' && peek() != '\0') {
        if (peek() == '\\') {
            str += read_escape();
        } else {
            str += advance();
        }
    }
    if (peek() != '"') {
        throw CompileError("Unterminated string", loc);
    }
    advance(); // skip closing quote
    Token t(TokenType::StringLiteral, str, loc);
    t.value = str;
    return t;
}

Token Lexer::read_char() {
    SourceLocation loc = current_location();
    advance(); // skip opening quote
    if (peek() == '\0') {
        throw CompileError("Unterminated char literal", loc);
    }
    if (peek() == '\'') {
        throw CompileError("Empty character literal", loc);
    }
    char c;
    if (peek() == '\\') {
        c = read_escape();
    } else {
        c = advance();
    }
    if (peek() != '\'') {
        throw CompileError("Unterminated char literal", loc);
    }
    advance(); // skip closing quote
    Token t(TokenType::CharLiteral, std::string(1, c), loc);
    t.value = (uint64_t)(unsigned char)c;
    return t;
}

std::vector<Token> Lexer::tokenize() {
    std::vector<Token> tokens;

    while (peek() != '\0') {
        // Skip all whitespace and comments
        while (isspace(peek()) || (peek() == '/' && peek(1) == '/')) {
            skip_whitespace();
            skip_comment();
        }
        if (peek() == '\0') break;

        SourceLocation loc = current_location();
        char c = peek();

        if (isdigit(c)) {
            tokens.push_back(read_number());
            continue;
        }

        if (isalpha(c) || c == '_') {
            tokens.push_back(read_identifier());
            continue;
        }

        if (c == '"') {
            tokens.push_back(read_string());
            continue;
        }

        if (c == '\'') {
            tokens.push_back(read_char());
            continue;
        }

        // Operators and symbols
        advance();
        switch (c) {
            case '$': tokens.emplace_back(TokenType::Dollar, "$", loc); break;
            case '@':
                if (match('@')) {
                    tokens.emplace_back(TokenType::DoubleAt, "@@", loc);
                } else {
                    tokens.emplace_back(TokenType::At, "@", loc);
                }
                break;
            case '#': tokens.emplace_back(TokenType::Hash, "#", loc); break;
            case '+': tokens.emplace_back(TokenType::Plus, "+", loc); break;
            case '*': tokens.emplace_back(TokenType::Star, "*", loc); break;
            case '/': tokens.emplace_back(TokenType::Slash, "/", loc); break;
            case '%': tokens.emplace_back(TokenType::Percent, "%", loc); break;
            case '^': tokens.emplace_back(TokenType::BitXor, "^", loc); break;
            case '~': tokens.emplace_back(TokenType::BitNot, "~", loc); break;
            case '(': tokens.emplace_back(TokenType::LeftParen, "(", loc); break;
            case ')': tokens.emplace_back(TokenType::RightParen, ")", loc); break;
            case '{': tokens.emplace_back(TokenType::LeftBrace, "{", loc); break;
            case '}': tokens.emplace_back(TokenType::RightBrace, "}", loc); break;
            case '[': tokens.emplace_back(TokenType::LeftBracket, "[", loc); break;
            case ']': tokens.emplace_back(TokenType::RightBracket, "]", loc); break;
            case ',': tokens.emplace_back(TokenType::Comma, ",", loc); break;
            case ';': tokens.emplace_back(TokenType::Semicolon, ";", loc); break;
            case '?': tokens.emplace_back(TokenType::Question, "?", loc); break;

            case '|':
                if (match('|')) {
                    tokens.emplace_back(TokenType::LogicalOr, "||", loc);
                } else {
                    // Single | is BitOr (also used for length operator)
                    tokens.emplace_back(TokenType::BitOr, "|", loc);
                }
                break;

            case '&':
                if (match('!')) {
                    tokens.emplace_back(TokenType::AmpersandBang, "&!", loc);
                } else if (match('^')) {
                    tokens.emplace_back(TokenType::AmpersandCaret, "&^", loc);
                } else if (match('&')) {
                    tokens.emplace_back(TokenType::LogicalAnd, "&&", loc);
                } else {
                    tokens.emplace_back(TokenType::Ampersand, "&", loc);
                }
                break;

            case '!':
                if (match('=')) {
                    tokens.emplace_back(TokenType::NotEqual, "!=", loc);
                } else {
                    tokens.emplace_back(TokenType::LogicalNot, "!", loc);
                }
                break;

            case '=':
                if (match('=')) {
                    tokens.emplace_back(TokenType::Equal, "==", loc);
                } else {
                    tokens.emplace_back(TokenType::Assign, "=", loc);
                }
                break;

            case '<':
                if (match('<')) {
                    tokens.emplace_back(TokenType::LeftShift, "<<", loc);
                } else if (match('=')) {
                    tokens.emplace_back(TokenType::LessEqual, "<=", loc);
                } else {
                    tokens.emplace_back(TokenType::Less, "<", loc);
                }
                break;

            case '>':
                if (match('>')) {
                    tokens.emplace_back(TokenType::RightShift, ">>", loc);
                } else if (match('=')) {
                    tokens.emplace_back(TokenType::GreaterEqual, ">=", loc);
                } else {
                    tokens.emplace_back(TokenType::Greater, ">", loc);
                }
                break;

            case '-':
                if (match('>')) {
                    if (match('|')) {
                        tokens.emplace_back(TokenType::BreakArrow, "->|", loc);
                    } else if (match('>')) {
                        tokens.emplace_back(TokenType::ContinueArrow, "->>", loc);
                    } else {
                        tokens.emplace_back(TokenType::Arrow, "->", loc);
                    }
                } else {
                    tokens.emplace_back(TokenType::Minus, "-", loc);
                }
                break;

            case '.':
                if (match('.')) {
                    tokens.emplace_back(TokenType::DotDot, "..", loc);
                } else {
                    tokens.emplace_back(TokenType::Dot, ".", loc);
                }
                break;

            case ':':
                if (match(':')) {
                    tokens.emplace_back(TokenType::DoubleColon, "::", loc);
                } else {
                    tokens.emplace_back(TokenType::Colon, ":", loc);
                }
                break;

            default:
                throw CompileError("Unexpected character: " + std::string(1, c), loc);
        }
    }

    tokens.emplace_back(TokenType::EndOfFile, "", current_location());
    return tokens;
}

}
