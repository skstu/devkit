#ifndef _SHARED_LIBRARY_H__
#define _SHARED_LIBRARY_H__

#if defined(WIN32)
#if defined(SHARED_IMPLEMENTATION)
#define SHARED_API __declspec(dllexport)
#define SHARED_API_PRIVATE __declspec(dllexport)
#else
#define SHARED_API __declspec(dllimport)
#define SHARED_API_PRIVATE __declspec(dllimport)
#endif  // defined(SHARED_IMPLEMENTATION)
#else   // WIN32
#if defined(SHARED_IMPLEMENTATION)
#define SHARED_API __attribute__((visibility("default")))
#define SHARED_API_PRIVATE __attribute__((visibility("default")))
#else
#define SHARED_API
#define SHARED_API_PRIVATE
#endif  // defined(SHARED_IMPLEMENTATION)
#endif  // WIN32

#endif  // _SHARED_LIBRARY_H__