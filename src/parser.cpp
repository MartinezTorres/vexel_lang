#include "vexel.hpp"
#include <algorithm>
#include <cctype>
#include <functional>

namespace vx {
namespace {
bool alpha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }
bool digit(char c) { return c >= '0' && c <= '9'; }
int hex(char c) {
    if (digit(c)) return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
class Parser {
    Graph& g;
    Id file;
    std::vector<Id> imports;
    std::vector<Id> tokens; // A transient index of graph-owned lexical nodes.
    std::size_t pos = 0;
    unsigned depth = 0;
    struct Depth {
        Parser& p;
        explicit Depth(Parser& parser) : p(parser) {
            if (++p.depth > 256) p.error("structural nesting limit exceeded",Diagnostic::Exhausted);
        }
        ~Depth() { --p.depth; }
    };
    const Node& tok(std::size_t ahead = 0) const { return g.nodes[tokens.at(std::min(pos + ahead, tokens.size() - 1))]; }
    bool is(const std::string& s) const { return tok().text == s; }
    bool take(const std::string& s) { if (!is(s)) return false; ++pos; return true; }
    void expect(const std::string& s) { if (!take(s)) error("expected '" + s + "', found '" + tok().text + "'"); }
    [[noreturn]] void error(const std::string& s,Diagnostic category=Diagnostic::Invalid) const { throw Error(tok().span, s,category); }
    std::string ident() {
        if (tok().key != "identifier") error("expected identifier");
        return g.nodes[tokens[pos++]].text;
    }
    bool metadataAhead() const {
        if (!is("[") || tok(1).text != "[" || tok(2).key != "identifier") return false;
        Id close=tok().body;
        std::size_t after=pos;
        while (after<tokens.size() && tokens[after]!=close) ++after;
        if (++after>=tokens.size()) return false;
        const auto& next=g.nodes[tokens[after]];
        // A single following '[' is ordinary indexing. Parenthesize an array
        // expression when attaching metadata to it; stacked annotations keep
        // their doubled delimiters.
        if (next.text=="[" && (after+1>=tokens.size() || g.nodes[tokens[after+1]].text!="[")) return false;
        return next.key=="identifier" || next.key=="number" || next.key=="string" || next.key=="character" ||
            next.text=="(" || next.text=="[" || next.text=="{" || next.text=="#" || next.text=="$" ||
            next.text=="&" || next.text=="^" || next.text=="!" || next.text=="::" || next.text=="?";
    }
    std::vector<Metadata> metadata() {
        std::vector<Metadata> result;
        while (metadataAhead()) {
            std::size_t begin=tok().span.begin;
            Metadata item;item.span=tok().span;
            expect("[");expect("[");item.name=ident();
            if (take("(")) {
                if (!is(")")) do { item.arguments.push_back(expr()); } while (take(","));
                expect(")");
            }
            expect("]");expect("]");
            item.span.end=g.nodes[tokens[pos-1]].span.end;
            item.text=g.files[file].bytes.substr(begin,item.span.end-begin);
            result.push_back(std::move(item));
        }
        return result;
    }
    void metadata(Id id,std::vector<Metadata> values) {
        auto& target=g.nodes[id].metadata;
        std::vector<std::string> text;for(const auto& value:values) text.push_back(value.text);
        target.insert(target.begin(),text.begin(),text.end());
        auto& attributes=g.nodes[id].attributes;
        attributes.insert(attributes.begin(),values.begin(),values.end());
    }
    Id add(Syntax kind, Span span, std::string text = {}, std::vector<Id> children = {}) {
        Node n; n.syntax = kind; n.span = span; n.text = std::move(text); n.children = std::move(children);
        if (pos) n.span.end = g.nodes[tokens[pos - 1]].span.end;
        n.identity = g.files[file].path.string() + "@" + std::to_string(n.span.begin) +
            ":" + std::to_string(n.span.end) + ":" + std::to_string(static_cast<int>(kind));
        return g.add(std::move(n));
    }
    void lex() {
        const auto& s = g.files[file].bytes;
        std::vector<std::pair<char, Id>> delimiters;
        for (std::size_t i = 0; i < s.size();) {
            const auto begin = i;
            Node n; n.syntax = Syntax::Lexeme;
            if (static_cast<unsigned char>(s[i]) >= 128) throw Error({file, i, i+1}, "source must be ASCII");
            if (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n') {
                n.syntax = Syntax::Trivia;
                while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n')) ++i;
            } else if (s.compare(i, 2, "//") == 0) {
                n.syntax = Syntax::Trivia;
                while (i < s.size() && s[i] != '\n') {
                    if (static_cast<unsigned char>(s[i]) >= 128) throw Error({file, i, i+1}, "source must be ASCII");
                    ++i;
                }
            } else if (alpha(s[i])) {
                n.key = "identifier";
                while (i < s.size() && (alpha(s[i]) || digit(s[i]))) ++i;
            } else if (digit(s[i])) {
                n.key = "number"; ++i;
                if (s[begin] == '0' && i < s.size() && (s[i] == 'x' || s[i] == 'X')) {
                    ++i; const auto start = i;
                    while (i < s.size() && hex(s[i]) >= 0) ++i;
                    if (i == start) throw Error({file, begin, i}, "hex literal requires digits");
                } else {
                    while (i < s.size() && digit(s[i])) ++i;
                    if (i+1 < s.size() && s[i]=='.' && digit(s[i+1])) {
                        ++i; while (i<s.size() && digit(s[i])) ++i;
                        if (i<s.size() && (s[i]=='e' || s[i]=='E')) {
                            ++i; if (i<s.size() && (s[i]=='+' || s[i]=='-')) ++i;
                            const auto exponent=i;
                            while (i<s.size() && digit(s[i])) ++i;
                            if (i==exponent) throw Error({file,begin,i},"floating exponent requires decimal digits");
                        }
                    }
                }
                if (i < s.size() && (alpha(s[i]) || (s[i] == '.' && i + 1 < s.size() && digit(s[i+1]))))
                    throw Error({file, begin, i+1}, "invalid numeric literal");
            } else if (s[i] == '"' || s[i] == '\'') {
                const char end = s[i++]; n.key = end == '"' ? "string" : "character";
                std::string value;
                while (i < s.size() && s[i] != end) {
                    unsigned char c = static_cast<unsigned char>(s[i++]);
                    if (c >= 128 || c == '\n' || c == '\r') throw Error({file, begin, i}, "invalid character in literal");
                    if (c == '\\') {
                        if (i == s.size()) throw Error({file, begin, i}, "unterminated escape");
                        const char e = s[i++];
                        if (e == 'n') c = '\n';
                        else if (e == 'r') c = '\r';
                        else if (e == 't') c = '\t';
                        else if (e == '\\' || e == '"' || e == '\'') c = static_cast<unsigned char>(e);
                        else if (e == 'x') {
                            if (i + 1 >= s.size() || hex(s[i]) < 0 || hex(s[i+1]) < 0)
                                throw Error({file, begin, i}, "expected two hex digits in escape");
                            c = static_cast<unsigned char>(hex(s[i]) * 16 + hex(s[i+1])); i += 2;
                        } else if (e >= '0' && e <= '7') {
                            unsigned v = static_cast<unsigned>(e - '0');
                            for (unsigned count = 1; count < 3 && i < s.size() && s[i] >= '0' && s[i] <= '7'; ++count)
                                v = v * 8 + static_cast<unsigned>(s[i++] - '0');
                            if (v > 255) throw Error({file, begin, i}, "octal escape exceeds a byte");
                            c = static_cast<unsigned char>(v);
                        } else throw Error({file, begin, i}, "unknown escape");
                    }
                    value += static_cast<char>(c);
                }
                if (i == s.size()) throw Error({file, begin, i}, "unterminated literal");
                ++i;
                if (end == '\'' && value.size() != 1) throw Error({file, begin, i}, "character literal must contain one byte");
                n.text = std::move(value);
            } else {
                static const std::vector<std::string> ops = {
                    ".<<=", ".>>=", ".&&=", ".||=", ".+=", ".-=", ".*=", "./=", ".%=", ".&=", ".|=", ".^=",
                    ".==", ".!=", ".<=", ".>=", ".<<", ".>>", ".&&", ".||", ".+", ".-", ".*", "./", ".%", ".&", ".|", ".^", ".<", ".>",
                    "->|", "->>", "<<=", ">>=", "&&=", "||=", "::", "->", "@@", "!@",
                    "==", "!=", "<=", ">=", "<<", ">>", "&&", "||", "+=", "-=", "*=", "/=", "%=",
                    "&=", "|=", "^=", ".."
                };
                bool found = false;
                for (const auto& op : ops) {
                    if (s.compare(i, op.size(), op) == 0) { i += op.size(); found = true; break; }
                }
                if (!found) {
                    if (std::string("&^!#~+-*/%|<>=?:;,(){}[].@$").find(s[i]) == std::string::npos)
                        throw Error({file, i, i+1}, "invalid source character");
                    ++i;
                }
            }
            if (n.key != "string" && n.key != "character") n.text = s.substr(begin, i-begin);
            n.span = {file, begin, i};
            Id id = g.add(std::move(n));
            g.nodes[g.files[file].node].children.push_back(id);
            if (g.nodes[id].syntax == Syntax::Trivia) continue;
            tokens.push_back(id);
            if (g.nodes[id].key.empty() && i == begin + 1) {
                char c = s[begin];
                if (c == '(' || c == '[' || c == '{') delimiters.emplace_back(c, id);
                else if (c == ')' || c == ']' || c == '}') {
                    if (delimiters.empty() || std::string("([{").find(delimiters.back().first) != std::string(")]}").find(c))
                        throw Error({file, begin, i}, "mismatched delimiter");
                    g.nodes[id].body = delimiters.back().second;
                    g.nodes[delimiters.back().second].body = id; delimiters.pop_back();
                }
            }
        }
        if (!delimiters.empty()) throw Error(g.nodes[delimiters.back().second].span, "unclosed delimiter");
        Node eof; eof.syntax = Syntax::Lexeme; eof.text = "<eof>"; eof.span = {file, s.size(), s.size()};
        tokens.push_back(g.add(std::move(eof)));
    }
    Id type() {
        Depth guard(*this);
        if (is("(")) {
            Span start = tok().span; expect("("); std::vector<Id> fields;
            do { fields.push_back(type()); } while (take(","));
            expect(")"); return add(Syntax::Type, start, "(tuple)", std::move(fields));
        }
        Span start = tok().span; expect("#");
        Id query = none;
        std::string name;
        if (take("[")) { query = expr(); expect("]"); }
        else {
            name = ident();
            if (take(".")) {
                if (tok().key != "number" || tok().text.find_first_not_of("0123456789") != std::string::npos)
                    error("fixed-point fractional width requires a decimal integer");
                name += "." + tok().text; ++pos;
            }
        }
        std::vector<Id> dims;
        while (take("[")) { dims.push_back(expr()); expect("]"); }
        Id id = add(Syntax::Type, start, name, std::move(dims));
        g.nodes[id].body = query; return id;
    }
    Id recordDeclaration() {
        Span start = tok().span; expect("#"); std::string name = ident(); expect("(");
        std::vector<Id> fields; std::set<std::string> names;
        if (!is(")")) do {
            auto tags=metadata();
            Span at = tok().span; std::string field = ident();
            if (!names.insert(field).second) error("duplicate record field: " + field);
            Id annotation = take(":") ? type() : none;
            Id p = add(Syntax::Parameter, at, field); g.nodes[p].annotation = annotation; metadata(p,std::move(tags)); fields.push_back(p);
        } while (take(","));
        expect(")"); expect(";"); return add(Syntax::RecordDecl, start, name, std::move(fields));
    }
    Id functionDeclaration(Span start) {
        expect("&");
        bool exported = take("^"), external = !exported && take("!"), noinline = !exported && !external && take("~");
        std::vector<Id> params; std::set<std::string> seen;
        std::size_t receivers = 0;
        bool method = take("(");
        if (method) {
            if (is(")")) error("method needs a receiver");
            do {
                auto tags=metadata();
                Span at = tok().span; std::string name = ident();
                if (!seen.insert(name).second) error("duplicate receiver: " + name);
                Id annotation = take(":") ? type() : none;
                Id param = add(Syntax::Parameter,at,name); g.nodes[param].annotation = annotation; metadata(param,std::move(tags)); params.push_back(param);
            } while (take(","));
            expect(")"); receivers = params.size();
            if (is("#")) {
                if (receivers != 1) error("a named method namespace requires one receiver");
                Id target = type(); expect("::");
                if (g.nodes[params[0]].annotation == none) g.nodes[params[0]].annotation = target;
            }
            if (exported || external) error("receiver methods are internal");
        }
        std::string name;
        if (method && tok().key != "identifier") {
            name = tok().text;
            if (!precedence(name) && name != "@" && name != "@@" && name != "!" && name != "~") error("expected method name or operator");
            ++pos;
        } else name = ident();
        if (method) name = "." + name;
        expect("(");
        if (!is(")")) do {
            auto tags=metadata();
            Span at = tok().span; bool expression = take("$");
            std::string param = (expression ? "$" : "") + ident();
            if (!seen.insert(param).second) error("duplicate parameter: " + param);
            Id annotation = take(":") ? type() : none;
            Id p = add(Syntax::Parameter,at,param); g.nodes[p].annotation = annotation;
            metadata(p,std::move(tags));
            g.nodes[p].expression = expression; params.push_back(p);
        } while (take(","));
        expect(")");
        Id result = take("->") ? type() : none;
        Id body = none;
        if (external) expect(";"); else body = block();
        Id fn = add(Syntax::Function,start,name,std::move(params));
        auto& n = g.nodes[fn]; n.annotation = result; n.body = body; n.receiverCount = receivers;
        n.exported = exported; n.external = external; n.noinline = noinline;
        for (Id param : n.children) if (g.nodes[param].expression && (exported || external || noinline))
            error("expression parameters require an inline internal function");
        if ((name == ".@" || name == ".@@") &&
            (receivers != 1 || n.children.size() != 2 || !g.nodes[n.children[1]].expression))
            error("iteration methods require one receiver and one expression parameter");
        if (method && name!=".@" && name!=".@@" && !alpha(name.at(1))) {
            std::string op=name.substr(1);
            bool unaryOnly=op=="!" || op=="~";
            bool either=op=="+" || op=="-" || op=="|";
            if (receivers!=1 || n.children.size()<(unaryOnly || either ? 1U : 2U) || n.children.size()>(unaryOnly ? 1U : 2U))
                error("operator methods require one receiver and the operator's value-argument arity");
            for (Id param:n.children) if(g.nodes[param].expression) error("operator methods cannot declare expression parameters");
        }
        return fn;
    }
    Id constructor(Span start, Id target) {
        expect("("); std::vector<Id> values;
        if (!is(")")) do { values.push_back(expr()); } while (take(","));
        expect(")");
        Id id = add(Syntax::Construct, start, g.nodes[target].text, std::move(values));
        g.nodes[id].annotation = target; return id;
    }
    static int precedence(const std::string& op) {
        if (op.size() > 1 && op[0] == '.' && op != "..") return precedence(op.substr(1));
        if (op == "=" || op == "+=" || op == "-=" || op == "*=" || op == "/=" || op == "%=" ||
            op == "&=" || op == "|=" || op == "^=" || op == "<<=" || op == ">>=" || op == "&&=" || op == "||=") return 1;
        if (op == "?" ) return 2;
        if (op == "||") return 3;
        if (op == "&&") return 4;
        if (op == "|") return 5;
        if (op == "^") return 6;
        if (op == "&") return 7;
        if (op == "==" || op == "!=" || op == "<" || op == ">" || op == "<=" || op == ">=") return 8;
        if (op == "<<" || op == ">>") return 9;
        if (op == "..") return 10;
        if (op == "+" || op == "-") return 11;
        if (op == "*" || op == "/" || op == "%") return 12;
        return 0;
    }
    Id primary(bool stopBar = false) {
        Depth guard(*this);
        if (metadataAhead()) {
            auto tags=metadata(); Id expression=primary(stopBar); metadata(expression,std::move(tags)); return expression;
        }
        Span start = tok().span;
        Id id;
        if (is("-") || is("+") || is("!") || is("~")) {
            std::string op = g.nodes[tokens[pos++]].text;
            return add(Syntax::Unary, start, op, {primary(stopBar)});
        }
        if (take("|")) {
            Id value = expr(1, true); expect("|");
            return add(Syntax::Unary, start, "|", {value});
        }
        if (take("?")) {
            if (is("{")) id = add(Syntax::Solve, start, "", {block()});
            else if (tok().key == "identifier") {
                // A function capability has the same signature-only rules as
                // a method probe, with no receiver. It does not call the body.
                std::string name = ident(); expect("(");
                std::vector<Id> arguments;
                if (!is(")")) do { arguments.push_back(expr()); } while (take(","));
                expect(")"); id = add(Syntax::MethodProbe, start, name, std::move(arguments));
            }
            else {
                expect("(");
                Id annotation = none;
                if (is("#")) { annotation = type(); expect(","); }
                Id value = expr(); expect(")");
                id = add(annotation == none ? Syntax::Require : Syntax::Choice, start, "", {value});
                g.nodes[id].annotation = annotation;
            }
        } else if (take("(")) {
            if (is("#") || is("(")) {
                const auto startPosition=pos, startNodes=g.nodes.size(), startImports=imports.size();
                Id target=none;bool cast=false;
                try {target=type();cast=take(")");}
                catch(const Error& error) {
                    if(error.category==Diagnostic::Exhausted) throw;
                }
                if (cast) return add(Syntax::Cast, start, "", {target, primary(stopBar)});
                // A constructor can begin a complete grouped expression, e.g.
                // (#Pair(1,2).left + 3), rather than only a bare constructor.
                pos=startPosition;g.nodes.resize(startNodes);imports.resize(startImports);
            }
            Id value = expr();
            if (take(",")) {
                std::vector<Id> fields{value};
                do { fields.push_back(expr()); } while (take(","));
                expect(")"); id = add(Syntax::Tuple, start, "", std::move(fields));
            } else { expect(")"); id = add(Syntax::Group, start, "", {value}); }
        } else if (is("#")) {
            Id target = type(); id = constructor(start, target);
        } else if (is("{")) id = block();
        else if (take("[")) {
            std::vector<Id> values;
            if (!is("]")) { do { values.push_back(expr()); } while (take(",")); }
            expect("]"); id = add(Syntax::Array, start, "", std::move(values));
        } else if (take("::")) {
            std::string path = ident();
            while (is("::") || is(".")) {
                bool slash = take("::"); if (!slash) expect(".");
                path += slash ? "/" : "."; path += ident();
            }
            id = add(Syntax::Resource, start, path);
        } else if (tok().key == "number" || tok().key == "string" || tok().key == "character") {
            const Node token = g.nodes[tokens[pos++]];
            id = add(token.key == "number" ? Syntax::Number : token.key == "string" ? Syntax::String : Syntax::Character,
                     start, token.text);
        } else if (take("$")) id = add(Syntax::Name, start, "$" + ident());
        else if (tok().key == "identifier") {
            std::string name=ident(),qualified=name;
            while (take("::")) { name=ident();qualified+="::"+name; }
            id=add(Syntax::Name,start,name);
            if (qualified!=name) g.nodes[id].key="qualified:"+qualified;
        }
        else error("expected expression; this syntax is not supported");
        while (true) {
            if (take("(")) {
                const Node callee = g.nodes[id];
                std::vector<Id> args; std::size_t receivers = 0;
                std::string name = callee.text;
                bool probe = callee.syntax == Syntax::Probe;
                if (callee.syntax == Syntax::Member || probe) {
                    name = "." + name;
                    Id receiver = callee.children[0];
                    if (g.nodes[receiver].syntax == Syntax::Tuple) args = g.nodes[receiver].children;
                    else args.push_back(receiver);
                    receivers = args.size();
                } else if (callee.syntax != Syntax::Name) error("calls require a named function or method");
                if (!is(")")) do { args.push_back(expr()); } while (take(","));
                expect(")"); id = add(probe ? Syntax::MethodProbe : Syntax::Call,start,name,std::move(args));
                g.nodes[id].receiverCount = receivers;
            } else if (take("[")) {
                Id index = expr(); expect("]"); id = add(Syntax::Index, start, "", {id, index});
            } else if (take(".")) {
                bool probe = take("?");
                id = add(probe ? Syntax::Probe : Syntax::Member, start, ident(), {id});
            } else break;
        }
        return id;
    }
    Id expr(int minimum = 1, bool stopBar = false) {
        Depth guard(*this);
        Span start = tok().span;
        Id lhs = primary(stopBar);
        while (true) {
            std::string op = tok().text;
            if (stopBar && op == "|") break;
            int p = precedence(op);
            if (!p || p < minimum) break;
            ++pos;
            if (op == "?") {
                Id yes = expr(3, stopBar);
                std::vector<Id> arms{lhs, yes};
                if (take(":")) arms.push_back(expr(3, stopBar));
                lhs = add(Syntax::Select, start, "", std::move(arms));
                if (is("?")) error("nested conditionals require parentheses");
            } else {
                Id rhs = expr(p == 1 ? p : p + 1, stopBar);
                if (p == 1 && g.nodes[lhs].syntax != Syntax::Name && g.nodes[lhs].syntax != Syntax::Index && g.nodes[lhs].syntax != Syntax::Member)
                    error("assignment requires a local variable, array element or field");
                lhs = add(p == 1 ? Syntax::Assign : Syntax::Binary, start, op, {lhs, rhs});
                if (p == 8 && precedence(tok().text) == 8) error("chained comparisons require parentheses");
            }
        }
        return lhs;
    }
    Id expressionStatement(Span at) {
        Id statement=expr();
        if (is("@") || is("@@") || is("!@")) {
            std::string op = g.nodes[tokens[pos++]].text;
            bool repeat = g.nodes[statement].syntax == Syntax::Group;
            if (repeat && op == "@@") error("sorted iteration requires an array, not a repeat condition");
            Id body = expr();
            statement = add(op == "!@" ? Syntax::Loop : repeat ? Syntax::Repeat : Syntax::Iterate,
                            at, repeat ? "condition" : op, {statement, body});
        }
        g.nodes[statement].terminated=take(";");return statement;
    }
    Id block() {
        Depth guard(*this);
        Span start = tok().span; expect("{");
        std::vector<Id> statements;
        while (!is("}")) {
            if (is("<eof>")) error("expected '}'");
            auto tags=metadata();
            const Span at = tok().span;
            Id statement;
            if (is("#") && tok(1).text == "{") {
                error("#{ ... } semantic rollback was removed; use ?function(args), receiver.?method(args), or receiver.?member with an ordinary conditional");
            }
            else if (is("!") && tok(1).text=="!") statement = storageDeclaration(true);
            else if (is("::") && (tok(1).key == "string" || importAhead())) statement = importDeclaration();
            else if (is("#")) {
                const auto startPosition=pos, startNodes=g.nodes.size(), startImports=imports.size();
                try {statement=recordDeclaration();}
                catch(const Error& error) {
                    const std::string message=error.what();
                    if(message.starts_with("duplicate record field") || error.category==Diagnostic::Exhausted) throw;
                    pos=startPosition;g.nodes.resize(startNodes);imports.resize(startImports);
                    statement=expressionStatement(at);
                }
            }
            else if (is("&")) statement = functionDeclaration(at);
            else if (is("->") || is("->|") || is("->>")) {
                std::string token = tok().text; ++pos;
                std::vector<Id> values;
                std::string completion = token == "->|" ? "break" : token == "->>" ? "continue" : "return";
                if (completion == "return" && !is(";")) values.push_back(expr());
                expect(";"); statement = add(Syntax::Return, at, completion, std::move(values));
            } else if (tok().key == "identifier" && tok(1).text == ",") {
                std::vector<Id> targets;
                do { Span name = tok().span; targets.push_back(add(Syntax::Name,name,ident())); } while (take(","));
                expect("="); targets.push_back(expr());
                statement = add(Syntax::Destructure,at,"",std::move(targets));
                g.nodes[statement].terminated = take(";");
            } else if (tok().key == "identifier" && tok(1).text == ":") {
                std::string name = ident(); expect(":"); Id annotation = type();
                std::vector<Id> values;
                if (take("=")) values.push_back(expr());
                statement = add(Syntax::Declare, at, name, std::move(values));
                g.nodes[statement].annotation = annotation;
                g.nodes[statement].terminated = take(";");
            } else statement=expressionStatement(at);
            metadata(statement,std::move(tags));statements.push_back(statement);
        }
        expect("}"); return add(Syntax::Block, start, "", std::move(statements));
    }
    bool importAhead() const {
        std::size_t offset=1;
        if (tok(offset).key!="identifier") return false;
        ++offset;
        while (tok(offset).text=="::" && tok(offset+1).key=="identifier") offset+=2;
        return tok(offset).text==";";
    }
    Id storageDeclaration(bool local) {
        Span start=tok().span;expect("!");bool bound=take("!");
        if (local && !bound) error("external symbol globals are top-level only");
        std::string name=ident();expect(":");Id annotation=type();expect(";");
        Id declaration=add(Syntax::Global,start,name);
        g.nodes[declaration].annotation=annotation;g.nodes[declaration].external=true;
        g.nodes[declaration].key=bound ? "backend-binding" : "external-binding";
        return declaration;
    }
    Id importDeclaration() {
        Span start=tok().span; expect("::");
        if (tok().key=="string") {
            std::string command=tok().text;++pos;expect("->");std::string name=ident();expect(";");
            Id resource=add(Syntax::Resource,start,command);g.nodes[resource].key="process";
            Id declaration=add(Syntax::Global,start,name,{resource});g.nodes[declaration].key="process-binding";
            return declaration;
        }
        std::string path=ident();
        while(take("::")) path+="/"+ident();
        expect(";"); Id declaration=add(Syntax::Import,start,path+".vx"); imports.push_back(declaration);
        return declaration;
    }
public:
    Parser(Graph& graph, Id f) : g(graph), file(f) { lex(); }
    std::vector<Id> parse() {
        std::vector<Id> declarations;
        while (!is("<eof>")) {
            auto tags=metadata();
            Span start = tok().span;
            Id declaration=none;
            if (is("#")) {
                declaration=recordDeclaration();
            } else if (is("&")) {
                declaration=functionDeclaration(start);
            } else if (is("::")) {
                declaration=importDeclaration();
            } else if (is("!")) {
                declaration=storageDeclaration(false);
            } else {
                bool exported = take("^");
                std::string name = ident();
                Id annotation = take(":") ? type() : none;
                std::vector<Id> values;
                if (take("=")) values.push_back(expr());
                else if (annotation==none || exported) error("an uninitialized global requires a type and cannot be exported");
                take(";");
                Id decl = add(Syntax::Global, start, name, std::move(values));
                declaration=decl;
                g.nodes[decl].annotation = annotation; g.nodes[decl].exported = exported;
            }
            metadata(declaration,std::move(tags));
            declarations.push_back(declaration);
        }
        Id body=add(Syntax::Block,{file,0,g.files[file].bytes.size()},"",std::move(declarations));
        g.nodes[g.files[file].node].body=body;
        return imports;
    }
};
}
void loadProgram(Graph& g, const std::filesystem::path& path) {
    g.project = std::filesystem::absolute(path).parent_path();
    std::set<Id> parsed;
    std::function<Id(const std::filesystem::path&, Span, bool)> load = [&](const auto& p, Span requester, bool bundledMath) -> Id {
        Id file = g.read(p, true, requester);
        if (!parsed.insert(file).second) return g.nodes[g.files[file].node].body;
        Parser parser(g, file);
        const auto imports=parser.parse();
        if(bundledMath) for(Id declaration:g.nodes[g.nodes[g.files[file].node].body].children)
            if(g.nodes[declaration].syntax==Syntax::Function) g.nodes[declaration].key="bundled-math";
        for (Id import : imports) {
            auto& n=g.nodes[import]; auto source=g.resolve(n.text,n.span);
            std::error_code error;
            bool regular=std::filesystem::is_regular_file(source,error);
            g.inputSnapshots->emplace("source-kind:"+std::filesystem::absolute(source).lexically_normal().string(),regular ? "file" : "other");
            if (!regular)
                n.typeError=Error(n.span,"cannot resolve import: "+n.text,Diagnostic::ImportResolution);
            else n.body=load(source,n.span,n.text=="std/math.vx" && !std::filesystem::exists(g.project/n.text,error));
        }
        return g.nodes[g.files[file].node].body;
    };
    Id root=load(path, {}, false);
    for (Id id:moduleDeclarations(g,root)) {
        const Node& n=g.nodes[id];
        if (n.syntax==Syntax::RecordDecl) {
            if (!g.types.emplace(n.text,id).second) throw Error(n.span,"duplicate record type: "+n.text);
        } else if (n.syntax==Syntax::Function || n.syntax==Syntax::Global) {
            auto& existing=g.symbols[n.text];
            for (Id other:existing) {
                const auto& o=g.nodes[other];
                if (n.syntax!=Syntax::Function || o.syntax!=Syntax::Function || n.exported || o.exported || n.external || o.external)
                    throw Error(n.span,"duplicate symbol: "+n.text);
            }
            existing.push_back(id);
        }
    }
}
std::vector<Id> moduleDeclarations(const Graph& g,Id block) {
    std::vector<Id> result; std::set<Id> modules,seen;
    std::function<void(Id)> collect=[&](Id body) {
        if (!modules.insert(body).second) return;
        for (Id child:g.nodes[body].children) {
            const auto& n=g.nodes[child];
            if (n.syntax==Syntax::Import) {
                if (n.typeError) throw *n.typeError;
                collect(n.body);
            } else if (seen.insert(child).second) result.push_back(child);
        }
    };
    collect(block);return result;
}
} // namespace vx
