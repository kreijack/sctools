/* scas.c - config file assembler for Soarer's Keyboard Converter. */

#include "token.h"
#include "hid_tokens.h"
#include "macro_tokens.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

#define SETTINGS_VERSION_MAJOR 1
#define SETTINGS_VERSION_MINOR 1

#define COMMENT_CHAR '#'

/* Block types */
#define BLOCK_NONE     0xff
#define BLOCK_LAYERDEF 0
#define BLOCK_REMAP    1
#define BLOCK_MACRO    2

static int process_file(const char *fname);
typedef int (*command_fn)(const char* args);

/* State variables */
static unsigned char  current_force_flags = 0;
static unsigned char  current_select = 0;
static unsigned char  current_scanset = 0;
static unsigned short current_keyboard_id = 0;
static unsigned char  current_layer = 0;
static int current_macro_phase = -1; /* -1 = invalid, 0 = make, 1 = break */
static unsigned char current_macro_release_meta = 1;
static unsigned char current_hid_code = 0;
static unsigned char current_desired_meta = 0;
static unsigned char current_matched_meta = 0;
static unsigned char block_type = BLOCK_NONE;

struct pair_list {
	unsigned short *list;
	unsigned int len;
};

#define LAYERDEF_LIST     0
#define REMAP_LIST        1
#define PRESS_MCMD_LIST   2
#define RELEASE_MCMD_LIST 3
#define N_PAIR_LISTS      4

static struct pair_list pair_lists[N_PAIR_LISTS] = {
	{ NULL, 0 }, /* layerdef_list */
	{ NULL, 0 }, /* remap_list */
	{ NULL, 0 }, /* press_mcommand_list */
	{ NULL, 0 }, /* release_mcommand_list */
};

static void pair_list_push(int i, unsigned char a, unsigned char b)
{
	unsigned short *new_list;

	new_list = realloc(pair_lists[i].list,
	                   (pair_lists[i].len + 1) * sizeof(unsigned short));
	if (!new_list) {
		perror("pair_list_push(): unable to expand pair list: ");
		return;
	}

	new_list[pair_lists[i].len] = (unsigned short)((a << 8) | b);
	pair_lists[i].list = new_list;
	++pair_lists[i].len;
}

static void pair_list_clear(int i)
{
	if (pair_lists[i].list) {
		free(pair_lists[i].list);
		pair_lists[i].list = NULL;
	}

	pair_lists[i].len = 0;
}

struct macro {
	unsigned char    hid_code;
	unsigned char    desired_meta;
	unsigned char    matched_meta;
	unsigned char    press_flags;
	unsigned char    release_flags;
	struct pair_list commands;
};

static struct macro *macro_list = NULL;
static unsigned int macro_list_len = 0;

static void macro_list_push(struct macro *mac)
{
	struct macro *new_list;

	new_list = realloc(macro_list,
	                   (macro_list_len + 1) * sizeof(struct macro));
	if (!new_list) {
		perror("macro_list_append(): unable to append list: ");
		return;
	}

	memcpy(new_list + macro_list_len, mac, sizeof(struct macro));
	macro_list = new_list;
	++macro_list_len;
}

static void macro_list_clear(void)
{
	unsigned int i;

	for (i = 0; i < macro_list_len; i++)
		free(macro_list[i].commands.list);
	free(macro_list);
	macro_list_len = 0;
}

struct block {
	unsigned char *bytes;
	unsigned char len;
};

static struct block *block_list = NULL;
static unsigned int block_list_len = 0;

#define block_append(B, X) do {                  \
	(B)->bytes[(B)->len++] = (unsigned char)(X); \
	if (!(B)->len) goto ret;                     \
} while(0);

static struct block *block_alloc(void)
{
	struct block *block = malloc(sizeof(struct block));
	if (block) {
		block->bytes = malloc(255);
		block->len   = 0;

		if (!block->bytes) {
			free(block);
			block = NULL;
		}
	}

	return block;
}
static void block_list_append(struct block *block)
{
	struct block *new_list;

	new_list = realloc(block_list,
	                   (block_list_len + 1) * sizeof(struct block));
	if (!new_list) {
		perror("block_list_append(): unable to append: ");
		return;
	}

	memcpy(new_list + block_list_len, block, sizeof(struct block));
	block_list = new_list;
	++block_list_len;
}

#define ERR_FILE_NOT_FOUND	1
#define ERR_INVALID_COMMAND	2
#define ERR_INVALID_ARGS	3
#define ERR_BLOCK_TOO_LARGE	4
#define ERR_MACRO_TOO_LONG	5
#define ERR_FILE_WRITE		6
#define N_ERR_MESSAGES      7

static const char *err_messages[N_ERR_MESSAGES] = {
	"unknown error\n",
	"file not found\n",
	"invalid command\n",
	"invalid arguments\n",
	"block too large\n",
	"macro too long\n",
	"unable to open file for writing\n"
};

static void print_error(int err)
{
	if (!err || err >= N_ERR_MESSAGES)
		err = 0;
	fputs(err_messages[err], stderr);
}

static const char *skip_whitespace(const char *p)
{
	while (p && *p && isspace(*p)) ++p;
	return p;
}

static const char *skip_non_whitespace(const char *p)
{
	while (p && *p && !isspace(*p)) ++p;
	return p;
}

static const char *skip_to_end_quote(const char *p)
{
	if (p && *p != '\"') {
		while (*p && *p != '\"' && *p != '\\') ++p;
	}

	return p;
}

static const char *skip_token(const char *p)
{
	if (!p || !*p) goto ret;

	p = skip_whitespace(p);
	if (*p == '\"') {
		p = skip_to_end_quote(p+1);
		if (*p == '\"') ++p;
	} else p = skip_non_whitespace(p);
	p = skip_whitespace(p);

ret:
	return p;
}

static char *get_token(const char *p)
{
	const char *p2;
	ssize_t len;
	char *token = NULL;

	if (!p || !*p) goto ret;

	p = skip_whitespace(p);
	if (*p == '\"') p2 = skip_to_end_quote(p+1);
	else           	p2 = skip_non_whitespace(p);

	len = p2 - p;
	if (!len) goto ret;

	token = malloc((size_t)len + 1);
	if (token) {
		memcpy(token, p, (size_t)len);
		token[len] = '\0';
	}

ret:
	return token;
}

static int parse_int(const char *p, int minval, int maxval)
{
	int num = INVALID_NUMBER;

	if (!p || !*p) goto ret;
	p = skip_whitespace(p);
	if (isdigit(*p)) num = atoi(p);
	if (num < minval || num > maxval)
		num = INVALID_NUMBER;

ret:
	return num;
}

static int parse_hid(const char *p)
{
	int num = INVALID_NUMBER;
	char *t = get_token(p);
	if (!t) goto ret;

	num = lookup_hid_token_by_name(t);
	free(t);

ret:
	return num;
}

static int parse_meta_match(const char *p, int *desired, int *matched)
{
	char *t;
	int ret = 0;
	int meta, inverted, desired_meta = 0, matched_meta = 0;

	if (!p || !desired || !matched)
		goto ret;

	while (*p) {
		inverted = 0;
		if (*p == '-') {
			inverted = 1;
			++p;
		}

		t = get_token(p);
		if (t) {
			meta = lookup_meta_token(t);
			free(t);
		} else meta = INVALID_NUMBER;

		if (meta == INVALID_NUMBER) {
			ret = 0;
			goto ret;
		}

		if (inverted) {
			desired_meta &= ~meta;
			matched_meta |= meta;
		} else {
			desired_meta |= meta;
			if (is_meta_handed(meta)) matched_meta |= meta;
			else matched_meta |= (meta & 0x0F);
		}

		p = skip_token(p);
	}

	ret = 1;

ret:
	*desired = desired_meta;
	*matched = matched_meta;
	return ret;
}

static int parse_meta_handed(const char *p)
{
	char *t;
	int meta;
	int ret = 0;
	if (!p) goto ret;

	while (*p) {
		t = get_token(p);
		if (t) {
			meta = lookup_meta_token(t);
			free(t);
		} else meta = INVALID_NUMBER;

		if (meta == INVALID_NUMBER /*|| !is_meta_handed(meta)*/) {
			ret = INVALID_NUMBER;
			break;
		}

		ret |= meta;
		p = skip_token(p);
	}

ret:
	return ret;
}

static int parse_macro_cmd(const char *p, unsigned char *cmd,
                           unsigned char *val)
{
	char *t;
	int q = INVALID_NUMBER, c = INVALID_NUMBER, v = INVALID_NUMBER;

	if (!p || !cmd || !val)
		goto ret;

	t = get_token(p);
	if (t) {
		c = lookup_macro_token_by_name(t);
		free(t);
	}

	if (c == INVALID_NUMBER) {
		v = c;
		goto ret;
	}

	p = skip_token(p);
	/* todo: Q_PLAY */
	if (c == Q_PUSH_META) {
		t = get_token(p);
		if (t) {
			q = lookup_macro_token_by_name(t);
			free(t);
		} else q = INVALID_NUMBER;

		if (q == INVALID_NUMBER) {
			v = q;
			goto ret;
		}

		c |= q;
		p = skip_token(p);
	}

	t = get_token(p);
	switch (get_macro_arg_type(c)) {
	case MACRO_ARG_HID:   v = lookup_hid_token_by_name(t); break;
	case MACRO_ARG_META:  v = parse_meta_handed(p);        break;
	case MACRO_ARG_DELAY: v = parse_int(t, 0, 255);        break;
	case MACRO_ARG_NONE:  v = 0;                           break;
	}
	if (t) free(t);

ret:
	*cmd = (unsigned char)c;
	*val = (unsigned char)v;
	return v == INVALID_NUMBER;
}

static int lookup_set_token(const char *t)
{
	int ret = INVALID_NUMBER;
	if (!t) goto ret;

	if (!strcmp(t, "set1"))         ret = 1;
	else if (!strcmp(t, "set2"))    ret = 2;
	else if (!strcmp(t, "set3"))    ret = 3;
	else if (!strcmp(t, "set2ext")) ret = 4;
	else if (!strcmp(t, "any"))     ret = 5;

ret:
	return ret;
}

static int parse_single_set(const char *p)
{
	int s = INVALID_NUMBER;
	char *t = get_token(p);

	if (t) {
		s = lookup_set_token(t);
		free(t);
	}

	return s;
}

static int parse_multi_set(const char *p)
{
	char *t;
	int s, val = 0;

	while (p && *p) {
		t = get_token(p);
		if (t) {
			s = lookup_set_token(t);
			free(t);

			if (s == INVALID_NUMBER) {
				val = s;
				break;
			}

			if (s) val |= 1 << (s - 1);
			else   val = 0;
		}

		p = skip_token(p);
	}

	return val;
}

static int parse_function_n(const char *p)
{
	int v = INVALID_NUMBER;

	if (strlen(p) > 2 && !strncmp(p, "FN", 2)) {
		p += 2;
		if (isdigit(*p)) {
			v = atoi(p);
			if (v < 1 || v > 8)
				v = INVALID_NUMBER;
		}
	}

	return v;
}

static int cmd_force(const char *args)
{
	int ret = ERR_INVALID_ARGS;
	int set = parse_single_set(args);

	if (set != INVALID_NUMBER) {
		current_force_flags &= 0xf0;
		current_force_flags |= (unsigned char)(set & 0xff);
		ret = 0;
	}

	/* todo: XT/AT force? */
	return ret;
}

static int cmd_select(const char *args)
{
	int s, ret = ERR_INVALID_ARGS;
	char *t = get_token(args);

	if (t && !strcmp(t, "any")) s = 0;
	else s = parse_int(args, 1, 7);

	if (s != INVALID_NUMBER) {
		current_select = (unsigned char)s;
		ret = 0;
	}

	if (t) free(t);
	return ret;
}

static int cmd_scanset(const char *args)
{
	int ret = ERR_INVALID_ARGS;
	int s = parse_multi_set(args);

	if (s != INVALID_NUMBER) {
		current_scanset = (unsigned char)s;
		ret = 0;
	}

	return ret;
}

static int parse_hex(const char *p, long minval, long maxval)
{
	long v;

	errno = 0;
	v = strtol(p, NULL, 16);
	if (v <= minval || v >= maxval || errno == EINVAL || errno == ERANGE)
		v = INVALID_NUMBER;
	return (int)v;
}

static int cmd_keyboard_id(const char *args)
{
	int v = ERR_INVALID_ARGS;
	char *t = get_token(args);

	if (!t) goto ret;
	if (!strcmp(t, "any")) {
		current_keyboard_id = 0;
		v = 0;
	} else {
		v = parse_hex(args, 0, 0xffff);
		if (v != INVALID_NUMBER) {
			current_keyboard_id = (unsigned short)v;
			v = 0;
		}
	}
	free(t);

ret:
	return v;
}

static int cmd_layer(const char *args)
{
	int v = parse_int(args, 0, 255);

	if (v != INVALID_NUMBER)
		current_layer = (unsigned char)v;
	return (v == INVALID_NUMBER) ? ERR_INVALID_ARGS : 0;
}

static int cmd_layerdef(const char *p)
{
	int fn, n, ret = ERR_INVALID_ARGS;
	unsigned char fn_combo = 0;

	p = skip_whitespace(p);
	for (p = skip_whitespace(p); *p; p = skip_token(p)) {
		fn = parse_function_n(p);
		if (fn == INVALID_NUMBER) break;
		fn_combo |= (unsigned char)(1 << (fn - 1));
	}
	if (!fn_combo) goto ret;

	/* layer id */
	n = parse_int(p, 1, 255);
	if (n == INVALID_NUMBER) goto ret;

	pair_list_push(LAYERDEF_LIST, fn_combo, (unsigned char)n);
	ret = 0;

ret:
	return ret;
}

static int cmd_remap(const char *p)
{
	int ret = ERR_INVALID_ARGS;
	int v1, v2;

	if (!p) goto ret;
	p = skip_whitespace(p);
	v1 = parse_hid(p);
	if (v1 == INVALID_NUMBER) goto ret;

	p = skip_token(p);
	v2 = parse_hid(p);
	if (v2 == INVALID_NUMBER) goto ret;

	pair_list_push(REMAP_LIST, (unsigned char)v1, (unsigned char)v2);
	ret = 0;

ret:
	return ret;
}

static int cmd_macro(const char *args)
{
	int hid_code, desired_meta, matched_meta, ret = ERR_INVALID_ARGS;
	char *t = get_token(args);

	hid_code = lookup_hid_token_by_name(t);
	if (hid_code == INVALID_NUMBER) goto ret;

	if (!parse_meta_match(skip_token(args), &desired_meta, &matched_meta))
		goto ret;

	ret = 0;
	current_macro_phase = 0;
	current_macro_release_meta = 1;
	current_hid_code     = (unsigned char)hid_code;
	current_desired_meta = (unsigned char)desired_meta;
	current_matched_meta = (unsigned char)matched_meta;

ret:
	if (t) free(t);
	return ret;
}

static int cmd_onbreak(const char *args)
{
	int ret = ERR_INVALID_COMMAND;
	char *t = NULL;

	if (current_macro_phase != 0)
		goto ret;

	ret = 0;
	current_macro_phase = 1;
	t = get_token(args);
	if (!t) current_macro_release_meta = 1;
	else if (!strcmp(t, "norestoremeta"))
		current_macro_release_meta = 0;
	else ret = ERR_INVALID_COMMAND;

ret:
	if (t) free(t);
	return ret;
}

static int cmd_macrostep(const char *args)
{
	unsigned char cmd, val;
	int list = PRESS_MCMD_LIST, ret = ERR_INVALID_ARGS;

	if (!parse_macro_cmd(args, &cmd, &val)) {
		if (current_macro_phase) list = RELEASE_MCMD_LIST;
		pair_list_push(list, cmd, val);
		ret = 0;
	}

	return ret;
}

static int cmd_endmacro(const char *args)
{
	int ret = ERR_INVALID_COMMAND;
	unsigned int i;
	struct macro mac;
	(void)args;

	if (current_macro_phase == -1) goto ret;
	current_macro_phase = -1;
	mac.hid_code        = current_hid_code;
	mac.desired_meta    = current_desired_meta;
	mac.matched_meta    = current_matched_meta;
	mac.press_flags     = pair_lists[PRESS_MCMD_LIST].len & 0x3f;
	mac.release_flags   = pair_lists[RELEASE_MCMD_LIST].len & 0x3f;
	mac.release_flags  |= (unsigned char)(current_macro_release_meta << 7);
	i = mac.press_flags;

	if (pair_lists[PRESS_MCMD_LIST].len > 63 ||
	    pair_lists[RELEASE_MCMD_LIST].len > 63) {
		ret = ERR_MACRO_TOO_LONG;
		goto ret;
	}

	mac.commands.list = malloc((pair_lists[PRESS_MCMD_LIST].len +
	                            pair_lists[RELEASE_MCMD_LIST].len)
	                            * sizeof(unsigned short));
	if (!mac.commands.list) {
		perror("cmd_endmacro: Unable to allocate command list: ");
		goto ret;
	}

	memcpy(mac.commands.list, pair_lists[PRESS_MCMD_LIST].list,
	       i * sizeof(unsigned short));
	memcpy(mac.commands.list + i, pair_lists[RELEASE_MCMD_LIST].list,
	       (mac.release_flags & 0x3f) * sizeof(unsigned short));
	mac.commands.len = i + pair_lists[RELEASE_MCMD_LIST].len;

	ret = 0;
	pair_list_clear(PRESS_MCMD_LIST);
	pair_list_clear(RELEASE_MCMD_LIST);
	macro_list_push(&mac);

ret:
	return ret;
}

static int cmd_layerdefblock(const char *args)
{
	(void)args;

	if (block_type != BLOCK_NONE)
		return ERR_INVALID_COMMAND;
	block_type = BLOCK_LAYERDEF;
	return 0;
}

static int cmd_remapblock(const char *args)
{
	(void)args;

	if (block_type != BLOCK_NONE)
		return ERR_INVALID_COMMAND;
	block_type = BLOCK_REMAP;
	return 0;
}

static int cmd_macroblock(const char *args)
{
	(void)args;

	if (block_type != BLOCK_NONE)
		return ERR_INVALID_COMMAND;
	block_type = BLOCK_MACRO;
	return 0;
}

static int cmd_invalid(const char *args)
{
	int ret;

	switch (block_type) {
	case BLOCK_LAYERDEF: ret = cmd_layerdef(args);  break;
	case BLOCK_REMAP:    ret = cmd_remap(args);     break;
	case BLOCK_MACRO:    ret = cmd_macrostep(args); break;
	default:             ret = ERR_INVALID_COMMAND;
	}

	return ret;
}

static int cmd_include(const char *args)
{
	int ret;
	char *t = get_token(args);
	ret = process_file(t);
	free(t);
	return ret;
}

static void fill_block_header(struct block *block)
{
	/* placeholder for size */
	block_append(block, 0);

	/* flags */
	block_append(block, (unsigned char)(
		block_type | (current_select << 3) |
		((current_scanset != 0) << 6)      |
		((current_keyboard_id != 0) << 7)));

	if (current_scanset)
		block_append(block, current_scanset);

	if (current_keyboard_id) {
		block_append(block, current_keyboard_id & 0xff);
		block_append(block, (current_keyboard_id >> 8) & 0xff);
	}

ret:
	return;
}

static int cmd_endlayerdefblock(const char *args)
{
	int ret = ERR_BLOCK_TOO_LARGE;
	unsigned int i, x;
	struct block *block;
	(void)args;

	block = block_alloc();
	if (!block) goto ret;

	fill_block_header(block);
	block_append(block, (unsigned char)(pair_lists[LAYERDEF_LIST].len));
	for (i = 0; i < pair_lists[LAYERDEF_LIST].len; i++) {
		x = pair_lists[LAYERDEF_LIST].list[i];
		block_append(block, (unsigned char)(x >> 8));
		block_append(block, (unsigned char)(x & 0xff));
	}

	block->bytes[0] = block->len;
	block_list_append(block);
	free(block);
	pair_list_clear(LAYERDEF_LIST);
	block_type = BLOCK_NONE;
	ret = 0;

ret:
	return ret;
}

static int cmd_endremapblock(const char *args)
{
	int ret = ERR_BLOCK_TOO_LARGE;
	unsigned int i, x;
	struct block *block;
	(void)args;

	block = block_alloc();
	if (!block) goto ret;

	fill_block_header(block);
	block_append(block, current_layer);
	block_append(block, (unsigned char)pair_lists[REMAP_LIST].len);

	for (i = 0; i < pair_lists[REMAP_LIST].len; i++) {
		x = pair_lists[REMAP_LIST].list[i];
		block_append(block, (unsigned char)(x >> 8));
		block_append(block, (unsigned char)(x & 0xff));
	}

	block->bytes[0] = block->len;
	block_list_append(block);
	free(block);
	pair_list_clear(REMAP_LIST);
	block_type = BLOCK_NONE;
	ret = 0;

ret:
	return ret;
}

static int cmd_endmacroblock(const char *args)
{
	int ret = ERR_BLOCK_TOO_LARGE;
	unsigned int i, j, x;
	struct block *block;
	(void)args;

	block = block_alloc();
	if (!block) goto ret;

	fill_block_header(block);
	block_append(block, (unsigned char)macro_list_len);

	for (i = 0; i < macro_list_len; i++) {
		block_append(block, macro_list[i].hid_code);
		block_append(block, macro_list[i].desired_meta);
		block_append(block, macro_list[i].matched_meta);
		block_append(block, macro_list[i].press_flags);
		block_append(block, macro_list[i].release_flags);

		for (j = 0; j < macro_list[i].commands.len; j++) {
			x = macro_list[i].commands.list[j];
			block_append(block, (unsigned char)(x >> 8));
			block_append(block, (unsigned char)(x & 0xff));
		}
	}

	block->bytes[0] = block->len;
	block_list_append(block);
	free(block);
	macro_list_clear();
	block_type = BLOCK_NONE;
	ret = 0;

ret:
	return ret;
}

static int cmd_endblock(const char *args)
{
	int ret = ERR_INVALID_COMMAND;

	switch (block_type) {
	case BLOCK_LAYERDEF: ret = cmd_endlayerdefblock(args); break;
	case BLOCK_REMAP:    ret = cmd_endremapblock(args);    break;
	case BLOCK_MACRO:    ret = cmd_endmacroblock(args);    break;
	}

	return ret;
}

struct command {
	const char *cmd;
	command_fn  fn;
};

#define N_COMMANDS 13
static const struct command command_map[N_COMMANDS] =
{
	{ "force",      cmd_force         },
	{ "include",    cmd_include       },
	{ "ifselect",   cmd_select        },
	{ "ifset",      cmd_scanset       },
	{ "ifkeyboard", cmd_keyboard_id   },
	{ "remapblock", cmd_remapblock    },
	{ "layerblock", cmd_layerdefblock },
	{ "macroblock", cmd_macroblock    },
	{ "layer",      cmd_layer         },
	{ "macro",      cmd_macro         },
	{ "onbreak",    cmd_onbreak       },
	{ "endmacro",   cmd_endmacro      },
	{ "endblock",   cmd_endblock      }
};

static command_fn find_command(const char *cmd)
{
	int i;

	for (i = 0; i < N_COMMANDS; i++) {
		if (!strcmp(cmd, command_map[i].cmd))
			return command_map[i].fn;
	}

	return cmd_invalid;
}

static int process_line(char *linebuf)
{
	command_fn fn;
	int ret = 0;
	char *t;
	const char *p = linebuf;
	char *com = strchr(linebuf, COMMENT_CHAR);

	if (com) *com = 0;
	t = get_token(linebuf);
	if (t) {
		fn = find_command(t);
		if (fn != cmd_invalid) p = skip_token(linebuf);
		ret = fn(p);
		free(t);
	}

	return ret;
}

static int process_file(const char *fname)
{
	FILE *fp;
	int linenum = 0, err = 0;
	char linebuf[256];

	fp = fopen(fname, "r");
	if (!fp) {
		err = ERR_FILE_NOT_FOUND;
		goto ret;
	}

	while (fgets(linebuf, sizeof(linebuf), fp)) {
		++linenum;
		err = process_line(linebuf);
		if (err) {
			fprintf(stderr, "error at line %d: ", linenum);
			break;
		}
	}

ret:
	if (fp) fclose(fp);
	return err;
}

static int write_target(const char *fname)
{
	unsigned int i;
	int err = 0;
	FILE *fp;

	fp = fopen(fname, "wb+");
	if (!fp) {
		err = ERR_FILE_WRITE;
		goto ret;
	}

	/* Header... */
	fputc('S', fp); /* signature... */
	fputc('C', fp);
	fputc(SETTINGS_VERSION_MAJOR, fp);
	fputc(SETTINGS_VERSION_MINOR, fp);
	fputc(current_force_flags, fp);
	fputc(0, fp); /* reserved */

	/* Blocks... */
	for (i = 0; i < block_list_len && !err; i++) {
		if (!fwrite(block_list[i].bytes, 1, block_list[i].len, fp))
				err = ERR_FILE_WRITE;
	}

ret:
	if (fp) fclose(fp);
	return err;
}

int main(int argc, char *argv[])
{
	int err = EXIT_SUCCESS, i;
	unsigned int j;
	puts("scas v1.10");

	if (argc < 3) {
		fputs("usage: scas <text_config> "
		      "[<text_config> ...] <binary_config>\n", stderr);
		goto ret;
	}

	for (i = 1; i < argc - 1; i++) {
		err = process_file(argv[i]);
		if (err) {
			print_error(err);
			goto ret;
		}
	}

	err = write_target(argv[argc - 1]);
	if (err) {
		fprintf(stderr, "unable to write to file: %s\n",
		        argv[argc - 1]);
		goto ret;
	}

	fprintf(stderr, "No errors. Wrote: %s\n", argv[argc - 1]);

ret:
	for (j = 0; j < block_list_len; j++)
		free(block_list[j].bytes);
	free(block_list);
	return err == EXIT_SUCCESS ? err : EXIT_FAILURE;
}

