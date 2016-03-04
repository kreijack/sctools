#ifndef HID_TOKENS_H
#define HID_TOKENS_H

int lookup_hid_token_by_name(const char *name);
const char *lookup_hid_token_by_value(int value);
int lookup_meta_token(const char *name);

#define is_meta_handed(X) (!((X) & ((X) >> 4)))

#endif /* HID_TOKENS_H */
