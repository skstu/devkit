#ifndef __SRC_SK_LOCAL_H_
#define __SRC_SK_LOCAL_H_

/* Allocator prototypes */
void *sk__malloc(size_t size);
void sk__free(void *ptr);
void sk__free_s(void **pptr);
void *sk__calloc(size_t count, size_t size);
char *sk__strdup(const char *s);
char *sk__strndup(const char *s, size_t n);
void *sk__realloc(void *ptr, size_t size);
void *sk__reallocf(void *ptr, size_t size);
char *sk__strcpy(char *dest, const char *src);
char *sk__strncpy(char *dest, const char *src, size_t n);
void *sk__memcpy(void *dest, const void *src, size_t n);
void *sk__memndup(const void *src, size_t n);
int sk__memcmp(const char *s1, const char *s2, size_t n);

#endif /*__SRC_SK_LOCAL_H_*/