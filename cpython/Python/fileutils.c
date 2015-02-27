#include "Python.h"
#include "osdefs.h"
#include <locale.h>

#ifdef MS_WINDOWS
#  include <windows.h>
#endif

#ifdef HAVE_LANGINFO_H
#include <langinfo.h>
#endif

#ifdef HAVE_SYS_IOCTL_H
#include <sys/ioctl.h>
#endif

#ifdef HAVE_FCNTL_H
#include <fcntl.h>
#endif /* HAVE_FCNTL_H */

#ifdef __APPLE__
extern wchar_t* _Py_DecodeUTF8_surrogateescape(const char *s, Py_ssize_t size);
#endif

#ifdef O_CLOEXEC
/* Does open() support the O_CLOEXEC flag? Possible values:

   -1: unknown
    0: open() ignores O_CLOEXEC flag, ex: Linux kernel older than 2.6.23
    1: open() supports O_CLOEXEC flag, close-on-exec is set

   The flag is used by _Py_open(), io.FileIO and os.open() */
int _Py_open_cloexec_works = -1;
#endif

PyObject *
_Py_device_encoding(int fd)
{
#if defined(MS_WINDOWS)
    UINT cp;
#endif
    if (!_PyVerify_fd(fd) || !isatty(fd)) {
        Py_RETURN_NONE;
    }
#if defined(MS_WINDOWS)
#ifndef MS_WINRT
    if (fd == 0)
        cp = GetConsoleCP();
    else if (fd == 1 || fd == 2)
        cp = GetConsoleOutputCP();
    else
#endif
        cp = 0;
    /* GetConsoleCP() and GetConsoleOutputCP() return 0 if the application
       has no console */
    if (cp != 0)
        return PyUnicode_FromFormat("cp%u", (unsigned int)cp);
#elif defined(CODESET)
    {
        char *codeset = nl_langinfo(CODESET);
        if (codeset != NULL && codeset[0] != 0)
            return PyUnicode_FromString(codeset);
    }
#endif
    Py_RETURN_NONE;
}

#if !defined(__APPLE__) && !defined(MS_WINDOWS)
extern int _Py_normalize_encoding(const char *, char *, size_t);

/* Workaround FreeBSD and OpenIndiana locale encoding issue with the C locale.
   On these operating systems, nl_langinfo(CODESET) announces an alias of the
   ASCII encoding, whereas mbstowcs() and wcstombs() functions use the
   ISO-8859-1 encoding. The problem is that os.fsencode() and os.fsdecode() use
   locale.getpreferredencoding() codec. For example, if command line arguments
   are decoded by mbstowcs() and encoded back by os.fsencode(), we get a
   UnicodeEncodeError instead of retrieving the original byte string.

   The workaround is enabled if setlocale(LC_CTYPE, NULL) returns "C",
   nl_langinfo(CODESET) announces "ascii" (or an alias to ASCII), and at least
   one byte in range 0x80-0xff can be decoded from the locale encoding. The
   workaround is also enabled on error, for example if getting the locale
   failed.

   Values of force_ascii:

       1: the workaround is used: Py_EncodeLocale() uses
          encode_ascii_surrogateescape() and Py_DecodeLocale() uses
          decode_ascii_surrogateescape()
       0: the workaround is not used: Py_EncodeLocale() uses wcstombs() and
          Py_DecodeLocale() uses mbstowcs()
      -1: unknown, need to call check_force_ascii() to get the value
*/
static int force_ascii = -1;

static int
check_force_ascii(void)
{
    char *loc;
#if defined(HAVE_LANGINFO_H) && defined(CODESET)
    char *codeset, **alias;
    char encoding[100];
    int is_ascii;
    unsigned int i;
    char* ascii_aliases[] = {
        "ascii",
        "646",
        "ansi-x3.4-1968",
        "ansi-x3-4-1968",
        "ansi-x3.4-1986",
        "cp367",
        "csascii",
        "ibm367",
        "iso646-us",
        "iso-646.irv-1991",
        "iso-ir-6",
        "us",
        "us-ascii",
        NULL
    };
#endif

    loc = setlocale(LC_CTYPE, NULL);
    if (loc == NULL)
        goto error;
    if (strcmp(loc, "C") != 0) {
        /* the LC_CTYPE locale is different than C */
        return 0;
    }

#if defined(HAVE_LANGINFO_H) && defined(CODESET)
    codeset = nl_langinfo(CODESET);
    if (!codeset || codeset[0] == '\0') {
        /* CODESET is not set or empty */
        goto error;
    }
    if (!_Py_normalize_encoding(codeset, encoding, sizeof(encoding)))
        goto error;

    is_ascii = 0;
    for (alias=ascii_aliases; *alias != NULL; alias++) {
        if (strcmp(encoding, *alias) == 0) {
            is_ascii = 1;
            break;
        }
    }
    if (!is_ascii) {
        /* nl_langinfo(CODESET) is not "ascii" or an alias of ASCII */
        return 0;
    }

    for (i=0x80; i<0xff; i++) {
        unsigned char ch;
        wchar_t wch;
        size_t res;

        ch = (unsigned char)i;
        res = mbstowcs(&wch, (char*)&ch, 1);
        if (res != (size_t)-1) {
            /* decoding a non-ASCII character from the locale encoding succeed:
               the locale encoding is not ASCII, force ASCII */
            return 1;
        }
    }
    /* None of the bytes in the range 0x80-0xff can be decoded from the locale
       encoding: the locale encoding is really ASCII */
    return 0;
#else
    /* nl_langinfo(CODESET) is not available: always force ASCII */
    return 1;
#endif

error:
    /* if an error occured, force the ASCII encoding */
    return 1;
}

static char*
encode_ascii_surrogateescape(const wchar_t *text, size_t *error_pos)
{
    char *result = NULL, *out;
    size_t len, i;
    wchar_t ch;

    if (error_pos != NULL)
        *error_pos = (size_t)-1;

    len = wcslen(text);

    result = PyMem_Malloc(len + 1);  /* +1 for NUL byte */
    if (result == NULL)
        return NULL;

    out = result;
    for (i=0; i<len; i++) {
        ch = text[i];

        if (ch <= 0x7f) {
            /* ASCII character */
            *out++ = (char)ch;
        }
        else if (0xdc80 <= ch && ch <= 0xdcff) {
            /* UTF-8b surrogate */
            *out++ = (char)(ch - 0xdc00);
        }
        else {
            if (error_pos != NULL)
                *error_pos = i;
            PyMem_Free(result);
            return NULL;
        }
    }
    *out = '\0';
    return result;
}
#endif   /* !defined(__APPLE__) && !defined(MS_WINDOWS) */

#if !defined(__APPLE__) && (!defined(MS_WINDOWS) || !defined(HAVE_MBRTOWC))
static wchar_t*
decode_ascii_surrogateescape(const char *arg, size_t *size)
{
    wchar_t *res;
    unsigned char *in;
    wchar_t *out;

    res = PyMem_RawMalloc((strlen(arg)+1)*sizeof(wchar_t));
    if (!res)
        return NULL;

    in = (unsigned char*)arg;
    out = res;
    while(*in)
        if(*in < 128)
            *out++ = *in++;
        else
            *out++ = 0xdc00 + *in++;
    *out = 0;
    if (size != NULL)
        *size = out - res;
    return res;
}
#endif


/* Decode a byte string from the locale encoding with the
   surrogateescape error handler: undecodable bytes are decoded as characters
   in range U+DC80..U+DCFF. If a byte sequence can be decoded as a surrogate
   character, escape the bytes using the surrogateescape error handler instead
   of decoding them.

   Return a pointer to a newly allocated wide character string, use
   PyMem_RawFree() to free the memory. If size is not NULL, write the number of
   wide characters excluding the null character into *size

   Return NULL on decoding error or memory allocation error. If *size* is not
   NULL, *size is set to (size_t)-1 on memory error or set to (size_t)-2 on
   decoding error.

   Decoding errors should never happen, unless there is a bug in the C
   library.

   Use the Py_EncodeLocale() function to encode the character string back to a
   byte string. */
wchar_t*
Py_DecodeLocale(const char* arg, size_t *size)
{
#ifdef __APPLE__
    wchar_t *wstr;
    wstr = _Py_DecodeUTF8_surrogateescape(arg, strlen(arg));
    if (size != NULL) {
        if (wstr != NULL)
            *size = wcslen(wstr);
        else
            *size = (size_t)-1;
    }
    return wstr;
#else
    wchar_t *res;
    size_t argsize;
    size_t count;
#ifdef HAVE_MBRTOWC
    unsigned char *in;
    wchar_t *out;
    mbstate_t mbs;
#endif

#ifndef MS_WINDOWS
    if (force_ascii == -1)
        force_ascii = check_force_ascii();

    if (force_ascii) {
        /* force ASCII encoding to workaround mbstowcs() issue */
        res = decode_ascii_surrogateescape(arg, size);
        if (res == NULL)
            goto oom;
        return res;
    }
#endif

#ifdef HAVE_BROKEN_MBSTOWCS
    /* Some platforms have a broken implementation of
     * mbstowcs which does not count the characters that
     * would result from conversion.  Use an upper bound.
     */
    argsize = strlen(arg);
#else
    argsize = mbstowcs(NULL, arg, 0);
#endif
    if (argsize != (size_t)-1) {
        res = (wchar_t *)PyMem_RawMalloc((argsize+1)*sizeof(wchar_t));
        if (!res)
            goto oom;
        count = mbstowcs(res, arg, argsize+1);
        if (count != (size_t)-1) {
            wchar_t *tmp;
            /* Only use the result if it contains no
               surrogate characters. */
            for (tmp = res; *tmp != 0 &&
                         !Py_UNICODE_IS_SURROGATE(*tmp); tmp++)
                ;
            if (*tmp == 0) {
                if (size != NULL)
                    *size = count;
                return res;
            }
        }
        PyMem_RawFree(res);
    }
    /* Conversion failed. Fall back to escaping with surrogateescape. */
#ifdef HAVE_MBRTOWC
    /* Try conversion with mbrtwoc (C99), and escape non-decodable bytes. */

    /* Overallocate; as multi-byte characters are in the argument, the
       actual output could use less memory. */
    argsize = strlen(arg) + 1;
    res = (wchar_t*)PyMem_RawMalloc(argsize*sizeof(wchar_t));
    if (!res)
        goto oom;
    in = (unsigned char*)arg;
    out = res;
    memset(&mbs, 0, sizeof mbs);
    while (argsize) {
        size_t converted = mbrtowc(out, (char*)in, argsize, &mbs);
        if (converted == 0)
            /* Reached end of string; null char stored. */
            break;
        if (converted == (size_t)-2) {
            /* Incomplete character. This should never happen,
               since we provide everything that we have -
               unless there is a bug in the C library, or I
               misunderstood how mbrtowc works. */
            PyMem_RawFree(res);
            if (size != NULL)
                *size = (size_t)-2;
            return NULL;
        }
        if (converted == (size_t)-1) {
            /* Conversion error. Escape as UTF-8b, and start over
               in the initial shift state. */
            *out++ = 0xdc00 + *in++;
            argsize--;
            memset(&mbs, 0, sizeof mbs);
            continue;
        }
        if (Py_UNICODE_IS_SURROGATE(*out)) {
            /* Surrogate character.  Escape the original
               byte sequence with surrogateescape. */
            argsize -= converted;
            while (converted--)
                *out++ = 0xdc00 + *in++;
            continue;
        }
        /* successfully converted some bytes */
        in += converted;
        argsize -= converted;
        out++;
    }
    if (size != NULL)
        *size = out - res;
#else   /* HAVE_MBRTOWC */
    /* Cannot use C locale for escaping; manually escape as if charset
       is ASCII (i.e. escape all bytes > 128. This will still roundtrip
       correctly in the locale's charset, which must be an ASCII superset. */
    res = decode_ascii_surrogateescape(arg, size);
    if (res == NULL)
        goto oom;
#endif   /* HAVE_MBRTOWC */
    return res;
oom:
    if (size != NULL)
        *size = (size_t)-1;
    return NULL;
#endif   /* __APPLE__ */
}

/* Encode a wide character string to the locale encoding with the
   surrogateescape error handler: surrogate characters in the range
   U+DC80..U+DCFF are converted to bytes 0x80..0xFF.

   Return a pointer to a newly allocated byte string, use PyMem_Free() to free
   the memory. Return NULL on encoding or memory allocation error.

   If error_pos is not NULL, *error_pos is set to the index of the invalid
   character on encoding error, or set to (size_t)-1 otherwise.

   Use the Py_DecodeLocale() function to decode the bytes string back to a wide
   character string. */
char*
Py_EncodeLocale(const wchar_t *text, size_t *error_pos)
{
#ifdef __APPLE__
    Py_ssize_t len;
    PyObject *unicode, *bytes = NULL;
    char *cpath;

    unicode = PyUnicode_FromWideChar(text, wcslen(text));
    if (unicode == NULL)
        return NULL;

    bytes = _PyUnicode_AsUTF8String(unicode, "surrogateescape");
    Py_DECREF(unicode);
    if (bytes == NULL) {
        PyErr_Clear();
        if (error_pos != NULL)
            *error_pos = (size_t)-1;
        return NULL;
    }

    len = PyBytes_GET_SIZE(bytes);
    cpath = PyMem_Malloc(len+1);
    if (cpath == NULL) {
        PyErr_Clear();
        Py_DECREF(bytes);
        if (error_pos != NULL)
            *error_pos = (size_t)-1;
        return NULL;
    }
    memcpy(cpath, PyBytes_AsString(bytes), len + 1);
    Py_DECREF(bytes);
    return cpath;
#else   /* __APPLE__ */
    const size_t len = wcslen(text);
    char *result = NULL, *bytes = NULL;
    size_t i, size, converted;
    wchar_t c, buf[2];

#ifndef MS_WINDOWS
    if (force_ascii == -1)
        force_ascii = check_force_ascii();

    if (force_ascii)
        return encode_ascii_surrogateescape(text, error_pos);
#endif

    /* The function works in two steps:
       1. compute the length of the output buffer in bytes (size)
       2. outputs the bytes */
    size = 0;
    buf[1] = 0;
    while (1) {
        for (i=0; i < len; i++) {
            c = text[i];
            if (c >= 0xdc80 && c <= 0xdcff) {
                /* UTF-8b surrogate */
                if (bytes != NULL) {
                    *bytes++ = c - 0xdc00;
                    size--;
                }
                else
                    size++;
                continue;
            }
            else {
                buf[0] = c;
                if (bytes != NULL)
                    converted = wcstombs(bytes, buf, size);
                else
                    converted = wcstombs(NULL, buf, 0);
                if (converted == (size_t)-1) {
                    if (result != NULL)
                        PyMem_Free(result);
                    if (error_pos != NULL)
                        *error_pos = i;
                    return NULL;
                }
                if (bytes != NULL) {
                    bytes += converted;
                    size -= converted;
                }
                else
                    size += converted;
            }
        }
        if (result != NULL) {
            *bytes = '\0';
            break;
        }

        size += 1; /* nul byte at the end */
        result = PyMem_Malloc(size);
        if (result == NULL) {
            if (error_pos != NULL)
                *error_pos = (size_t)-1;
            return NULL;
        }
        bytes = result;
    }
    return result;
#endif   /* __APPLE__ */
}

/* In principle, this should use HAVE__WSTAT, and _wstat
   should be detected by autoconf. However, no current
   POSIX system provides that function, so testing for
   it is pointless.
   Not sure whether the MS_WINDOWS guards are necessary:
   perhaps for cygwin/mingw builds?
*/
#if defined(HAVE_STAT) && !defined(MS_WINDOWS)

/* Get file status. Encode the path to the locale encoding. */

int
_Py_wstat(const wchar_t* path, struct stat *buf)
{
    int err;
    char *fname;
    fname = Py_EncodeLocale(path, NULL);
    if (fname == NULL) {
        errno = EINVAL;
        return -1;
    }
    err = stat(fname, buf);
    PyMem_Free(fname);
    return err;
}
#endif


#if defined(HAVE_FSTAT) || defined(MS_WINDOWS)

#ifdef MS_WINDOWS
static __int64 secs_between_epochs = 11644473600; /* Seconds between 1.1.1601 and 1.1.1970 */

static void
FILE_TIME_to_time_t_nsec(FILETIME *in_ptr, time_t *time_out, int* nsec_out)
{
    /* XXX endianness. Shouldn't matter, as all Windows implementations are little-endian */
    /* Cannot simply cast and dereference in_ptr,
       since it might not be aligned properly */
    __int64 in;
    memcpy(&in, in_ptr, sizeof(in));
    *nsec_out = (int)(in % 10000000) * 100; /* FILETIME is in units of 100 nsec. */
    *time_out = Py_SAFE_DOWNCAST((in / 10000000) - secs_between_epochs, __int64, time_t);
}

void
_Py_time_t_to_FILE_TIME(time_t time_in, int nsec_in, FILETIME *out_ptr)
{
    /* XXX endianness */
    __int64 out;
    out = time_in + secs_between_epochs;
    out = out * 10000000 + nsec_in / 100;
    memcpy(out_ptr, &out, sizeof(out));
}

/* Below, we *know* that ugo+r is 0444 */
#if _S_IREAD != 0400
#error Unsupported C library
#endif
static int
attributes_to_mode(DWORD attr)
{
    int m = 0;
    if (attr & FILE_ATTRIBUTE_DIRECTORY)
        m |= _S_IFDIR | 0111; /* IFEXEC for user,group,other */
    else
        m |= _S_IFREG;
    if (attr & FILE_ATTRIBUTE_READONLY)
        m |= 0444;
    else
        m |= 0666;
    return m;
}

#ifdef MS_WINRT
void
_Py_attribute_data_to_stat(FILE_BASIC_INFO *basicInfo, FILE_STANDARD_INFO *standardInfo, int reparse_tag, struct _Py_stat_struct *result)
{
    memset(result, 0, sizeof(*result));
    result->st_mode = attributes_to_mode(basicInfo->FileAttributes);
    result->st_size = standardInfo->EndOfFile.QuadPart;
    FILE_TIME_to_time_t_nsec(&basicInfo->CreationTime, &result->st_ctime, &result->st_ctime_nsec);
    FILE_TIME_to_time_t_nsec(&basicInfo->LastWriteTime, &result->st_mtime, &result->st_mtime_nsec);
    FILE_TIME_to_time_t_nsec(&basicInfo->LastAccessTime, &result->st_atime, &result->st_atime_nsec);
    result->st_nlink = standardInfo->NumberOfLinks;
    result->st_ino = 0;
    if (reparse_tag == IO_REPARSE_TAG_SYMLINK) {
        /* first clear the S_IFMT bits */
        result->st_mode ^= (result->st_mode & S_IFMT);
        /* now set the bits that make this a symlink */
        result->st_mode |= S_IFLNK;
    }
}
#else
void
_Py_attribute_data_to_stat(BY_HANDLE_FILE_INFORMATION *info, ULONG reparse_tag, struct _Py_stat_struct *result)
{
    memset(result, 0, sizeof(*result));
    result->st_mode = attributes_to_mode(info->dwFileAttributes);
    result->st_size = (((__int64)info->nFileSizeHigh)<<32) + info->nFileSizeLow;
    result->st_dev = info->dwVolumeSerialNumber;
    result->st_rdev = result->st_dev;
    FILE_TIME_to_time_t_nsec(&info->ftCreationTime, &result->st_ctime, &result->st_ctime_nsec);
    FILE_TIME_to_time_t_nsec(&info->ftLastWriteTime, &result->st_mtime, &result->st_mtime_nsec);
    FILE_TIME_to_time_t_nsec(&info->ftLastAccessTime, &result->st_atime, &result->st_atime_nsec);
    result->st_nlink = info->nNumberOfLinks;
    result->st_ino = (((__int64)info->nFileIndexHigh)<<32) + info->nFileIndexLow;
    if (reparse_tag == IO_REPARSE_TAG_SYMLINK) {
        /* first clear the S_IFMT bits */
        result->st_mode ^= (result->st_mode & S_IFMT);
        /* now set the bits that make this a symlink */
        result->st_mode |= S_IFLNK;
    }
    result->st_file_attributes = info->dwFileAttributes;
}
#endif
#endif

/* Return information about a file.

   On POSIX, use fstat().

   On Windows, use GetFileType() and GetFileInformationByHandle() which support
   files larger than 2 GB.  fstat() may fail with EOVERFLOW on files larger
   than 2 GB because the file size type is an signed 32-bit integer: see issue
   #23152.
   */
int
_Py_fstat(int fd, struct _Py_stat_struct *result)
{
#ifdef MS_WINDOWS
#ifdef MS_WINRT
    FILE_BASIC_INFO fbi;
    FILE_STANDARD_INFO fsi;
#else
    BY_HANDLE_FILE_INFORMATION info;
#endif
    HANDLE h;
    int type;

    if (!_PyVerify_fd(fd))
        h = INVALID_HANDLE_VALUE;
    else
        h = (HANDLE)_get_osfhandle(fd);

    /* Protocol violation: we explicitly clear errno, instead of
       setting it to a POSIX error. Callers should use GetLastError. */
    errno = 0;

    if (h == INVALID_HANDLE_VALUE) {
        /* This is really a C library error (invalid file handle).
           We set the Win32 error to the closes one matching. */
        SetLastError(ERROR_INVALID_HANDLE);
        return -1;
    }
    memset(result, 0, sizeof(*result));

#ifdef MS_WINRT
    if (!GetFileInformationByHandleEx(h, FileBasicInfo, &fbi, sizeof(fbi))) {
        return -1;
    }
    if (!GetFileInformationByHandleEx(h, FileStandardInfo, &fsi, sizeof(fsi))) {
        return -1;
    }

    _Py_attribute_data_to_stat(&fbi, &fsi, 0, result);
#else
    type = GetFileType(h);
    if (type == FILE_TYPE_UNKNOWN) {
        DWORD error = GetLastError();
        if (error != 0) {
            return -1;
        }
        /* else: valid but unknown file */
    }

    if (type != FILE_TYPE_DISK) {
        if (type == FILE_TYPE_CHAR)
            result->st_mode = _S_IFCHR;
        else if (type == FILE_TYPE_PIPE)
            result->st_mode = _S_IFIFO;
        return 0;
    }

    if (!GetFileInformationByHandle(h, &info)) {
        return -1;
    }
    _Py_attribute_data_to_stat(&info, 0, result);
    /* specific to fstat() */
    result->st_ino = (((__int64)info.nFileIndexHigh)<<32) + info.nFileIndexLow;
#endif
    return 0;
#else
    return fstat(fd, result);
#endif
}
#endif   /* HAVE_FSTAT || MS_WINDOWS */


#ifdef HAVE_STAT
/* Call _wstat() on Windows, or encode the path to the filesystem encoding and
   call stat() otherwise. Only fill st_mode attribute on Windows.

   Return 0 on success, -1 on _wstat() / stat() error, -2 if an exception was
   raised. */

int
_Py_stat(PyObject *path, struct stat *statbuf)
{
#ifdef MS_WINDOWS
    int err;
    struct _stat wstatbuf;
    wchar_t *wpath;

    wpath = PyUnicode_AsUnicode(path);
    if (wpath == NULL)
        return -2;
    err = _wstat(wpath, &wstatbuf);
    if (!err)
        statbuf->st_mode = wstatbuf.st_mode;
    return err;
#else
    int ret;
    PyObject *bytes = PyUnicode_EncodeFSDefault(path);
    if (bytes == NULL)
        return -2;
    ret = stat(PyBytes_AS_STRING(bytes), statbuf);
    Py_DECREF(bytes);
    return ret;
#endif
}

#endif   /* HAVE_STAT */


static int
get_inheritable(int fd, int raise)
{
#ifdef MS_WINDOWS
#ifdef MS_WINRT
    return 0;
#else
    HANDLE handle;
    DWORD flags;

    if (!_PyVerify_fd(fd)) {
        if (raise)
            PyErr_SetFromErrno(PyExc_OSError);
        return -1;
    }

    handle = (HANDLE)_get_osfhandle(fd);
    if (handle == INVALID_HANDLE_VALUE) {
        if (raise)
            PyErr_SetFromWindowsErr(0);
        return -1;
    }

    if (!GetHandleInformation(handle, &flags)) {
        if (raise)
            PyErr_SetFromWindowsErr(0);
        return -1;
    }

    return (flags & HANDLE_FLAG_INHERIT);
#endif
#else
    int flags;

    flags = fcntl(fd, F_GETFD, 0);
    if (flags == -1) {
        if (raise)
            PyErr_SetFromErrno(PyExc_OSError);
        return -1;
    }
    return !(flags & FD_CLOEXEC);
#endif
}

/* Get the inheritable flag of the specified file descriptor.
   Return 1 if the file descriptor can be inherited, 0 if it cannot,
   raise an exception and return -1 on error. */
int
_Py_get_inheritable(int fd)
{
    return get_inheritable(fd, 1);
}

static int
set_inheritable(int fd, int inheritable, int raise, int *atomic_flag_works)
{
#ifdef MS_WINDOWS
#ifndef MS_WINRT
    HANDLE handle;
    DWORD flags;
#endif
#else
#if defined(HAVE_SYS_IOCTL_H) && defined(FIOCLEX) && defined(FIONCLEX)
    static int ioctl_works = -1;
    int request;
    int err;
#endif
    int flags;
    int res;
#endif

    /* atomic_flag_works can only be used to make the file descriptor
       non-inheritable */
    assert(!(atomic_flag_works != NULL && inheritable));

    if (atomic_flag_works != NULL && !inheritable) {
        if (*atomic_flag_works == -1) {
            int inheritable = get_inheritable(fd, raise);
            if (inheritable == -1)
                return -1;
            *atomic_flag_works = !inheritable;
        }

        if (*atomic_flag_works)
            return 0;
    }

#ifdef MS_WINDOWS
#ifdef MS_WINRT
    return 0;
#else
    if (!_PyVerify_fd(fd)) {
        if (raise)
            PyErr_SetFromErrno(PyExc_OSError);
        return -1;
    }

    handle = (HANDLE)_get_osfhandle(fd);
    if (handle == INVALID_HANDLE_VALUE) {
        if (raise)
            PyErr_SetFromWindowsErr(0);
        return -1;
    }

    if (inheritable)
        flags = HANDLE_FLAG_INHERIT;
    else
        flags = 0;
    if (!SetHandleInformation(handle, HANDLE_FLAG_INHERIT, flags)) {
        if (raise)
            PyErr_SetFromWindowsErr(0);
        return -1;
    }
    return 0;
#endif
#else

#if defined(HAVE_SYS_IOCTL_H) && defined(FIOCLEX) && defined(FIONCLEX)
    if (ioctl_works != 0) {
        /* fast-path: ioctl() only requires one syscall */
        if (inheritable)
            request = FIONCLEX;
        else
            request = FIOCLEX;
        err = ioctl(fd, request, NULL);
        if (!err) {
            ioctl_works = 1;
            return 0;
        }

        if (errno != ENOTTY) {
            if (raise)
                PyErr_SetFromErrno(PyExc_OSError);
            return -1;
        }
        else {
            /* Issue #22258: Here, ENOTTY means "Inappropriate ioctl for
               device". The ioctl is declared but not supported by the kernel.
               Remember that ioctl() doesn't work. It is the case on
               Illumos-based OS for example. */
            ioctl_works = 0;
        }
        /* fallback to fcntl() if ioctl() does not work */
    }
#endif

    /* slow-path: fcntl() requires two syscalls */
    flags = fcntl(fd, F_GETFD);
    if (flags < 0) {
        if (raise)
            PyErr_SetFromErrno(PyExc_OSError);
        return -1;
    }

    if (inheritable)
        flags &= ~FD_CLOEXEC;
    else
        flags |= FD_CLOEXEC;
    res = fcntl(fd, F_SETFD, flags);
    if (res < 0) {
        if (raise)
            PyErr_SetFromErrno(PyExc_OSError);
        return -1;
    }
    return 0;
#endif
}

/* Make the file descriptor non-inheritable.
   Return 0 on success, set errno and return -1 on error. */
static int
make_non_inheritable(int fd)
{
    return set_inheritable(fd, 0, 0, NULL);
}

/* Set the inheritable flag of the specified file descriptor.
   On success: return 0, on error: raise an exception if raise is nonzero
   and return -1.

   If atomic_flag_works is not NULL:

    * if *atomic_flag_works==-1, check if the inheritable is set on the file
      descriptor: if yes, set *atomic_flag_works to 1, otherwise set to 0 and
      set the inheritable flag
    * if *atomic_flag_works==1: do nothing
    * if *atomic_flag_works==0: set inheritable flag to False

   Set atomic_flag_works to NULL if no atomic flag was used to create the
   file descriptor.

   atomic_flag_works can only be used to make a file descriptor
   non-inheritable: atomic_flag_works must be NULL if inheritable=1. */
int
_Py_set_inheritable(int fd, int inheritable, int *atomic_flag_works)
{
    return set_inheritable(fd, inheritable, 1, atomic_flag_works);
}

/* Open a file with the specified flags (wrapper to open() function).
   The file descriptor is created non-inheritable. */
int
_Py_open(const char *pathname, int flags)
{
    int fd;
#ifdef MS_WINDOWS
    fd = open(pathname, flags | O_NOINHERIT);
    if (fd < 0)
        return fd;
#else

    int *atomic_flag_works;
#ifdef O_CLOEXEC
    atomic_flag_works = &_Py_open_cloexec_works;
    flags |= O_CLOEXEC;
#else
    atomic_flag_works = NULL;
#endif
    fd = open(pathname, flags);
    if (fd < 0)
        return fd;

    if (set_inheritable(fd, 0, 0, atomic_flag_works) < 0) {
        close(fd);
        return -1;
    }
#endif   /* !MS_WINDOWS */
    return fd;
}

/* Open a file. Use _wfopen() on Windows, encode the path to the locale
   encoding and use fopen() otherwise. The file descriptor is created
   non-inheritable. */
FILE *
_Py_wfopen(const wchar_t *path, const wchar_t *mode)
{
    FILE *f;
#ifndef MS_WINDOWS
    char *cpath;
    char cmode[10];
    size_t r;
    r = wcstombs(cmode, mode, 10);
    if (r == (size_t)-1 || r >= 10) {
        errno = EINVAL;
        return NULL;
    }
    cpath = Py_EncodeLocale(path, NULL);
    if (cpath == NULL)
        return NULL;
    f = fopen(cpath, cmode);
    PyMem_Free(cpath);
#else
    f = _wfopen(path, mode);
#endif
    if (f == NULL)
        return NULL;
    if (make_non_inheritable(fileno(f)) < 0) {
        fclose(f);
        return NULL;
    }
    return f;
}

/* Wrapper to fopen(). The file descriptor is created non-inheritable. */
FILE*
_Py_fopen(const char *pathname, const char *mode)
{
    FILE *f = fopen(pathname, mode);
    if (f == NULL)
        return NULL;
    if (make_non_inheritable(fileno(f)) < 0) {
        fclose(f);
        return NULL;
    }
    return f;
}

/* Open a file. Call _wfopen() on Windows, or encode the path to the filesystem
   encoding and call fopen() otherwise. The file descriptor is created
   non-inheritable.

   Return the new file object on success, or NULL if the file cannot be open or
   (if PyErr_Occurred()) on unicode error. */
FILE*
_Py_fopen_obj(PyObject *path, const char *mode)
{
    FILE *f;
#ifdef MS_WINDOWS
    wchar_t *wpath;
    wchar_t wmode[10];
    int usize;

    if (!PyUnicode_Check(path)) {
        PyErr_Format(PyExc_TypeError,
                     "str file path expected under Windows, got %R",
                     Py_TYPE(path));
        return NULL;
    }
    wpath = PyUnicode_AsUnicode(path);
    if (wpath == NULL)
        return NULL;

    usize = MultiByteToWideChar(CP_ACP, 0, mode, -1, wmode, sizeof(wmode));
    if (usize == 0)
        return NULL;

    f = _wfopen(wpath, wmode);
#else
    PyObject *bytes;
    if (!PyUnicode_FSConverter(path, &bytes))
        return NULL;
    f = fopen(PyBytes_AS_STRING(bytes), mode);
    Py_DECREF(bytes);
#endif
    if (f == NULL)
        return NULL;
    if (make_non_inheritable(fileno(f)) < 0) {
        fclose(f);
        return NULL;
    }
    return f;
}

#ifdef HAVE_READLINK

/* Read value of symbolic link. Encode the path to the locale encoding, decode
   the result from the locale encoding. Return -1 on error. */

int
_Py_wreadlink(const wchar_t *path, wchar_t *buf, size_t bufsiz)
{
    char *cpath;
    char cbuf[MAXPATHLEN];
    wchar_t *wbuf;
    int res;
    size_t r1;

    cpath = Py_EncodeLocale(path, NULL);
    if (cpath == NULL) {
        errno = EINVAL;
        return -1;
    }
    res = (int)readlink(cpath, cbuf, Py_ARRAY_LENGTH(cbuf));
    PyMem_Free(cpath);
    if (res == -1)
        return -1;
    if (res == Py_ARRAY_LENGTH(cbuf)) {
        errno = EINVAL;
        return -1;
    }
    cbuf[res] = '\0'; /* buf will be null terminated */
    wbuf = Py_DecodeLocale(cbuf, &r1);
    if (wbuf == NULL) {
        errno = EINVAL;
        return -1;
    }
    if (bufsiz <= r1) {
        PyMem_RawFree(wbuf);
        errno = EINVAL;
        return -1;
    }
    wcsncpy(buf, wbuf, bufsiz);
    PyMem_RawFree(wbuf);
    return (int)r1;
}
#endif

#ifdef HAVE_REALPATH

/* Return the canonicalized absolute pathname. Encode path to the locale
   encoding, decode the result from the locale encoding.
   Return NULL on error. */

wchar_t*
_Py_wrealpath(const wchar_t *path,
              wchar_t *resolved_path, size_t resolved_path_size)
{
    char *cpath;
    char cresolved_path[MAXPATHLEN];
    wchar_t *wresolved_path;
    char *res;
    size_t r;
    cpath = Py_EncodeLocale(path, NULL);
    if (cpath == NULL) {
        errno = EINVAL;
        return NULL;
    }
    res = realpath(cpath, cresolved_path);
    PyMem_Free(cpath);
    if (res == NULL)
        return NULL;

    wresolved_path = Py_DecodeLocale(cresolved_path, &r);
    if (wresolved_path == NULL) {
        errno = EINVAL;
        return NULL;
    }
    if (resolved_path_size <= r) {
        PyMem_RawFree(wresolved_path);
        errno = EINVAL;
        return NULL;
    }
    wcsncpy(resolved_path, wresolved_path, resolved_path_size);
    PyMem_RawFree(wresolved_path);
    return resolved_path;
}
#endif

/* Get the current directory. size is the buffer size in wide characters
   including the null character. Decode the path from the locale encoding.
   Return NULL on error. */

wchar_t*
_Py_wgetcwd(wchar_t *buf, size_t size)
{
#ifdef MS_WINRT
    return L"";
#elif defined(MS_WINDOWS)
    int isize = (int)Py_MIN(size, INT_MAX);
    return _wgetcwd(buf, isize);
#else
    char fname[MAXPATHLEN];
    wchar_t *wname;
    size_t len;

    if (getcwd(fname, Py_ARRAY_LENGTH(fname)) == NULL)
        return NULL;
    wname = Py_DecodeLocale(fname, &len);
    if (wname == NULL)
        return NULL;
    if (size <= len) {
        PyMem_RawFree(wname);
        return NULL;
    }
    wcsncpy(buf, wname, size);
    PyMem_RawFree(wname);
    return buf;
#endif
}

/* Duplicate a file descriptor. The new file descriptor is created as
   non-inheritable. Return a new file descriptor on success, raise an OSError
   exception and return -1 on error.

   The GIL is released to call dup(). The caller must hold the GIL. */
int
_Py_dup(int fd)
{
#ifdef MS_WINDOWS
    HANDLE handle;
    DWORD ftype;
#endif

    if (!_PyVerify_fd(fd)) {
        PyErr_SetFromErrno(PyExc_OSError);
        return -1;
    }

#ifdef MS_WINDOWS
    handle = (HANDLE)_get_osfhandle(fd);
    if (handle == INVALID_HANDLE_VALUE) {
        PyErr_SetFromWindowsErr(0);
        return -1;
    }

#ifdef MS_WINRT
    ftype = FILE_TYPE_UNKNOWN;
#else
    /* get the file type, ignore the error if it failed */
    ftype = GetFileType(handle);
#endif

    Py_BEGIN_ALLOW_THREADS
    fd = dup(fd);
    Py_END_ALLOW_THREADS
    if (fd < 0) {
        PyErr_SetFromErrno(PyExc_OSError);
        return -1;
    }

    /* Character files like console cannot be make non-inheritable */
    if (ftype != FILE_TYPE_CHAR) {
        if (_Py_set_inheritable(fd, 0, NULL) < 0) {
            close(fd);
            return -1;
        }
    }
#elif defined(HAVE_FCNTL_H) && defined(F_DUPFD_CLOEXEC)
    Py_BEGIN_ALLOW_THREADS
    fd = fcntl(fd, F_DUPFD_CLOEXEC, 0);
    Py_END_ALLOW_THREADS
    if (fd < 0) {
        PyErr_SetFromErrno(PyExc_OSError);
        return -1;
    }

#else
    Py_BEGIN_ALLOW_THREADS
    fd = dup(fd);
    Py_END_ALLOW_THREADS
    if (fd < 0) {
        PyErr_SetFromErrno(PyExc_OSError);
        return -1;
    }

    if (_Py_set_inheritable(fd, 0, NULL) < 0) {
        close(fd);
        return -1;
    }
#endif
    return fd;
}

#ifndef MS_WINDOWS
/* Get the blocking mode of the file descriptor.
   Return 0 if the O_NONBLOCK flag is set, 1 if the flag is cleared,
   raise an exception and return -1 on error. */
int
_Py_get_blocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        PyErr_SetFromErrno(PyExc_OSError);
        return -1;
    }

    return !(flags & O_NONBLOCK);
}

/* Set the blocking mode of the specified file descriptor.

   Set the O_NONBLOCK flag if blocking is False, clear the O_NONBLOCK flag
   otherwise.

   Return 0 on success, raise an exception and return -1 on error. */
int
_Py_set_blocking(int fd, int blocking)
{
#if defined(HAVE_SYS_IOCTL_H) && defined(FIONBIO)
    int arg = !blocking;
    if (ioctl(fd, FIONBIO, &arg) < 0)
        goto error;
#else
    int flags, res;

    flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0)
        goto error;

    if (blocking)
        flags = flags & (~O_NONBLOCK);
    else
        flags = flags | O_NONBLOCK;

    res = fcntl(fd, F_SETFL, flags);
    if (res < 0)
        goto error;
#endif
    return 0;

error:
    PyErr_SetFromErrno(PyExc_OSError);
    return -1;
}
#endif

