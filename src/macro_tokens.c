#include <string.h>

#include "token.h"
#include "macro_tokens.h"

#define N_MACRO_TOKENS 14
static const struct token macro_token_list[N_MACRO_TOKENS] = {
	{ "NOP",          Q_NOP          }, /* value = ignored     */
	{ "PRESS",        Q_KEY_PRESS    }, /* value = hid code    */
	{ "MAKE",         Q_KEY_MAKE     }, /* value = hid code    */
	{ "BREAK",        Q_KEY_RELEASE  }, /* value = hid code    */
	{ "ASSIGN_META",  Q_ASSIGN_META  }, /* value = metas       */
	{ "SET_META",     Q_SET_META     }, /* value = metas       */
	{ "CLEAR_META",   Q_CLEAR_META   }, /* value = metas       */
	{ "TOGGLE_META",  Q_TOGGLE_META  }, /* value = metas       */
	{ "POP_META",     Q_POP_META     }, /* value = ignored     */
	{ "POP_ALL_META", Q_POP_ALL_META }, /* value = ignored     */
	{ "DELAY",        Q_DELAY_MS     }, /* value = delay count */
	{ "CLEAR_ALL",    Q_CLEAR_ALL    }, /* (internal use)      */
	{ "BOOT",         Q_BOOT         }, /* value = ignored     */

	/* can be combnined with any other command. value = other command's value */
	{ "PUSH_META",    Q_PUSH_META    }
};

const char *lookup_macro_token_by_value(int value)
{
	int i;

	for (i = 0; i < N_MACRO_TOKENS; i++) {
		if (macro_token_list[i].value == value)
			return macro_token_list[i].token;
	}

	return "INVALID";
}

int lookup_macro_token_by_name(const char *name)
{
	int i;

	if (!name) goto ret;
	for (i = 0; i < N_MACRO_TOKENS; i++) {
		if (!strcmp(macro_token_list[i].token, name))
			return macro_token_list[i].value;
	}

ret:
	return INVALID_NUMBER;
}

int get_macro_arg_type(int cmd)
{
	int ret = INVALID_NUMBER;

	switch ( cmd & ~Q_PUSH_META ) {
	case Q_KEY_PRESS:
	case Q_KEY_MAKE:
	case Q_KEY_RELEASE:
		ret = MACRO_ARG_HID;
	break;
	case Q_ASSIGN_META:
	case Q_SET_META:
	case Q_CLEAR_META:
	case Q_TOGGLE_META:
		ret = MACRO_ARG_META;
	break;
	case Q_DELAY_MS:
		ret = MACRO_ARG_DELAY;
	break;
	case Q_NOP:
	case Q_POP_META:
	case Q_POP_ALL_META:
	case Q_CLEAR_ALL:
	case Q_BOOT:
		ret = MACRO_ARG_NONE;
	break;
	}

	return ret;
}

