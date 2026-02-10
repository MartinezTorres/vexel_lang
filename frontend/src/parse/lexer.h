#pragma once
#include "common.h"
#include <variant>
#include <optional>

namespace vexel {

enum class TokenType {
    // Literals
    IntLiteral, FloatLiteral, StringLiteral, CharLiteral,
    // Identifiers
    Identifier,
    // Sigils
    Dollar, At, DoubleAt, Ampersand, Hash,
    // Operators
    Plus, Minus, Star, Slash, Percent,
    BitOr, BitXor, BitNot,
    LeftShift, RightShift,
    Equal, NotEqual, Less, LessEqual, Greater, GreaterEqual,
    Assign, Arrow, BreakArrow, ContinueArrow,
    Dot, Comma, Semicolon, Colon, DoubleColon,
    LogicalNot, LogicalAnd, LogicalOr,
    Question,
    // Brackets
    LeftParen, RightParen, LeftBrace, RightBrace, LeftBracket, RightBracket,
    // Range
    DotDot,
    // Special
    EndOfFile,
    // Combined sigils
    AmpersandBang, AmpersandCaret
};

struct Token {
    TokenType type;
    std::string lexeme;
    SourceLocation location;
    std::variant<int64_t, uint64_t, double, std::string> value;

    Token(TokenType t, const std::string& lex, const SourceLocation& loc)
        : type(t), lexeme(lex), location(loc) {}
};

class Lexer {
    std::string source;
    std::string filename;
    size_t pos;
    int line;
    int column;

public:
    Lexer(const std::string& src, const std::string& fname);
    std::vector<Token> tokenize();

private:
    char peek(int offset = 0);
    char advance();
    bool match(char expected);
    void skip_whitespace();
    void skip_comment();
    SourceLocation current_location();

    Token read_number();
    Token read_identifier();
    Token read_string();
    Token read_char();
    char read_escape();
    void ensure_ascii(char c);
};

}
