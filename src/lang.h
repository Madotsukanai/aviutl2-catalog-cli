#ifndef LANG_H
#define LANG_H

const char * lang_get(const char *key);

#define _(x) lang_get(x)

#endif // LANG_H
