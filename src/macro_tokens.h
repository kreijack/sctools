#ifndef MACRO_TOKENS_H
#define MACRO_TOKENS_H

enum queue_command {
	Q_NOP          = 0,    /* value = ignored                     */
	Q_KEY_PRESS    = 1,    /* value = hid code                    */
	Q_KEY_MAKE     = 2,    /* value = hid code                    */
	Q_KEY_RELEASE  = 3,    /* value = hid code                    */
	Q_ASSIGN_META  = 4,    /* value = metas                       */
	Q_SET_META     = 5,    /* value = metas                       */
	Q_CLEAR_META   = 6,    /* value = metas                       */
	Q_TOGGLE_META  = 7,    /* value = metas                       */
	Q_POP_META     = 8,    /* value = ignored                     */
	Q_POP_ALL_META = 9,    /* value = ignored                     */
	Q_DELAY_MS     = 10,   /* value = delay count                 */
	Q_CLEAR_ALL    = 11,   /* value = ignored                     */
	Q_BOOT         = 12,   /* value = ignored                     */
	Q_PUSH_META    = 0x80  /* can be or'ed with any other command */
};

#define MACRO_ARG_NONE  0
#define MACRO_ARG_HID   1
#define MACRO_ARG_META  2
#define MACRO_ARG_DELAY 3

int lookup_macro_token_by_name(const char *name);
const char *lookup_macro_token_by_value(int value);
int get_macro_arg_type(int cmd);

#endif /* MACRO_TOKENS_H */
