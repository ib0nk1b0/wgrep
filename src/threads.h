
#define atomic_uint32_eval(x)   (uint32_t)__iso_volatile_load32((__int32*)(x))
#define atomic_uint32_inc(x)    _InterlockedIncrement((long*)&(x));
#define atomic_uint32_dec(x)    _InterlockedDecrement((long*)&(x));
#define atomic_uint64_add(x, v) _interlockedadd64((__int64 *)&(x), (v));
