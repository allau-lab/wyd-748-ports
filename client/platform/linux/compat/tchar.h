#pragma once
#include <cstring>
#include <cstdio>

#ifndef _T
#define _T(x) x
#endif
#ifndef TEXT
#define TEXT(x) x
#endif
#ifndef _tcscpy
#define _tcscpy strcpy
#endif
#ifndef _tcscat
#define _tcscat strcat
#endif
#ifndef _tcslen
#define _tcslen strlen
#endif
#ifndef _tcsncpy
#define _tcsncpy strncpy
#endif
#ifndef _tcsrchr
#define _tcsrchr strrchr
#endif
#ifndef _tcsicmp
#define _tcsicmp strcasecmp
#endif
#ifndef _stprintf
#define _stprintf sprintf
#endif
#ifndef _sntprintf
#define _sntprintf snprintf
#endif
#ifndef _stscanf
#define _stscanf sscanf
#endif

using TCHAR = char;
using LPTSTR = char*;
using LPCTSTR = const char*;
