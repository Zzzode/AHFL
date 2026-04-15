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
	| workflowDecl;

moduleDecl: 'module' qualifiedIdent ';';

importDecl: 'import' qualifiedIdent ('as' IDENT)? ';';

qualifiedIdent: IDENT ('::' IDENT)*;

qualifiedIdentList: qualifiedIdent (',' qualifiedIdent)* ','?;

identList: IDENT (',' IDENT)* ','?;

identListOpt: identList?;

qualifiedIdentListOpt: qualifiedIdentList?;

type_:
	primitiveType
	| qualifiedIdent
	| 'Optional' '<' type_ '>'
	| 'List' '<' type_ '>'
	| 'Set' '<' type_ '>'
	| 'Map' '<' type_ ',' type_ '>';

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

typeAliasDecl: 'type' IDENT '=' type_ ';';

structDecl: 'struct' IDENT '{' structFieldDecl* '}';

structFieldDecl: IDENT ':' type_ ('=' constExpr)? ';';

enumDecl:
	'enum' IDENT '{' enumVariant (',' enumVariant)* ','? '}';

enumVariant: IDENT;

capabilityDecl:
	'capability' IDENT '(' paramList? ')' '->' type_ ';';

predicateDecl:
	'predicate' IDENT '(' paramList? ')' '->' 'Bool' ';';

paramList: param (',' param)* ','?;

param: IDENT ':' type_;

agentDecl:
	'agent' IDENT '{' inputDecl contextDecl outputDecl statesDecl initialDecl finalDecl
		capabilitiesDecl quotaDecl? transitionDecl* '}';

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
	| forbidDecl;

requiresDecl: 'requires' ':' expr ';';

ensuresDecl: 'ensures' ':' expr ';';

invariantDecl: 'invariant' ':' temporalExpr ';';

forbidDecl: 'forbid' ':' temporalExpr ';';

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

block: '{' statement* '}';

statement:
	letStmt
	| assignStmt
	| ifStmt
	| gotoStmt
	| returnStmt
	| assertStmt
	| exprStmt;

letStmt: 'let' IDENT (':' type_)? '=' expr ';';

assignStmt: lValue '=' expr ';';

ifStmt: 'if' expr block ('else' block)?;

gotoStmt: 'goto' IDENT ';';

returnStmt: 'return' expr ';';

assertStmt: 'assert' expr ';';

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

postfixExpr: primaryExpr ('.' IDENT | '[' expr ']')*;

primaryExpr:
	literal
	| callExpr
	| structLiteral
	| qualifiedValueExpr
	| pathExpr
	| listLiteral
	| setLiteral
	| mapLiteral
	| 'some' '(' expr ')'
	| 'none'
	| '(' expr ')';

pathExpr: pathRoot ('.' IDENT)*;

pathRoot: IDENT | 'input' | 'output';

qualifiedValueExpr: IDENT '::' IDENT ('::' IDENT)*;

callExpr: qualifiedIdent '(' exprList? ')';

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

listLiteral: '[' exprList? ']';

setLiteral: 'set' '[' exprList? ']';

mapLiteral: 'map' '[' mapEntryList? ']';

mapEntryList: mapEntry (',' mapEntry)* ','?;

mapEntry: expr ':' expr;

structLiteral: qualifiedIdent '{' structInitList? '}';

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
