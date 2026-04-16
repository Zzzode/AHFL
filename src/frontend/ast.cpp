#include "ahfl/frontend/ast.hpp"

#include <sstream>
#include <utility>

namespace ahfl::ast {

namespace {

std::string with_name(std::string_view prefix, const std::string &value) {
    std::string text(prefix);
    text += value;
    return text;
}

std::string with_count(std::string prefix, std::size_t count, std::string_view noun) {
    std::ostringstream builder;
    builder << prefix << " (" << count << " " << noun;
    builder << (count == 1 ? ")" : "s)");
    return builder.str();
}

std::string join_segments(const std::vector<std::string> &segments) {
    std::string text;

    for (std::size_t index = 0; index < segments.size(); ++index) {
        if (index != 0) {
            text += "::";
        }

        text += segments[index];
    }

    return text;
}

} // namespace

std::string_view to_string(NodeKind kind) noexcept {
    switch (kind) {
    case NodeKind::Program:
        return "Program";
    case NodeKind::ModuleDecl:
        return "ModuleDecl";
    case NodeKind::ImportDecl:
        return "ImportDecl";
    case NodeKind::ConstDecl:
        return "ConstDecl";
    case NodeKind::TypeAliasDecl:
        return "TypeAliasDecl";
    case NodeKind::StructDecl:
        return "StructDecl";
    case NodeKind::EnumDecl:
        return "EnumDecl";
    case NodeKind::CapabilityDecl:
        return "CapabilityDecl";
    case NodeKind::PredicateDecl:
        return "PredicateDecl";
    case NodeKind::AgentDecl:
        return "AgentDecl";
    case NodeKind::ContractDecl:
        return "ContractDecl";
    case NodeKind::FlowDecl:
        return "FlowDecl";
    case NodeKind::WorkflowDecl:
        return "WorkflowDecl";
    }

    return "Unknown";
}

std::string_view to_string(ContractClauseKind kind) noexcept {
    switch (kind) {
    case ContractClauseKind::Requires:
        return "requires";
    case ContractClauseKind::Ensures:
        return "ensures";
    case ContractClauseKind::Invariant:
        return "invariant";
    case ContractClauseKind::Forbid:
        return "forbid";
    }

    return "unknown";
}

std::string_view to_string(TypeSyntaxKind kind) noexcept {
    switch (kind) {
    case TypeSyntaxKind::Unit:
        return "Unit";
    case TypeSyntaxKind::Bool:
        return "Bool";
    case TypeSyntaxKind::Int:
        return "Int";
    case TypeSyntaxKind::Float:
        return "Float";
    case TypeSyntaxKind::String:
        return "String";
    case TypeSyntaxKind::BoundedString:
        return "BoundedString";
    case TypeSyntaxKind::UUID:
        return "UUID";
    case TypeSyntaxKind::Timestamp:
        return "Timestamp";
    case TypeSyntaxKind::Duration:
        return "Duration";
    case TypeSyntaxKind::Decimal:
        return "Decimal";
    case TypeSyntaxKind::Named:
        return "Named";
    case TypeSyntaxKind::Optional:
        return "Optional";
    case TypeSyntaxKind::List:
        return "List";
    case TypeSyntaxKind::Set:
        return "Set";
    case TypeSyntaxKind::Map:
        return "Map";
    }

    return "Unknown";
}

std::string QualifiedName::spelling() const {
    return join_segments(segments);
}

std::string PathSyntax::spelling() const {
    std::string text = root_name;

    for (const auto &member : members) {
        text += ".";
        text += member;
    }

    return text;
}

std::string TypeSyntax::spelling() const {
    switch (kind) {
    case TypeSyntaxKind::Unit:
    case TypeSyntaxKind::Bool:
    case TypeSyntaxKind::Int:
    case TypeSyntaxKind::Float:
    case TypeSyntaxKind::String:
    case TypeSyntaxKind::UUID:
    case TypeSyntaxKind::Timestamp:
    case TypeSyntaxKind::Duration:
        return std::string(to_string(kind));
    case TypeSyntaxKind::BoundedString: {
        std::ostringstream builder;
        builder << "String(" << string_bounds->first << ", " << string_bounds->second << ")";
        return builder.str();
    }
    case TypeSyntaxKind::Decimal: {
        std::ostringstream builder;
        builder << "Decimal(" << *decimal_scale << ")";
        return builder.str();
    }
    case TypeSyntaxKind::Named:
        return name->spelling();
    case TypeSyntaxKind::Optional:
        return "Optional<" + first->spelling() + ">";
    case TypeSyntaxKind::List:
        return "List<" + first->spelling() + ">";
    case TypeSyntaxKind::Set:
        return "Set<" + first->spelling() + ">";
    case TypeSyntaxKind::Map:
        return "Map<" + first->spelling() + ", " + second->spelling() + ">";
    }

    return "<invalid-type>";
}

Node::Node(NodeKind kind, ahfl::SourceRange range) : kind(kind), range(range) {}

Decl::Decl(NodeKind kind, std::string raw_text, ahfl::SourceRange range)
    : Node(kind, range), raw_text(std::move(raw_text)) {}

Program::Program(std::string source_name, ahfl::SourceRange range)
    : Node(NodeKind::Program, range), source_name(std::move(source_name)) {}

void Program::accept(Visitor &visitor) {
    visitor.visit(*this);
}

ModuleDecl::ModuleDecl(Owned<QualifiedName> name, std::string raw_text, ahfl::SourceRange range)
    : Decl(NodeKind::ModuleDecl, std::move(raw_text), range), name(std::move(name)) {}

void ModuleDecl::accept(Visitor &visitor) {
    visitor.visit(*this);
}

std::string ModuleDecl::headline() const {
    return with_name("module ", name->spelling());
}

ImportDecl::ImportDecl(Owned<QualifiedName> path,
                       std::string alias,
                       std::string raw_text,
                       ahfl::SourceRange range)
    : Decl(NodeKind::ImportDecl, std::move(raw_text), range), path(std::move(path)),
      alias(std::move(alias)) {}

void ImportDecl::accept(Visitor &visitor) {
    visitor.visit(*this);
}

std::string ImportDecl::headline() const {
    if (alias.empty()) {
        return with_name("import ", path->spelling());
    }

    return "import " + path->spelling() + " as " + alias;
}

ConstDecl::ConstDecl(std::string name, std::string raw_text, ahfl::SourceRange range)
    : Decl(NodeKind::ConstDecl, std::move(raw_text), range), name(std::move(name)) {}

void ConstDecl::accept(Visitor &visitor) {
    visitor.visit(*this);
}

std::string ConstDecl::headline() const {
    if (!type) {
        return with_name("const ", name);
    }

    return "const " + name + " : " + type->spelling();
}

TypeAliasDecl::TypeAliasDecl(std::string name, std::string raw_text, ahfl::SourceRange range)
    : Decl(NodeKind::TypeAliasDecl, std::move(raw_text), range), name(std::move(name)) {}

void TypeAliasDecl::accept(Visitor &visitor) {
    visitor.visit(*this);
}

std::string TypeAliasDecl::headline() const {
    if (!aliased_type) {
        return with_name("type ", name);
    }

    return "type " + name + " = " + aliased_type->spelling();
}

StructDecl::StructDecl(std::string name, std::string raw_text, ahfl::SourceRange range)
    : Decl(NodeKind::StructDecl, std::move(raw_text), range), name(std::move(name)) {}

void StructDecl::accept(Visitor &visitor) {
    visitor.visit(*this);
}

std::string StructDecl::headline() const {
    return with_count("struct " + name, fields.size(), "field");
}

EnumDecl::EnumDecl(std::string name, std::string raw_text, ahfl::SourceRange range)
    : Decl(NodeKind::EnumDecl, std::move(raw_text), range), name(std::move(name)) {}

void EnumDecl::accept(Visitor &visitor) {
    visitor.visit(*this);
}

std::string EnumDecl::headline() const {
    return with_count("enum " + name, variants.size(), "variant");
}

CapabilityDecl::CapabilityDecl(std::string name, std::string raw_text, ahfl::SourceRange range)
    : Decl(NodeKind::CapabilityDecl, std::move(raw_text), range), name(std::move(name)) {}

void CapabilityDecl::accept(Visitor &visitor) {
    visitor.visit(*this);
}

std::string CapabilityDecl::headline() const {
    std::ostringstream builder;
    builder << "capability " << name << " (" << params.size() << " param";
    builder << (params.size() == 1 ? "" : "s") << ")";

    if (return_type) {
        builder << " -> " << return_type->spelling();
    }

    return builder.str();
}

PredicateDecl::PredicateDecl(std::string name, std::string raw_text, ahfl::SourceRange range)
    : Decl(NodeKind::PredicateDecl, std::move(raw_text), range), name(std::move(name)) {}

void PredicateDecl::accept(Visitor &visitor) {
    visitor.visit(*this);
}

std::string PredicateDecl::headline() const {
    return with_count("predicate " + name, params.size(), "param");
}

AgentDecl::AgentDecl(std::string name, std::string raw_text, ahfl::SourceRange range)
    : Decl(NodeKind::AgentDecl, std::move(raw_text), range), name(std::move(name)) {}

void AgentDecl::accept(Visitor &visitor) {
    visitor.visit(*this);
}

std::string AgentDecl::headline() const {
    std::ostringstream builder;
    builder << "agent " << name << " (" << states.size() << " states, " << transitions.size()
            << " transitions)";
    return builder.str();
}

ContractDecl::ContractDecl(Owned<QualifiedName> target,
                           std::string raw_text,
                           ahfl::SourceRange range)
    : Decl(NodeKind::ContractDecl, std::move(raw_text), range), target(std::move(target)) {}

void ContractDecl::accept(Visitor &visitor) {
    visitor.visit(*this);
}

std::string ContractDecl::headline() const {
    return with_count("contract for " + target->spelling(), clauses.size(), "clause");
}

FlowDecl::FlowDecl(Owned<QualifiedName> target, std::string raw_text, ahfl::SourceRange range)
    : Decl(NodeKind::FlowDecl, std::move(raw_text), range), target(std::move(target)) {}

void FlowDecl::accept(Visitor &visitor) {
    visitor.visit(*this);
}

std::string FlowDecl::headline() const {
    return with_count("flow for " + target->spelling(), state_handlers.size(), "handler");
}

WorkflowDecl::WorkflowDecl(std::string name, std::string raw_text, ahfl::SourceRange range)
    : Decl(NodeKind::WorkflowDecl, std::move(raw_text), range), name(std::move(name)) {}

void WorkflowDecl::accept(Visitor &visitor) {
    visitor.visit(*this);
}

std::string WorkflowDecl::headline() const {
    return with_count("workflow " + name, nodes.size(), "node");
}

void RecursiveVisitor::visit(Program &node) {
    for (auto &declaration : node.declarations) {
        declaration->accept(*this);
    }
}

void RecursiveVisitor::visit(ModuleDecl &) {}

void RecursiveVisitor::visit(ImportDecl &) {}

void RecursiveVisitor::visit(ConstDecl &) {}

void RecursiveVisitor::visit(TypeAliasDecl &) {}

void RecursiveVisitor::visit(StructDecl &) {}

void RecursiveVisitor::visit(EnumDecl &) {}

void RecursiveVisitor::visit(CapabilityDecl &) {}

void RecursiveVisitor::visit(PredicateDecl &) {}

void RecursiveVisitor::visit(AgentDecl &) {}

void RecursiveVisitor::visit(ContractDecl &) {}

void RecursiveVisitor::visit(FlowDecl &) {}

void RecursiveVisitor::visit(WorkflowDecl &) {}

} // namespace ahfl::ast
