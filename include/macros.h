#ifndef MACROS_H
#define MACROS_H

#define SK_FREE_PTR(p)                                                         \
  do {                                                                         \
    if (!p)                                                                    \
      break;                                                                   \
    free(p);                                                                   \
    p = nullptr;                                                               \
  } while (0);

#define SK_DELETE_PTR(p)                                                       \
  do {                                                                         \
    if (!p)                                                                    \
      break;                                                                   \
    delete p;                                                                  \
    p = nullptr;                                                               \
  } while (0);

#define SK_RELEASE_PTR(p)                                                      \
  do {                                                                         \
    if (!p)                                                                    \
      break;                                                                   \
    p->Release();                                                              \
    p = nullptr;                                                               \
  } while (0);

#endif // MACROS_H