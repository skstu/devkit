/* Copyright Joyent, Inc. and other Node contributors. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#ifndef __SK_ERRNO_H_
#define __SK_ERRNO_H_

#include <errno.h>
#if EDOM > 0
#define SK__ERR(x) (-(x))
#else
#define SK__ERR(x) (x)
#endif

#define SK__EOF (-4095)
#define SK__UNKNOWN (-4094)

#define SK__EAI_ADDRFAMILY (-3000)
#define SK__EAI_AGAIN (-3001)
#define SK__EAI_BADFLAGS (-3002)
#define SK__EAI_CANCELED (-3003)
#define SK__EAI_FAIL (-3004)
#define SK__EAI_FAMILY (-3005)
#define SK__EAI_MEMORY (-3006)
#define SK__EAI_NODATA (-3007)
#define SK__EAI_NONAME (-3008)
#define SK__EAI_OVERFLOW (-3009)
#define SK__EAI_SERVICE (-3010)
#define SK__EAI_SOCKTYPE (-3011)
#define SK__EAI_BADHINTS (-3013)
#define SK__EAI_PROTOCOL (-3014)

/* Only map to the system errno on non-Windows platforms. It's apparently
 * a fairly common practice for Windows programmers to redefine errno codes.
 */
#if defined(E2BIG) && !defined(_WIN32)
#define SK__E2BIG SK__ERR(E2BIG)
#else
#define SK__E2BIG (-4093)
#endif

#if defined(EACCES) && !defined(_WIN32)
#define SK__EACCES SK__ERR(EACCES)
#else
#define SK__EACCES (-4092)
#endif

#if defined(EADDRINUSE) && !defined(_WIN32)
#define SK__EADDRINUSE SK__ERR(EADDRINUSE)
#else
#define SK__EADDRINUSE (-4091)
#endif

#if defined(EADDRNOTAVAIL) && !defined(_WIN32)
#define SK__EADDRNOTAVAIL SK__ERR(EADDRNOTAVAIL)
#else
#define SK__EADDRNOTAVAIL (-4090)
#endif

#if defined(EAFNOSUPPORT) && !defined(_WIN32)
#define SK__EAFNOSUPPORT SK__ERR(EAFNOSUPPORT)
#else
#define SK__EAFNOSUPPORT (-4089)
#endif

#if defined(EAGAIN) && !defined(_WIN32)
#define SK__EAGAIN SK__ERR(EAGAIN)
#else
#define SK__EAGAIN (-4088)
#endif

#if defined(EALREADY) && !defined(_WIN32)
#define SK__EALREADY SK__ERR(EALREADY)
#else
#define SK__EALREADY (-4084)
#endif

#if defined(EBADF) && !defined(_WIN32)
#define SK__EBADF SK__ERR(EBADF)
#else
#define SK__EBADF (-4083)
#endif

#if defined(EBUSY) && !defined(_WIN32)
#define SK__EBUSY SK__ERR(EBUSY)
#else
#define SK__EBUSY (-4082)
#endif

#if defined(ECANCELED) && !defined(_WIN32)
#define SK__ECANCELED SK__ERR(ECANCELED)
#else
#define SK__ECANCELED (-4081)
#endif

#if defined(ECHARSET) && !defined(_WIN32)
#define SK__ECHARSET SK__ERR(ECHARSET)
#else
#define SK__ECHARSET (-4080)
#endif

#if defined(ECONNABORTED) && !defined(_WIN32)
#define SK__ECONNABORTED SK__ERR(ECONNABORTED)
#else
#define SK__ECONNABORTED (-4079)
#endif

#if defined(ECONNREFUSED) && !defined(_WIN32)
#define SK__ECONNREFUSED SK__ERR(ECONNREFUSED)
#else
#define SK__ECONNREFUSED (-4078)
#endif

#if defined(ECONNRESET) && !defined(_WIN32)
#define SK__ECONNRESET SK__ERR(ECONNRESET)
#else
#define SK__ECONNRESET (-4077)
#endif

#if defined(EDESTADDRREQ) && !defined(_WIN32)
#define SK__EDESTADDRREQ SK__ERR(EDESTADDRREQ)
#else
#define SK__EDESTADDRREQ (-4076)
#endif

#if defined(EEXIST) && !defined(_WIN32)
#define SK__EEXIST SK__ERR(EEXIST)
#else
#define SK__EEXIST (-4075)
#endif

#if defined(EFAULT) && !defined(_WIN32)
#define SK__EFAULT SK__ERR(EFAULT)
#else
#define SK__EFAULT (-4074)
#endif

#if defined(EHOSTUNREACH) && !defined(_WIN32)
#define SK__EHOSTUNREACH SK__ERR(EHOSTUNREACH)
#else
#define SK__EHOSTUNREACH (-4073)
#endif

#if defined(EINTR) && !defined(_WIN32)
#define SK__EINTR SK__ERR(EINTR)
#else
#define SK__EINTR (-4072)
#endif

#if defined(EINVAL) && !defined(_WIN32)
#define SK__EINVAL SK__ERR(EINVAL)
#else
#define SK__EINVAL (-4071)
#endif

#if defined(EIO) && !defined(_WIN32)
#define SK__EIO SK__ERR(EIO)
#else
#define SK__EIO (-4070)
#endif

#if defined(EISCONN) && !defined(_WIN32)
#define SK__EISCONN SK__ERR(EISCONN)
#else
#define SK__EISCONN (-4069)
#endif

#if defined(EISDIR) && !defined(_WIN32)
#define SK__EISDIR SK__ERR(EISDIR)
#else
#define SK__EISDIR (-4068)
#endif

#if defined(ELOOP) && !defined(_WIN32)
#define SK__ELOOP SK__ERR(ELOOP)
#else
#define SK__ELOOP (-4067)
#endif

#if defined(EMFILE) && !defined(_WIN32)
#define SK__EMFILE SK__ERR(EMFILE)
#else
#define SK__EMFILE (-4066)
#endif

#if defined(EMSGSIZE) && !defined(_WIN32)
#define SK__EMSGSIZE SK__ERR(EMSGSIZE)
#else
#define SK__EMSGSIZE (-4065)
#endif

#if defined(ENAMETOOLONG) && !defined(_WIN32)
#define SK__ENAMETOOLONG SK__ERR(ENAMETOOLONG)
#else
#define SK__ENAMETOOLONG (-4064)
#endif

#if defined(ENETDOWN) && !defined(_WIN32)
#define SK__ENETDOWN SK__ERR(ENETDOWN)
#else
#define SK__ENETDOWN (-4063)
#endif

#if defined(ENETUNREACH) && !defined(_WIN32)
#define SK__ENETUNREACH SK__ERR(ENETUNREACH)
#else
#define SK__ENETUNREACH (-4062)
#endif

#if defined(ENFILE) && !defined(_WIN32)
#define SK__ENFILE SK__ERR(ENFILE)
#else
#define SK__ENFILE (-4061)
#endif

#if defined(ENOBUFS) && !defined(_WIN32)
#define SK__ENOBUFS SK__ERR(ENOBUFS)
#else
#define SK__ENOBUFS (-4060)
#endif

#if defined(ENODEV) && !defined(_WIN32)
#define SK__ENODEV SK__ERR(ENODEV)
#else
#define SK__ENODEV (-4059)
#endif

#if defined(ENOENT) && !defined(_WIN32)
#define SK__ENOENT SK__ERR(ENOENT)
#else
#define SK__ENOENT (-4058)
#endif

#if defined(ENOMEM) && !defined(_WIN32)
#define SK__ENOMEM SK__ERR(ENOMEM)
#else
#define SK__ENOMEM (-4057)
#endif

#if defined(ENONET) && !defined(_WIN32)
#define SK__ENONET SK__ERR(ENONET)
#else
#define SK__ENONET (-4056)
#endif

#if defined(ENOSPC) && !defined(_WIN32)
#define SK__ENOSPC SK__ERR(ENOSPC)
#else
#define SK__ENOSPC (-4055)
#endif

#if defined(ENOSYS) && !defined(_WIN32)
#define SK__ENOSYS SK__ERR(ENOSYS)
#else
#define SK__ENOSYS (-4054)
#endif

#if defined(ENOTCONN) && !defined(_WIN32)
#define SK__ENOTCONN SK__ERR(ENOTCONN)
#else
#define SK__ENOTCONN (-4053)
#endif

#if defined(ENOTDIR) && !defined(_WIN32)
#define SK__ENOTDIR SK__ERR(ENOTDIR)
#else
#define SK__ENOTDIR (-4052)
#endif

#if defined(ENOTEMPTY) && !defined(_WIN32)
#define SK__ENOTEMPTY SK__ERR(ENOTEMPTY)
#else
#define SK__ENOTEMPTY (-4051)
#endif

#if defined(ENOTSOCK) && !defined(_WIN32)
#define SK__ENOTSOCK SK__ERR(ENOTSOCK)
#else
#define SK__ENOTSOCK (-4050)
#endif

#if defined(ENOTSUP) && !defined(_WIN32)
#define SK__ENOTSUP SK__ERR(ENOTSUP)
#else
#define SK__ENOTSUP (-4049)
#endif

#if defined(EPERM) && !defined(_WIN32)
#define SK__EPERM SK__ERR(EPERM)
#else
#define SK__EPERM (-4048)
#endif

#if defined(EPIPE) && !defined(_WIN32)
#define SK__EPIPE SK__ERR(EPIPE)
#else
#define SK__EPIPE (-4047)
#endif

#if defined(EPROTO) && !defined(_WIN32)
#define SK__EPROTO SK__ERR(EPROTO)
#else
#define SK__EPROTO (-4046)
#endif

#if defined(EPROTONOSUPPORT) && !defined(_WIN32)
#define SK__EPROTONOSUPPORT SK__ERR(EPROTONOSUPPORT)
#else
#define SK__EPROTONOSUPPORT (-4045)
#endif

#if defined(EPROTOTYPE) && !defined(_WIN32)
#define SK__EPROTOTYPE SK__ERR(EPROTOTYPE)
#else
#define SK__EPROTOTYPE (-4044)
#endif

#if defined(EROFS) && !defined(_WIN32)
#define SK__EROFS SK__ERR(EROFS)
#else
#define SK__EROFS (-4043)
#endif

#if defined(ESHUTDOWN) && !defined(_WIN32)
#define SK__ESHUTDOWN SK__ERR(ESHUTDOWN)
#else
#define SK__ESHUTDOWN (-4042)
#endif

#if defined(ESPIPE) && !defined(_WIN32)
#define SK__ESPIPE SK__ERR(ESPIPE)
#else
#define SK__ESPIPE (-4041)
#endif

#if defined(ESRCH) && !defined(_WIN32)
#define SK__ESRCH SK__ERR(ESRCH)
#else
#define SK__ESRCH (-4040)
#endif

#if defined(ETIMEDOUT) && !defined(_WIN32)
#define SK__ETIMEDOUT SK__ERR(ETIMEDOUT)
#else
#define SK__ETIMEDOUT (-4039)
#endif

#if defined(ETXTBSY) && !defined(_WIN32)
#define SK__ETXTBSY SK__ERR(ETXTBSY)
#else
#define SK__ETXTBSY (-4038)
#endif

#if defined(EXDEV) && !defined(_WIN32)
#define SK__EXDEV SK__ERR(EXDEV)
#else
#define SK__EXDEV (-4037)
#endif

#if defined(EFBIG) && !defined(_WIN32)
#define SK__EFBIG SK__ERR(EFBIG)
#else
#define SK__EFBIG (-4036)
#endif

#if defined(ENOPROTOOPT) && !defined(_WIN32)
#define SK__ENOPROTOOPT SK__ERR(ENOPROTOOPT)
#else
#define SK__ENOPROTOOPT (-4035)
#endif

#if defined(ERANGE) && !defined(_WIN32)
#define SK__ERANGE SK__ERR(ERANGE)
#else
#define SK__ERANGE (-4034)
#endif

#if defined(ENXIO) && !defined(_WIN32)
#define SK__ENXIO SK__ERR(ENXIO)
#else
#define SK__ENXIO (-4033)
#endif

#if defined(EMLINK) && !defined(_WIN32)
#define SK__EMLINK SK__ERR(EMLINK)
#else
#define SK__EMLINK (-4032)
#endif

/* EHOSTDOWN is not visible on BSD-like systems when _POSIX_C_SOURCE is
 * defined. Fortunately, its value is always 64 so it's possible albeit
 * icky to hard-code it.
 */
#if defined(EHOSTDOWN) && !defined(_WIN32)
#define SK__EHOSTDOWN SK__ERR(EHOSTDOWN)
#elif defined(__APPLE__) || defined(__DragonFly__) || defined(__FreeBSD__) ||  \
    defined(__NetBSD__) || defined(__OpenBSD__)
#define SK__EHOSTDOWN (-64)
#else
#define SK__EHOSTDOWN (-4031)
#endif

#if defined(EREMOTEIO) && !defined(_WIN32)
#define SK__EREMOTEIO SK__ERR(EREMOTEIO)
#else
#define SK__EREMOTEIO (-4030)
#endif

#if defined(ENOTTY) && !defined(_WIN32)
#define SK__ENOTTY SK__ERR(ENOTTY)
#else
#define SK__ENOTTY (-4029)
#endif

#if defined(EFTYPE) && !defined(_WIN32)
#define SK__EFTYPE SK__ERR(EFTYPE)
#else
#define SK__EFTYPE (-4028)
#endif

#if defined(EILSEQ) && !defined(_WIN32)
#define SK__EILSEQ SK__ERR(EILSEQ)
#else
#define SK__EILSEQ (-4027)
#endif

#if defined(EOVERFLOW) && !defined(_WIN32)
#define SK__EOVERFLOW SK__ERR(EOVERFLOW)
#else
#define SK__EOVERFLOW (-4026)
#endif

#if defined(ESOCKTNOSUPPORT) && !defined(_WIN32)
#define SK__ESOCKTNOSUPPORT SK__ERR(ESOCKTNOSUPPORT)
#else
#define SK__ESOCKTNOSUPPORT (-4025)
#endif

/* FreeBSD defines ENODATA in /usr/include/c++/v1/errno.h which is only visible
 * if C++ is being used. Define it directly to avoid problems when integrating
 * libuv in a C++ project.
 */
#if defined(ENODATA) && !defined(_WIN32)
#define SK__ENODATA SK__ERR(ENODATA)
#elif defined(__FreeBSD__)
#define SK__ENODATA (-9919)
#else
#define SK__ENODATA (-4024)
#endif

#if defined(EUNATCH) && !defined(_WIN32)
#define SK__EUNATCH SK__ERR(EUNATCH)
#else
#define SK__EUNATCH (-4023)
#endif

#endif /* __SK_ERRNO_H_ */
