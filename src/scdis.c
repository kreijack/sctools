/* scdis.c - config file disassembler for Soarer's Keyboard Converter. */

#include "hid_tokens.h"
#include "macro_tokens.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* File buffer */
#define FILE_BUFSIZ (16 * 1024)
static unsigned char filebuf[FILE_BUFSIZ];

/* Block types */
#define BLOCK_NONE     0xff
#define BLOCK_LAYERDEF 0
#define BLOCK_REMAP    1
#define BLOCK_MACRO    2

static FILE *fout = NULL;
static const char *protocols[2] = { "xt", "at" };
static const char *sets[8] = { "set1", "set2", "set3", "set2ext", "INVALIDSET", "INVALIDSET", "INVALIDSET", "INVALIDSET" };
static const char *metas[4] = { "CTRL", "SHIFT", "ALT", "GUI" };
static const char *hmetas[8] = {
	"LCTRL", "LSHIFT", "LALT", "LGUI",
	"RCTRL", "RSHIFT", "RALT", "RGUI"
};

static char *string_append(char *s, const char *s1, int with_space)
{
	char *ret = s;
	size_t slen = 0;

	if (s) slen = strlen(s);
	if (!s1 || !*s1) goto ret;

	s = realloc(ret, slen + strlen(s1) + 2);
	if (!s) {
		if (ret) free(ret);
		ret = NULL;
		goto ret;
	}

	memcpy(s + slen, s1, strlen(s1));
	if (with_space) {
		s[slen + strlen(s1)] = ' ';
		slen++;
	}

	s[slen + strlen(s1)] = '\0';
	ret = s;

ret:
	return ret;
}


static const char *get_force_set(unsigned char force)
{
	unsigned char force_set = force & 0x0F;
	if (1 <= force_set && force_set <= 4)
		return sets[force_set - 1];
	return "ERROR";
}

static const char *get_force_protocol(unsigned char force)
{
	unsigned char force_protocol = (force & 0xF0) >> 4;
	if (1 <= force_protocol && force_protocol <= 2)
		return protocols[force_protocol - 1];
	return "ERROR";
}

static char *get_ifset(unsigned char ifset)
{
	char *ret = NULL;
	unsigned int i;

	if (!ifset) {
		ret = malloc(4);
		if (ret) memcpy(ret, "any", 4);
		goto ret;
	}

	for (i = 0; i < 8 && ifset; i++) {
		if (ifset & 1)
			ret = string_append(ret, sets[i], 1);
		ifset >>= 1;
	}

ret:
	return ret;
}

static char *get_macro_match_metas(unsigned char desired, unsigned char matched)
{
	char *ret = NULL;
	unsigned int mask, i;
	unsigned char unhanded = (unsigned char)((desired & ~matched) & 0xf0);

	for (i = 0; i < 4; i++) {
		mask = (unsigned int)((1 << (i + 4)) | (1 << i));
		if (unhanded & mask) {
			ret = string_append(ret, metas[i], 1);
			if (!ret) goto ret;
			desired &= (unsigned char)~mask;
			matched &= (unsigned char)~mask;
		}
	}

	for (i = 0; i < 8; i++) {
		mask = (unsigned char)(1 << i);
		if (matched & mask) {
			if (!(desired & mask)) {
				ret = string_append(ret, "-", 0);
				if (!ret) goto ret;
			}

			ret = string_append(ret, hmetas[i], 1);
			if (!ret) goto ret;
		}
	}

ret:
	return ret;
}

static char *get_macrostep_metas(int val)
{
	unsigned int i;
	char *ret = NULL;

	for (i = 0; i < 8; i++) {
		if (val & (1 << i)) {
			ret = string_append(ret, metas[i], 1);
			if (!ret) goto ret;
		}
	}

ret:
	return ret;
}

static char *get_macrostep(int cmd, int val)
{
	char *ret = NULL;
	char buffer[64];
	int argtype = get_macro_arg_type(cmd);

	if (cmd & Q_PUSH_META)
		ret = string_append(ret, "PUSH_META ", 0);

	ret = string_append(ret,
	                    lookup_macro_token_by_value(cmd & ~Q_PUSH_META), 1);
	if (!ret) goto ret;

	switch (argtype) {
	case MACRO_ARG_NONE:
		break;
	case MACRO_ARG_HID:
		ret = string_append(ret, lookup_hid_token_by_value(val), 1);
		break;
	case MACRO_ARG_META:
		ret = string_append(ret, get_macrostep_metas(val), 1);
		break;
	case MACRO_ARG_DELAY:
		sprintf(buffer, "%d", val);
		ret = string_append(ret, buffer, 0);
		break;
	default:
		ret = string_append(ret, "INVALID", 0);
		break;
	}

ret:
	return ret;
}

static int process_layerblock(const unsigned char *buf,
                              const unsigned char *bufend)
{
	unsigned int buflen, i, j;
	unsigned char fn;

	fputs("layerblock\n", fout);
	buflen = (unsigned int)(bufend - buf);
	fprintf(fout, "# count: %u\n", buflen);

	if (buflen < 2 || buflen != (unsigned int)((buf[0] << 1) + 1)) {
		fputs("# ERROR: block size mismatch\n", fout);
		goto err;
	}

	for (i = 1; i < buflen; i += 2) {
		fputc('\t', fout);
		fn = buf[i];
		j = 1;

		while (fn) {
			if (fn & 1) fprintf(fout, "FN%d ", j);
			fn >>= 1; ++j;
		}

		/* layer */
		fprintf(fout, "%d\n", buf[i + 1]);
	}

	return 0;

err:
	return 1;
}

static int process_remapblock(const unsigned char *buf,
                              const unsigned char *bufend)
{
	unsigned int buflen, i;

	fputs("remapblock\n", fout);
	buflen = (unsigned int)(bufend - buf);

	if (buflen < 2 || buflen != (unsigned int)((buf[1] << 1) + 2)) {
		fputs("# ERROR: block size mismatch\n", fout);
		goto err;
	}

	fprintf(fout, "# count: %u\n", buf[1]);
	fprintf(fout, "layer %d\n", buf[0]);
	for (i = 2; i < buflen; i += 2) {
		fprintf(fout, "\t%s %s\n",
		        lookup_hid_token_by_value(buf[i]),
		        lookup_hid_token_by_value(buf[i + 1]));
	}

	return 0;

err:
	return 1;
}

/**
 * buf[0] = hid_code
 * buf[1] = desired_meta
 * buf[2] = matched_meta
 * buf[3] = press_flags
 * buf[4] = release_flags
 * buf[5 .. (press_flags & 0x3f)] = press commands
 * buf[5 + (press_flags & 0x3f) .. (release_flags & 0x3f)] = release commands
 */
static int process_macro(const unsigned char *buf, const unsigned char *bufend)
{
	char *s;
	unsigned int buflen, i, j;

	buflen = (unsigned int)(bufend - buf);
	if (buflen < 5) {
		fputs("# ERROR: macro truncated\n", fout);
		goto err;
	}

	s = get_macro_match_metas(buf[1], buf[2]);
	if (!s) goto err;

	fprintf(fout, "macro %s %s # %02X %02X\n",
	        lookup_hid_token_by_value(buf[0]), s, buf[1], buf[2]);
	free(s);

	i = (unsigned int)(5 + (((buf[3] & 0x3f) + (buf[4] & 0x3f)) << 1));
	if (buflen < i) {
		fputs("# ERROR: macro size mismatch\n", fout);
		goto err;
	}

	/* Presses */
	for (i = 0, j = 5; i < (buf[3] & 0x3f); i++, j += 2) {
		s = get_macrostep(buf[j], buf[j + 1]);
		if (!s) goto err;
		fprintf(fout, "\t%s\n", s);
		free(s);
	}

	/* Releases */
	if (buf[4] & 0x3f) {
		fprintf(fout, "onbreak%s\n",
		        (buf[4] & 0x40) ? "" : " norestoremeta");
	}

	for (i = 0; i < (buf[4] & 0x3f); i++, j += 2) {
		s = get_macrostep(buf[j], buf[j + 1]);
		if (!s) goto err;
		fprintf(fout, "\t%s\n", s);
		free(s);
	}

	fputs("endmacro\n", fout);
	return 0;

err:
	fputs("endmacro\n", fout);
	return 1;
}

static int process_macroblock(const unsigned char *buf,
                              const unsigned char *bufend)
{
	unsigned int buflen, macrolen, i, j;

	fputs("macroblock\n", fout);
	buflen = (unsigned int)(bufend - buf);

	if (buflen < (unsigned int)(buf[0] * 5)) {
		fputs("# ERROR: block size mismatch\n", fout);
		goto err;
	}

	fprintf(fout, "# macro count: %u\n", buf[0]);
	for (i = 0, j = 1; i < buf[0]; i++, j += macrolen) {
		macrolen = 5;
		macrolen += (unsigned int)((buf[j + 3] & 0x3f) << 1);
		macrolen += (unsigned int)((buf[j + 4] & 0x3f) << 1);

		if (process_macro(buf + j, buf + j + macrolen)) {
			fprintf(fout, "# ERROR: process_macro() failed on macro #%u\n", i);
			goto err;
		}
	}

	return 0;

err:
	return 1;

}

/**
 * buf[0] = block length
 * buf[1] = block flags
 *          masks:
 *             - 0x80 - has_id: a 2 byte keyboard ID is present
 *             - 0x40 - has_set a 1 byte set ID is present
 *             - 0x38 - block_select
 *             - 0x07 - block_type
 * buf[2] = set / block_id hi (if ! buf[1] & 0x40)
 * buf[3] = block_id lo (if ! buf[1] & 0x40) / block_id hi (if buf[1] & 0xC0)
 * buf[4] = block_id lo (if buf[1] & 0x80) / data
 */
static int process_block(const unsigned char * buf, size_t buflen)
{
	int ret = 1;
	unsigned short id;
	unsigned int i;
	char *s;
	const unsigned char *bufend = buf + buflen;

	fprintf(fout, "# block length: %lu\n", buflen);
	if (buflen < (unsigned int)(2 + ((buf[1] & 0xc0) >> 6))) {
		fputs("# ERROR: block truncated\n", fout);
		goto ret;
	}

	if (buflen != (size_t)buf[0]) {
		fputs("# ERROR: block size mismatch\n", fout);
		goto ret;
	}

	i = 2;
	if (buf[1] & 0x40) {
		s = get_ifset(buf[2]);
		if (!s) goto err;
		fprintf(fout, "ifset %s\n", s);
		free(s);
		++i;
	}

	if (buf[1] & 0x80) {
		id = (unsigned short)((buf[i + 1] << 8) | buf[i]);
		fprintf(fout, "ifkeyboard %04X\n", id);
		i += 2;
	} else fputs("ifkeyboard any\n", fout);

	if (buf[1] & 0x38)
		fprintf(fout, "ifselect %d\n", ((buf[1] & 0x38) >> 3));
	else fputs("ifselect any\n", fout);

	switch (buf[1] & 7) {
	case BLOCK_LAYERDEF: ret = process_layerblock(buf + i, bufend); break;
	case BLOCK_REMAP:    ret = process_remapblock(buf + i, bufend); break;
	case BLOCK_MACRO:    ret = process_macroblock(buf + i, bufend); break;
	case BLOCK_NONE:     ret = 0;                               break;
	default:
		fprintf(fout, "# ERROR: invalid block type %u\n", buf[1] & 7);
		break;
	}

ret:
	fputs("endblock\n", fout);
	return ret;

err:
	perror("unable to process block: ");
	return 1;
}

static int process_file(const unsigned char *buf, size_t buflen)
{
	unsigned int i;
	int ret = 1;

	/* header */
	fprintf(fout, "# length: %lu\n", buflen);
	fprintf(fout, "# signature: %c %c\n", buf[0], buf[1]);
	fprintf(fout, "# version: %d %d\n", buf[2], buf[3]);

	if (buf[4] & 0x0f)
		fprintf(fout, "force %s\n", get_force_set(buf[4]));

	if (buf[4] & 0xf0)
		fprintf(fout, "force %s\n", get_force_protocol(buf[4]));

	/* blocks (buf[5] is reserved) */
	ret = 0;
	for (i = 6; i < buflen; i += buf[i]) {
		if (!buf[i]) {
			fputs("ERROR: block length is zero!\n", fout);
			goto ret;
		}

		ret |= process_block(buf + i, buf[i]);
	}

ret:
	return ret;
}

int main(int argc, char** argv)
{
	FILE *fp;
	size_t buflen;

	puts("scdis v1.10");
	fout = stdout;

	if (argc != 2 && argc != 3) {
		fputs("usage: scdis <binary_config> [<text_config>]\n", stderr);
		exit(EXIT_FAILURE);
	}

	fp = fopen(argv[1], "rb");
	if (!fp) {
		fprintf(stderr, "error: could not open input file %s\n", argv[1]);
		exit(EXIT_FAILURE);
	}

	buflen = fread(filebuf, 1, FILE_BUFSIZ, fp);
	fclose(fp);

	if (argc == 3) {
		fout = fopen(argv[2], "w+");
		if (!fout) {
			fprintf(stderr, "error: could not open output file %s\n", argv[2]);
			exit(EXIT_FAILURE);
		}
	}

	if (process_file(filebuf, buflen)) {
		fclose(fout);
		fputs("errors encountered, see output file\n", stderr);
		exit(EXIT_FAILURE);
	}

	fclose(fout);
	return 0;
}
