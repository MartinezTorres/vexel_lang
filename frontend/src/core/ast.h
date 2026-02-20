#pragma once
#include "common.h"
#include <variant>

namespace vexel {

struct Symbol;
struct Type;
struct Expr;
struct Stmt;

using TypePtr = std::shared_ptr<Type>;
using ExprPtr = std::shared_ptr<Expr>;
using StmtPtr = std::shared_ptr<Stmt>;

struct Annotation {
    std::string name;
    std::vector<std::string> args;
    SourceLocation location;
};

enum class VarLinkageKind {
    Normal,
    ExternalSymbol,
    BackendBound
};

struct Type {
    enum class Kind { Primitive, Array, Named, TypeVar, TypeOf };
    Kind kind;
    SourceLocation location;
    Symbol* resolved_symbol = nullptr;

    // For Primitive
    PrimitiveType primitive;
    uint64_t integer_bits = 0;
    // For Array
    TypePtr element_type;
    ExprPtr array_size;
    // For Named
    std::string type_name;
    // For TypeVar
    std::string var_name;
    // For TypeOf
    ExprPtr typeof_expr;

    static TypePtr make_primitive(PrimitiveType p,
                                  const SourceLocation& loc = SourceLocation(),
                                  uint64_t int_bits = 0);
    static TypePtr make_array(TypePtr elem, ExprPtr size, const SourceLocation& loc = SourceLocation());
    static TypePtr make_named(const std::string& name, const SourceLocation& loc = SourceLocation());
    static TypePtr make_typevar(const std::string& name, const SourceLocation& loc = SourceLocation());
    static TypePtr make_typeof(ExprPtr expr, const SourceLocation& loc = SourceLocation());

    std::string to_string() const;
};

struct Expr {
    enum class Kind {
        IntLiteral, FloatLiteral, StringLiteral, CharLiteral,
        Identifier, Binary, Unary, Call, Index, Member,
        ArrayLiteral, TupleLiteral, Block, Conditional, Cast, Assignment,
        Range, Length, Iteration, Repeat, Resource, Process
    };
    Kind kind;
    SourceLocation location;
    TypePtr type;
    std::vector<Annotation> annotations;

    // Literals
    uint64_t uint_val;
    double float_val;
    std::string string_val;
    std::string raw_literal;
    bool literal_is_unsigned = false;

    // Identifier
    std::string name;
    bool is_expr_param_ref = false;  // True if this is a $param reference
    bool creates_new_variable = false;  // True if this assignment creates a new variable
    TypePtr declared_var_type;  // For declaration assignments, preserve declared/inferred variable type.
    int scope_instance_id = -1;  // For identifiers: which scope instance the symbol is from (-1 = not imported)
    bool is_mutable_binding = false;  // True if identifier refers to a mutable binding
    Symbol* resolved_symbol = nullptr;

    // Binary/Unary
    std::string op;
    ExprPtr left, right;
    ExprPtr operand;

    // Call
    std::vector<ExprPtr> args;
    std::vector<ExprPtr> receivers;

    // ArrayLiteral
    std::vector<ExprPtr> elements;

    // Block
    std::vector<StmtPtr> statements;
    ExprPtr result_expr;

    bool is_sorted_iteration = false;
    bool was_parenthesized = false;

    // Conditional
    ExprPtr condition, true_expr, false_expr;

    // Loop invariant:
    // - Iteration stores iterable in operand and body in right.
    // - Repeat stores condition in condition and body in right.
    // - left is intentionally unused for loop nodes.

    // Cast
    TypePtr target_type;

    // Resource path segments (for ::foo::bar expressions)
    std::vector<std::string> resource_path;
    std::string process_command;

    static ExprPtr make_int(int64_t val, const SourceLocation& loc = SourceLocation(), const std::string& raw = "");
    static ExprPtr make_uint(uint64_t val, const SourceLocation& loc = SourceLocation(), const std::string& raw = "");
    static ExprPtr make_float(double val, const SourceLocation& loc = SourceLocation(), const std::string& raw = "");
    static ExprPtr make_char(uint64_t val, const SourceLocation& loc = SourceLocation(), const std::string& raw = "");
    static ExprPtr make_string(const std::string& val, const SourceLocation& loc = SourceLocation());
    static ExprPtr make_identifier(const std::string& name, const SourceLocation& loc = SourceLocation());
    static ExprPtr make_binary(const std::string& op, ExprPtr l, ExprPtr r, const SourceLocation& loc = SourceLocation());
    static ExprPtr make_unary(const std::string& op, ExprPtr operand, const SourceLocation& loc = SourceLocation());
    static ExprPtr make_call(ExprPtr func, std::vector<ExprPtr> args, const SourceLocation& loc = SourceLocation());
    static ExprPtr make_index(ExprPtr arr, ExprPtr idx, const SourceLocation& loc = SourceLocation());
    static ExprPtr make_member(ExprPtr obj, const std::string& field, const SourceLocation& loc = SourceLocation());
    static ExprPtr make_array(std::vector<ExprPtr> elems, const SourceLocation& loc = SourceLocation());
    static ExprPtr make_tuple(std::vector<ExprPtr> elems, const SourceLocation& loc = SourceLocation());
    static ExprPtr make_block(std::vector<StmtPtr> stmts, ExprPtr result, const SourceLocation& loc = SourceLocation());
    static ExprPtr make_conditional(ExprPtr cond, ExprPtr t, ExprPtr f, const SourceLocation& loc = SourceLocation());
    static ExprPtr make_cast(TypePtr type, ExprPtr expr, const SourceLocation& loc = SourceLocation());
    static ExprPtr make_assignment(ExprPtr lhs, ExprPtr rhs, const SourceLocation& loc = SourceLocation());
    static ExprPtr make_range(ExprPtr start, ExprPtr end, const SourceLocation& loc = SourceLocation());
    static ExprPtr make_length(ExprPtr expr, const SourceLocation& loc = SourceLocation());
    static ExprPtr make_iteration(ExprPtr iterable, ExprPtr body, bool sorted = false,
                                  const SourceLocation& loc = SourceLocation());
    static ExprPtr make_repeat(ExprPtr cond, ExprPtr body, const SourceLocation& loc = SourceLocation());
    static ExprPtr make_resource(const std::vector<std::string>& path, const SourceLocation& loc = SourceLocation());
    static ExprPtr make_process(const std::string& command, const SourceLocation& loc = SourceLocation());
};

struct Parameter {
    std::string name;
    TypePtr type;
    bool is_expression_param;
    SourceLocation location;
    std::vector<Annotation> annotations;
    Symbol* resolved_symbol = nullptr;

    Parameter(const std::string& n, TypePtr t = nullptr, bool is_expr = false, const SourceLocation& loc = SourceLocation(),
              const std::vector<Annotation>& ann = {})
        : name(n), type(t), is_expression_param(is_expr), location(loc), annotations(ann) {}
};

struct Field {
    std::string name;
    TypePtr type;
    SourceLocation location;
    std::vector<Annotation> annotations;

    Field(const std::string& n, TypePtr t = nullptr, const SourceLocation& loc = SourceLocation(),
          const std::vector<Annotation>& ann = {})
        : name(n), type(t), location(loc), annotations(ann) {}
};

struct Stmt {
    enum class Kind {
        Expr, Return, Break, Continue, VarDecl, FuncDecl,
        TypeDecl, Import, ConditionalStmt
    };
    Kind kind;
    SourceLocation location;
    int scope_instance_id = -1;  // For imported declarations: which scope instance (-1 = not imported)
    std::vector<Annotation> annotations;
    Symbol* resolved_symbol = nullptr;
    std::vector<Symbol*> ref_param_symbols;

    // Expr
    ExprPtr expr;

    // Return/Break/Continue
    ExprPtr return_expr;

    // VarDecl
    std::string var_name;
    TypePtr var_type;
    ExprPtr var_init;
    bool is_mutable;
    VarLinkageKind var_linkage = VarLinkageKind::Normal;

    // FuncDecl
    std::string func_name;
    std::string type_namespace;  // For &(r)Type::method syntax (empty if no namespace)
    std::vector<Parameter> params;
    std::vector<std::string> ref_params;
    std::vector<TypePtr> ref_param_types;  // Inferred types for reference/receiver parameters
    TypePtr return_type;
    std::vector<TypePtr> return_types;  // For tuple returns (empty if single return)
    ExprPtr body;
    bool is_external = false;
    bool is_exported = false;
    bool is_generic = false;  // True if function has type parameters (params without types)
    bool is_instantiation = false;  // True if this is a concrete generic instantiation

    // TypeDecl
    std::string type_decl_name;
    std::vector<Field> fields;

    // Import
    std::vector<std::string> import_path;

    // ConditionalStmt
    ExprPtr condition;
    StmtPtr true_stmt;

    static StmtPtr make_expr(ExprPtr e, const SourceLocation& loc = SourceLocation());
    static StmtPtr make_return(ExprPtr e, const SourceLocation& loc = SourceLocation());
    static StmtPtr make_break(const SourceLocation& loc = SourceLocation());
    static StmtPtr make_continue(const SourceLocation& loc = SourceLocation());
    static StmtPtr make_var(const std::string& name, TypePtr type, ExprPtr init, bool mut,
                            const SourceLocation& loc = SourceLocation(),
                            bool exported = false,
                            VarLinkageKind linkage = VarLinkageKind::Normal);
    static StmtPtr make_func(const std::string& name, std::vector<Parameter> params, std::vector<std::string> ref_params,
                             TypePtr ret, ExprPtr body, bool external, bool exported, const SourceLocation& loc = SourceLocation(),
                             const std::string& type_ns = "", const std::vector<TypePtr>& ret_types = {});
    static StmtPtr make_type(const std::string& name, std::vector<Field> fields, const SourceLocation& loc = SourceLocation());
    static StmtPtr make_import(std::vector<std::string> path, const SourceLocation& loc = SourceLocation());
    static StmtPtr make_conditional_stmt(ExprPtr cond, StmtPtr stmt, const SourceLocation& loc = SourceLocation());
};

struct Module {
    std::string name;
    std::string path;
    std::vector<StmtPtr> top_level;
    // Optional per-top-level source instance IDs used by instance-aware passes.
    // When present, size must match top_level.size().
    std::vector<int> top_level_instance_ids;
    SourceLocation location;
};

}
