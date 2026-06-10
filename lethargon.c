/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>
#include "lethargon.h"

static void die(const char *msg) {
	fprintf(stderr, "error: %s\n", msg);
	exit(1);
}

static void dief(const char *fmt, ...) {
	va_list ap;
	fprintf(stderr, "error: ");
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fprintf(stderr, "\n");
	exit(1);
}

void buf_init(Buf *b) {
	b->d = NULL;
	b->cap = b->len = 0;
}

void buf_reserve(Buf *b, size_t n) {
	if (b->cap >= n) return;
	size_t nc = b->cap ? b->cap * 2 : 512;
	while (nc < n) nc *= 2;
	uint8_t *p = realloc(b->d, nc);
	if (!p) die("realloc");
	b->d = p;
	b->cap = nc;
}

void buf_append(Buf *b, const void *s, size_t n) {
	buf_reserve(b, b->len + n);
	memcpy(b->d + b->len, s, n);
	b->len += n;
}

void buf_u8(Buf *b, uint8_t x) {
	buf_append(b, &x, 1);
}

void buf_u32(Buf *b, uint32_t x) {
	uint8_t tmp[4];
	tmp[0] = x;
	tmp[1] = x >> 8;
	tmp[2] = x >> 16;
	tmp[3] = x >> 24;
	buf_append(b, tmp, 4);
}

void buf_patch32(Buf *b, size_t off, uint32_t x) {
	if (off + 4 > b->len) die("patch32 oob");
	b->d[off+0] = x;
	b->d[off+1] = x >> 8;
	b->d[off+2] = x >> 16;
	b->d[off+3] = x >> 24;
}

static void le16(uint8_t *p, uint16_t x) {
	p[0]=x; p[1]=x>>8;
}

static void le32(uint8_t *p, uint32_t x) {
	p[0]=x;
	p[1]=x>>8;
	p[2]=x>>16;
	p[3]=x>>24;
}

static int isalpha_(char c) {
	return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||c=='_';
}

static int isalnum_(char c) {
	return isalpha_(c)||(c>='0'&&c<='9');
}

static int isdigit_(char c) {
	return c>='0'&&c<='9';
}

static int isspace_(char c) {
	return c==' '||c=='\t'||c=='\n'||c=='\r';
}

Lx *lx_new(char *src) {
	Lx *l = calloc(1, sizeof(Lx));
	l->src = src;
	l->pos = 0;
	l->line = 1;
	return l;
}

static Tok make_tok(Tk t, int line) {
	Tok tok;
	memset(&tok, 0, sizeof(tok));
	tok.t = t;
	tok.line = line;
	return tok;
}

static Tok lx_one(Lx *l) {
	char *s = l->src;
	while (isspace_(s[l->pos])) {
		if (s[l->pos]=='\n') l->line++;
		l->pos++;
	}
	if (s[l->pos]=='#') {
		while (s[l->pos] && s[l->pos]!='\n') l->pos++;
		return lx_one(l);
	}
	int ln = l->line;
	char c = s[l->pos];
	if (!c) return make_tok(TK_EOF, ln);
	if (c=='"') {
		l->pos++;
		int start = l->pos;
		int len = 0;
		while (s[l->pos] && s[l->pos]!='"') {
			if (s[l->pos]=='\\') l->pos++;
			l->pos++;
			len++;
		}
		if (!s[l->pos]) die("unterminated string");
		char *buf = malloc(len+1);
		int bi = 0, i = start;
		while (s[i] && s[i]!='"') {
			if (s[i]=='\\') {
				i++;
				switch(s[i]) {
				case 'n': buf[bi++]='\n'; break;
				case 't': buf[bi++]='\t'; break;
				case '\\': buf[bi++]='\\'; break;
				case '"': buf[bi++]='"'; break;
				case '0': buf[bi++]=0; break;
				default: buf[bi++]=s[i]; break;
				}
			} else {
				buf[bi++]=s[i];
			}
			i++;
		}
		buf[bi]=0;
		l->pos++;
		Tok tok = make_tok(TK_STR, ln);
		tok.s = buf;
		tok.slen = bi;
		return tok;
	}
	if (isdigit_(c)) {
		long n = 0;
		while (isdigit_(s[l->pos])) n=n*10+(s[l->pos++]-'0');
		Tok tok = make_tok(TK_NUM, ln);
		tok.num = n;
		return tok;
	}
	if (isalpha_(c)) {
		int start = l->pos;
		while (isalnum_(s[l->pos])) l->pos++;
		int len = l->pos - start;
		char *id = malloc(len+1);
		memcpy(id, s+start, len);
		id[len]=0;
		Tk t = TK_ID;
		if (!strcmp(id,"var"))    t=TK_VAR;
		else if (!strcmp(id,"const"))  t=TK_CONST;
		else if (!strcmp(id,"fn"))     t=TK_FN;
		else if (!strcmp(id,"if"))     t=TK_IF;
		else if (!strcmp(id,"elif"))   t=TK_ELIF;
		else if (!strcmp(id,"else"))   t=TK_ELSE;
		else if (!strcmp(id,"while"))  t=TK_WHILE;
		else if (!strcmp(id,"return")) t=TK_RETURN;
		else if (!strcmp(id,"true"))   t=TK_TRUE;
		else if (!strcmp(id,"false"))  t=TK_FALSE;
		else if (!strcmp(id,"out"))    t=TK_OUT;
		else if (!strcmp(id,"in"))     t=TK_IN;
		Tok tok = make_tok(t, ln);
		tok.s = id;
		return tok;
	}
	l->pos++;
	switch(c) {
	case '+': return make_tok(TK_PLUS, ln);
	case '-': return make_tok(TK_MINUS, ln);
	case '*': return make_tok(TK_STAR, ln);
	case '/': return make_tok(TK_SLASH, ln);
	case '%': return make_tok(TK_PCT, ln);
	case '(': return make_tok(TK_LPAREN, ln);
	case ')': return make_tok(TK_RPAREN, ln);
	case '{': return make_tok(TK_LBRACE, ln);
	case '}': return make_tok(TK_RBRACE, ln);
	case ';': return make_tok(TK_SEMI, ln);
	case ',': return make_tok(TK_COMMA, ln);
	case '!':
		if (s[l->pos]=='=') { l->pos++; return make_tok(TK_NEQ, ln); }
		return make_tok(TK_BANG, ln);
	case '=':
		if (s[l->pos]=='=') { l->pos++; return make_tok(TK_EQ, ln); }
		return make_tok(TK_ASSIGN, ln);
	case '<':
		if (s[l->pos]=='=') { l->pos++; return make_tok(TK_LE, ln); }
		return make_tok(TK_LT, ln);
	case '>':
		if (s[l->pos]=='=') { l->pos++; return make_tok(TK_GE, ln); }
		return make_tok(TK_GT, ln);
	case '&':
		if (s[l->pos]=='&') { l->pos++; return make_tok(TK_AND, ln); }
		dief("unexpected '&' on line %d", ln);
		break;
	case '|':
		if (s[l->pos]=='|') { l->pos++; return make_tok(TK_OR, ln); }
		dief("unexpected '|' on line %d", ln);
		break;
		dief("unexpected char '%c' on line %d", c, ln);
	}
	return make_tok(TK_EOF, ln);
}

void lx_advance(Lx *l) {
	l->cur = l->peek;
	l->peek = lx_one(l);
}

static void lx_init(Lx *l) {
	l->peek = lx_one(l);
	lx_advance(l);
}

static Tok cur(Lx *l) {
	return l->cur;
}

static Tok expect(Lx *l, Tk t) {
	if (l->cur.t != t) dief("expected token %d got %d on line %d", t, l->cur.t, l->cur.line);
	Tok tok = l->cur;
	lx_advance(l);
	return tok;
}

static int check(Lx *l, Tk t) {
	return l->cur.t == t;
}

static int match(Lx *l, Tk t) {
	if (check(l,t)) {
		lx_advance(l);
		return 1; 
	}
	return 0;
}

Nd *nd_new(Nt t) {
	Nd *n = calloc(1, sizeof(Nd));
	n->t = t;
	return n;
}

static void nd_add_ch(Nd *n, Nd *ch) {
	n->ch = realloc(n->ch, (n->nch+1)*sizeof(Nd*));
	n->ch[n->nch++] = ch;
}

static Nd *pexpr(Lx *l);

static Nd *pstmt(Lx *l);

static Nd *pblock(Lx *l);

static Nd *pprim(Lx *l) {
	Tok t = cur(l);
	if (t.t == TK_NUM) {
		lx_advance(l);
		Nd *n = nd_new(ND_NUM);
		n->num = t.num;
		return n;
	}
	if (t.t == TK_STR) {
		lx_advance(l);
		Nd *n = nd_new(ND_STR);
		n->s = t.s;
		n->slen = t.slen;
		return n;
	}
	if (t.t == TK_TRUE || t.t == TK_FALSE) {
		lx_advance(l);
		Nd *n = nd_new(ND_BOOL);
		n->bval = (t.t == TK_TRUE) ? 1 : 0;
		return n;
	}
	if (t.t == TK_IN) {
		lx_advance(l);
		expect(l, TK_LPAREN);
		expect(l, TK_RPAREN);
		return nd_new(ND_IN);
	}
	if (t.t == TK_OUT) {
		lx_advance(l);
		expect(l, TK_LPAREN);
		Nd *n = nd_new(ND_OUT);
		n->a = pexpr(l);
		expect(l, TK_RPAREN);
		return n;
	}
	if (t.t == TK_FN) {
		lx_advance(l);
		Nd *n = nd_new(ND_FN);
		n->s = NULL;
		expect(l, TK_LPAREN);
		n->params = NULL;
		n->npar = 0;
		if (!check(l, TK_RPAREN)) {
			do {
				Tok p = expect(l, TK_ID);
				n->params = realloc(n->params, (n->npar+1)*sizeof(char*));
				n->params[n->npar++] = p.s;
			} while (match(l, TK_COMMA));
		}
		expect(l, TK_RPAREN);
		n->a = pblock(l);
		return n;
	}
	if (t.t == TK_ID) {
		lx_advance(l);
		if (check(l, TK_LPAREN)) {
			lx_advance(l);
			Nd *n = nd_new(ND_CALL);
			n->s = t.s;
			if (!check(l, TK_RPAREN)) {
				do {
					nd_add_ch(n, pexpr(l));
				} while (match(l, TK_COMMA));
			}
			expect(l, TK_RPAREN);
			return n;
		}
		Nd *n = nd_new(ND_ID);
		n->s = t.s;
		return n;
	}
	if (t.t == TK_LPAREN) {
		lx_advance(l);
		Nd *n = pexpr(l);
		expect(l, TK_RPAREN);
		return n;
	}
	dief("unexpected token %d on line %d", t.t, t.line);
	return NULL;
}

static Nd *punary(Lx *l) {
	if (check(l, TK_BANG)) {
		lx_advance(l);
		Nd *n = nd_new(ND_UN);
		n->op = TK_BANG;
		n->a = punary(l);
		return n;
	}
	if (check(l, TK_MINUS)) {
		lx_advance(l);
		Nd *n = nd_new(ND_UN);
		n->op = TK_MINUS;
		n->a = punary(l);
		return n;
	}
	return pprim(l);
}

static Nd *pmul(Lx *l) {
	Nd *n = punary(l);
	while (check(l,TK_STAR)||check(l,TK_SLASH)||check(l,TK_PCT)) {
		Tk op = cur(l).t;
		lx_advance(l);
		Nd *r = nd_new(ND_BIN);
		r->op = op;
		r->a = n;
		r->b = punary(l);
		n = r;
	}
	return n;
}

static Nd *padd(Lx *l) {
	Nd *n = pmul(l);
	while (check(l,TK_PLUS)||check(l,TK_MINUS)) {
		Tk op = cur(l).t;
		lx_advance(l);
		Nd *r = nd_new(ND_BIN);
		r->op = op;
		r->a = n;
		r->b = pmul(l);
		n = r;
	}
	return n;
}

static Nd *pcmp(Lx *l) {
	Nd *n = padd(l);
	while (check(l,TK_LT)||check(l,TK_LE)||check(l,TK_GT)||check(l,TK_GE)) {
		Tk op = cur(l).t;
		lx_advance(l);
		Nd *r = nd_new(ND_BIN);
		r->op = op;
		r->a = n;
		r->b = padd(l);
		n = r;
	}
	return n;
}

static Nd *peq(Lx *l) {
	Nd *n = pcmp(l);
	while (check(l,TK_EQ)||check(l,TK_NEQ)) {
		Tk op = cur(l).t;
		lx_advance(l);
		Nd *r = nd_new(ND_BIN);
		r->op = op;
		r->a = n;
		r->b = pcmp(l);
		n = r;
	}
	return n;
}

static Nd *pand(Lx *l) {
	Nd *n = peq(l);
	while (check(l,TK_AND)) {
		lx_advance(l);
		Nd *r = nd_new(ND_BIN);
		r->op = TK_AND;
		r->a = n;
		r->b = peq(l);
		n = r;
	}
	return n;
}

static Nd *por(Lx *l) {
	Nd *n = pand(l);
	while (check(l,TK_OR)) {
		lx_advance(l);
		Nd *r = nd_new(ND_BIN);
		r->op = TK_OR;
		r->a = n;
		r->b = pand(l);
		n = r;
	}
	return n;
}

static Nd *pexpr(Lx *l) {
	Nd *n = por(l);
	if (check(l, TK_ASSIGN)) {
		if (n->t != ND_ID) die("lhs of assignment must be identifier");
		lx_advance(l);
		Nd *r = nd_new(ND_ASSIGN);
		r->s = n->s;
		r->a = pexpr(l);
		free(n);
		return r;
	}
	return n;
}

static Nd *pblock(Lx *l) {
	expect(l, TK_LBRACE);
	Nd *n = nd_new(ND_BLOCK);
	while (!check(l, TK_RBRACE) && !check(l, TK_EOF)) {
		nd_add_ch(n, pstmt(l));
	}
	expect(l, TK_RBRACE);
	return n;
}

static Nd *pstmt(Lx *l) {
	Tok t = cur(l);
	if (t.t == TK_VAR) {
		lx_advance(l);
		Tok nm = expect(l, TK_ID);
		expect(l, TK_ASSIGN);
		Nd *n = nd_new(ND_VAR);
		n->s = nm.s;
		n->a = pexpr(l);
		expect(l, TK_SEMI);
		return n;
	}
	if (t.t == TK_CONST) {
		lx_advance(l);
		Tok nm = expect(l, TK_ID);
		expect(l, TK_ASSIGN);
		Nd *n = nd_new(ND_CONST);
		n->s = nm.s;
		n->a = pexpr(l);
		expect(l, TK_SEMI);
		return n;
	}
	if (t.t == TK_RETURN) {
		lx_advance(l);
		Nd *n = nd_new(ND_RETURN);
		if (!check(l, TK_SEMI)) n->a = pexpr(l);
		expect(l, TK_SEMI);
		return n;
	}
	if (t.t == TK_IF) {
		lx_advance(l);
		Nd *n = nd_new(ND_IF);
		match(l, TK_LPAREN);
		n->a = pexpr(l);
		match(l, TK_RPAREN);
		n->b = pblock(l);
		Nd *cur_node = n;
		while (check(l, TK_ELIF)) {
			lx_advance(l);
			Nd *elif = nd_new(ND_IF);
			match(l, TK_LPAREN);
			elif->a = pexpr(l);
			match(l, TK_RPAREN);
			elif->b = pblock(l);
			cur_node->c = elif;
			cur_node = elif;
		}
		if (match(l, TK_ELSE)) {
			cur_node->c = pblock(l);
		}
		return n;
	}
	if (t.t == TK_WHILE) {
		lx_advance(l);
		Nd *n = nd_new(ND_WHILE);
		match(l, TK_LPAREN);
		n->a = pexpr(l);
		match(l, TK_RPAREN);
		n->b = pblock(l);
		return n;
	}
	if (t.t == TK_LBRACE) {
		return pblock(l);
	}
	Nd *n = pexpr(l);
	expect(l, TK_SEMI);
	return n;
}

static Nd *ptoplevel(Lx *l) {
	if (check(l, TK_FN)) {
		lx_advance(l);
		Tok nm = expect(l, TK_ID);
		Nd *n = nd_new(ND_FN);
		n->s = nm.s;
		expect(l, TK_LPAREN);
		n->params = NULL;
		n->npar = 0;
		if (!check(l, TK_RPAREN)) {
			do {
				Tok p = expect(l, TK_ID);
				n->params = realloc(n->params, (n->npar+1)*sizeof(char*));
				n->params[n->npar++] = p.s;
			} while (match(l, TK_COMMA));
		}
		expect(l, TK_RPAREN);
		n->a = pblock(l);
		return n;
	}
	return pstmt(l);
}

Nd *parse(Lx *l) {
	lx_init(l);
	Nd *prog = nd_new(ND_PROG);
	while (!check(l, TK_EOF)) {
		nd_add_ch(prog, ptoplevel(l));
	}
	return prog;
}

#define TEXT_BASE 0x10094u
#define ROD_BASE 0x500000u
#define BSS_BASE 0x600000u
#define BSS_SCRATCH 32u
#define BSS_SZ (MAX_GLBS * 4u + BSS_SCRATCH)
#define MAX_LOCALS 256
#define MAX_GLBS 1024
#define MAX_LOOPS 1024
#define MAX_FNS 256
#define MAX_FPATCHES 4096

typedef struct {
	Glb glbs[MAX_GLBS];
	int nglb;
	Buf rod;
	Buf code;
	Fsym fns[MAX_FNS];
	int nfn;
	Fpatch fpatches[MAX_FPATCHES];
	int nfpatch;
} Cg2;

static int g_find(Cg2 *g, const char *nm) {
	for (int i=0; i<g->nglb; i++)
		if (!strcmp(g->glbs[i].name, nm)) return i;
	return -1;
}

static int g_add(Cg2 *g, const char *nm, int is_const) {
	if (g->nglb >= MAX_GLBS) die("too many globals");
	g->glbs[g->nglb].name = strdup(nm);
	g->glbs[g->nglb].goff = g->nglb * 4;
	g->glbs[g->nglb].is_const = is_const;
	g->glbs[g->nglb].cval = 0;
	g->glbs[g->nglb].is_str = 0;
	g->glbs[g->nglb].soff = 0;
	return g->nglb++;
}

static int rod_add(Cg2 *g, const char *s, int len) {
	int off = (int)g->rod.len;
	buf_append(&g->rod, s, len+1);
	return off;
}

static uint32_t cpos(Cg2 *g) {
	return (uint32_t)g->code.len;
}

static void A(Cg2 *g, uint32_t instr) {
	buf_u32(&g->code, instr); 
}

static void arm_push(Cg2 *g, uint32_t reglist) {
	A(g, 0xE92D0000 | reglist);
}

static void arm_pop(Cg2 *g, uint32_t reglist) {
	A(g, 0xE8BD0000 | reglist);
}

static uint32_t arm_imm8r(uint32_t v) {
	for (int rot=0; rot<16; rot++) {
		uint32_t rv = (v >> (rot*2)) | (v << (32-rot*2));
		if (rv <= 0xFF) return (rot << 8) | rv;
		rv = (v << (rot*2)) | (v >> (32-rot*2));
		if (rv <= 0xFF) return (rot << 8) | rv;
	}
	return 0xFFFFFFFF;
}

static int can_imm8r(uint32_t v) {
	return arm_imm8r(v) != 0xFFFFFFFF || v == 0;
}

static void arm_mov_r(Cg2 *g, int rd, uint32_t val) {
	if (val == 0) {
		A(g, 0xE3A00000 | (rd<<12));
		return;
	}
	uint32_t enc = arm_imm8r(val);
	if (enc != 0xFFFFFFFF) {
		A(g, 0xE3A00000 | (rd<<12) | enc);
		return;
	}
	uint32_t lo = val & 0xFFFF;
	uint32_t hi = (val >> 16) & 0xFFFF;
	A(g, 0xE3000000 | (rd<<12) | ((lo>>12)<<16) | (lo&0xFFF));
	if (hi) {
		A(g, 0xE3400000 | (rd<<12) | ((hi>>12)<<16) | (hi&0xFFF));
	}
}

static void arm_mov_rr(Cg2 *g, int rd, int rs) {
	A(g, 0xE1A00000 | (rd<<12) | rs);
}

static void arm_add_rr(Cg2 *g, int rd, int rn, int rm) {
	A(g, 0xE0800000 | (rd<<12) | (rn<<16) | rm);
}

static void arm_sub_rr(Cg2 *g, int rd, int rn, int rm) {
	A(g, 0xE0400000 | (rd<<12) | (rn<<16) | rm);
}

static void arm_mul_rr(Cg2 *g, int rd, int rs, int rm) {
	A(g, 0xE0000090 | (rd<<16) | (rs<<8) | rm);
}

static void arm_cmp_rr(Cg2 *g, int rn, int rm) {
	A(g, 0xE1500000 | (rn<<16) | rm);
}

static void arm_and_rr(Cg2 *g, int rd, int rn, int rm) {
	A(g, 0xE0000000 | (rd<<12) | (rn<<16) | rm);
}

static void arm_orr_rr(Cg2 *g, int rd, int rn, int rm) {
	A(g, 0xE1800000 | (rd<<12) | (rn<<16) | rm);
}

static void arm_push1(Cg2 *g, int r) {
	arm_push(g, 1<<r);
}

static void arm_pop1(Cg2 *g, int r) {
	arm_pop(g, 1<<r);
}

static void arm_swi(Cg2 *g) {
	A(g, 0xEF000000);
}

static uint32_t emit_b_placeholder(Cg2 *g) {
	uint32_t pos = cpos(g);
	A(g, 0xEA000000);
	return pos;
}

static uint32_t emit_beq_placeholder(Cg2 *g) {
	uint32_t pos = cpos(g);
	A(g, 0x0A000000);
	return pos;
}

static uint32_t emit_bne_placeholder(Cg2 *g) {
	uint32_t pos = cpos(g);
	A(g, 0x1A000000);
	return pos;
}

static uint32_t emit_bhs_placeholder(Cg2 *g) {
	uint32_t pos = cpos(g);
	A(g, 0x2A000000);
	return pos;
}

static void patch_b(Buf *b, uint32_t pos, uint32_t target) {
	int32_t off = ((int32_t)(target - TEXT_BASE - (pos + 8))) >> 2;
	uint32_t existing;
	memcpy(&existing, b->d + pos, 4);
	uint32_t cond = existing & 0xFF000000;
	buf_patch32(b, pos, cond | (uint32_t)(off & 0x00FFFFFF));
}

static void arm_bl_placeholder(Cg2 *g, char *nm, Cg2 *gs) {
	if (gs->nfpatch >= MAX_FPATCHES) die("too many call patches");
	uint32_t pos = cpos(g);
	gs->fpatches[gs->nfpatch].pos = pos;
	gs->fpatches[gs->nfpatch].name = nm;
	gs->nfpatch++;
	A(g, 0xEB000000);
}

static void arm_syscall_write(Cg2 *g, int fd, int r_ptr, int r_len) {
	arm_mov_r(g, 7, 4);
	arm_mov_r(g, 0, fd);
	arm_mov_rr(g, 1, r_ptr);
	arm_mov_rr(g, 2, r_len);
	arm_swi(g);
}

static void arm_syscall_exit(Cg2 *g, int code) {
	arm_mov_r(g, 7, 1);
	arm_mov_r(g, 0, code);
	arm_swi(g);
}

static void arm_syscall_read1(Cg2 *g, int dst_r) {
	arm_mov_r(g, 7, 3);
	arm_mov_r(g, 0, 0);
	arm_mov_rr(g, 1, dst_r);
	arm_mov_r(g, 2, 1);
	arm_swi(g);
}

#define TAG_INT 0
#define TAG_BOOL 1
#define TAG_STR 2
#define TAG_FN 3

typedef struct {
	char *names[MAX_LOCALS];
	int offs[MAX_LOCALS];
	int n;
	int frame_sz;
} Lenv;

static int lenv_find(Lenv *e, const char *nm) {
	for (int i=e->n-1; i>=0; i--)
		if (!strcmp(e->names[i], nm)) return e->offs[i];
	return -9999;
}

static void lenv_add(Lenv *e, const char *nm, int off) {
	if (e->n >= MAX_LOCALS) die("too many locals");
	e->names[e->n] = strdup(nm);
	e->offs[e->n] = off;
	e->n++;
}

static void gen_expr(Cg2 *g, Nd *n, Lenv *env, int in_fn);

static void gen_stmt(Cg2 *g, Nd *n, Lenv *env, int in_fn);

static void load_var(Cg2 *g, const char *nm, Lenv *env, int in_fn) {
	(void)in_fn;
	int lo = lenv_find(env, nm);
	if (lo != -9999) {
		if (lo < 0) {
			A(g, 0xE51B0000 | (0<<12) | (-lo));
		} else {
			A(g, 0xE59B0000 | (0<<12) | lo);
		}
		return;
	}
	int gi = g_find(g, nm);
	if (gi >= 0) {
		Glb *gl = &g->glbs[gi];
		if (gl->is_const && !gl->is_str) {
			arm_mov_r(g, 0, (uint32_t)gl->cval);
			return;
		}
		if (gl->is_const && gl->is_str) {
			arm_mov_r(g, 0, ROD_BASE + gl->soff);
			return;
		}
		arm_mov_r(g, 1, BSS_BASE + gl->goff);
		A(g, 0xE5910000);
		return;
	}
	dief("undefined variable '%s'", nm);
}

static void store_var(Cg2 *g, const char *nm, Lenv *env, int in_fn) {
	(void)in_fn;
	int lo = lenv_find(env, nm);
	if (lo != -9999) {
		if (lo < 0) {
			A(g, 0xE50B0000 | (0<<12) | (-lo));
		} else {
			A(g, 0xE58B0000 | (0<<12) | lo);
		}
		return;
	}
	int gi = g_find(g, nm);
	if (gi >= 0) {
		Glb *gl = &g->glbs[gi];
		if (gl->is_const) dief("assignment to const '%s'", nm);
		arm_mov_r(g, 1, BSS_BASE + gl->goff);
		A(g, 0xE5810000);
		return;
	}
	dief("undefined variable '%s'", nm);
}

static void gen_expr(Cg2 *g, Nd *n, Lenv *env, int in_fn) {
	switch (n->t) {
	case ND_NUM:
		arm_mov_r(g, 0, (uint32_t)n->num);
		break;
	case ND_BOOL:
		arm_mov_r(g, 0, n->bval ? 1 : 0);
		break;
	case ND_STR:
		arm_mov_r(g, 0, ROD_BASE + n->soff);
		break;
	case ND_ID:
		load_var(g, n->s, env, in_fn);
		break;
	case ND_ASSIGN:
		gen_expr(g, n->a, env, in_fn);
		store_var(g, n->s, env, in_fn);
		break;
	case ND_UN:
		gen_expr(g, n->a, env, in_fn);
		if (n->op == TK_MINUS) {
			A(g, 0xE2600000);
		} else if (n->op == TK_BANG) {
			arm_cmp_rr(g, 0, 0);
			arm_mov_r(g, 0, 0);
			A(g, 0x03A00001);
		}
		break;
	case ND_BIN: {
		gen_expr(g, n->a, env, in_fn);
		arm_push1(g, 0);
		gen_expr(g, n->b, env, in_fn);
		arm_pop1(g, 1);
		switch (n->op) {
		case TK_PLUS:  arm_add_rr(g, 0, 1, 0); break;
		case TK_MINUS: arm_sub_rr(g, 0, 1, 0); break;
		case TK_STAR:  arm_mul_rr(g, 0, 1, 0); break;
		case TK_SLASH:
			arm_mov_rr(g, 2, 0);
			arm_mov_rr(g, 0, 1); {
				arm_cmp_rr(g, 0, 2);
				uint32_t blt = emit_bne_placeholder(g);
				arm_mov_r(g, 0, 0);
				uint32_t bend = emit_b_placeholder(g);
				patch_b(&g->code, blt, cpos(g));
				arm_push1(g, 2);
				arm_push1(g, 0);
				arm_mov_r(g, 3, 0);
				uint32_t lp2 = cpos(g);
				arm_sub_rr(g, 0, 0, 2);
				A(g, 0xE2833001);
				arm_cmp_rr(g, 0, 2);
				uint32_t bge = emit_bne_placeholder(g);
				patch_b(&g->code, bge, lp2);
				arm_mov_rr(g, 0, 3);
				patch_b(&g->code, bend, cpos(g));
			}
			break;
		case TK_PCT:
			arm_mov_rr(g, 2, 0);
			arm_mov_rr(g, 0, 1); {
				uint32_t lp2 = cpos(g);
				arm_cmp_rr(g, 0, 2);
				uint32_t blt = emit_bne_placeholder(g);
				patch_b(&g->code, blt, cpos(g));
				arm_sub_rr(g, 0, 0, 2);
				patch_b(&g->code, blt, lp2);
			}
			break;
		case TK_LT:
			arm_cmp_rr(g, 1, 0);
			arm_mov_r(g, 0, 0);
			A(g, 0xB3A00001);
			break;
		case TK_LE:
			arm_cmp_rr(g, 1, 0);
			arm_mov_r(g, 0, 0);
			A(g, 0xD3A00001);
			break;
		case TK_GT:
			arm_cmp_rr(g, 1, 0);
			arm_mov_r(g, 0, 0);
			A(g, 0xC3A00001);
			break;
		case TK_GE:
			arm_cmp_rr(g, 1, 0);
			arm_mov_r(g, 0, 0);
			A(g, 0xA3A00001);
			break;
		case TK_EQ:
			arm_cmp_rr(g, 1, 0);
			arm_mov_r(g, 0, 0);
			A(g, 0x03A00001);
			break;
		case TK_NEQ:
			arm_cmp_rr(g, 1, 0);
			arm_mov_r(g, 0, 0);
			A(g, 0x13A00001);
			break;
		case TK_AND: arm_and_rr(g, 0, 1, 0); break;
		case TK_OR:  arm_orr_rr(g, 0, 1, 0); break;
		default: die("unknown binop");
		}
		break;
	}
	case ND_CALL: {
		for (int i=n->nch-1; i>=0; i--) {
			gen_expr(g, n->ch[i], env, in_fn);
			arm_push1(g, 0);
		}
		arm_bl_placeholder(g, n->s, g);
		if (n->nch > 0) {
			uint32_t adj = n->nch * 4;
			if (can_imm8r(adj))
				A(g, 0xE28DD000 | arm_imm8r(adj));
			else {
				arm_mov_r(g, 1, adj);
				arm_add_rr(g, 13, 13, 1);
			}
		}
		break;
	}
	case ND_OUT: {
		gen_expr(g, n->a, env, in_fn);
		arm_push1(g, 4);
		arm_push1(g, 5);
		arm_push1(g, 0);
		arm_bl_placeholder(g, "__out", g);
		A(g, 0xE28DD004);
		arm_pop1(g, 5);
		arm_pop1(g, 4);
		break;
	}
	case ND_IN: {
		arm_mov_r(g, 4, BSS_BASE + BSS_SZ - 4);
		arm_syscall_read1(g, 4);
		arm_mov_r(g, 1, BSS_BASE + BSS_SZ - 4);
		A(g, 0xE5910000);
		A(g, 0xE20000FF);
		break;
	}
	case ND_FN: {
		uint32_t skip = emit_b_placeholder(g);
		uint32_t fn_addr = TEXT_BASE + cpos(g);
		arm_push(g, (1<<4)|(1<<11)|(1<<14));
		arm_mov_rr(g, 11, 13);
		Lenv fenv;
		memset(&fenv, 0, sizeof(fenv));
		for (int i=0; i<n->npar; i++) {
			int off = 12 + i*4;
			lenv_add(&fenv, n->params[i], off);
		}
		gen_stmt(g, n->a, &fenv, 1);
		arm_pop(g, (1<<4)|(1<<11)|(1<<15));
		patch_b(&g->code, skip, TEXT_BASE + cpos(g));
		arm_mov_r(g, 0, fn_addr);
		break;
	}
	default:
		dief("gen_expr: unhandled node %d", n->t);
	}
}

static void gen_stmt(Cg2 *g, Nd *n, Lenv *env, int in_fn) {
	switch (n->t) {
	case ND_BLOCK:
		for (int i=0; i<n->nch; i++) gen_stmt(g, n->ch[i], env, in_fn);
		break;
	case ND_VAR: {
		gen_expr(g, n->a, env, in_fn);
		if (in_fn) {
			env->frame_sz += 4;
			int off = -(env->frame_sz);
			A(g, 0xE24DD004);
			A(g, 0xE50B0000 | (0<<12) | env->frame_sz);
			lenv_add(env, n->s, off);
		} else {
			int gi = g_add(g, n->s, 0);
			arm_mov_r(g, 1, BSS_BASE + g->glbs[gi].goff);
			A(g, 0xE5810000);
		}
		break;
	}
	case ND_CONST: {
		if (n->a->t == ND_NUM) {
			if (g_find(g, n->s) >= 0) dief("redefinition of '%s'", n->s);
			int gi = g_add(g, n->s, 1);
			g->glbs[gi].cval = n->a->num;
			g->glbs[gi].is_str = 0;
		} else if (n->a->t == ND_STR) {
			if (g_find(g, n->s) >= 0) dief("redefinition of '%s'", n->s);
			int gi = g_add(g, n->s, 1);
			g->glbs[gi].is_str = 1;
			g->glbs[gi].soff = rod_add(g, n->a->s, n->a->slen);
		} else {
			if (in_fn) {
				gen_expr(g, n->a, env, in_fn);
				env->frame_sz += 4;
				int off = -(env->frame_sz);
				A(g, 0xE24DD004);
				A(g, 0xE50B0000 | (0<<12) | env->frame_sz);
				lenv_add(env, n->s, off);
			} else {
				int gi = g_add(g, n->s, 0);
				gen_expr(g, n->a, env, in_fn);
				arm_mov_r(g, 1, BSS_BASE + g->glbs[gi].goff);
				A(g, 0xE5810000);
			}
		}
		break;
	}
	case ND_RETURN:
		if (n->a) gen_expr(g, n->a, env, in_fn);
		else arm_mov_r(g, 0, 0);
		if (env->frame_sz > 0) {
			uint32_t adj = env->frame_sz;
			if (can_imm8r(adj))
				A(g, 0xE28DD000 | arm_imm8r(adj));
			else {
				arm_mov_r(g, 1, adj);
				arm_add_rr(g, 13, 13, 1);
			}
		}
		arm_pop(g, (1<<4)|(1<<11)|(1<<15));
		break;
	case ND_IF: {
		gen_expr(g, n->a, env, in_fn);
		arm_cmp_rr(g, 0, 0);
		uint32_t bfalse = emit_beq_placeholder(g);
		gen_stmt(g, n->b, env, in_fn);
		if (n->c) {
			uint32_t bend = emit_b_placeholder(g);
			patch_b(&g->code, bfalse, TEXT_BASE + cpos(g));
			gen_stmt(g, n->c, env, in_fn);
			patch_b(&g->code, bend, TEXT_BASE + cpos(g));
		} else {
			patch_b(&g->code, bfalse, TEXT_BASE + cpos(g));
		}
		break;
	}
	case ND_WHILE: {
		uint32_t top = TEXT_BASE + cpos(g);
		gen_expr(g, n->a, env, in_fn);
		arm_cmp_rr(g, 0, 0);
		uint32_t bfalse = emit_beq_placeholder(g);
		gen_stmt(g, n->b, env, in_fn);
		uint32_t bb = emit_b_placeholder(g);
		patch_b(&g->code, bb, top);
		patch_b(&g->code, bfalse, TEXT_BASE + cpos(g));
		break;
	}
	case ND_FN: {
		if (!n->s) { gen_expr(g, n, env, in_fn); break; }
		if (g->nfn >= MAX_FNS) die("too many functions");
		uint32_t skip = emit_b_placeholder(g);
		uint32_t fn_addr = TEXT_BASE + cpos(g);
		g->fns[g->nfn].name = n->s;
		g->fns[g->nfn].addr = fn_addr;
		g->nfn++;
		arm_push(g, (1<<4)|(1<<11)|(1<<14));
		arm_mov_rr(g, 11, 13);
		Lenv fenv;
		memset(&fenv, 0, sizeof(fenv));
		for (int i=0; i<n->npar; i++) {
			int off = 12 + i*4;
			lenv_add(&fenv, n->params[i], off);
		}
		gen_stmt(g, n->a, &fenv, 1);
		arm_mov_r(g, 0, 0);
		if (fenv.frame_sz > 0) {
			uint32_t adj = fenv.frame_sz;
			if (can_imm8r(adj))
				A(g, 0xE28DD000 | arm_imm8r(adj));
			else {
				arm_mov_r(g, 1, adj);
				arm_add_rr(g, 13, 13, 1);
			}
		}
		arm_pop(g, (1<<4)|(1<<11)|(1<<15));
		patch_b(&g->code, skip, TEXT_BASE + cpos(g));
		break;
	}
	default:
		gen_expr(g, n, env, in_fn);
		break;
	}
}

static void emit_itoa_fn(Cg2 *g) {
	if (g->nfn >= MAX_FNS) die("too many functions");
	g->fns[g->nfn].name = "__itoa";
	g->fns[g->nfn].addr = TEXT_BASE + cpos(g);
	g->nfn++;
	arm_push(g, (1<<4)|(1<<5)|(1<<6)|(1<<14));
	arm_mov_rr(g, 4, 0);
	arm_mov_r(g, 5, BSS_BASE + BSS_SZ - 20);
	arm_mov_rr(g, 6, 5);
	arm_mov_r(g, 7, 10);
	/*handle zero specially*/
	arm_cmp_rr(g, 4, 4);
	A(g, 0xE3540000);
	uint32_t bnz = emit_bne_placeholder(g);
	A(g, 0xE2466001);
	arm_mov_r(g, 0, '0');
	A(g, 0xE5C60000);
	uint32_t bzero_done = emit_b_placeholder(g);
	patch_b(&g->code, bnz, TEXT_BASE + cpos(g));
	uint32_t dlp = TEXT_BASE + cpos(g);
	arm_mov_rr(g, 0, 4);
	arm_mov_r(g, 1, 0);
	arm_mov_r(g, 2, 0);
	arm_mov_rr(g, 2, 0);
	arm_mov_r(g, 1, 0); {
		uint32_t sl = TEXT_BASE + cpos(g);
		arm_cmp_rr(g, 2, 7);
		uint32_t blt = emit_bne_placeholder(g);
		buf_patch32(&g->code, blt, 0x3A000000);
		arm_sub_rr(g, 2, 2, 7);
		A(g, 0xE2811001);
		uint32_t bk = emit_b_placeholder(g);
		patch_b(&g->code, bk, sl);
		patch_b(&g->code, blt, TEXT_BASE + cpos(g));
	}
	A(g, 0xE2466001);
	A(g, 0xE2420030);
	A(g, 0xE2822030);
	A(g, 0xE5C62000);
	arm_mov_rr(g, 4, 1);
	A(g, 0xE3540000);
	uint32_t bloop = emit_bne_placeholder(g);
	patch_b(&g->code, bloop, dlp);
	patch_b(&g->code, bzero_done, TEXT_BASE + cpos(g));
	arm_mov_rr(g, 0, 6);
	arm_sub_rr(g, 1, 5, 6);
	arm_pop(g, (1<<4)|(1<<5)|(1<<6)|(1<<15));
}

static void emit_out_fn(Cg2 *g) {
	if (g->nfn >= MAX_FNS) die("too many functions");
	g->fns[g->nfn].name = "__out";
	g->fns[g->nfn].addr = TEXT_BASE + cpos(g);
	g->nfn++;
	arm_push(g, (1<<4)|(1<<5)|(1<<6)|(1<<14));
	arm_mov_rr(g, 4, 0);
	arm_mov_r(g, 5, ROD_BASE);
	arm_cmp_rr(g, 4, 5);
	uint32_t bstr = emit_bhs_placeholder(g);
	arm_mov_rr(g, 0, 4);
	arm_bl_placeholder(g, "__itoa", g);
	arm_mov_rr(g, 6, 0);
	arm_mov_rr(g, 2, 1);
	arm_mov_r(g, 7, 4);
	arm_mov_r(g, 0, 1);
	arm_mov_rr(g, 1, 6);
	arm_swi(g);
	uint32_t done = emit_b_placeholder(g);
	patch_b(&g->code, bstr, TEXT_BASE + cpos(g));
	arm_mov_rr(g, 1, 4);
	arm_mov_r(g, 2, 0); {
		uint32_t lp = TEXT_BASE + cpos(g);
		A(g, 0xE7D13002);
		A(g, 0xE3530000);
		uint32_t bz = emit_beq_placeholder(g);
		A(g, 0xE2822001);
		uint32_t bk = emit_b_placeholder(g);
		patch_b(&g->code, bk, lp);
		patch_b(&g->code, bz, TEXT_BASE + cpos(g));
	}
	arm_mov_r(g, 7, 4);
	arm_mov_r(g, 0, 1);
	arm_swi(g);
	patch_b(&g->code, done, TEXT_BASE + cpos(g));
	arm_pop(g, (1<<4)|(1<<5)|(1<<6)|(1<<15));
}

static void emit_out_str_fn(Cg2 *g) {
	if (g->nfn >= MAX_FNS) die("too many functions");
	g->fns[g->nfn].name = "__out_str";
	g->fns[g->nfn].addr = TEXT_BASE + cpos(g);
	g->nfn++;
	arm_push(g, (1<<4)|(1<<14));
	arm_mov_rr(g, 4, 0);
	arm_mov_rr(g, 1, 0);
	arm_mov_r(g, 2, 0); {
		uint32_t lp = TEXT_BASE + cpos(g);
		A(g, 0xE7D13002);
		A(g, 0xE3530000);
		uint32_t bz = emit_beq_placeholder(g);
		A(g, 0xE2822001);
		uint32_t bk = emit_b_placeholder(g);
		patch_b(&g->code, bk, lp);
		patch_b(&g->code, bz, TEXT_BASE + cpos(g));
	}
	arm_syscall_write(g, 1, 4, 2);
	arm_pop(g, (1<<4)|(1<<15));
}

void cg_init(Cg *cg) {
	buf_init(&cg->code);
	buf_init(&cg->rod);
	cg->goff = 0;
	cg->glbs = NULL;
	cg->nglb = 0;
	cg->entry = TEXT_BASE;
}

static Cg2 *cg2_new(void) {
	Cg2 *g = calloc(1, sizeof(Cg2));
	buf_init(&g->code);
	buf_init(&g->rod);
	return g;
}

static void resolve_strings(Cg2 *g, Nd *prog) {
	if (!prog) return;
	if (prog->t == ND_STR) {
		prog->soff = rod_add(g, prog->s, prog->slen);
		return;
	}
	if (prog->a) resolve_strings(g, prog->a);
	if (prog->b) resolve_strings(g, prog->b);
	if (prog->c) resolve_strings(g, prog->c);
	for (int i=0; i<prog->nch; i++) resolve_strings(g, prog->ch[i]);
}

void codegen(Nd *prog, Cg *cg_out) {
	Cg2 *g = cg2_new();
	resolve_strings(g, prog);
	emit_itoa_fn(g);
	emit_out_fn(g);
	emit_out_str_fn(g);
	uint32_t body_entry = TEXT_BASE + cpos(g);
	Lenv env;
	memset(&env, 0, sizeof(env));
	arm_push(g, (1<<4)|(1<<14));
	for (int i=0; i<prog->nch; i++) gen_stmt(g, prog->ch[i], &env, 0);
	arm_syscall_exit(g, 0);
	for (int i=0; i<g->nfpatch; i++) {
		Fpatch *fp = &g->fpatches[i];
		int found = 0;
		for (int j=0; j<g->nfn; j++) {
			if (!strcmp(g->fns[j].name, fp->name)) {
				patch_b(&g->code, fp->pos, g->fns[j].addr);
				found = 1;
				break;
			}
		}
		if (!found) dief("undefined function '%s'", fp->name);
	}
	cg_out->code = g->code;
	cg_out->rod = g->rod;
	cg_out->entry = body_entry;
	free(g);
}

void emit_elf(Cg *g, const char *path) {
	uint32_t ehsz = 52;
	uint32_t phsz = 32;
	uint32_t nph = 3;
	uint32_t hdrsz = ehsz + phsz * nph;
	uint32_t page = 0x1000;
	uint32_t rod_sz = (uint32_t)g->rod.len;
	uint32_t entry = g->entry;
	Buf file;
	buf_init(&file);
	for (uint32_t i = 0; i < hdrsz; i++) buf_u8(&file, 0);
	buf_append(&file, g->code.d, g->code.len);
	uint32_t rod_off = 0;
	if (rod_sz > 0) {
		while (file.len % page) buf_u8(&file, 0);
		rod_off = (uint32_t)file.len;
		buf_append(&file, g->rod.d, g->rod.len);
	}
	uint32_t text_filesz = rod_off ? rod_off - hdrsz : (uint32_t)file.len - hdrsz;
	uint8_t *E = file.d;
	E[0]=0x7f; E[1]='E'; E[2]='L'; E[3]='F';
	E[4]=1; E[5]=1; E[6]=1; E[7]=0;
	le16(E+0x10, 2);
	le16(E+0x12, 0x28);
	le32(E+0x14, 1);
	le32(E+0x18, entry);
	le32(E+0x1C, ehsz);
	le32(E+0x20, 0);
	le32(E+0x24, 0x05000000);
	le16(E+0x28, ehsz);
	le16(E+0x2A, phsz);
	le16(E+0x2C, nph);
	le16(E+0x2E, 0);
	le16(E+0x30, 0);
	le16(E+0x32, 0);
	uint8_t *P1 = E + ehsz;
	le32(P1+0x00, 1);
	le32(P1+0x04, hdrsz);
	le32(P1+0x08, TEXT_BASE);
	le32(P1+0x0C, TEXT_BASE);
	le32(P1+0x10, text_filesz);
	le32(P1+0x14, text_filesz);
	le32(P1+0x18, 5);
	le32(P1+0x1C, page);
	int pi = 1;
	if (rod_sz > 0) {
		uint8_t *P2 = E + ehsz + phsz * pi++;
		le32(P2+0x00, 1);
		le32(P2+0x04, rod_off);
		le32(P2+0x08, ROD_BASE);
		le32(P2+0x0C, ROD_BASE);
		le32(P2+0x10, rod_sz);
		le32(P2+0x14, rod_sz);
		le32(P2+0x18, 4);
		le32(P2+0x1C, page);
	}
	uint8_t *P3 = E + ehsz + phsz * pi;
	le32(P3+0x00, 1);
	le32(P3+0x04, 0);
	le32(P3+0x08, BSS_BASE);
	le32(P3+0x0C, BSS_BASE);
	le32(P3+0x10, 0);
	le32(P3+0x14, BSS_SZ);
	le32(P3+0x18, 6);
	le32(P3+0x1C, page);
	FILE *f = fopen(path, "wb");
	if (!f) { perror(path); exit(1); }
	if (fwrite(file.d, 1, file.len, f) != file.len) { perror("fwrite"); exit(1); }
	fclose(f);
	chmod(path, 0755);
	fprintf(stderr, "wrote ARM32 ELF to %s (%u bytes, entry 0x%x)\n", path, (uint32_t)file.len, entry);
	free(file.d);
}

char *rc(const char *path) {
	FILE *f = fopen(path, "rb");
	if (!f) { perror(path); exit(1); }
	fseek(f, 0, SEEK_END);
	long n = ftell(f);
	fseek(f, 0, SEEK_SET);
	char *buf = malloc(n+1);
	if (!buf) die("malloc");
	if (fread(buf, 1, n, f) != (size_t)n) { perror("fread"); exit(1); }
	fclose(f);
	buf[n] = 0;
	return buf;
}

int main(int argc, char **argv) {
	if (argc < 2) {
		fprintf(stderr, "usage: %s src.lt -o elf.out\n", argv[0]);
		return 1;
	}
	const char *in = argv[1];
	const char *out = "a.out";
	for (int i=2; i<argc; i++) {
		if (!strcmp(argv[i], "-o") && i+1 < argc) out = argv[++i];
		else { fprintf(stderr, "unknown arg: %s\n", argv[i]); return 1; }
	}
	char *src = rc(in);
	Lx *lx = lx_new(src);
	Nd *prog = parse(lx);
	Cg cg;
	cg_init(&cg);
	codegen(prog, &cg);
	emit_elf(&cg, out);
	free(src);
	return 0;
}
