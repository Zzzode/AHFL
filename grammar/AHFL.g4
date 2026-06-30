grammar AHFL;

// AHFL Core V0.1
//
// This grammar models syntax only. The following constraints are intentionally left to semantic
// analysis: - symbol resolution across declaration kinds - exact schema matching at agent/workflow
// boundaries - predicate purity and capability effect checks - capability allow-list enforcement
// inside flows - state reachability and transition validation - workflow DAG validation -
// control-flow completion checks for non-final/final state handlers

program: (moduleDecl | importDecl | topLevelDecl)* EOF;

topLevelDecl:
	constDecl
	| typeAliasDecl
	| structDecl
	| enumDecl
	| capabilityDecl
	| predicateDecl
	| agentDecl
	| contractDecl
	| flowDecl
	| workflowDecl
	| fnDecl
	| traitDecl
	| implDecl;

moduleDecl: 'module' qualifiedIdent ';';

importDecl: 'import' qualifiedIdent ('as' identifier)? ';';

identifier: IDENT | 'Optional' | 'List' | 'Set' | 'Map' | 'Fn' | 'map' | 'set' | 'self' | 'unwrap' | 'requires' | 'unreachable';

qualifiedIdent: identifier ('::' identifier)*;

// Identifier used at the head of a standalone call expression.  Explicitly
// excludes the statement-family keywords ('unwrap', 'requires', 'unreachable')
// so constructs like `requires(true, "msg");` always parse as requiresStmt
// (listed before exprStmt in the `statement` rule alternatives) rather than
// as a function call.  Member-call syntax `.unwrap(sentinel)` still works
// because postfixExpr's call-suffix arm uses the keyword-permissive
// `identifier` rule directly.
callableNamePiece
    :   IDENT
    |   'Optional' | 'List' | 'Set' | 'Map' | 'Fn' | 'map' | 'set' | 'self'
    ;
callableName: callableNamePiece ('::' callableNamePiece)*;

qualifiedIdentList: qualifiedIdent (',' qualifiedIdent)* ','?;

identList: IDENT (',' IDENT)* ','?;

identListOpt: identList?;

qualifiedIdentListOpt: qualifiedIdentList?;

type_:
	primitiveType
	| fnType
	| qualifiedIdent ('<' type_ (',' type_)* '>')?;

primitiveType:
	'Unit'
	| 'Bool'
	| 'Int'
	| 'Float'
	| 'String'
	| 'String' '(' INT_LITERAL ',' INT_LITERAL ')'
	| 'UUID'
	| 'Timestamp'
	| 'Duration'
	| 'Decimal' '(' INT_LITERAL ')';

constDecl: 'const' IDENT ':' type_ '=' constExpr ';';

typeAliasDecl: 'type' identifier typeParams? '=' type_ ';';

structDecl: 'struct' identifier typeParams? '{' structFieldDecl* '}';

structFieldDecl: IDENT ':' type_ ('=' constExpr)? ';';

enumDecl:
	'enum' identifier typeParams? '{' enumVariant (',' enumVariant)* ','? '}';

// P1 (ADT): an enum variant optionally carries a payload, one of:
//   * absent — classic payload-less variant (e.g. `None`)
//   * positional tuple — `IDENT ( typeList )` (e.g. `Some(T)`, `Err(E)`)
//   * struct (named fields) — `IDENT ( variantFieldList )` (RFC d-1 POC,
//     e.g. `Point(x: Int, y: Int)`). Absence vs tuple vs struct is
//     disambiguated by the first token after `(`: if the second token is
//     `:` it is a struct variant; otherwise a positional tuple is assumed.
//     Backward compatibility with payload-less variants is preserved.
enumVariant
    : IDENT                                                              # unitEnumVariant
    | IDENT '(' variantFieldList ')'                                     # structEnumVariant
    | IDENT '(' typeList ')'                                             # tupleEnumVariant
    ;

// Named-field payload for a struct enum variant (RFC d-1 minimal POC).
// Mirrors the surface syntax of fn param declarations / struct field
// declarations but without the trailing semicolon and with an optional
// default-initialiser, matching struct-field grammar.
variantFieldDecl: IDENT ':' type_ ('=' constExpr)?;

variantFieldList: variantFieldDecl (',' variantFieldDecl)* ','?;

// Positional tuple payload type list (RFC §1.5 TupleFieldList). Reused by
// variant patterns below.
typeList: type_ (',' type_)* ','?;

// P2: first-class function type. Syntax mirrors fn declarations:
//   Fn() -> Ret
//   Fn(A) -> Ret
//   Fn(A, B) -> Ret
//   Fn(A, B) -> Ret effect Pure
//   Fn(A, B) -> Ret effect cap1, cap2
// Return type defaults to Unit when omitted; effect defaults to Pure.
fnType: 'Fn' '(' typeList? ')' ('->' type_)? ('effect' effectSpec)?;

capabilityDecl:
	'capability' IDENT '(' paramList? ')' '->' type_ (';' | capabilityEffectBlock);

capabilityEffectBlock: '{' capabilityEffectItem* '}';

capabilityEffectItem:
	'effect' ':' capabilityEffectKind ';'
	| 'domain' ':' qualifiedIdent ';'
	| 'idempotency' ':' pathExpr ';'
	| 'receipt' ':' capabilityReceiptMode ';'
	| 'retry' ':' capabilityRetryMode ';'
	| 'timeout' ':' durationLiteral ';'
	| 'compensation' ':' qualifiedIdent ';'
	| 'policy' ':' '[' qualifiedIdentListOpt ']' ';';

capabilityEffectKind:
	'read'
	| 'external_side_effect'
	| 'durable_write'
	| 'financial_write'
	| 'unknown';

capabilityReceiptMode:
	'required'
	| 'optional'
	| 'none';

capabilityRetryMode:
	'safe'
	| 'safe_if_idempotent'
	| 'unsafe';

predicateDecl:
	'predicate' IDENT '(' paramList? ')' '->' 'Bool' ';';

paramList: param (',' param)* ','?;

// Parameter declaration. Four shapes share this rule (RFC §3.2.2 / §1.3 / §1.4):
//   - named param:              IDENT ':' type_          (any fn / capability / predicate)
//   - bare self receiver:       'self'                   (trait method / impl method)
//   - bare mutable self:        'mut' 'self'             (trait method / impl method)
//   - typed self receiver:      'self' ':' type_         (trait method / impl method)
//   - typed mutable self:       'mut' 'self' ':' type_   (trait method / impl method)
// Semantic validation (self only inside trait/impl items, not top-level) is
// deferred to the typecheck pass (P3b).
param:
	  IDENT ':' type_
	| 'mut'? 'self' (':' type_)?;

agentDecl:
	'agent' IDENT '{' inputDecl contextDecl? outputDecl statesDecl initialDecl finalDecl
		capabilitiesDecl? quotaDecl? transitionDecl* '}';

inputDecl: 'input' ':' type_ ';';

contextDecl: 'context' ':' type_ ';';

outputDecl: 'output' ':' type_ ';';

statesDecl: 'states' ':' '[' identList ']' ';';

initialDecl: 'initial' ':' IDENT ';';

finalDecl: 'final' ':' '[' identList ']' ';';

capabilitiesDecl: 'capabilities' ':' '[' identListOpt ']' ';';

quotaDecl: 'quota' ':' '{' quotaItem* '}';

quotaItem:
	'max_tool_calls' ':' integerLiteral ';'
	| 'max_execution_time' ':' durationLiteral ';';

transitionDecl: 'transition' IDENT '->' IDENT ';';

contractDecl:
	'contract' 'for' qualifiedIdent '{' contractItem* '}';

contractItem:
	requiresDecl
	| ensuresDecl
	| invariantDecl
	| forbidDecl
	| decreasesDecl;

requiresDecl: 'requires' ':' expr ';';

ensuresDecl: 'ensures' ':' expr ';';

invariantDecl: 'invariant' ':' temporalExpr ';';

forbidDecl: 'forbid' ':' temporalExpr ';';

decreasesDecl: 'decreases' ':' (expr | '*') ';';

flowDecl: 'flow' 'for' qualifiedIdent '{' stateHandler* '}';

stateHandler: 'state' IDENT statePolicy? block;

statePolicy: 'with' '{' statePolicyItem* '}';

statePolicyItem:
	'retry' ':' integerLiteral ';'
	| 'retry_on' ':' '[' qualifiedIdentListOpt ']' ';'
	| 'timeout' ':' durationLiteral ';';

workflowDecl:
	'workflow' IDENT '{' workflowInputDecl workflowOutputDecl workflowItem* workflowReturnDecl '}';

workflowInputDecl: 'input' ':' type_ ';';

workflowOutputDecl: 'output' ':' type_ ';';

workflowItem:
	workflowNodeDecl
	| workflowSafetyDecl
	| workflowLivenessDecl;

workflowNodeDecl:
	'node' IDENT ':' qualifiedIdent '(' expr ')' (
		'after' '[' identListOpt ']'
	)? ';';

workflowSafetyDecl: 'safety' ':' workflowTemporalExpr ';';

workflowLivenessDecl: 'liveness' ':' workflowTemporalExpr ';';

workflowReturnDecl: 'return' ':' expr ';';

// P5 (RFC §3.3 / corelib-stdlib-api.zh.md §5): @builtin attribute on fn declarations.
// Only stdlib modules may declare @builtin functions. The attribute binds a
// compiler/runtime builtin name to the fn so that calls to the fn are lowered
// to the builtin implementation instead of a normal fn call.
//   @builtin("list_raw_length") fn length<T>(xs: List<T>) effect Pure -> Int;
builtinAttr: '@builtin' '(' STRING_LITERAL ')';

// P2 (RFC §3.2.2 / §3.2.3 / §6): top-level function declarations with generic
// type parameters, an optional effect clause, and an optional where-clause.
//   fn name<T: bound, U>(p: Type, ...) -> Ret [effect] [where ...] { ... }
//   fn name(...);                      // prototype (no body)
// The grammar models syntax only: purity/effect enforcement and where-clause
// evaluation are deferred to the typecheck pass (P2b).
fnDecl:
	DOC_COMMENT? builtinAttr? 'fn' identifier typeParams? '(' paramList? ')' ('->' type_)? effectClause?
		whereClause? (fnBody | ';');

typeParams: '<' typeParam (',' typeParam)* ','? '>';

typeParam: IDENT (':' typeBoundList)?;

typeBoundList: type_ ('+' type_)*;

fnBody: block;

// P2 (RFC §2): function effect clause. A clause is exactly one of:
//   - a literal purity annotation (`Pure` / `Nondet`)
//   - one or more capability references (comma-separated), naming the
//     capabilities the function may exercise.
// `Pure` and `Nondet` are keywords (RFC §2); capability references are bare
// qualified idents, mirroring the capabilityRef spelling used elsewhere.
effectClause: 'effect' effectSpec decreasesClause?;

effectSpec: 'Pure' | 'Nondet' | capabilityRef (',' capabilityRef)*;

capabilityRef: qualifiedIdent;

// P4a (RFC corelib-effect-system.zh.md §3.1): optional termination measure
// following an effect clause. The measure is a single expr (a single nat, a
// tuple expr for lexicographic ordering, or `*` for the trivial measure on
// non-recursive functions). The grammar accepts the surface syntax only;
// effect-system termination checking (decreases prover, §3.2) is deferred to
// the typecheck pass (P4b).
//
//   fn f(xs: List<Int>) effect Pure decreases length(xs) { ... }
//   fn g(n: Int, xs: List<Int>) effect Pure decreases (n, length(xs)) { ... }
//   fn h(x: Int) effect Pure decreases * { ... }    // non-recursive only
decreasesClause: 'decreases' expr;

// P2 (RFC §6): where-clause constraining generic type parameters. The grammar
// accepts a comma-separated list of type-predicate / type-bound constraints;
// semantics are evaluated in the typecheck pass (P2b).
whereClause: 'where' whereConstraint (',' whereConstraint)* ','?;

whereConstraint:
	type_ '::' identifier ('(' typeList ')')?  // type predicate, e.g. T::Addable(U)
	| type_ ':' typeBoundList;             // bound list, e.g. T: Hashable

// P2 (RFC §6): lambda (closure) expression as a primaryExpr.
//   \ param -> expr          (single param, optional parens)
//   \ (p, ...) -> expr       (param list)
//   \ -> expr                (zero-arg thunk)
// The body is a single expr (RFC §6 closures are expression-bodied).
lambdaExpr: BACKSLASH lambdaParamList? '->' expr;

lambdaParamList:
	lambdaParam                             // single unparenthesised param
	| '(' (lambdaParam (',' lambdaParam)*)? ','? ')';

lambdaParam: IDENT (':' type_)?;

// P3 (RFC §3.2.2 / type-system §1.3 / §1.4): trait declarations and impl
// blocks. The grammar models the syntactic surface only; trait resolution,
// coherence / orphan-rule enforcement, signature matching, and where-clause
// evaluation are deferred to later passes (P3b).
//   trait Name<T>: Super { fn method<U>(p: T) -> Ret [effect] [where ...]; type Assoc; }
//   impl<T> TraitRef for TargetType [where ...] { fn method(...) { ... } type Assoc = T; }
traitDecl:
	DOC_COMMENT? 'trait' IDENT typeParams? (':' typeBoundList)? whereClause? '{' traitItem* '}';

traitItem: traitFnItem | assocTypeItem | assocConstItem;

traitFnItem:
	'fn' identifier typeParams? '(' paramList? ')' ('->' type_)? effectClause?
		whereClause? ';';

assocTypeItem:
	'type' identifier typeParams? (':' typeBoundList)? ('=' type_)? ';';

// P3 (RFC type-system §1.3): associated constant in a trait declaration.
//   const NAME: Type [= const_expr];
// Both the default initializer and the declaration-only (semicolon) forms
// are accepted; absence of a default forces the implementing impl to provide
// the value.
assocConstItem:
	'const' identifier ':' type_ ('=' constExpr)? ';';

implDecl:
	DOC_COMMENT? 'impl' typeParams? (traitRef 'for')? type_ whereClause? '{'
		implItem* '}';

// A trait reference in an impl header (e.g. `Foldable<T>`). Reuses type_ so a
// generic trait with arguments parses uniformly with named types.
traitRef: type_;

// Impl body items are defined as a single alternation (RFC §1.4). Consolidating
// fn + assoc-type + assoc-const into one `implItem` rule lets future item
// kinds be added in a single place, and mirrors the `traitItem` pattern used
// above.
implItem: implFnItem | assocTypeDef | assocConstDef;

// Method definition inside an impl block: same surface as fnDecl. Body is
// mandatory for normal methods; @builtin facade methods may use the ";"
// prototype shorthand (matching module-level `fn name(...);`) in which case
// the body is synthesised from the named builtin hook.
// The @builtin attribute is accepted here (P5, RFC §3.3) so stdlib facade
// methods that wrap compiler intrinsics can be declared inside inherent impl
// blocks (`impl String { @builtin("string_raw_length") fn length(self) -> Int effect Pure decreases 0; }`).
implFnItem:
	builtinAttr? 'fn' identifier typeParams? '(' paramList? ')' ('->' type_)? effectClause?
		whereClause? (fnBody | ';');
// Associated type definition inside an impl block.
assocTypeDef: 'type' identifier '=' type_ ';';

// P3 (RFC type-system §1.4): associated constant definition inside an impl
// block. The initializer is mandatory (an impl must provide a value for every
// trait assoc-const that lacks a default).
assocConstDef: 'const' identifier ':' type_ '=' constExpr ';';

block: '{' statement* '}';

statement:
	letStmt
	| assignStmt
	| ifStmt
	| ifLetStmt
	| gotoStmt
	| returnStmt
	| assertStmt
	| unwrapStmt
	| requiresStmt
	| unreachableStmt
	| exprStmt;

letStmt: 'let' IDENT (':' type_)? '=' expr ';';

assignStmt: lValue '=' expr ';';

ifStmt: 'if' expr block ('else' block)?;

// RFC e-1 (Wave-19 Lane 3b F2): optional narrowing surface syntax, minimal POC.
// Only VariantName(ident[, ident]*) patterns are accepted at this stage.
// Narrowing semantics / typecheck integration are deliberately deferred.
ifLetStmt: 'if' 'let' iflet_pattern=ifLetPattern '=' expr thenBlock=block ('else' elseBlock=block)?;
ifLetPattern: variant=IDENT ('(' ifLetPatternVar (',' ifLetPatternVar)* ','? ')')?;
ifLetPatternVar: IDENT;

gotoStmt: 'goto' IDENT ';';

returnStmt: 'return' expr ';';

// assert has two legal syntactic forms so legacy user code (`assert cond;`)
// still parses while the new `assert(cond[, "msg"]);` form supports optional
// message (arity-2).  Both are normalized to `AssertStmtSyntax` by the
// frontend so downstream stages see a single shape.  P4-01 emits WrongArity
// at semantic level for `assert();` (0 args) or `assert(a,b,c,...);` (3+).
assertStmt
	: 'assert' '(' exprList? ')' ';'
	| 'assert' expr ';'
	;

// unwrap expects exactly one expression (Option<T>).  Variable arity is
// accepted by the grammar so WrongArity can be reported at the typechecker
// (with a stable `typecheck.WRONG_ARITY` code) instead of as a parse error.
unwrapStmt: 'unwrap' '(' exprList? ')' ';';

// requires mirrors assert: accepts 1 (condition) or 2 (condition + message)
// arguments.  Disambiguated from contract `requires:` by the `(` lookahead.
requiresStmt: 'requires' '(' exprList? ')' ';';

// unreachable has two forms: bare `unreachable;` (0 args) and the
// message-bearing `unreachable(["msg"]);` form.  Using exprList for the
// parenthesized body allows semantic-level WrongArity when someone writes
// `unreachable("a", "b");` (2 args).
unreachableStmt
	: 'unreachable' '(' exprList? ')' ';'
	| 'unreachable' ';'
	;

exprStmt: expr ';';

lValue: pathExpr;

expr: impliesExpr;

impliesExpr: orExpr ('=>' orExpr)*;

orExpr: andExpr (('or' | '||') andExpr)*;

andExpr: equalityExpr (('and' | '&&') equalityExpr)*;

equalityExpr: compareExpr (('==' | '!=') compareExpr)*;

compareExpr: addExpr (('<' | '<=' | '>' | '>=') addExpr)*;

addExpr: mulExpr (('+' | '-') mulExpr)*;

mulExpr: unaryExpr (('*' | '/' | '%') unaryExpr)*;

unaryExpr: ('not' | '!' | '-' | '+') unaryExpr | postfixExpr;

postfixExpr:
	primaryExpr
	(
		'.' identifier ('<' typeList '>')? '(' exprList? ')'
		| '.' identifier
		| '[' expr ']'
		| '?'
	)*;

primaryExpr:
	  literal
	| callExpr
	| unwrapExpr
	| structLiteral
	| qualifiedValueExpr
	| pathExpr
	| matchExpr
	| lambdaExpr
	| '(' expr ')';

// P4-02: `unwrap(e)` is an expression that produces T from an Option<T>
// operand at runtime, or raises ExecAssertFailed if the operand is None.
// Grammatically it lives in primaryExpr so precedence is identical to
// callExpr / parenthesised expressions (higher than any postfix suffix).
unwrapExpr: 'unwrap' '(' expr ')';

// P1 (ADT): `match` expression. `match scrutinee { arm1; arm2; ... }`.
// Each arm is `pattern [if guard] => expr,`. The trailing comma is optional
// for the last arm (RFC §1.6 tolerates comma or newline endings).
matchExpr: 'match' expr '{' matchArm* '}';

matchArm: pattern ('if' expr)? '=>' expr ','?;

// P1 (ADT): patterns (RFC §1.6). Composition mirrors the RFC EBNF:
//   pattern  ::= orPattern
//   orPattern ::= concatPattern ('|' concatPattern)*
//   concatPattern is one of the concrete pattern forms.
//
// Grouping/nesting is handled by `tuplePattern`: `(p)` parses as a single-
// element tuple and `tuplePattern` re-enters `pattern` via `patternList`, so
// `(pattern)` does not need a separate parenthesized alt here.
pattern: orPattern;

orPattern: concatPattern ('|' concatPattern)*;

concatPattern:
	literalPattern
	| variantPattern
	| wildcardPattern
	| bindingPattern
	| tuplePattern;

// Literal patterns. `none` is sugar for Option::None (RFC §1.6).
literalPattern:
	'true'
	| 'false'
	| integerLiteral
	| floatLiteral
	| stringLiteral
	| 'none';

// Variant pattern: an ADT variant optionally carrying sub-patterns.
// Two forms per RFC §1.6:
//   - Fully qualified: `Ident '::' Ident ...` (e.g. Option::Some, Option::Some(x))
//   - Short form: a bare `Ident` followed by a payload list (e.g. `Some(x)`).
// The short form requires `(` so a bare `IDENT` (no payload) unambiguously
// parses as `bindingPattern`.
variantPattern:
	IDENT '::' IDENT ('::' IDENT)* ('(' patternList ')')?
	| IDENT '(' patternList ')';

wildcardPattern: '_';

// Binding pattern: `mut? name`, optionally `@`-bound to a nested pattern
// (`name @ Some(x)`). `mut` is accepted syntactically (RFC reserves it for
// future refinement; first version treats all bindings as immutable).
bindingPattern: 'mut'? IDENT ('@' concatPattern)?;

tuplePattern: '(' patternList? ')';

patternList: pattern (',' pattern)* ','?;

pathExpr: pathRoot ('.' IDENT)*;

pathRoot: IDENT | 'input' | 'output' | 'self';

qualifiedValueExpr: identifier '::' identifier ('::' identifier)*;

callExpr: callableName ('<' typeList '>')? '(' exprList? ')';

exprList: expr (',' expr)* ','?;

literal:
	'true'
	| 'false'
	| integerLiteral
	| floatLiteral
	| decimalLiteral
	| stringLiteral
	| durationLiteral;

integerLiteral: INT_LITERAL;

floatLiteral: FLOAT_LITERAL;

decimalLiteral: DECIMAL_LITERAL;

stringLiteral: STRING_LITERAL;

durationLiteral: DURATION_LITERAL;

structLiteral: qualifiedIdent '{' structInitList? '}';

listLiteral: '[' exprList? ']';

setLiteral: '#[' exprList? ']';

mapLiteral: '#{' mapEntryList? '}';

mapEntryList: mapEntry (',' mapEntry)* ','?;

mapEntry: expr ':' expr;

structInitList: structInit (',' structInit)* ','?;

structInit: IDENT ':' expr;

constExpr: expr;

temporalExpr: temporalImpliesExpr;

workflowTemporalExpr: temporalExpr;

temporalImpliesExpr: temporalOrExpr ('=>' temporalOrExpr)*;

temporalOrExpr:
	temporalAndExpr (('or' | '||') temporalAndExpr)*;

temporalAndExpr:
	temporalUntilExpr (('and' | '&&') temporalUntilExpr)*;

temporalUntilExpr:
	temporalUnaryExpr ('until' temporalUnaryExpr)*;

temporalUnaryExpr: (
		'always'
		| 'eventually'
		| 'next'
		| 'not'
		| '!'
	) temporalUnaryExpr
	| temporalAtom;

temporalAtom:
	'(' temporalExpr ')'
	| expr
	| 'called' '(' IDENT ')'
	| 'in_state' '(' IDENT ')'
	| 'running' '(' IDENT ')'
	| 'completed' '(' IDENT (',' IDENT)? ')';

DURATION_LITERAL: DIGIT+ ('ms' | 's' | 'm' | 'h');

DECIMAL_LITERAL: DIGIT+ '.' DIGIT+ 'd';

FLOAT_LITERAL: DIGIT+ '.' DIGIT+ EXPONENT?;

INT_LITERAL: DIGIT+;

// P2 (RFC §6): backslash introduces a lambda expression.
BACKSLASH: '\\';

STRING_LITERAL: '"' (ESCAPE_SEQUENCE | ~["\\\r\n])* '"';

IDENT: LETTER (LETTER | DIGIT | '_')*;

DOC_COMMENT: '///' ~[\r\n]* -> skip;

LINE_COMMENT: '//' ~[\r\n]* -> skip;

BLOCK_COMMENT: '/*' .*? '*/' -> skip;

WS: [ \t\r\n\u000C]+ -> skip;

fragment LETTER: [A-Za-z];

fragment DIGIT: [0-9];

fragment EXPONENT: [eE] [+-]? DIGIT+;

fragment ESCAPE_SEQUENCE: '\\' ["\\nrt];
