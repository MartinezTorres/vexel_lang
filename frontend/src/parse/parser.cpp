#include "parser.h"
#include "constants.h"

namespace vexel {

Parser::Parser(std::vector<Token> toks)
    : tokens(std::move(toks)),
      pos(0),
      panic_mode(false),
      allow_statement_conditionals(false),
      statement_expr_depth(0),
      statement_expr_allowed_depth(0) {}

const Token& Parser::current() {
    if (pos >= tokens.size()) return tokens.back();
    return tokens[pos];
}

const Token& Parser::peek(int offset) {
    size_t p = pos + offset;
    if (p >= tokens.size()) return tokens.back();
    return tokens[p];
}

const Token& Parser::previous() {
    if (pos == 0) return tokens[0];
    return tokens[pos - 1];
}

void Parser::record_error(const std::string& msg, const SourceLocation& loc) {
    errors.emplace_back(DiagnosticLevel::Error, msg, loc);
    panic_mode = true;
}

void Parser::synchronize() {
    panic_mode = false;
    while (!check(TokenType::EndOfFile)) {
        // Check if we're at a synchronization point (statement boundary)
        if (previous().type == TokenType::Semicolon) {
            // We're past a semicolon - check if current token looks like a valid statement start
            switch (current().type) {
                case TokenType::Ampersand:
                case TokenType::AmpersandBang:
                case TokenType::AmpersandCaret:
                case TokenType::Hash:
                case TokenType::DoubleColon:
                case TokenType::Identifier:
                    // Valid statement start, we're synchronized
                    return;
                default:
                    // Not a valid statement start, keep advancing
                    pos++;
                    break;
            }
        } else {
            // We haven't found a semicolon yet, check if current token is a statement start
            switch (current().type) {
                case TokenType::Ampersand:
                case TokenType::AmpersandBang:
                case TokenType::AmpersandCaret:
                case TokenType::Hash:
                case TokenType::DoubleColon:
                    return;
                default:
                    pos++;
            }
        }
    }
}

bool Parser::check(TokenType type) {
    return current().type == type;
}

bool Parser::match(TokenType type) {
    if (check(type)) {
        pos++;
        return true;
    }
    return false;
}

Token Parser::consume(TokenType type, const std::string& msg) {
    if (!check(type)) {
        record_error(msg, current().location);
        return current();
    }
    return tokens[pos++];
}

void Parser::skip_semis() {
    while (match(TokenType::Semicolon));
}

bool Parser::is_annotation_start() {
    return check(TokenType::LeftBracket) &&
           peek().type == TokenType::LeftBracket &&
           peek(2).type == TokenType::Identifier;
}

std::string Parser::parse_annotation_arg() {
    Token tok = current();
    switch (tok.type) {
        case TokenType::Identifier:
        case TokenType::StringLiteral:
        case TokenType::IntLiteral:
        case TokenType::FloatLiteral: {
            pos++;
            return tok.lexeme;
        }
        default:
            throw CompileError("Expected annotation argument", tok.location);
    }
}

std::vector<Annotation> Parser::parse_annotations() {
    std::vector<Annotation> annotations;
    while (is_annotation_start()) {
        consume(TokenType::LeftBracket, "Expected '[' to start annotation");
        consume(TokenType::LeftBracket, "Expected '[' to start annotation");

        do {
            Token name_tok = consume(TokenType::Identifier, "Expected annotation name");
            Annotation ann;
            ann.name = name_tok.lexeme;
            ann.location = name_tok.location;

            if (match(TokenType::LeftParen)) {
                if (!check(TokenType::RightParen)) {
                    do {
                        ann.args.push_back(parse_annotation_arg());
                    } while (match(TokenType::Comma));
                }
                consume(TokenType::RightParen, "Expected ')' to close annotation arguments");
            }

            annotations.push_back(ann);
        } while (match(TokenType::Comma));

        consume(TokenType::RightBracket, "Expected ']' to close annotation");
        consume(TokenType::RightBracket, "Expected ']' to close annotation");
    }
    return annotations;
}

Module Parser::parse_module(const std::string& name, const std::string& path) {
    Module mod;
    mod.name = name;
    mod.path = path;

    while (!check(TokenType::EndOfFile)) {
        skip_semis();
        if (check(TokenType::EndOfFile)) break;

        if (panic_mode) {
            synchronize();
            if (check(TokenType::EndOfFile)) break;
        }

        try {
            std::vector<Annotation> annotations = parse_annotations();
            StmtPtr top = parse_top_level();
            top->annotations = annotations;
            mod.top_level.push_back(top);
            skip_semis();
        } catch (const CompileError& e) {
            errors.emplace_back(DiagnosticLevel::Error, e.what(), e.location);
            synchronize();
            if (check(TokenType::EndOfFile)) break;
        }
    }

    if (!errors.empty()) {
        std::string combined_msg = "Parse failed with " + std::to_string(errors.size()) + " error(s):\n";
        for (const auto& err : errors) {
            combined_msg += "  " + err.to_string() + "\n";
        }
        throw CompileError(combined_msg, errors[0].location);
    }

    return mod;
}

StmtPtr Parser::parse_top_level() {
    TokenType t = current().type;
    if (t == TokenType::Ampersand || t == TokenType::AmpersandBang || t == TokenType::AmpersandCaret) {
        return parse_func_decl();
    }
    if (t == TokenType::Hash) {
        return parse_type_decl();
    }
    if (t == TokenType::DoubleColon) {
        return parse_import();
    }
    return parse_global();
}

static bool is_operator_function_token(TokenType type) {
    switch (type) {
        case TokenType::Plus:
        case TokenType::Minus:
        case TokenType::Star:
        case TokenType::Slash:
        case TokenType::Percent:
        case TokenType::Equal:
        case TokenType::NotEqual:
        case TokenType::Less:
        case TokenType::LessEqual:
        case TokenType::Greater:
        case TokenType::GreaterEqual:
        case TokenType::At:
        case TokenType::DoubleAt:
            return true;
        default:
            return false;
    }
}

std::string Parser::parse_function_name() {
    if (check(TokenType::Identifier)) {
        return consume(TokenType::Identifier, "Expected function name").lexeme;
    }

    Token tok = current();
    if (is_operator_function_token(tok.type)) {
        pos++;
        return tok.lexeme;
    }

    throw CompileError("Expected function name or overloadable operator", tok.location);
}

StmtPtr Parser::parse_func_decl() {
    SourceLocation loc = current().location;

    bool is_external = match(TokenType::AmpersandBang);
    bool is_exported = match(TokenType::AmpersandCaret);
    if (!is_external && !is_exported) {
        consume(TokenType::Ampersand, "Expected function declaration");
    }

    std::vector<std::string> ref_params;
    // Check for reference parameters: &(r1,r2)name vs &name
    if (check(TokenType::LeftParen)) {
        // Look ahead to distinguish (ref params) from (value params)
        size_t saved_pos = pos;
        pos++; // skip (

        bool looks_like_ref = true;
        if (check(TokenType::RightParen)) {
            looks_like_ref = false; // empty parens before name
        } else {
            while (!check(TokenType::RightParen) && !check(TokenType::EndOfFile)) {
                if (!check(TokenType::Identifier)) {
                    looks_like_ref = false;
                    break;
                }
                pos++; // skip identifier
                if (check(TokenType::Colon) || check(TokenType::Dollar)) {
                    // Has type annotation or $ - these are value params
                    looks_like_ref = false;
                    break;
                }
                if (!match(TokenType::Comma)) break;
            }
        }

        pos = saved_pos; // restore

        if (looks_like_ref && check(TokenType::LeftParen)) {
            ref_params = parse_ref_params();
        }
    }

    // Check for Type::method syntax
    std::string type_namespace;
    size_t saved_pos = pos;
    bool namespace_found = false;
    if (match(TokenType::Hash)) {
        if (check(TokenType::Identifier)) {
            Token maybe_type = current();
            pos++;
            if (match(TokenType::DoubleColon)) {
                type_namespace = maybe_type.lexeme;
                namespace_found = true;
            } else {
                pos = saved_pos;
            }
        } else {
            pos = saved_pos;
        }
    }
    if (!namespace_found) {
        saved_pos = pos;
        if (check(TokenType::Identifier)) {
            Token maybe_type = current();
            pos++;
            if (match(TokenType::DoubleColon)) {
                type_namespace = maybe_type.lexeme;
                namespace_found = true;
            } else {
                pos = saved_pos;
            }
        }
    }
    if (namespace_found && ref_params.size() != 1) {
        throw CompileError("Type::method syntax requires exactly one receiver parameter", current().location);
    }

    if (match(TokenType::Hash)) {
        // Sigil belongs to function name; consume and ignore for storage
    }
    std::string name = parse_function_name();
    consume(TokenType::LeftParen, "Expected '('");
    std::vector<Parameter> params = parse_params();
    consume(TokenType::RightParen, "Expected ')'");

    TypePtr return_type = nullptr;
    std::vector<TypePtr> return_types;
    if (match(TokenType::Arrow)) {
        if (!check(TokenType::LeftBrace) && !check(TokenType::Semicolon)) {
            // Check for tuple return: -> (T1, T2, ...)
            if (check(TokenType::LeftParen)) {
                consume(TokenType::LeftParen, "Expected '('");
                do {
                    return_types.push_back(parse_type());
                } while (match(TokenType::Comma));
                consume(TokenType::RightParen, "Expected ')'");

                // Validate: at least 2 types for tuple
                if (return_types.size() < 2) {
                    throw CompileError("Tuple return type must have at least 2 elements", loc);
                }
            } else {
                // Single return type
                return_type = parse_type();
            }
        }
    }

    ExprPtr body = nullptr;
    if (is_external) {
        consume(TokenType::Semicolon, "Expected ';' after external function");
    } else {
        body = parse_block();
    }

    return Stmt::make_func(name, params, ref_params, return_type, body, is_external, is_exported, loc, type_namespace, return_types);
}

StmtPtr Parser::parse_type_decl() {
    SourceLocation loc = current().location;
    consume(TokenType::Hash, "Expected '#'");
    std::string name = consume(TokenType::Identifier, "Expected type name").lexeme;
    consume(TokenType::LeftParen, "Expected '('");
    std::vector<Field> fields = parse_fields();
    consume(TokenType::RightParen, "Expected ')'");
    consume(TokenType::Semicolon, "Expected ';'");
    return Stmt::make_type(name, fields, loc);
}

StmtPtr Parser::parse_import() {
    SourceLocation loc = current().location;
    consume(TokenType::DoubleColon, "Expected '::'");
    if (check(TokenType::StringLiteral)) {
        std::string command = std::get<std::string>(current().value);
        pos++;
        consume(TokenType::Arrow, "Expected '->' after process command");
        std::string var_name = consume(TokenType::Identifier, "Expected identifier after ->").lexeme;
        consume(TokenType::Semicolon, "Expected ';'");
        ExprPtr proc = Expr::make_process(command, loc);
        TypePtr str_type = Type::make_primitive(PrimitiveType::String, loc);
        return Stmt::make_var(var_name, str_type, proc, false, loc);
    }

    std::vector<std::string> path = parse_qualified_name();
    consume(TokenType::Semicolon, "Expected ';'");
    return Stmt::make_import(path, loc);
}

StmtPtr Parser::parse_global() {
    SourceLocation loc = current().location;
    bool is_exported = match(TokenType::BitXor);
    std::string name = consume(TokenType::Identifier, "Expected variable name").lexeme;

    TypePtr type = nullptr;
    if (match(TokenType::Colon)) {
        type = parse_type();
    } else if (check(TokenType::Hash) || check(TokenType::LeftBracket)) {
        type = parse_type();
    }

    ExprPtr init = nullptr;
    if (match(TokenType::Assign)) {
        init = parse_expr();
    }

    if (!type && !init) {
        throw CompileError("Global declaration without initializer must have type annotation", loc);
    }

    bool is_mut = (!init && type);
    return Stmt::make_var(name, type, init, is_mut, loc, is_exported);
}

StmtPtr Parser::parse_stmt() {
    std::vector<Annotation> annotations = parse_annotations();
    StmtPtr stmt = parse_stmt_no_semi();
    stmt->annotations = annotations;
    skip_semis();
    return stmt;
}

StmtPtr Parser::parse_stmt_no_semi() {
    SourceLocation loc = current().location;

    if (match(TokenType::Arrow)) {
        if (match(TokenType::BitOr)) {
            consume(TokenType::Semicolon, "Expected ';'");
            return Stmt::make_break(loc);
        }
        if (match(TokenType::Greater)) {
            consume(TokenType::Semicolon, "Expected ';'");
            return Stmt::make_continue(loc);
        }
        ExprPtr ret_expr = nullptr;
        if (!check(TokenType::Semicolon)) {
            ret_expr = parse_expr();
        }
        consume(TokenType::Semicolon, "Expected ';'");
        return Stmt::make_return(ret_expr, loc);
    }

    if (match(TokenType::BreakArrow)) {
        consume(TokenType::Semicolon, "Expected ';'");
        return Stmt::make_break(loc);
    }

    if (match(TokenType::ContinueArrow)) {
        consume(TokenType::Semicolon, "Expected ';'");
        return Stmt::make_continue(loc);
    }

    if (check(TokenType::Hash)) {
        return parse_type_decl();
    }

    if (check(TokenType::DoubleColon)) {
        return parse_import();
    }

    if (check(TokenType::Ampersand)) {
        return parse_func_decl();
    }

    // Check for multi-assignment: a, b, c = expr
    if (check(TokenType::Identifier)) {
        size_t saved = pos;
        std::vector<std::string> ids;
        std::vector<SourceLocation> id_locs;

        // Parse first identifier
        ids.push_back(current().lexeme);
        id_locs.push_back(current().location);
        pos++;

        // Check for comma (multi-assignment pattern)
        if (match(TokenType::Comma)) {
            // Parse remaining identifiers
            do {
                if (!check(TokenType::Identifier)) {
                    // Not multi-assignment, backtrack
                    pos = saved;
                    goto not_multi_assign;
                }
                ids.push_back(current().lexeme);
                id_locs.push_back(current().location);
                pos++;
            } while (match(TokenType::Comma));

            // Check for assignment
            if (match(TokenType::Assign)) {
                // This IS multi-assignment: a, b, c = expr
                // Desugar to: { __tmp = expr; a = __tmp.__0; b = __tmp.__1; ... }
                ExprPtr rhs = parse_expr();

                // Generate temporary variable name
                static int tmp_counter = 0;
                std::string tmp_name = std::string(TUPLE_TMP_PREFIX) + std::to_string(tmp_counter++);

                // Create block to hold desugared statements
                std::vector<StmtPtr> stmts;

                // __tmp = rhs
                stmts.push_back(Stmt::make_var(tmp_name, nullptr, rhs, true, loc));

                // a = __tmp.__0; b = __tmp.__1; ...
                for (size_t i = 0; i < ids.size(); i++) {
                    ExprPtr tmp_ref = Expr::make_identifier(tmp_name, loc);
                    std::string field_name = std::string(MANGLED_PREFIX) + std::to_string(i);
                    ExprPtr field_access = Expr::make_member(tmp_ref, field_name, id_locs[i]);
                    ExprPtr assignment = Expr::make_assignment(
                        Expr::make_identifier(ids[i], id_locs[i]),
                        field_access,
                        id_locs[i]
                    );
                    stmts.push_back(Stmt::make_expr(assignment, id_locs[i]));
                }

                // Return block expression wrapped in statement
                ExprPtr block = Expr::make_block(stmts, nullptr, loc);
                return Stmt::make_expr(block, loc);
            }
        }

        // Not multi-assignment, backtrack
        pos = saved;
    }
    not_multi_assign:

    // Could be expression statement, conditional statement, iteration, or variable declaration
    ExprPtr expr = parse_expr_allowing_stmt_conditional();

    // Check for @ / @@ iteration
    bool has_iteration = false;
    bool sorted_iteration = false;
    if (match(TokenType::DoubleAt)) {
        has_iteration = true;
        sorted_iteration = true;
    } else if (match(TokenType::At)) {
        has_iteration = true;
    }
    if (has_iteration) {
        ExprPtr body = parse_expr();
        return Stmt::make_expr(Expr::make_iteration(expr, body, sorted_iteration, loc), loc);
    }

    // Check for conditional statement
    if (match(TokenType::Question)) {
        StmtPtr stmt = parse_stmt();
        return Stmt::make_conditional_stmt(expr, stmt, loc);
    }

    // Check if this is a variable declaration (identifier with type annotation, no assignment)
    if (expr->kind == Expr::Kind::Identifier && expr->type) {
        // This is a mutable variable declaration: name:type;
        return Stmt::make_var(expr->name, expr->type, nullptr, true, loc);
    }

    return Stmt::make_expr(expr, loc);
}

ExprPtr Parser::parse_expr() {
    struct DepthGuard {
        int& depth;
        DepthGuard(int& d) : depth(d) { depth++; }
        ~DepthGuard() { depth--; }
    } guard(statement_expr_depth);
    return parse_assignment();
}

ExprPtr Parser::parse_expr_allowing_stmt_conditional() {
    bool previous = allow_statement_conditionals;
    int previous_allowed = statement_expr_allowed_depth;
    allow_statement_conditionals = true;
    statement_expr_allowed_depth = statement_expr_depth + 1;
    ExprPtr result = parse_expr();
    allow_statement_conditionals = previous;
    statement_expr_allowed_depth = previous_allowed;
    return result;
}

ExprPtr Parser::parse_assignment() {
    ExprPtr expr = parse_conditional();

    if (match(TokenType::Assign)) {
        ExprPtr rhs = parse_assignment();
        return Expr::make_assignment(expr, rhs, expr->location);
    }

    return expr;
}

ExprPtr Parser::parse_conditional() {
    ExprPtr expr = parse_logic_or();

    if (check(TokenType::Question)) {
        // Try to parse as conditional expression (? expr : expr)
        // If it fails or there's no ':', backtrack (it's a statement conditional)
        size_t saved = pos;
        pos++; // consume '?'
        ExprPtr true_expr = nullptr;
        bool parsed_true = false;

        // Parse true branch with manual rollback instead of exceptions
        {
            size_t before_true = pos;
            try {
                true_expr = parse_expr();
                parsed_true = true;
            } catch (const CompileError&) {
                parsed_true = false;
                pos = before_true;
            }
        }

        if (parsed_true && match(TokenType::Colon)) {
            ExprPtr false_expr = parse_conditional();
            auto require_parentheses = [&](ExprPtr branch) {
                if (branch && branch->kind == Expr::Kind::Conditional && !branch->was_parenthesized) {
                    throw CompileError("ambiguous nested conditional: add parentheses", branch->location);
                }
            };
            require_parentheses(true_expr);
            require_parentheses(false_expr);
            return Expr::make_conditional(expr, true_expr, false_expr, expr->location);
        }

        // No ':', so this is a statement conditional, not expression conditional
        // Backtrack; if statement conditionals are not allowed here, emit error
        pos = saved;
        bool can_use_statement_conditional =
            allow_statement_conditionals &&
            statement_expr_depth == statement_expr_allowed_depth;
        if (!can_use_statement_conditional) {
            throw CompileError("Statement conditional is not an expression", current().location);
        }
    }

    return expr;
}

ExprPtr Parser::parse_logic_or() {
    ExprPtr left = parse_logic_and();

    while (check(TokenType::LogicalOr)) {
        std::string op = current().lexeme;
        pos++;
        ExprPtr right = parse_logic_and();
        left = Expr::make_binary(op, left, right, left->location);
    }

    return left;
}

ExprPtr Parser::parse_logic_and() {
    ExprPtr left = parse_bit_or();

    while (check(TokenType::LogicalAnd)) {
        std::string op = current().lexeme;
        pos++;
        ExprPtr right = parse_bit_or();
        left = Expr::make_binary(op, left, right, left->location);
    }

    return left;
}

ExprPtr Parser::parse_bit_or() {
    ExprPtr left = parse_bit_xor();

    while (check(TokenType::BitOr)) {
        std::string op = current().lexeme;
        pos++;
        ExprPtr right = parse_bit_xor();
        left = Expr::make_binary(op, left, right, left->location);
    }

    return left;
}

ExprPtr Parser::parse_bit_xor() {
    ExprPtr left = parse_bit_and();

    while (check(TokenType::BitXor)) {
        std::string op = current().lexeme;
        pos++;
        ExprPtr right = parse_bit_and();
        left = Expr::make_binary(op, left, right, left->location);
    }

    return left;
}

ExprPtr Parser::parse_bit_and() {
    ExprPtr left = parse_compare();

    while (check(TokenType::Ampersand)) {
        std::string op = current().lexeme;
        pos++;
        ExprPtr right = parse_compare();
        left = Expr::make_binary(op, left, right, left->location);
    }

    return left;
}

ExprPtr Parser::parse_compare() {
    ExprPtr left = parse_shift();

    if (check(TokenType::Equal) || check(TokenType::NotEqual) ||
        check(TokenType::Less) || check(TokenType::LessEqual) ||
        check(TokenType::Greater) || check(TokenType::GreaterEqual)) {
        std::string op = current().lexeme;
        SourceLocation op_loc = current().location;
        pos++;
        ExprPtr right = parse_shift();

        if (check(TokenType::Equal) || check(TokenType::NotEqual) ||
            check(TokenType::Less) || check(TokenType::LessEqual) ||
            check(TokenType::Greater) || check(TokenType::GreaterEqual)) {
            throw CompileError("Ambiguous chained comparison: use explicit parentheses like (a < b) < c", op_loc);
        }

        return Expr::make_binary(op, left, right, left->location);
    }

    return left;
}

ExprPtr Parser::parse_shift() {
    ExprPtr left = parse_range();

    while (check(TokenType::LeftShift) || check(TokenType::RightShift)) {
        std::string op = current().lexeme;
        pos++;
        ExprPtr right = parse_range();
        left = Expr::make_binary(op, left, right, left->location);
    }

    return left;
}

ExprPtr Parser::parse_range() {
    ExprPtr left = parse_sum();

    if (match(TokenType::DotDot)) {
        ExprPtr right = parse_sum();
        ExprPtr range_expr = Expr::make_range(left, right, left->location);

        // Check for @ / @@ iteration on the range
        bool sorted_iteration = false;
        bool has_iteration = false;
        if (match(TokenType::DoubleAt)) {
            sorted_iteration = true;
            has_iteration = true;
        } else if (match(TokenType::At)) {
            has_iteration = true;
        }
        if (has_iteration) {
            ExprPtr body = parse_expr();
            return Expr::make_iteration(range_expr, body, sorted_iteration, range_expr->location);
        }

        return range_expr;
    }

    return left;
}

ExprPtr Parser::parse_sum() {
    ExprPtr left = parse_prod();

    while (check(TokenType::Plus) || check(TokenType::Minus)) {
        std::string op = current().lexeme;
        pos++;
        ExprPtr right = parse_prod();
        left = Expr::make_binary(op, left, right, left->location);
    }

    return left;
}

ExprPtr Parser::parse_prod() {
    ExprPtr left = parse_unary();

    while (check(TokenType::Star) || check(TokenType::Slash) || check(TokenType::Percent)) {
        std::string op = current().lexeme;
        pos++;
        ExprPtr right = parse_unary();
        left = Expr::make_binary(op, left, right, left->location);
    }

    return left;
}

ExprPtr Parser::parse_unary() {
    SourceLocation loc = current().location;

    if (check(TokenType::Minus) || check(TokenType::LogicalNot) || check(TokenType::BitNot)) {
        std::string op = current().lexeme;
        pos++;
        ExprPtr operand = parse_unary();
        if (op == "-" &&
            operand &&
            operand->kind == Expr::Kind::IntLiteral &&
            !operand->literal_is_unsigned) {
            int64_t val = static_cast<int64_t>(operand->uint_val);
            std::string raw = operand->raw_literal.empty()
                ? std::string("-") + std::to_string(val)
                : std::string("-") + operand->raw_literal;
            return Expr::make_int(-val, loc, raw);
        }
        return Expr::make_unary(op, operand, loc);
    }

    if (match(TokenType::BitOr)) {
        ExprPtr operand = parse_unary();
        consume(TokenType::BitOr, "Expected '|'");
        return Expr::make_length(operand, loc);
    }

    if (match(TokenType::LeftParen)) {
        size_t after_paren = pos;

        // Check for multi-receiver method call syntax (r1, r2).method(...)
        if (check(TokenType::Identifier)) {
            size_t saved = pos;
            std::vector<ExprPtr> receivers;
            receivers.push_back(Expr::make_identifier(consume(TokenType::Identifier, "").lexeme, loc));

            bool is_multi_receiver = false;
            if (match(TokenType::Comma)) {
                is_multi_receiver = true;
                do {
                    receivers.push_back(Expr::make_identifier(consume(TokenType::Identifier, "Expected identifier").lexeme, loc));
                } while (match(TokenType::Comma));
            }

            if (is_multi_receiver && check(TokenType::RightParen)) {
                size_t paren_pos = pos;
                pos++; // skip ')'
                if (match(TokenType::Dot)) {
                    std::string method = consume(TokenType::Identifier, "Expected method name").lexeme;
                    consume(TokenType::LeftParen, "Expected '('");
                    std::vector<ExprPtr> args;
                    if (!check(TokenType::RightParen)) {
                        do {
                            args.push_back(parse_expr());
                        } while (match(TokenType::Comma));
                    }
                    consume(TokenType::RightParen, "Expected ')'");

                    auto func = Expr::make_identifier(method, loc);
                    auto call = Expr::make_call(func, args, loc);
                    call->receivers = receivers;
                    return parse_postfix_suffix(call);
                }
                pos = paren_pos;
            }

            // Not a multi-receiver call, backtrack to immediately after '('
            pos = saved;
        }

        pos = after_paren;

        if (check(TokenType::Hash)) {
            // Cast
            TypePtr type = parse_type();
            consume(TokenType::RightParen, "Expected ')'");
            ExprPtr operand = parse_unary();
            return Expr::make_cast(type, operand, loc);
        }

        ExprPtr expr = parse_expr();

        if (check(TokenType::Comma)) {
            std::vector<ExprPtr> elements;
            elements.push_back(expr);

            while (match(TokenType::Comma)) {
                elements.push_back(parse_expr());
            }
            consume(TokenType::RightParen, "Expected ')'");
            return Expr::make_tuple(elements, loc);
        }

        consume(TokenType::RightParen, "Expected ')'");

        if (match(TokenType::At)) {
            ExprPtr body = parse_expr();
            return parse_postfix_suffix(Expr::make_repeat(expr, body, loc));
        }

        expr->was_parenthesized = true;
        return parse_postfix_suffix(expr);
    }

    return parse_postfix();
}

ExprPtr Parser::parse_postfix() {
    ExprPtr expr = parse_primary();
    return parse_postfix_suffix(expr);
}

ExprPtr Parser::parse_postfix_suffix(ExprPtr expr) {
    while (true) {
        if (match(TokenType::LeftParen)) {
            std::vector<ExprPtr> args;
            if (!check(TokenType::RightParen)) {
                do {
                    args.push_back(parse_expr());
                } while (match(TokenType::Comma));
            }
            consume(TokenType::RightParen, "Expected ')'");
            expr = Expr::make_call(expr, args, expr->location);
        } else if (match(TokenType::LeftBracket)) {
            ExprPtr index = parse_expr();
            consume(TokenType::RightBracket, "Expected ']'");
            expr = Expr::make_index(expr, index, expr->location);
        } else if (match(TokenType::Dot)) {
            std::string member = consume(TokenType::Identifier, "Expected member name").lexeme;

            // Check if this is a method call
            if (check(TokenType::LeftParen)) {
                pos++;
                std::vector<ExprPtr> args;
                if (!check(TokenType::RightParen)) {
                    do {
                        args.push_back(parse_expr());
                    } while (match(TokenType::Comma));
                }
                consume(TokenType::RightParen, "Expected ')'");

                // Create method call
                auto method = Expr::make_identifier(member, expr->location);
                auto call = Expr::make_call(method, args, expr->location);
                call->receivers.push_back(expr);
                expr = call;
            } else {
                expr = Expr::make_member(expr, member, expr->location);
            }
        } else {
            break;
        }
    }

    return expr;
}

ExprPtr Parser::parse_primary() {
    std::vector<Annotation> annotations = parse_annotations();
    SourceLocation loc = current().location;

    if (match(TokenType::DoubleColon)) {
        std::vector<std::string> path = parse_resource_path(true);
        auto e = Expr::make_resource(path, loc);
        e->annotations = annotations;
        return e;
    }

    if (check(TokenType::IntLiteral)) {
        Token t = current();
        pos++;
        if (std::holds_alternative<int64_t>(t.value)) {
            auto e = Expr::make_int(std::get<int64_t>(t.value), loc, t.lexeme);
            e->annotations = annotations;
            return e;
        } else {
            auto e = Expr::make_uint(std::get<uint64_t>(t.value), loc, t.lexeme);
            e->annotations = annotations;
            return e;
        }
    }

    if (check(TokenType::FloatLiteral)) {
        double val = std::get<double>(current().value);
        pos++;
        auto e = Expr::make_float(val, loc, tokens[pos - 1].lexeme);
        e->annotations = annotations;
        return e;
    }

    if (check(TokenType::StringLiteral)) {
        std::string val = std::get<std::string>(current().value);
        pos++;
        auto e = Expr::make_string(val, loc);
        e->annotations = annotations;
        return e;
    }

    if (check(TokenType::CharLiteral)) {
        uint64_t val = std::get<uint64_t>(current().value);
        pos++;
        auto e = Expr::make_char(val, loc, tokens[pos - 1].lexeme);
        e->annotations = annotations;
        return e;
    }

    if (check(TokenType::LeftBrace)) {
        auto e = parse_block();
        e->annotations = annotations;
        return e;
    }

    if (check(TokenType::LeftBracket)) {
        auto e = parse_array();
        e->annotations = annotations;
        return e;
    }

    // Expression parameter reference: $identifier
    if (match(TokenType::Dollar)) {
        std::string name = consume(TokenType::Identifier, "Expected identifier after $").lexeme;
        auto id = Expr::make_identifier(name, loc);
        id->is_expr_param_ref = true;  // Mark as expression parameter reference
        id->annotations = annotations;
        return id;
    }

    if (check(TokenType::Identifier)) {
        std::vector<std::string> path;
        path.push_back(current().lexeme);
        pos++;

        while (match(TokenType::DoubleColon)) {
            path.push_back(consume(TokenType::Identifier, "Expected identifier").lexeme);
        }

        auto id = Expr::make_identifier(path.back(), loc);

        // Check for type annotation (only if followed by #)
        if (check(TokenType::Colon)) {
            size_t colon_pos = pos;
            pos++; // consume colon
            if (check(TokenType::Hash) || check(TokenType::LeftBracket)) {
                // This is a type annotation
                TypePtr type = parse_type();
                id->type = type;
            } else {
                // Not a type annotation, rewind
                pos = colon_pos;
            }
        }

        id->annotations = annotations;
        return id;
    }

    throw CompileError("Unexpected token in expression: " + current().lexeme, loc);
}

ExprPtr Parser::parse_block() {
    SourceLocation loc = current().location;
    consume(TokenType::LeftBrace, "Expected '{'");

    std::vector<StmtPtr> stmts;
    ExprPtr result = nullptr;

    skip_semis();
    while (!check(TokenType::RightBrace) && !check(TokenType::EndOfFile)) {
        // Speculatively parse an expression to see if it's the final result; rollback on failure
        size_t saved = pos;
        ExprPtr expr = nullptr;
        bool parsed_expr = false;

        try {
            expr = parse_expr();
            parsed_expr = true;
        } catch (const CompileError&) {
            parsed_expr = false;
        }

        if (parsed_expr) {
            skip_semis();
            if (check(TokenType::RightBrace)) {
                result = expr;
                break;
            }
            // Not the final expression; rewind and treat parsed tokens as a statement
            pos = saved;
        } else {
            pos = saved;
        }

        stmts.push_back(parse_stmt());
        skip_semis();
    }

    consume(TokenType::RightBrace, "Expected '}'");
    return Expr::make_block(stmts, result, loc);
}

std::vector<std::string> Parser::parse_resource_path(bool leading_colon_already_consumed) {
    if (!leading_colon_already_consumed) {
        consume(TokenType::DoubleColon, "Expected '::'");
    }

    std::vector<std::string> segments;
    auto parse_segment = [&]() {
        std::string segment = consume(TokenType::Identifier, "Expected identifier").lexeme;
        while (match(TokenType::Dot)) {
            segment += "." + consume(TokenType::Identifier, "Expected identifier").lexeme;
        }
        return segment;
    };

    segments.push_back(parse_segment());
    while (match(TokenType::DoubleColon)) {
        segments.push_back(parse_segment());
    }

    return segments;
}

ExprPtr Parser::parse_array() {
    SourceLocation loc = current().location;
    consume(TokenType::LeftBracket, "Expected '['");

    std::vector<ExprPtr> elems;
    if (!check(TokenType::RightBracket)) {
        do {
            elems.push_back(parse_expr());
        } while (match(TokenType::Comma));
    }

    consume(TokenType::RightBracket, "Expected ']'");
    return Expr::make_array(elems, loc);
}

TypePtr Parser::parse_type() {
    SourceLocation loc = current().location;
    ExprPtr leading_size = nullptr;
    if (match(TokenType::LeftBracket)) {
        leading_size = parse_expr();
        consume(TokenType::RightBracket, "Expected ']'");
    }

    consume(TokenType::Hash, "Expected '#'");

    std::string name = consume(TokenType::Identifier, "Expected type name").lexeme;

    // Check for primitive types
    static std::unordered_map<std::string, PrimitiveType> primitives = {
        {"i8", PrimitiveType::I8}, {"i16", PrimitiveType::I16},
        {"i32", PrimitiveType::I32}, {"i64", PrimitiveType::I64},
        {"u8", PrimitiveType::U8}, {"u16", PrimitiveType::U16},
        {"u32", PrimitiveType::U32}, {"u64", PrimitiveType::U64},
        {"f32", PrimitiveType::F32}, {"f64", PrimitiveType::F64},
        {"b", PrimitiveType::Bool}, {"s", PrimitiveType::String}
    };

    TypePtr type;
    auto it = primitives.find(name);
    if (it != primitives.end()) {
        type = Type::make_primitive(it->second, loc);
    } else {
        type = Type::make_named(name, loc);
    }

    ExprPtr size = leading_size;
    if (match(TokenType::LeftBracket)) {
        if (size) {
            throw CompileError("Array size specified twice in type", loc);
        }
        size = parse_expr();
        consume(TokenType::RightBracket, "Expected ']'");
    }

    if (size) {
        return Type::make_array(type, size, loc);
    }

    return type;
}

std::vector<Parameter> Parser::parse_params() {
    std::vector<Parameter> params;

    if (check(TokenType::RightParen)) {
        return params;
    }

    do {
        std::vector<Annotation> annotations = parse_annotations();
        SourceLocation loc = current().location;
        bool is_expr = match(TokenType::Dollar);
        std::string name = consume(TokenType::Identifier, "Expected parameter name").lexeme;
        TypePtr type = nullptr;
        if (match(TokenType::Colon)) {
            type = parse_type();
        }
        params.emplace_back(name, type, is_expr, loc, annotations);
    } while (match(TokenType::Comma));

    return params;
}

std::vector<std::string> Parser::parse_ref_params() {
    std::vector<std::string> refs;
    consume(TokenType::LeftParen, "Expected '('");

    do {
        refs.push_back(consume(TokenType::Identifier, "Expected identifier").lexeme);
    } while (match(TokenType::Comma));

    consume(TokenType::RightParen, "Expected ')'");
    return refs;
}

std::vector<Field> Parser::parse_fields() {
    std::vector<Field> fields;

    if (check(TokenType::RightParen)) {
        return fields;
    }

    do {
        std::vector<Annotation> annotations = parse_annotations();
        SourceLocation loc = current().location;
        std::string name = consume(TokenType::Identifier, "Expected field name").lexeme;
        TypePtr type = nullptr;
        if (match(TokenType::Colon)) {
            type = parse_type();
        }
        fields.emplace_back(name, type, loc, annotations);
    } while (match(TokenType::Comma));

    return fields;
}

std::vector<std::string> Parser::parse_qualified_name() {
    std::vector<std::string> path;
    path.push_back(consume(TokenType::Identifier, "Expected identifier").lexeme);

    while (match(TokenType::DoubleColon)) {
        path.push_back(consume(TokenType::Identifier, "Expected identifier").lexeme);
    }

    return path;
}

}
