# Syntax

A compact grammar of the language as the parser in `src/parser.c` accepts it.
For semantics see [LANGUAGE.md](LANGUAGE.md).

Notation: `X?` optional, `X*` zero or more, `X+` one or more, `|` choice,
`'x'` literal token. `NL` means the token starts on a new line.

## Lexical

```
comment     = '//' ... end of line | '/*' ... '*/'
ident       = (letter | '_' | non-ASCII) (letter | digit | '_' | non-ASCII)*
int         = digits | '0x' hex | '0b' bin          (underscores allowed)
float       = digits '.' digits exponent? | digits exponent
string      = '"' ... '"' | "'" ... "'"             (escapes \n \t \r \0 \\ \" \' \xHH \u{H+})
op          = run of  + - * / % < > = ! & | ^ ~ # ? @ $ \   and  ..
              ('=' and '->' are reserved; 'op=' is compound assignment)
keyword     = import export global function return const let if else for in
              while break continue match enum true false
contextual  = type precedence over under as
```

Statements end at a newline. `;` is optional.

## Program

```
program     = statement*

statement   = import | export
            | let
            | function_decl
            | type_decl | enum_decl | precedence
            | for | while
            | 'return' expression? | 'break' | 'continue'
            | block
            | assignment | expression

block       = '{' statement* '}'
```

### Modules

```
import      = 'import' (ident | string) ('->' (ident | '{' name (',' name)* '}'))?
export      = 'export' (export_item | '{' export_item (',' export_item)* '}')      top level only
export_item = name ('->' name)?
name        = ident | op
```

* `import io` loads a library module (native, or `name.pp` in a library directory).
* `import "./path/file"` loads a file relative to the importing file. `.pp` may be omitted.

### Declarations

```
let         = ('let' | 'const') type? ident ('=' expression)?
function_decl = 'global'? generics? 'function' name params ('->' type)? block     'global': top level only
generics    = '<' ident (',' ident)* '>'
params      = '(' (param (',' param)*)? ')'
param       = type? ident
type_decl   = 'type' ident generics? '=' type
enum_decl   = 'enum' type? ident enum_generics? '{' variant (','? variant)* ','? '}'
enum_generics = '<' ident ('=' type)? (',' ident ('=' type)?)* '>'
variant     = ident ('(' (type (',' type)*)? ')')? ('=' expression)?
precedence  = 'precedence' name ('over' | 'under' | 'as') name
```

### Types

```
type        = postfix_type ('||' postfix_type)*
postfix_type = primary_type ('[' ']' | '?')*
primary_type = prim
            | (ident ':')? ident ('<' type (',' type)* '>')?     e.g. Error<int>, geometry:Vec
            | 'function'
            | '{' (type ident ','?)* '}'                         record
            | '(' type ')'
prim        = int | float | string | bool | void | any | range | object | type
```

`T?` is sugar for `Option<T>`.

### Control flow

```
for         = 'for' ('let' | 'const')? ident (',' ident)? 'in' expression block
while       = 'while' expression block
if          = 'if' (('let' | 'const')? pattern '=')? expression block
              ('else' (if | block))?
match       = 'match' expression '{' (arm ','?)* '}'
arm         = pattern ('if' expression)? '->' (block | expression)
pattern     = '_' | literal | '-' number | ident
            | path ('(' (pattern (',' pattern)*)? ')')?          e.g. some(x), Color:RED, mod:Enum:A
path        = ident (':' ident)*
```

### Expressions

```
assignment  = target ('=' | op '=') expression
target      = ident | postfix ':' name | postfix '[' expression ']'

expression  = unary (op unary)*              precedence resolved at runtime;
                                             an op on a new line ends the expression
                                             unless inside (), [] or {}
unary       = op '(' args ')' postfix_ops    classic call of an operator: +(a, b)
            | op unary                       prefix: -x, #xs, ...10
            | op                             operator as a value (before , ) ] } or NL)
            | postfix

postfix     = primary postfix_op*
postfix_op  = '(' args ')'                   call
            | '[' expression ']'             index (no space before '[')
            | ':' name                       attribute: object field, module export, enum variant
            | '.' name (':' name)* '(' args ')'
                                             a.f(x) == f(a, x)
                                             a.mod:f(x) == mod:f(a, x)

primary     = literal | ident
            | ident arg_expression           unary call by juxtaposition: success value
            | '(' expression ')'
            | '[' (expression (',' expression)* ','?)? ']'
            | '{' (key (':' expression)? (',' key (':' expression)?)* ','?)? '}'
            | generics? 'function' name? params ('->' type)? block
            | if | match

args        = (expression (',' expression)*)?
literal     = int | float | string | 'true' | 'false'
key         = ident | keyword | string
```

## `.` versus `:`

`.` is reserved for calling. `:` reads an attribute.

```
obj:field              read a field
obj:field = 1          write a field
module:symbol          an exported symbol
Enum:VARIANT           an enum constant / constructor
obj:fn(x)              call a function stored in a field
x.f(y)                 f(x, y)
4.module:function(module:constant)  ==  module:function(4, module:constant)
```
