#ifndef _BREAKPADCTYPE_H
#define _BREAKPADCTYPE_H

#include <libnetq/CType.h>

/*
 * Character classes libnetq/CType.h does not have, in the same style: ASCII
 * only, and the argument may be evaluated more than once.
 */

/* A character allowed in a file name the service writes: [A-Za-z0-9._+@-]. */
#define BreakpadIsSafeNameChar(c) \
  (NQIsDigit(c) || NQIsAlpha(c) || (c) == '.' || (c) == '_' || (c) == '-' || (c) == '+' || (c) == '@')

/* ASCII lower case; anything else is returned unchanged. */
#define BreakpadToLower(c) (NQIsUpper(c) ? (char)((c) | 0x20) : (c))

#endif /* _BREAKPADCTYPE_H */
