/* CareOS v9 -- kernel/care_lang.c -- Care Language Interpreter
 * Syntax: var x = expr;  print expr;  if (cond) { } else { }  while (cond) { }
 * Comments: // or #   Strings: "..."   Numbers: integers
 * Operators: + - * /  Comparisons: == != < > <= >= */
#include "kernel.h"
#include "../gui/gui.h"

/* ── Tokens ──────────────────────────────────────────────────────────────── */
typedef enum {
    T_EOF=0, T_NUM, T_STR, T_IDENT,
    T_EQ, T_EQEQ, T_NEQ, T_LT, T_GT, T_LE, T_GE,
    T_PLUS, T_MINUS, T_STAR, T_SLASH,
    T_SEMI, T_LPAREN, T_RPAREN, T_LBRACE, T_RBRACE, T_COMMA,
    T_KW_VAR, T_KW_PRINT, T_KW_IF, T_KW_ELSE,
    T_KW_WHILE, T_KW_RETURN, T_KW_FUNC,
} tok_t;

typedef struct {
    tok_t type;
    i32   num;
    char  str[64];
} token_t;

/* ── Lexer ───────────────────────────────────────────────────────────────── */
typedef struct {
    const char *src;
    u32 pos, len;
    token_t cur;
} lexer_t;

static void lex_next(lexer_t *l) {
    /* skip whitespace and comments */
    while (l->pos < l->len) {
        char c = l->src[l->pos];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') { l->pos++; continue; }
        if (c == '/' && l->pos+1 < l->len && l->src[l->pos+1] == '/') {
            while (l->pos < l->len && l->src[l->pos] != '\n') l->pos++;
            continue;
        }
        if (c == '#') { while (l->pos < l->len && l->src[l->pos] != '\n') l->pos++; continue; }
        break;
    }
    if (l->pos >= l->len) { l->cur.type = T_EOF; return; }
    char c = l->src[l->pos];

    /* number */
    if (c >= '0' && c <= '9') {
        l->cur.type = T_NUM; l->cur.num = 0;
        while (l->pos < l->len && l->src[l->pos] >= '0' && l->src[l->pos] <= '9')
            l->cur.num = l->cur.num * 10 + (l->src[l->pos++] - '0');
        return;
    }

    /* string */
    if (c == '"') {
        l->pos++; l->cur.type = T_STR;
        u32 i = 0;
        while (l->pos < l->len && l->src[l->pos] != '"' && i < 63) {
            if (l->src[l->pos] == '\\' && l->pos+1 < l->len) {
                l->pos++;
                char e = l->src[l->pos++];
                l->cur.str[i++] = (e == 'n') ? '\n' : (e == 't') ? '\t' : e;
            } else {
                l->cur.str[i++] = l->src[l->pos++];
            }
        }
        if (l->pos < l->len) l->pos++;
        l->cur.str[i] = '\0';
        return;
    }

    /* identifier / keyword */
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_') {
        u32 i = 0;
        while (l->pos < l->len && i < 63 &&
               ((l->src[l->pos] >= 'a' && l->src[l->pos] <= 'z') ||
                (l->src[l->pos] >= 'A' && l->src[l->pos] <= 'Z') ||
                (l->src[l->pos] >= '0' && l->src[l->pos] <= '9') ||
                l->src[l->pos] == '_'))
            l->cur.str[i++] = l->src[l->pos++];
        l->cur.str[i] = '\0';
        if (kstrcmp(l->cur.str,"var"   )==0) { l->cur.type=T_KW_VAR;    return; }
        if (kstrcmp(l->cur.str,"print" )==0) { l->cur.type=T_KW_PRINT;  return; }
        if (kstrcmp(l->cur.str,"if"    )==0) { l->cur.type=T_KW_IF;     return; }
        if (kstrcmp(l->cur.str,"else"  )==0) { l->cur.type=T_KW_ELSE;   return; }
        if (kstrcmp(l->cur.str,"while" )==0) { l->cur.type=T_KW_WHILE;  return; }
        if (kstrcmp(l->cur.str,"return")==0) { l->cur.type=T_KW_RETURN; return; }
        if (kstrcmp(l->cur.str,"func"  )==0) { l->cur.type=T_KW_FUNC;   return; }
        l->cur.type = T_IDENT;
        return;
    }

    /* operators */
    l->pos++;
    switch (c) {
        case ';': l->cur.type=T_SEMI;   return;
        case '(': l->cur.type=T_LPAREN; return;
        case ')': l->cur.type=T_RPAREN; return;
        case '{': l->cur.type=T_LBRACE; return;
        case '}': l->cur.type=T_RBRACE; return;
        case ',': l->cur.type=T_COMMA;  return;
        case '+': l->cur.type=T_PLUS;   return;
        case '-': l->cur.type=T_MINUS;  return;
        case '*': l->cur.type=T_STAR;   return;
        case '/': l->cur.type=T_SLASH;  return;
        case '=':
            if (l->pos < l->len && l->src[l->pos]=='=') { l->pos++; l->cur.type=T_EQEQ; }
            else l->cur.type=T_EQ;
            return;
        case '!':
            if (l->pos < l->len && l->src[l->pos]=='=') { l->pos++; l->cur.type=T_NEQ; }
            else l->cur.type=T_EOF;
            return;
        case '<':
            if (l->pos < l->len && l->src[l->pos]=='=') { l->pos++; l->cur.type=T_LE; }
            else l->cur.type=T_LT;
            return;
        case '>':
            if (l->pos < l->len && l->src[l->pos]=='=') { l->pos++; l->cur.type=T_GE; }
            else l->cur.type=T_GT;
            return;
        default: l->cur.type=T_EOF; return;
    }
}

/* ── Output target (NULL = terminal_write, non-NULL = accumulate to buf) ── */
static char *g_cl_out     = NULL;
static u32   g_cl_out_max = 0;
static u32   g_cl_out_len = 0;

/* ── Value ───────────────────────────────────────────────────────────────── */
typedef struct { bool is_str; i32 num; char str[64]; } cl_val_t;

static cl_val_t vnum(i32 n) { cl_val_t v; v.is_str=false; v.num=n; v.str[0]='\0'; return v; }
static cl_val_t vstr(const char *s) {
    cl_val_t v; v.is_str=true; v.num=0;
    kstrncpy(v.str,s,63); v.str[63]='\0'; return v;
}

/* ── Environment ─────────────────────────────────────────────────────────── */
#define CL_MAX_VARS 32
#define CL_MAX_FUNCS 16
#define CL_MAX_ARGS 4
typedef struct { char name[32]; bool is_str; i32 num; char str[64]; } cl_var_t;
typedef struct { 
    char name[32]; 
    bool is_native;
    cl_val_t (*native_fn)(cl_val_t *args, u32 nargs);
    u32 body_pos; 
    char args[CL_MAX_ARGS][32]; 
    u32 nargs; 
} cl_func_t;
typedef struct cl_env_s {
    cl_var_t vars[CL_MAX_VARS];
    u32 nvars;
    cl_func_t funcs[CL_MAX_FUNCS];
    u32 nfuncs;
    bool had_error, ret_called;
    i32 ret_num;
    bool ret_is_str;
    char ret_str[64];
    struct cl_env_s *parent;
} cl_env_t;

static cl_var_t *cl_find(cl_env_t *e, const char *name) {
    for (u32 i=0; i<e->nvars; i++)
        if (kstrcmp(e->vars[i].name,name)==0) return &e->vars[i];
    return e->parent ? cl_find(e->parent, name) : NULL;
}
static cl_var_t *cl_make_local(cl_env_t *e, const char *name) {
    for (u32 i=0; i<e->nvars; i++)
        if (kstrcmp(e->vars[i].name,name)==0) return &e->vars[i];
    if (e->nvars >= CL_MAX_VARS) return NULL;
    cl_var_t *v = &e->vars[e->nvars++];
    kstrncpy(v->name,name,31); v->name[31]='\0';
    v->is_str=false; v->num=0; v->str[0]='\0';
    return v;
}
static cl_var_t *cl_make(cl_env_t *e, const char *name) {
    cl_var_t *v = cl_find(e,name);
    if (v) return v;
    return cl_make_local(e, name);
}
static cl_func_t *cl_find_func(cl_env_t *e, const char *name) {
    for (u32 i=0; i<e->nfuncs; i++)
        if (kstrcmp(e->funcs[i].name,name)==0) return &e->funcs[i];
    return e->parent ? cl_find_func(e->parent, name) : NULL;
}

/* ── Expression evaluation ───────────────────────────────────────────────── */
static cl_val_t cl_expr(cl_env_t *e, lexer_t *l);
static void cl_block(cl_env_t *e, lexer_t *l);

static cl_val_t cl_primary(cl_env_t *e, lexer_t *l) {
    if (l->cur.type == T_NUM)   { cl_val_t v=vnum(l->cur.num); lex_next(l); return v; }
    if (l->cur.type == T_STR)   { cl_val_t v=vstr(l->cur.str); lex_next(l); return v; }
    if (l->cur.type == T_IDENT) {
        char name[32]; kstrncpy(name,l->cur.str,31); name[31]='\0';
        lex_next(l);
        if (l->cur.type == T_LPAREN) {
            lex_next(l);
            cl_val_t arg_vals[CL_MAX_ARGS]; u32 nargs = 0;
            while (l->cur.type != T_RPAREN && l->cur.type != T_EOF && nargs < CL_MAX_ARGS) {
                arg_vals[nargs++] = cl_expr(e,l);
                if (l->cur.type == T_COMMA) lex_next(l);
                else break;
            }
            if (l->cur.type == T_RPAREN) lex_next(l);
            
            cl_func_t *f = cl_find_func(e, name);
            if (f) {
                if (f->is_native) {
                    return f->native_fn(arg_vals, nargs);
                }
                cl_env_t fn_env; kmemset(&fn_env,0,sizeof(fn_env));
                cl_env_t *root = e; while(root->parent) root = root->parent;
                fn_env.parent = root;
                for (u32 i=0; i<f->nargs && i<nargs; i++) {
                    cl_var_t *v = cl_make_local(&fn_env, f->args[i]);
                    if (v) {
                        v->is_str = arg_vals[i].is_str;
                        if (arg_vals[i].is_str) kstrncpy(v->str, arg_vals[i].str, 63); else v->num = arg_vals[i].num;
                    }
                }
                lexer_t fn_lex; fn_lex.src = l->src; fn_lex.pos = f->body_pos; fn_lex.len = l->len;
                lex_next(&fn_lex);
                cl_block(&fn_env, &fn_lex);
                if (fn_env.ret_is_str) return vstr(fn_env.ret_str);
                return vnum(fn_env.ret_num);
            }
            return vnum(0);
        }
        cl_var_t *v = cl_find(e,name);
        if (!v) return vnum(0);
        return v->is_str ? vstr(v->str) : vnum(v->num);
    }
    if (l->cur.type == T_MINUS) { lex_next(l); return vnum(-cl_primary(e,l).num); }
    if (l->cur.type == T_LPAREN) {
        lex_next(l);
        cl_val_t v = cl_expr(e,l);
        if (l->cur.type==T_RPAREN) lex_next(l);
        return v;
    }
    lex_next(l); return vnum(0);
}

static cl_val_t cl_term(cl_env_t *e, lexer_t *l) {
    cl_val_t r = cl_primary(e,l);
    while (l->cur.type==T_STAR || l->cur.type==T_SLASH) {
        tok_t op=l->cur.type; lex_next(l);
        cl_val_t rr=cl_primary(e,l);
        r = (op==T_STAR) ? vnum(r.num*rr.num) : vnum(rr.num ? r.num/rr.num : 0);
    }
    return r;
}

static cl_val_t cl_arith(cl_env_t *e, lexer_t *l) {
    cl_val_t r = cl_term(e,l);
    while (l->cur.type==T_PLUS || l->cur.type==T_MINUS) {
        tok_t op=l->cur.type; lex_next(l);
        cl_val_t rr=cl_term(e,l);
        if (op==T_PLUS && (r.is_str || rr.is_str)) {
            char tmp[64];
            if (r.is_str) kstrncpy(tmp,r.str,63); else kitoa(r.num,tmp,10);
            tmp[63]='\0';
            u32 used=(u32)kstrlen(tmp);
            u32 rem=63-used;
            if (rr.is_str) kstrncpy(tmp+used,rr.str,rem); else kitoa(rr.num,tmp+used,10);
            tmp[63]='\0';
            r=vstr(tmp);
        } else {
            r = vnum(op==T_PLUS ? r.num+rr.num : r.num-rr.num);
        }
    }
    return r;
}

static cl_val_t cl_expr(cl_env_t *e, lexer_t *l) {
    cl_val_t r = cl_arith(e,l);
    tok_t op=l->cur.type;
    if (op==T_EQEQ||op==T_NEQ||op==T_LT||op==T_GT||op==T_LE||op==T_GE) {
        lex_next(l);
        cl_val_t rr=cl_arith(e,l);
        bool res;
        if (r.is_str || rr.is_str) {
            char ls[64],rs[64];
            if (r.is_str) kstrncpy(ls,r.str,63); else kitoa(r.num,ls,10);
            if (rr.is_str) kstrncpy(rs,rr.str,63); else kitoa(rr.num,rs,10);
            ls[63]=rs[63]='\0';
            int c2=kstrcmp(ls,rs);
            res = (op==T_EQEQ)?(c2==0):(op==T_NEQ)?(c2!=0):false;
        } else {
            switch(op) {
                case T_EQEQ: res=r.num==rr.num; break;
                case T_NEQ:  res=r.num!=rr.num; break;
                case T_LT:   res=r.num< rr.num; break;
                case T_GT:   res=r.num> rr.num; break;
                case T_LE:   res=r.num<=rr.num; break;
                case T_GE:   res=r.num>=rr.num; break;
                default: res=false; break;
            }
        }
        r=vnum(res?1:0);
    }
    return r;
}

/* ── Block skip (skip { ... } without executing) ────────────────────────── */
static void cl_skip_block(lexer_t *l) {
    /* called with l->cur == T_LBRACE */
    lex_next(l);
    int depth=1;
    while (depth>0 && l->cur.type!=T_EOF) {
        if (l->cur.type==T_LBRACE) depth++;
        else if (l->cur.type==T_RBRACE) depth--;
        if (depth>0) lex_next(l);
    }
    if (l->cur.type==T_RBRACE) lex_next(l);
}

/* ── Statement and block execution ──────────────────────────────────────── */
static void cl_block(cl_env_t *e, lexer_t *l);

static void cl_stmt(cl_env_t *e, lexer_t *l) {
    if (e->had_error || e->ret_called) return;

    /* var name = expr; */
    if (l->cur.type == T_KW_VAR) {
        lex_next(l);
        if (l->cur.type != T_IDENT) { e->had_error=true; return; }
        char name[32]; kstrncpy(name,l->cur.str,31); name[31]='\0';
        lex_next(l);
        if (l->cur.type != T_EQ) { e->had_error=true; return; }
        lex_next(l);
        cl_val_t v=cl_expr(e,l);
        cl_var_t *var=cl_make_local(e,name);
        if (var) { var->is_str=v.is_str; if(v.is_str)kstrncpy(var->str,v.str,63); else var->num=v.num; }
        if (l->cur.type==T_SEMI) lex_next(l);
        return;
    }

    /* print expr; */
    if (l->cur.type == T_KW_PRINT) {
        lex_next(l);
        cl_val_t v=cl_expr(e,l);
        char pb[80];
        if (v.is_str) kstrncpy(pb,v.str,79);
        else kitoa(v.num,pb,10);
        pb[79]='\0';
        if (g_cl_out) {
            u32 plen=(u32)kstrlen(pb);
            u32 room=g_cl_out_max>g_cl_out_len+1?g_cl_out_max-g_cl_out_len-1:0;
            u32 copy=plen<room?plen:room;
            kmemcpy(g_cl_out+g_cl_out_len,pb,copy); g_cl_out_len+=copy;
            if (g_cl_out_len<g_cl_out_max-1) g_cl_out[g_cl_out_len++]='\n';
            g_cl_out[g_cl_out_len]='\0';
        } else {
            terminal_write(pb); terminal_write("\n");
        }
        if (l->cur.type==T_SEMI) lex_next(l);
        return;
    }

    /* if (cond) { } [else { }] */
    if (l->cur.type == T_KW_IF) {
        lex_next(l);
        if (l->cur.type==T_LPAREN) lex_next(l);
        cl_val_t cond=cl_expr(e,l);
        if (l->cur.type==T_RPAREN) lex_next(l);

        if (cond.num) {
            if (l->cur.type==T_LBRACE) lex_next(l);
            cl_block(e,l);
            if (l->cur.type==T_RBRACE) lex_next(l);
            if (l->cur.type==T_KW_ELSE) { lex_next(l); if(l->cur.type==T_LBRACE) cl_skip_block(l); }
        } else {
            if (l->cur.type==T_LBRACE) cl_skip_block(l);
            if (l->cur.type==T_KW_ELSE) {
                lex_next(l);
                if (l->cur.type==T_LBRACE) lex_next(l);
                cl_block(e,l);
                if (l->cur.type==T_RBRACE) lex_next(l);
            }
        }
        return;
    }

    /* while (cond) { } */
    if (l->cur.type == T_KW_WHILE) {
        u32 replay=l->pos; /* position just after 'while' keyword */
        lex_next(l);       /* cur = '(' */
        if (l->cur.type==T_LPAREN) lex_next(l);
        cl_val_t cond=cl_expr(e,l);
        if (l->cur.type==T_RPAREN) lex_next(l);
        if (l->cur.type!=T_LBRACE) return;

        for (u32 iter=0; iter<10000 && !e->had_error && !e->ret_called; iter++) {
            if (!cond.num) { cl_skip_block(l); break; }
            lex_next(l); /* skip { */
            cl_block(e,l);
            if (l->cur.type==T_RBRACE) lex_next(l);
            /* re-evaluate condition */
            l->pos=replay;
            lex_next(l);
            if (l->cur.type==T_LPAREN) lex_next(l);
            cond=cl_expr(e,l);
            if (l->cur.type==T_RPAREN) lex_next(l);
            /* l->cur should be { again */
        }
        /* safety: if loop hit iteration cap with body still pending, skip it */
        if (l->cur.type==T_LBRACE) cl_skip_block(l);
        return;
    }

    /* return [expr]; */
    if (l->cur.type == T_KW_RETURN) {
        lex_next(l);
        if (l->cur.type!=T_SEMI) {
            cl_val_t v=cl_expr(e,l);
            e->ret_is_str = v.is_str;
            if (v.is_str) kstrncpy(e->ret_str, v.str, 63);
            else e->ret_num=v.num;
        }
        e->ret_called=true;
        if (l->cur.type==T_SEMI) lex_next(l);
        return;
    }

    /* func name(arg1, arg2) { } */
    if (l->cur.type == T_KW_FUNC) {
        lex_next(l);
        if (l->cur.type != T_IDENT) { e->had_error=true; return; }
        char fname[32]; kstrncpy(fname,l->cur.str,31); fname[31]='\0';
        lex_next(l);
        if (l->cur.type != T_LPAREN) { e->had_error=true; return; }
        lex_next(l);
        
        char args[CL_MAX_ARGS][32]; u32 nargs = 0;
        while (l->cur.type == T_IDENT && nargs < CL_MAX_ARGS) {
            kstrncpy(args[nargs], l->cur.str, 31); args[nargs][31]='\0';
            nargs++;
            lex_next(l);
            if (l->cur.type == T_COMMA) lex_next(l);
            else break;
        }
        if (l->cur.type != T_RPAREN) { e->had_error=true; return; }
        lex_next(l);
        if (l->cur.type != T_LBRACE) { e->had_error=true; return; }
        
        cl_env_t *root = e; while(root->parent) root = root->parent;
        if (root->nfuncs < CL_MAX_FUNCS) {
            cl_func_t *f = &root->funcs[root->nfuncs++];
            kstrncpy(f->name, fname, 31); f->name[31]='\0';
            f->nargs = nargs;
            for (u32 i=0; i<nargs; i++) {
                kstrncpy(f->args[i], args[i], 31); f->args[i][31]='\0';
            }
            f->body_pos = l->pos;
        }
        cl_skip_block(l);
        return;
    }

    /* name = expr; */
    if (l->cur.type == T_IDENT) {
        char name[32]; kstrncpy(name,l->cur.str,31); name[31]='\0';
        lex_next(l);
        if (l->cur.type==T_EQ) {
            lex_next(l);
            cl_val_t v=cl_expr(e,l);
            cl_var_t *var=cl_find(e,name);
            if (!var) var=cl_make(e,name);
            if (var) { var->is_str=v.is_str; if(v.is_str)kstrncpy(var->str,v.str,63); else var->num=v.num; }
            if (l->cur.type==T_SEMI) lex_next(l);
            return;
        }
        /* unknown expression statement — skip to ; */
        while (l->cur.type!=T_SEMI && l->cur.type!=T_EOF) lex_next(l);
        if (l->cur.type==T_SEMI) lex_next(l);
        return;
    }

    /* skip unrecognised token */
    if (l->cur.type!=T_EOF && l->cur.type!=T_RBRACE) lex_next(l);
}

static void cl_block(cl_env_t *e, lexer_t *l) {
    while (!e->had_error && !e->ret_called &&
           l->cur.type!=T_EOF && l->cur.type!=T_RBRACE)
        cl_stmt(e,l);
}

/* ── Native Functions ────────────────────────────────────────────────────── */
static void cl_register_native(cl_env_t *e, const char *name, cl_val_t (*native_fn)(cl_val_t*, u32)) {
    if (e->nfuncs >= CL_MAX_FUNCS) return;
    cl_func_t *f = &e->funcs[e->nfuncs++];
    kstrncpy(f->name, name, 31); f->name[31]='\0';
    f->is_native = true;
    f->native_fn = native_fn;
}

static cl_val_t nat_sys_alert(cl_val_t *args, u32 nargs) {
    if (nargs > 0 && args[0].is_str) notify_push("CareLang", args[0].str, COL_PRIMARY);
    return vnum(0);
}
static cl_val_t nat_sys_window(cl_val_t *args, u32 nargs) {
    if (nargs >= 1 && args[0].is_str) wm_open(APP_NONE, args[0].str, 100, 100, 400, 300);
    return vnum(0);
}
static cl_val_t nat_sys_beep(cl_val_t *args, u32 nargs) {
    if (nargs >= 1 && !args[0].is_str) speaker_beep(args[0].num, 200);
    return vnum(0);
}
static cl_val_t nat_sys_exec(cl_val_t *args, u32 nargs) {
    if (nargs >= 1 && !args[0].is_str) wm_open((app_id_t)args[0].num, "App", 150, 150, 600, 400);
    return vnum(0);
}

static void cl_init_natives(cl_env_t *env) {
    cl_register_native(env, "sys_alert", nat_sys_alert);
    cl_register_native(env, "sys_window", nat_sys_window);
    cl_register_native(env, "sys_beep", nat_sys_beep);
    cl_register_native(env, "sys_exec", nat_sys_exec);
}

/* ── Public entry points ─────────────────────────────────────────────────── */

/* Output goes to terminal_write (VGA text console / kernel shell) */
int care_lang_exec(const char *src, u32 len) {
    if (!src || len==0) return -1;
    g_cl_out=NULL; g_cl_out_max=0; g_cl_out_len=0;
    cl_env_t env; kmemset(&env,0,sizeof(env));
    cl_init_natives(&env);
    lexer_t  lex; lex.src=src; lex.pos=0; lex.len=len;
    lex_next(&lex);
    cl_block(&env,&lex);
    return env.had_error ? -1 : 0;
}

/* Output is accumulated into out[0..out_max-1] (for GUI terminal win_append) */
int care_lang_exec_buf(const char *src, u32 len, char *out, u32 out_max) {
    if (!src || len==0 || !out || out_max==0) return -1;
    out[0]='\0';
    g_cl_out=out; g_cl_out_max=out_max; g_cl_out_len=0;
    cl_env_t env; kmemset(&env,0,sizeof(env));
    cl_init_natives(&env);
    lexer_t  lex; lex.src=src; lex.pos=0; lex.len=len;
    lex_next(&lex);
    cl_block(&env,&lex);
    g_cl_out=NULL;
    return env.had_error ? -1 : 0;
}
