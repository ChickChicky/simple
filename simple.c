#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>

#define TOKENS_CAPACITY_DEFAULT 256
#define TOKENS_CAPACITY_GROW 128

#define STR_CAPACITY_DEFAULT 64
#define STR_CAPACITY_GROW 16

#define LEX_NODES_CAPACITY_DEFAULT 16
#define LEX_NODES_CAPACITY_GROW 16

#define ERR(msg,  ...) { printf(msg "%s\n", ##__VA_ARGS__, strerror(errno)); return errno; }
#define TRY(expr, ...) { if (expr) ERR(__VA_ARGS__) }

#define MALLOC(value) memcpy(malloc(sizeof(*(value))),value,sizeof(*(value)))

// TODO: Refactor into enums
#define TK_CHAR    1
#define TK_STRING  2
#define TK_NUMBER  3
#define TK_NAME    4
#define TK_LPAREN  5
#define TK_RPAREN  6
#define TK_ADD     7
#define TK_SUB     8
#define TK_MUL     9
#define TK_DIV     10
#define TK_EQ      11
#define TK_NE      12
#define TK_GT      13
#define TK_GE      14
#define TK_LT      15
#define TK_LE      16
#define TK_INC     17
#define TK_DEC     18
#define TK_SHL     19
#define TK_SHR     20
#define TK_SET     21
#define TK_NOT     22

#define LEX_ROOT  1
#define LEX_TYPE  2
#define LEX_BLOCK 3

#define NODE_ROOT           1
#define NODE_TYPE           2
#define NODE_DEF            3
#define NODE_FUNCTION       4
#define NODE_FUNCTION_PARAM 5
#define NODE_BLOCK          6

#define NODE_TYPE_UNIT    0
#define NODE_TYPE_NAME    1
#define NODE_TYPE_POINTER 2

static inline int isnum(char c) {
    return c >= '0' && c <= '9';
}

static inline int isalpha(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

static inline int ishex(char c) {
    return (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F') || ishex(c) || isnum(c);
}

static inline unsigned char hexnum(char c) {
    if (c >= '0' && c <= '9')
        return c-'0';
    if (c >= 'A' && c <= 'F')
        return c-'A';
    if (c >= 'a' && c <= 'f')
        return c-'a';
    return 0;
}

static inline int isnamei(char c) {
    return isalpha(c) || c == '_';
}

static inline int isname(char c) {
    return isnamei(c) || isnum(c);
}

const char* token_kind(unsigned char kind) {
    switch (kind) {
        case TK_CHAR: return "TK_CHAR";
        case TK_STRING: return "TK_STRING";
        case TK_NUMBER: return "TK_NUMBER";
        case TK_NAME: return "TK_NAME";
        case TK_LPAREN: return "TK_LPAREN";
        case TK_RPAREN: return "TK_RPAREN";
        case TK_ADD: return "TK_ADD";
        case TK_SUB: return "TK_SUB";
        case TK_MUL: return "TK_MUL";
        case TK_DIV: return "TK_DIV";
        case TK_EQ: return "TK_EQ";
        case TK_NE: return "TK_NE";
        case TK_GT: return "TK_GT";
        case TK_GE: return "TK_GE";
        case TK_LT: return "TK_LT";
        case TK_LE: return "TK_LE";
        case TK_INC: return "TK_INC";
        case TK_DEC: return "TK_DEC";
        case TK_SHL: return "TK_SHL";
        case TK_SHR: return "TK_SHR";
        case TK_SET: return "TK_SET";
        case TK_NOT: return "TK_NOT";
    }
    return "INVALID";
}

#define DUB_STR(str) ((unsigned short)(*(const char*)(str)) | ((unsigned short)(*(const char*)((str)+1)) << 8))
#define DUB_CHR(a,b) ((unsigned short)(a) | ((unsigned short)(b) << 8))

typedef struct {
    const char* t;
    size_t l;
    size_t c;
    unsigned char k;
    void* d;
} Token;

typedef struct {
    Token* tokens;
    size_t cap;
    size_t len;
} Tokens;

typedef struct {
    Tokens* tokens;
    size_t i;
} lex_state;

typedef struct {
    char* str;
    size_t cap;
    size_t len;
} str_t;

typedef struct {
    const char* message;
} lex_error;

typedef struct {
    unsigned char kind;
    void* data;
} lex_node;

typedef struct {
    lex_node* nodes;
    size_t cap;
    size_t len;
} lex_nodes;

typedef struct {
    lex_nodes children;
} lex_node_root;

typedef struct {
    unsigned char kind;
    void* data;
} lex_node_type;

typedef struct {
    const char* name;
    lex_node_type type;
} lex_node_def;

typedef struct {
    lex_nodes children;
} lex_node_block;

typedef struct {
    const char* name;
    lex_node_type type;
    lex_nodes params;
    lex_node_block body;
} lex_node_fn;

typedef struct {
    const char* name;
    lex_node_type type;
} lex_node_fn_param;

typedef struct {
    unsigned char status;
    union {
        lex_error error;
        lex_node node;
    } result;
} lex_result;

lex_node_type lex_node_type_deref(lex_node_type* node) {
    if (node->kind == NODE_TYPE_POINTER)
        return *(lex_node_type*)(node->data);
    return (lex_node_type){.kind=0};
}

lex_node_type lex_node_type_ref(lex_node_type* node) {
    return (lex_node_type){.kind=NODE_TYPE_POINTER,.data=node};
}

static inline lex_result lex_result_error(const char* message) {
    return (lex_result){
        .status = 0,
        .result = {
            .error = (lex_error){
                .message = message,
            }
        },
    };
}

static inline lex_result lex_result_node(lex_node node) {
    return (lex_result){
        .status = 1,
        .result = {
            .node = node,
        },
    };
}

void lex_nodes_init(lex_nodes* nodes) {
    nodes->len = 0;
    nodes->cap = LEX_NODES_CAPACITY_DEFAULT;
    nodes->nodes = (lex_node*)malloc(sizeof(lex_node)*nodes->cap);
}

void lex_nodes_grow(lex_nodes* nodes, const size_t new_capacity) {
    lex_node* new_nodes = (lex_node*)malloc(sizeof(lex_node)*new_capacity);
    memcpy(new_nodes, nodes->nodes, sizeof(lex_node)*nodes->cap);
    free(nodes->nodes);
    nodes->nodes = new_nodes;
    nodes->cap = new_capacity;
}

void lex_nodes_push(lex_nodes* nodes, lex_node node) {
    if (nodes->cap <= nodes->len)
        lex_nodes_grow(nodes, nodes->len+LEX_NODES_CAPACITY_GROW);
    nodes->nodes[nodes->len++] = node;
}

void lex_nodes_free(lex_nodes* nodes) {
    nodes->len = 0;
    nodes->cap = 0;
    free(nodes->nodes);
}

void tokens_init(Tokens* tokens) {
    tokens->len = 0;
    tokens->cap = TOKENS_CAPACITY_DEFAULT;
    tokens->tokens = (Token*)malloc(sizeof(Token)*tokens->cap);
}

void tokens_grow(Tokens* tokens, const size_t new_capacity) {
    Token* new_tokens = (Token*)malloc(sizeof(Token)*new_capacity);
    memcpy(new_tokens, tokens->tokens, sizeof(Token)*tokens->cap);
    free(tokens->tokens);
    tokens->tokens = new_tokens;
    tokens->cap = new_capacity;
}

void tokens_push(Tokens* tokens, const Token token) {
    if (tokens->cap <= tokens->len)
        tokens_grow(tokens, tokens->len+TOKENS_CAPACITY_GROW);
    tokens->tokens[tokens->len++] = token;
}

int tokens_pop(Tokens *restrict tokens,Token *const restrict token) {
    if (tokens->len <= 0)
        return 1;
    Token tk = tokens->tokens[--tokens->len];
    if (token)
        *token = tk;
    return 0;
}

void tokens_free(Tokens* tokens) {
    tokens->len = 0;
    tokens->cap = 0;
    free(tokens->tokens);
}

// Creates a new empty string
void str_init(str_t* str) {
    str->len = 0;
    str->cap = STR_CAPACITY_DEFAULT;
    str->str = (char*)malloc(str->cap);
    memset(str->str, 0, str->cap);
}

// Creates a new string from lengthed data
void str_init_data(str_t *restrict str, const char *restrict data, const size_t len) {
    str->len = len;
    str->cap = ((len+1)/STR_CAPACITY_DEFAULT+1)*STR_CAPACITY_DEFAULT;
    str->str = (char*)malloc(str->cap);
    memset(str->str, 0, str->cap); // TODO: Only set last bytes
    memcpy(str->str, data, len);
}

// Creates a string from a null-terminated string
void str_init_cstr(str_t *restrict str, const char *restrict cstr) {
    str->len = strlen(cstr);
    str->cap = ((str->len+1)/STR_CAPACITY_DEFAULT+1)*STR_CAPACITY_DEFAULT;
    str->str = (char*)malloc(str->cap);
    memset(str->str, 0, str->cap); // TODO: Only set last bytes
    memcpy(str->str, cstr, str->len);
}

// Frees a previously allocated string
void str_free(str_t* str) {
    free(str->str);
    str->str = NULL;
    str->len = 0;
    str->cap = 0;
}

// Duplicates a string as a C string
void str_dup_c(str_t *restrict str, char *restrict *restrict new_str) {
    const size_t len = strlen(str->str);
    char* cstr = malloc(len+1);
    cstr[len] = 0;
    memcpy(cstr, str->str, len);
    *new_str = cstr;
}

// Duplicates a string's data
void str_dup_data(str_t*restrict str, char*restrict*restrict data) {
    const size_t len = str->len;
    char* cstr = malloc(len+1);
    cstr[len] = 0;
    memcpy(cstr, str->str, len);
    *data = cstr;
}

// Duplicates a string
void str_dup(str_t* restrict str, str_t* restrict new_str) {
    const size_t cap = str->cap;
    new_str->cap = cap;
    new_str->len = str->len;
    new_str->str = (char*)malloc(cap);
    memcpy(new_str->str, str->str, cap);
}

// Appends a single character at the end of a string
void str_push(str_t* str, char c) {
    if (str->len+1 >= str->cap) {
        const size_t newlen = str->len+STR_CAPACITY_GROW;
        char* new_str = (char*)malloc(newlen);
        memset(new_str, 0, newlen); // TODO: Only set last bytes
        memcpy(new_str, str->str, str->len);
        str->cap = newlen;
        free(str->str);
        str->str = new_str;
    }
    str->str[str->len++] = c;
}

void tokenize(const char* text, Tokens* tokens) {
    size_t len = strlen(text);

    size_t tk_start = 0;
    size_t tk_esc = 0;
    unsigned char tk_kind = 0;

    unsigned long tk_num;
    str_t tk_str;

    int in_comment = 0;

    size_t row = 0;
    size_t col = 0;

    for (size_t i = 0; i < len; i++) {
        char c = text[i];

        col++;
        if (c == '\n') {
            col = 0;
            row++;
        }

        if (in_comment) {
            if (i < len-1 && c == ';' && text[i+1] == ')') {
                in_comment = 0;
                i++;
            }
            continue;
        }

        if (tk_kind == TK_STRING) {
            if (tk_esc) {
                short v = 0x100;
                if (tk_esc+1 == len) {
                    continue;
                }
                switch (text[tk_esc+1]) {
                    case 't': if (v == 0x100) v = 0x09;
                    case 'n': if (v == 0x100) v = 0x0a;
                    case 'r': if (v == 0x100) v = 0x0d;
                    case 'e': if (v == 0x100) v = 0x1b;
                    case '0': if (v == 0x100) v = 0x00;
                        str_push(&tk_str, v);
                        i = tk_esc+1;
                        break;
                    case 'x':
                        if (i+4 < len && ishex(text[tk_esc+2]) && ishex(text[tk_esc+3])) {
                            v = (hexnum(text[tk_esc+2]) << 4) | hexnum(text[tk_esc+3]);
                            str_push(&tk_str, v);
                        } else {
                            // TODO: Invalid syntax
                        }
                        i = tk_esc + 3;
                        break;
                    default:
                        str_push(&tk_str, c);
                        break;
                }
                tk_esc = 0;
            } else {
                if (c == '\\')
                    tk_esc = i;
                else if (c == '"') {
                    str_t* d = malloc(sizeof(str_t));
                    str_dup(&tk_str, d);
                    const size_t l = i-tk_start+1;
                    char* t = (char*)malloc(l+1);
                    t[l] = 0;
                    memcpy(t, text+tk_start, l);
                    tokens_push(tokens, (Token){.t=t,.c=col,.l=row,.k=tk_kind,.d=d});
                    tk_kind = 0;
                    str_free(&tk_str);
                }
                else
                    str_push(&tk_str, c);
            }
            continue;
        }

        if (tk_kind == TK_NUMBER) {
            int end_num = 0;
            
            if (tk_kind+1 < len  && text[tk_start] == '0' && text[tk_start+1] == 'x') {
                if (i-tk_start < 2)
                    continue;
                if (ishex(c)) {
                    tk_num <<= 4;
                    tk_num |= hexnum(c);
                } else
                    end_num = 1;
            }

            else {
                if (isnum(c)) {
                    tk_num *= 10;
                    tk_num |= hexnum(c);
                } else
                    end_num = 1;
            }

            if (end_num) {
                size_t l = i-tk_start;
                char* t = (char*)malloc(l+1);
                t[l] = 0;
                memmove(t, text+tk_start, l);
                unsigned long* d = (unsigned long*)malloc(sizeof(long));
                *d = tk_num;
                tokens_push(tokens, (Token){.t=t,.c=col,.l=row,.k=tk_kind,.d=d});
                tk_kind = 0;
                i--;
            }

            continue;
        }

        if (tk_kind == TK_NAME) {
            if (!isname(c)) {
                size_t l = i-tk_start;
                char* t = (char*)malloc(l+1);
                t[l] = 0;
                memmove(t, text+tk_start, l);
                tokens_push(tokens, (Token){.t=t,.c=col,.l=row,.k=tk_kind,.d=NULL});
                tk_kind = 0;
                i--;
            }

            continue;
        }

        if (c == '"') {
            tk_kind = TK_STRING;
            str_init(&tk_str);
            tk_start = i;
            continue;
        }

        if (isnum(c)) {
            tk_kind = TK_NUMBER;
            tk_num = 0;
            tk_start = i;
            continue;
        }

        if (isnamei(c)) {
            tk_kind = TK_NAME;
            tk_start = i;
            i--;
            continue;
        }

        if (
            c == '(' ||
            c == ')' ||
            c == '=' ||
            c == '+' ||
            c == '-' ||
            c == '*' ||
            c == '/' ||
            c == '!' ||
            c == '>' ||
            c == '<'
        ) {
            unsigned char kind = 0;

            int skip = 0;
            
            if (i < len-1) {
                unsigned short const dub = DUB_STR(text+i);
                
                switch (dub) {
                    case DUB_CHR('(',';'):
                        in_comment = 1;
                        skip = 1;
                        break;
                    
                    case DUB_CHR('+','+'): if (!kind) kind = TK_INC;
                    case DUB_CHR('-','-'): if (!kind) kind = TK_DEC;
                    case DUB_CHR('=','='): if (!kind) kind = TK_EQ;
                    case DUB_CHR('!','='): if (!kind) kind = TK_NE;
                    case DUB_CHR('>','='): if (!kind) kind = TK_GE;
                    case DUB_CHR('<','='): if (!kind) kind = TK_LE;
                    case DUB_CHR('<','<'): if (!kind) kind = TK_SHL;
                    case DUB_CHR('>','>'): if (!kind) kind = TK_SHR;
                        char* t = (char*)malloc(3);
                        t[0] = c;
                        t[1] = text[i+1];
                        t[2] = 0;
                        tokens_push(tokens, (Token){.t=t,.c=col,.l=row,.k=kind,.d=NULL});
                        skip = 1;
                        break;
                }
            }

            if (skip) {
                i += skip;
                continue;
            }

            switch (c) {
                case '(': if (!kind) kind = TK_LPAREN;
                case ')': if (!kind) kind = TK_RPAREN;
                case '=': if (!kind) kind = TK_SET;
                case '+': if (!kind) kind = TK_ADD;
                case '-': if (!kind) kind = TK_SUB;
                case '*': if (!kind) kind = TK_MUL;
                case '/': if (!kind) kind = TK_DIV;
                case '!': if (!kind) kind = TK_NOT;
                case '>': if (!kind) kind = TK_GT;
                case '<': if (!kind) kind = TK_LT;
                    char* t = (char*)malloc(2);
                    t[0] = c;
                    t[1] = 0;
                    tokens_push(tokens, (Token){.t=t,.c=col,.l=row,.k=kind,.d=NULL});
                    break;
            }

            continue;
        }
    }
}

lex_result lex_util(lex_state* st, const unsigned char state) {
    if (state == LEX_ROOT) {
        lex_node_root root_node;
        lex_nodes_init(&root_node.children);

        while (st->i+1 < st->tokens->len) {
            const Token tk = st->tokens->tokens[st->i];
            
            if (tk.k != TK_LPAREN || st->i+2 >= st->tokens->len) {
                return lex_result_error("Expected an instruction");
            }
            
            switch (st->tokens->tokens[st->i+1].k) {
                case TK_NAME: {
                    const char* const n = st->tokens->tokens[++st->i].t;
                    
                    if (st->i+1 > st->tokens->len)
                        return lex_result_error("Unexpected EOF");

                    if (!strcmp(n, "fn")) {
                        st->i++;

                        lex_node_fn fn;
                        lex_nodes_init(&fn.params);

                        // TODO: Probably extract parsing of type + name into a separate function or node kind
                        lex_result type_result = lex_util(st, LEX_TYPE);
                        if (!type_result.status)
                            return type_result;

                        fn.type = *(lex_node_type*)type_result.result.node.data;
                        free(type_result.result.node.data);

                        const Token name_tk = st->tokens->tokens[st->i++];
                        if (name_tk.k != TK_NAME)
                            return lex_result_error("Name expected after 'fn' type");
                        fn.name = name_tk.t;

                        if (st->tokens->tokens[st->i++].k != TK_LPAREN)
                            return lex_result_error("Argument list expected after 'fn' name");

                        while (st->tokens->tokens[st->i].k != TK_RPAREN) {
                            if (st->i+1 > st->tokens->len)
                                return lex_result_error("Unexpected EOF");

                            lex_node_fn_param param;

                            lex_result ptype_result = lex_util(st, LEX_TYPE);
                            if (!ptype_result.status)
                                return ptype_result;

                            param.type = *(lex_node_type*)ptype_result.result.node.data;
                            free(ptype_result.result.node.data);

                            const Token name_tk = st->tokens->tokens[st->i++];
                            if (name_tk.k != TK_NAME)
                                return lex_result_error("Name expected after 'fn' parameter type");
                            param.name = name_tk.t;

                            lex_nodes_push(&fn.params,(lex_node){
                                .kind = NODE_FUNCTION_PARAM,
                                .data = MALLOC(&param),
                            });
                        }

                        st->i++;

                        lex_result body_result = lex_util(st, LEX_BLOCK);
                        if (!body_result.status)
                            return body_result;
                        fn.body = *(lex_node_block*)body_result.result.node.data;
                        free(body_result.result.node.data);

                        st->i++;
                        lex_nodes_push(&root_node.children,(lex_node){
                            .kind = NODE_FUNCTION,
                            .data = MALLOC(&fn),
                        });
                    }
                    
                    else if (!strcmp(n, "def")) {
                        st->i++;

                        lex_node_def def;

                        lex_result type_result = lex_util(st, LEX_TYPE);
                        if (!type_result.status)
                            return type_result;

                        def.type = *(lex_node_type*)type_result.result.node.data;
                        free(type_result.result.node.data);
                        
                        const Token name_tk = st->tokens->tokens[st->i++];
                        if (name_tk.k != TK_NAME)
                            return lex_result_error("Name expected after 'def' type");
                        def.name = name_tk.t;

                        lex_nodes_push(&root_node.children,(lex_node){
                            .kind = NODE_DEF,
                            .data = MALLOC(&def),
                        });
                    } 
                    
                    else {
                        return lex_result_error("Invalid keyword");
                    }
                } break;
                
                default: {
                    return lex_result_error("Unexpected token");
                } break;
            }
        }

        return lex_result_node((lex_node){
            .kind = NODE_ROOT,
            .data = MALLOC(&root_node),
        });
    }
    
    if (state == LEX_TYPE) {
        lex_node_type type_node = {
            .kind = NODE_TYPE_UNIT,
            .data = NULL
        };

        if (st->i >= st->tokens->len || st->tokens->tokens[st->i++].k != TK_LPAREN)
            return lex_result_error("Type expressions must start with a `(`");

        for (;;st->i++) {
            if (st->i >= st->tokens->len)
                return lex_result_error("Unfinished type expression");
            
            const Token tk = st->tokens->tokens[st->i];
            
            if (tk.k == TK_RPAREN) {
                st->i++;
                break;
            }

            if (tk.k == TK_NAME) {
                if (type_node.kind != NODE_TYPE_UNIT)
                    return lex_result_error("Unexpected identifier");
                type_node.kind = NODE_TYPE_NAME;
                char* name = memcpy(malloc(strlen(tk.t)+1),tk.t,strlen(tk.t)+1);
                type_node.data = name;
            }

            else if (tk.k == TK_MUL) {
                if (type_node.kind == NODE_TYPE_UNIT)
                    return lex_result_error("Unexpected star");
                type_node = lex_node_type_ref(MALLOC(&type_node));
            }

            else {
                return lex_result_error("Unexpected token");
            }
        }

        return lex_result_node((lex_node){
            .kind = NODE_TYPE,
            .data = MALLOC(&type_node)
        });
    }

    if (state == LEX_BLOCK) {
        lex_node_block block_node;
        lex_nodes_init(&block_node.children);

        while (st->i+1 < st->tokens->len) {
            const Token tk = st->tokens->tokens[st->i];

            if (tk.k == TK_RPAREN)
                break;
            
            if (tk.k != TK_LPAREN || st->i+2 >= st->tokens->len) {
                return lex_result_error("Expected an instruction");
            }
            
            switch (st->tokens->tokens[st->i+1].k) {
                case TK_NAME: {
                    const char* const n = st->tokens->tokens[++st->i].t;
                    
                    if (st->i+1 > st->tokens->len)
                        return lex_result_error("Unexpected EOF");

                    if (!strcmp(n, "def")) {
                        st->i++;

                        lex_node_def def;

                        lex_result type_result = lex_util(st, LEX_TYPE);
                        if (!type_result.status)
                            return type_result;

                        def.type = *(lex_node_type*)type_result.result.node.data;
                        free(type_result.result.node.data);
                        
                        const Token name_tk = st->tokens->tokens[st->i++];
                        if (name_tk.k != TK_NAME)
                            return lex_result_error("Name expected after 'def' type");
                        def.name = name_tk.t;

                        lex_nodes_push(&block_node.children,(lex_node){
                            .kind = NODE_DEF,
                            .data = MALLOC(&def),
                        });
                    }

                    else {
                        return lex_result_error("Invalid keyword");
                    }
                } break;
                
                default: {
                    return lex_result_error("Unexpected token");
                } break;
            }
        }

        return lex_result_node((lex_node){
            .kind = NODE_TYPE,
            .data = MALLOC(&block_node)
        });
    }

    return lex_result_error("Invalid state");
}

lex_result lex(Tokens* tokens) {
    lex_state st = {
        .i = 0,
        .tokens = tokens,
    };
    return lex_util(&st, LEX_ROOT);
}

const char* shift_args(int* argc, const char*** argv) {
    return (*argc)--, *(*argv)++;
}

void debug_ast_type(lex_node_type node) {
    if (node.kind == NODE_TYPE_UNIT) {
        printf("()");
    }
    else if (node.kind == NODE_TYPE_NAME) {
        printf("%s", (const char*)node.data);
    }
    else if (node.kind == NODE_TYPE_POINTER) {
        debug_ast_type(*(lex_node_type*)node.data);
        printf("*");
    }
    else {
        printf("\x1b[91m?\x1b[39m");
    }
}

void debug_ast(lex_node node, int indent) {
    if (node.kind == NODE_ROOT) {
        lex_node_root* data = node.data;
        printf("%*s\x1b[91;1mROOT\x1b[39;22m {\n", indent, "");
        for (size_t i = 0; i < data->children.len; i++) {
            debug_ast(data->children.nodes[i], indent+2);
        }
        printf("%*s}\n", indent, "");
    }
    else if (node.kind == NODE_DEF) {
        lex_node_def* data = node.data;
        printf("%*s\x1b[91;1mDEF\x1b[39;22m \x1b[94m", indent, "");
        debug_ast_type(data->type);
        printf(" \x1b[39m\x1b[95;1m%s\x1b[39;22m", data->name);
        printf("\n");
    }
    else if (node.kind == NODE_TYPE) {
        printf("%*s\x1b[91;1mTYPE\x1b[39;22m ", indent, "");
        debug_ast_type(*(lex_node_type*)node.data);
        printf("\n");
    }
    else if (node.kind == NODE_FUNCTION) {
        lex_node_fn* data = node.data;
        printf("%*s\x1b[91;1mFN\x1b[39;22m \x1b[94m", indent, "");
        debug_ast_type(data->type);
        printf("\x1b[39m \x1b[95;1m%s\x1b[39;22m {\n", data->name);
        for (size_t i = 0; i < data->params.len; i++)
            debug_ast(data->params.nodes[i], indent+2);
        debug_ast((lex_node){.kind=NODE_BLOCK,.data=&data->body}, indent+2);
        printf("%*s}\n", indent, "");
    }
    else if (node.kind == NODE_FUNCTION_PARAM) {
        lex_node_fn_param* data = node.data;
        printf("%*s\x1b[91;1mPARAM\x1b[39;22m \x1b[94m", indent, "");
        debug_ast_type(data->type);
        printf("\x1b[39m \x1b[95;1m%s\x1b[39;22m\n", data->name);
    }
    else if (node.kind == NODE_BLOCK) {
        lex_node_root* data = node.data;
        printf("%*s\x1b[91;1mBLOCK\x1b[39;22m {\n", indent, "");
        for (size_t i = 0; i < data->children.len; i++) {
            debug_ast(data->children.nodes[i], indent+2);
        }
        printf("%*s}\n", indent, "");
    }
    else {
        printf("%*s\x1b[90m(Invalid node kind %d)\x1b[39m\n", indent, "", node.kind);
    }
}

int main(int argc, const char** argv) {
    const char* program = shift_args(&argc, &argv);
    
    if (argc == 0) {
        printf("Usage: %s <file.spl>\n", program);
        return 1;
    }

    const char* source_path = shift_args(&argc, &argv);
    
    FILE* f = fopen(source_path, "rt");
    if (f == NULL) {
        printf("Could not open %s: %s\n", source_path, strerror(errno));
        return 1;
    }

    TRY( fseek(f, 0, SEEK_END) );
    size_t source_len = ftell(f);
    if (!source_len) {
        printf("An empty program was provided\n");
        printf(
            "Try a simple one:\n"
            "\x1b[90m|\x1b[39m(\x1b[91mfn\x1b[39m \x1b[96mmain\x1b[39m()\n"
            "\x1b[90m|\x1b[39m  (\x1b[96mprintf\x1b[39m \x1b[92m\"Hello, world!\x1b[1m\\n\x1b[22m\"\x1b[39m)\n"
            "\x1b[90m|\x1b[39m  (\x1b[91mreturn\x1b[39m \x1b[35m0\x1b[39m)\n"
            "\x1b[90m|\x1b[39m)\n"
        );
        return 0;
    }
    TRY( fseek(f, 0, SEEK_SET) );
    char* source = (char*)malloc(source_len);
    TRY( !fread(source, source_len, 1, f), "Failed to read: " );
    TRY( fclose(f), "Failed to close: " );

    Tokens tokens;
    tokens_init(&tokens);
    tokenize(source, &tokens);

    lex_result result = lex(&tokens);
    
    if (result.status == 0) {
        printf("Syntax error:\n  %s\n", result.result.error.message);
        return 1;
    }

    printf("showing %zu tokens:\n", tokens.len);
    for (size_t i = 0; i < tokens.len; i++) {
        Token tk = tokens.tokens[i];
        printf("  %02zu \x1b[92m%s\x1b[39m [%02x %s]\n", i, tk.t, tk.k, token_kind(tk.k));
        if (tk.k == TK_STRING) {
            str_t str = *(str_t*)tk.d;
            for (size_t j = 0; j < str.len; j++) {
                char c = str.str[j];
                printf("    %02zu %02x\n", j, c);
            }
        }
        if (tk.k == TK_NUMBER) {
            long num = *(long*)tk.d;
            printf("    %zu\n", num);
        }
    }
    printf("end\n");

    printf("showing AST:\n");
    debug_ast(result.result.node, 0);
    printf("end\n");

    return 0;
}
