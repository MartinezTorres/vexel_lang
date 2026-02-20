#pragma once
#include "lexer.h"
#include "ast.h"

namespace vexel {

class Parser {
    std::vector<Token> tokens;
    size_t pos;
    std::vector<Diagnostic> errors;
    bool panic_mode;
    bool allow_statement_conditionals;
    int statement_expr_depth;
    int statement_expr_allowed_depth;

    enum class AnnotationContext {
        TopLevel,
        Statement,
        Expression
    };

public:
    Parser(std::vector<Token> toks);
    Module parse_module(const std::string& name, const std::string& path);

private:
    void synchronize();
    void record_error(const std::string& msg, const SourceLocation& loc);
    const Token& previous();
    const Token& current();
    const Token& peek(int offset = 1);
    bool check(TokenType type);
    bool match(TokenType type);
    Token consume(TokenType type, const std::string& msg);
    void skip_semis();
    bool is_annotation_start();
    bool token_starts_expression(TokenType type) const;
    bool token_starts_statement(TokenType type) const;
    bool token_starts_top_level(TokenType type) const;
    bool token_starts_annotation_target(TokenType type, AnnotationContext context) const;
    bool token_can_be_annotation_arg(TokenType type) const;
    bool try_parse_annotation_block(size_t start, size_t& end, std::vector<Annotation>& out) const;
    std::vector<Annotation> parse_annotations_disambiguated(AnnotationContext context);
    std::vector<Annotation> parse_annotations();
    std::string parse_annotation_arg();

    bool looks_like_var_decl_with_linkage(bool allow_double_bang_local) const;
    StmtPtr parse_var_decl(bool allow_double_bang_local);

    StmtPtr parse_top_level();
    StmtPtr parse_func_decl();
    StmtPtr parse_type_decl();
    StmtPtr parse_import();
    StmtPtr parse_global();
    StmtPtr parse_stmt();
    StmtPtr parse_stmt_no_semi();

    ExprPtr parse_expr();
    ExprPtr parse_expr_allowing_stmt_conditional();
    ExprPtr parse_assignment();
    ExprPtr parse_conditional();
    ExprPtr parse_logic_or();
    ExprPtr parse_logic_and();
    ExprPtr parse_bit_or();
    ExprPtr parse_bit_xor();
    ExprPtr parse_bit_and();
    ExprPtr parse_compare();
    ExprPtr parse_shift();
    ExprPtr parse_range();
    ExprPtr parse_sum();
    ExprPtr parse_prod();
    ExprPtr parse_unary();
    ExprPtr parse_postfix();
    ExprPtr parse_postfix_suffix(ExprPtr expr);
    ExprPtr parse_primary();

    ExprPtr parse_block();
    ExprPtr parse_array();
    std::vector<std::string> parse_resource_path(bool leading_colon_already_consumed = false);

    TypePtr parse_type();
    std::vector<Parameter> parse_params();
    std::vector<std::string> parse_ref_params();
    std::vector<Field> parse_fields();
    std::vector<std::string> parse_qualified_name();
    std::string parse_function_name();
};

}
