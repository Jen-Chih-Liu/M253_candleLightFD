#ifndef _COMPILER_H_
#define _COMPILER_H_

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifndef __aligned
#define __aligned(x) __attribute__((__aligned__(x)))
#endif

#ifndef __packed
#define __packed __attribute__((__packed__))
#endif

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif

#ifndef min
#define min(a, b) (((a) < (b)) ? (a) : (b))
#endif

#ifndef max
#define max(a, b) (((a) > (b)) ? (a) : (b))
#endif

#ifndef struct_size
#define struct_size(ptr, field, num) \
    (offsetof(__typeof__(*(ptr)), field) + sizeof((ptr)->field[0]) * (num))
#endif

#ifndef DECLARE_FLEX_ARRAY
#define DECLARE_FLEX_ARRAY(_t, _n) \
    struct { \
        struct { } __dummy_ ## _n; \
        _t _n[0]; \
    }
#endif

#ifndef IS_ENABLED
#define IS_ENABLED(cfg) ((cfg) == 1)
#endif

#endif /* _COMPILER_H_ */
