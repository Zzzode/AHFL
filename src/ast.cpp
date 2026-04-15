#include "ahfl/ast.hpp"

#include <utility>

namespace ahfl::ast {

namespace {

std::string with_name(std::string_view prefix, const std::string &value) {
    std::string text(prefix);
    text += value;
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

Node::Node(NodeKind kind, ahfl::SourceRange range) : kind(kind), range(range) {}

Decl::Decl(NodeKind kind, std::string raw_text, ahfl::SourceRange range)
    : Node(kind, range), raw_text(std::move(raw_text)) {}

Program::Program(std::string source_name, ahfl::SourceRange range)
    : Node(NodeKind::Program, range), source_name(std::move(source_name)) {}

void Program::accept(Visitor &visitor) {
    visitor.visit(*this);
}

ModuleDecl::ModuleDecl(std::string name, std::string raw_text, ahfl::SourceRange range)
    : Decl(NodeKind::ModuleDecl, std::move(raw_text), range), name(std::move(name)) {}

void ModuleDecl::accept(Visitor &visitor) {
    visitor.visit(*this);
}

std::string ModuleDecl::headline() const {
    return with_name("module ", name);
}

ImportDecl::ImportDecl(std::string path,
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
        return with_name("import ", path);
    }

    return "import " + path + " as " + alias;
}

ConstDecl::ConstDecl(std::string name, std::string raw_text, ahfl::SourceRange range)
    : Decl(NodeKind::ConstDecl, std::move(raw_text), range), name(std::move(name)) {}

void ConstDecl::accept(Visitor &visitor) {
    visitor.visit(*this);
}

std::string ConstDecl::headline() const {
    return with_name("const ", name);
}

TypeAliasDecl::TypeAliasDecl(std::string name, std::string raw_text, ahfl::SourceRange range)
    : Decl(NodeKind::TypeAliasDecl, std::move(raw_text), range), name(std::move(name)) {}

void TypeAliasDecl::accept(Visitor &visitor) {
    visitor.visit(*this);
}

std::string TypeAliasDecl::headline() const {
    return with_name("type ", name);
}

StructDecl::StructDecl(std::string name, std::string raw_text, ahfl::SourceRange range)
    : Decl(NodeKind::StructDecl, std::move(raw_text), range), name(std::move(name)) {}

void StructDecl::accept(Visitor &visitor) {
    visitor.visit(*this);
}

std::string StructDecl::headline() const {
    return with_name("struct ", name);
}

EnumDecl::EnumDecl(std::string name, std::string raw_text, ahfl::SourceRange range)
    : Decl(NodeKind::EnumDecl, std::move(raw_text), range), name(std::move(name)) {}

void EnumDecl::accept(Visitor &visitor) {
    visitor.visit(*this);
}

std::string EnumDecl::headline() const {
    return with_name("enum ", name);
}

CapabilityDecl::CapabilityDecl(std::string name, std::string raw_text, ahfl::SourceRange range)
    : Decl(NodeKind::CapabilityDecl, std::move(raw_text), range), name(std::move(name)) {}

void CapabilityDecl::accept(Visitor &visitor) {
    visitor.visit(*this);
}

std::string CapabilityDecl::headline() const {
    return with_name("capability ", name);
}

PredicateDecl::PredicateDecl(std::string name, std::string raw_text, ahfl::SourceRange range)
    : Decl(NodeKind::PredicateDecl, std::move(raw_text), range), name(std::move(name)) {}

void PredicateDecl::accept(Visitor &visitor) {
    visitor.visit(*this);
}

std::string PredicateDecl::headline() const {
    return with_name("predicate ", name);
}

AgentDecl::AgentDecl(std::string name, std::string raw_text, ahfl::SourceRange range)
    : Decl(NodeKind::AgentDecl, std::move(raw_text), range), name(std::move(name)) {}

void AgentDecl::accept(Visitor &visitor) {
    visitor.visit(*this);
}

std::string AgentDecl::headline() const {
    return with_name("agent ", name);
}

ContractDecl::ContractDecl(std::string target, std::string raw_text, ahfl::SourceRange range)
    : Decl(NodeKind::ContractDecl, std::move(raw_text), range), target(std::move(target)) {}

void ContractDecl::accept(Visitor &visitor) {
    visitor.visit(*this);
}

std::string ContractDecl::headline() const {
    return "contract for " + target;
}

FlowDecl::FlowDecl(std::string target, std::string raw_text, ahfl::SourceRange range)
    : Decl(NodeKind::FlowDecl, std::move(raw_text), range), target(std::move(target)) {}

void FlowDecl::accept(Visitor &visitor) {
    visitor.visit(*this);
}

std::string FlowDecl::headline() const {
    return "flow for " + target;
}

WorkflowDecl::WorkflowDecl(std::string name, std::string raw_text, ahfl::SourceRange range)
    : Decl(NodeKind::WorkflowDecl, std::move(raw_text), range), name(std::move(name)) {}

void WorkflowDecl::accept(Visitor &visitor) {
    visitor.visit(*this);
}

std::string WorkflowDecl::headline() const {
    return with_name("workflow ", name);
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
