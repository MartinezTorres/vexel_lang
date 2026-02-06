#include "ast.h"
#include <sstream>

namespace vexel {

// Type factory methods
TypePtr Type::make_primitive(PrimitiveType p, const SourceLocation& loc) {
    auto t = std::make_shared<Type>();
    t->kind = Kind::Primitive;
    t->primitive = p;
    t->location = loc;
    return t;
}

TypePtr Type::make_array(TypePtr elem, ExprPtr size, const SourceLocation& loc) {
    auto t = std::make_shared<Type>();
    t->kind = Kind::Array;
    t->element_type = elem;
    t->array_size = size;
    t->location = loc;
    return t;
}

TypePtr Type::make_named(const std::string& name, const SourceLocation& loc) {
    auto t = std::make_shared<Type>();
    t->kind = Kind::Named;
    t->type_name = name;
    t->location = loc;
    return t;
}

TypePtr Type::make_typevar(const std::string& name, const SourceLocation& loc) {
    auto t = std::make_shared<Type>();
    t->kind = Kind::TypeVar;
    t->var_name = name;
    t->location = loc;
    return t;
}

std::string Type::to_string() const {
    std::ostringstream os;
    switch (kind) {
        case Kind::Primitive:
            os << primitive_name(primitive);
            break;
        case Kind::Array:
            os << element_type->to_string() << "[...]";
            break;
        case Kind::Named:
            os << type_name;
            break;
        case Kind::TypeVar:
            os << var_name;
            break;
    }
    return os.str();
}

// Expr factory methods
ExprPtr Expr::make_int(int64_t val, const SourceLocation& loc, const std::string& raw) {
    auto e = std::make_shared<Expr>();
    e->kind = Kind::IntLiteral;
    e->uint_val = (uint64_t)val;
    e->literal_is_unsigned = false;
    e->location = loc;
    e->raw_literal = raw;
    return e;
}

ExprPtr Expr::make_uint(uint64_t val, const SourceLocation& loc, const std::string& raw) {
    auto e = std::make_shared<Expr>();
    e->kind = Kind::IntLiteral;
    e->uint_val = val;
    e->literal_is_unsigned = true;
    e->location = loc;
    e->raw_literal = raw;
    return e;
}

ExprPtr Expr::make_float(double val, const SourceLocation& loc, const std::string& raw) {
    auto e = std::make_shared<Expr>();
    e->kind = Kind::FloatLiteral;
    e->float_val = val;
    e->location = loc;
    e->raw_literal = raw;
    return e;
}

ExprPtr Expr::make_char(uint64_t val, const SourceLocation& loc, const std::string& raw) {
    auto e = std::make_shared<Expr>();
    e->kind = Kind::CharLiteral;
    e->uint_val = val;
    e->location = loc;
    e->raw_literal = raw;
    return e;
}

ExprPtr Expr::make_string(const std::string& val, const SourceLocation& loc) {
    auto e = std::make_shared<Expr>();
    e->kind = Kind::StringLiteral;
    e->string_val = val;
    e->location = loc;
    return e;
}

ExprPtr Expr::make_identifier(const std::string& name, const SourceLocation& loc) {
    auto e = std::make_shared<Expr>();
    e->kind = Kind::Identifier;
    e->name = name;
    e->location = loc;
    return e;
}

ExprPtr Expr::make_binary(const std::string& op, ExprPtr l, ExprPtr r, const SourceLocation& loc) {
    auto e = std::make_shared<Expr>();
    e->kind = Kind::Binary;
    e->op = op;
    e->left = l;
    e->right = r;
    e->location = loc;
    return e;
}

ExprPtr Expr::make_unary(const std::string& op, ExprPtr operand, const SourceLocation& loc) {
    auto e = std::make_shared<Expr>();
    e->kind = Kind::Unary;
    e->op = op;
    e->operand = operand;
    e->location = loc;
    return e;
}

ExprPtr Expr::make_call(ExprPtr func, std::vector<ExprPtr> args, const SourceLocation& loc) {
    auto e = std::make_shared<Expr>();
    e->kind = Kind::Call;
    e->operand = func;
    e->args = args;
    e->location = loc;
    return e;
}

ExprPtr Expr::make_index(ExprPtr arr, ExprPtr idx, const SourceLocation& loc) {
    auto e = std::make_shared<Expr>();
    e->kind = Kind::Index;
    e->operand = arr;
    e->args.push_back(idx);
    e->location = loc;
    return e;
}

ExprPtr Expr::make_member(ExprPtr obj, const std::string& field, const SourceLocation& loc) {
    auto e = std::make_shared<Expr>();
    e->kind = Kind::Member;
    e->operand = obj;
    e->name = field;
    e->location = loc;
    return e;
}

ExprPtr Expr::make_array(std::vector<ExprPtr> elems, const SourceLocation& loc) {
    auto e = std::make_shared<Expr>();
    e->kind = Kind::ArrayLiteral;
    e->elements = elems;
    e->location = loc;
    return e;
}

ExprPtr Expr::make_tuple(std::vector<ExprPtr> elems, const SourceLocation& loc) {
    auto e = std::make_shared<Expr>();
    e->kind = Kind::TupleLiteral;
    e->elements = elems;
    e->location = loc;
    return e;
}

ExprPtr Expr::make_block(std::vector<StmtPtr> stmts, ExprPtr result, const SourceLocation& loc) {
    auto e = std::make_shared<Expr>();
    e->kind = Kind::Block;
    e->statements = stmts;
    e->result_expr = result;
    e->location = loc;
    return e;
}

ExprPtr Expr::make_conditional(ExprPtr cond, ExprPtr t, ExprPtr f, const SourceLocation& loc) {
    auto e = std::make_shared<Expr>();
    e->kind = Kind::Conditional;
    e->condition = cond;
    e->true_expr = t;
    e->false_expr = f;
    e->location = loc;
    return e;
}

ExprPtr Expr::make_cast(TypePtr type, ExprPtr expr, const SourceLocation& loc) {
    auto e = std::make_shared<Expr>();
    e->kind = Kind::Cast;
    e->target_type = type;
    e->operand = expr;
    e->location = loc;
    return e;
}

ExprPtr Expr::make_assignment(ExprPtr lhs, ExprPtr rhs, const SourceLocation& loc) {
    auto e = std::make_shared<Expr>();
    e->kind = Kind::Assignment;
    e->left = lhs;
    e->right = rhs;
    e->location = loc;
    return e;
}

ExprPtr Expr::make_range(ExprPtr start, ExprPtr end, const SourceLocation& loc) {
    auto e = std::make_shared<Expr>();
    e->kind = Kind::Range;
    e->left = start;
    e->right = end;
    e->location = loc;
    return e;
}

ExprPtr Expr::make_length(ExprPtr expr, const SourceLocation& loc) {
    auto e = std::make_shared<Expr>();
    e->kind = Kind::Length;
    e->operand = expr;
    e->location = loc;
    return e;
}

ExprPtr Expr::make_iteration(ExprPtr iterable, ExprPtr body, bool sorted, const SourceLocation& loc) {
    auto e = std::make_shared<Expr>();
    e->kind = Kind::Iteration;
    e->operand = iterable;
    e->right = body;
    e->location = loc;
    e->is_sorted_iteration = sorted;
    return e;
}

ExprPtr Expr::make_repeat(ExprPtr cond, ExprPtr body, const SourceLocation& loc) {
    auto e = std::make_shared<Expr>();
    e->kind = Kind::Repeat;
    e->condition = cond;
    e->right = body;
    e->location = loc;
    return e;
}

ExprPtr Expr::make_resource(const std::vector<std::string>& path, const SourceLocation& loc) {
    auto e = std::make_shared<Expr>();
    e->kind = Kind::Resource;
    e->resource_path = path;
    e->location = loc;
    return e;
}

ExprPtr Expr::make_process(const std::string& command, const SourceLocation& loc) {
    auto e = std::make_shared<Expr>();
    e->kind = Kind::Process;
    e->process_command = command;
    e->location = loc;
    return e;
}

// Stmt factory methods
StmtPtr Stmt::make_expr(ExprPtr e, const SourceLocation& loc) {
    auto s = std::make_shared<Stmt>();
    s->kind = Kind::Expr;
    s->expr = e;
    s->location = loc;
    return s;
}

StmtPtr Stmt::make_return(ExprPtr e, const SourceLocation& loc) {
    auto s = std::make_shared<Stmt>();
    s->kind = Kind::Return;
    s->return_expr = e;
    s->location = loc;
    return s;
}

StmtPtr Stmt::make_break(const SourceLocation& loc) {
    auto s = std::make_shared<Stmt>();
    s->kind = Kind::Break;
    s->location = loc;
    return s;
}

StmtPtr Stmt::make_continue(const SourceLocation& loc) {
    auto s = std::make_shared<Stmt>();
    s->kind = Kind::Continue;
    s->location = loc;
    return s;
}

StmtPtr Stmt::make_var(const std::string& name, TypePtr type, ExprPtr init, bool mut, const SourceLocation& loc) {
    auto s = std::make_shared<Stmt>();
    s->kind = Kind::VarDecl;
    s->var_name = name;
    s->var_type = type;
    s->var_init = init;
    s->is_mutable = mut;
    s->location = loc;
    return s;
}

StmtPtr Stmt::make_func(const std::string& name, std::vector<Parameter> params, std::vector<std::string> ref_params,
                       TypePtr ret, ExprPtr body, bool external, bool exported, const SourceLocation& loc, const std::string& type_ns, const std::vector<TypePtr>& ret_types) {
    auto s = std::make_shared<Stmt>();
    s->kind = Kind::FuncDecl;
    s->func_name = name;
    s->type_namespace = type_ns;
    s->params = params;
    s->ref_params = ref_params;
    s->ref_param_types = std::vector<TypePtr>(ref_params.size(), nullptr);
    s->return_type = ret;
    s->return_types = ret_types;
    s->body = body;
    s->is_external = external;
    s->is_exported = exported;
    s->is_generic = false;  // Will be set during type checking
    s->is_instantiation = false;
    s->location = loc;
    return s;
}

StmtPtr Stmt::make_type(const std::string& name, std::vector<Field> fields, const SourceLocation& loc) {
    auto s = std::make_shared<Stmt>();
    s->kind = Kind::TypeDecl;
    s->type_decl_name = name;
    s->fields = fields;
    s->location = loc;
    return s;
}

StmtPtr Stmt::make_import(std::vector<std::string> path, const SourceLocation& loc) {
    auto s = std::make_shared<Stmt>();
    s->kind = Kind::Import;
    s->import_path = path;
    s->location = loc;
    return s;
}

StmtPtr Stmt::make_conditional_stmt(ExprPtr cond, StmtPtr stmt, const SourceLocation& loc) {
    auto s = std::make_shared<Stmt>();
    s->kind = Kind::ConditionalStmt;
    s->condition = cond;
    s->true_stmt = stmt;
    s->location = loc;
    return s;
}

}
