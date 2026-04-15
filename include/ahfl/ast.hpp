#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "ahfl/ownership.hpp"
#include "ahfl/source.hpp"

namespace ahfl::ast {

class Visitor;

enum class NodeKind {
    Program,
    ModuleDecl,
    ImportDecl,
    ConstDecl,
    TypeAliasDecl,
    StructDecl,
    EnumDecl,
    CapabilityDecl,
    PredicateDecl,
    AgentDecl,
    ContractDecl,
    FlowDecl,
    WorkflowDecl,
};

[[nodiscard]] std::string_view to_string(NodeKind kind) noexcept;

struct Node {
    NodeKind kind;
    ahfl::SourceRange range;

    explicit Node(NodeKind kind, ahfl::SourceRange range = {});
    virtual ~Node() = default;

    virtual void accept(Visitor &visitor) = 0;
};

struct Decl : Node {
    std::string raw_text;

    Decl(NodeKind kind, std::string raw_text, ahfl::SourceRange range = {});
    ~Decl() override = default;

    [[nodiscard]] virtual std::string headline() const = 0;
};

struct Program final : Node {
    std::string source_name;
    std::vector<Owned<Decl>> declarations;

    explicit Program(std::string source_name, ahfl::SourceRange range = {});

    void accept(Visitor &visitor) override;
};

struct ModuleDecl final : Decl {
    std::string name;

    ModuleDecl(std::string name, std::string raw_text, ahfl::SourceRange range = {});
    void accept(Visitor &visitor) override;
    [[nodiscard]] std::string headline() const override;
};

struct ImportDecl final : Decl {
    std::string path;
    std::string alias;

    ImportDecl(std::string path,
               std::string alias,
               std::string raw_text,
               ahfl::SourceRange range = {});
    void accept(Visitor &visitor) override;
    [[nodiscard]] std::string headline() const override;
};

struct ConstDecl final : Decl {
    std::string name;

    ConstDecl(std::string name, std::string raw_text, ahfl::SourceRange range = {});
    void accept(Visitor &visitor) override;
    [[nodiscard]] std::string headline() const override;
};

struct TypeAliasDecl final : Decl {
    std::string name;

    TypeAliasDecl(std::string name, std::string raw_text, ahfl::SourceRange range = {});
    void accept(Visitor &visitor) override;
    [[nodiscard]] std::string headline() const override;
};

struct StructDecl final : Decl {
    std::string name;

    StructDecl(std::string name, std::string raw_text, ahfl::SourceRange range = {});
    void accept(Visitor &visitor) override;
    [[nodiscard]] std::string headline() const override;
};

struct EnumDecl final : Decl {
    std::string name;

    EnumDecl(std::string name, std::string raw_text, ahfl::SourceRange range = {});
    void accept(Visitor &visitor) override;
    [[nodiscard]] std::string headline() const override;
};

struct CapabilityDecl final : Decl {
    std::string name;

    CapabilityDecl(std::string name, std::string raw_text, ahfl::SourceRange range = {});
    void accept(Visitor &visitor) override;
    [[nodiscard]] std::string headline() const override;
};

struct PredicateDecl final : Decl {
    std::string name;

    PredicateDecl(std::string name, std::string raw_text, ahfl::SourceRange range = {});
    void accept(Visitor &visitor) override;
    [[nodiscard]] std::string headline() const override;
};

struct AgentDecl final : Decl {
    std::string name;

    AgentDecl(std::string name, std::string raw_text, ahfl::SourceRange range = {});
    void accept(Visitor &visitor) override;
    [[nodiscard]] std::string headline() const override;
};

struct ContractDecl final : Decl {
    std::string target;

    ContractDecl(std::string target, std::string raw_text, ahfl::SourceRange range = {});
    void accept(Visitor &visitor) override;
    [[nodiscard]] std::string headline() const override;
};

struct FlowDecl final : Decl {
    std::string target;

    FlowDecl(std::string target, std::string raw_text, ahfl::SourceRange range = {});
    void accept(Visitor &visitor) override;
    [[nodiscard]] std::string headline() const override;
};

struct WorkflowDecl final : Decl {
    std::string name;

    WorkflowDecl(std::string name, std::string raw_text, ahfl::SourceRange range = {});
    void accept(Visitor &visitor) override;
    [[nodiscard]] std::string headline() const override;
};

class Visitor {
  public:
    virtual ~Visitor() = default;

    virtual void visit(Program &node) = 0;
    virtual void visit(ModuleDecl &node) = 0;
    virtual void visit(ImportDecl &node) = 0;
    virtual void visit(ConstDecl &node) = 0;
    virtual void visit(TypeAliasDecl &node) = 0;
    virtual void visit(StructDecl &node) = 0;
    virtual void visit(EnumDecl &node) = 0;
    virtual void visit(CapabilityDecl &node) = 0;
    virtual void visit(PredicateDecl &node) = 0;
    virtual void visit(AgentDecl &node) = 0;
    virtual void visit(ContractDecl &node) = 0;
    virtual void visit(FlowDecl &node) = 0;
    virtual void visit(WorkflowDecl &node) = 0;
};

class RecursiveVisitor : public Visitor {
  public:
    void visit(Program &node) override;
    void visit(ModuleDecl &node) override;
    void visit(ImportDecl &node) override;
    void visit(ConstDecl &node) override;
    void visit(TypeAliasDecl &node) override;
    void visit(StructDecl &node) override;
    void visit(EnumDecl &node) override;
    void visit(CapabilityDecl &node) override;
    void visit(PredicateDecl &node) override;
    void visit(AgentDecl &node) override;
    void visit(ContractDecl &node) override;
    void visit(FlowDecl &node) override;
    void visit(WorkflowDecl &node) override;
};

} // namespace ahfl::ast
