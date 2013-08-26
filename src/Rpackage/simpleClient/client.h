#ifndef _CLIENT_H_
#define _CLIENT_H_

#if defined(_MSC_VER) || defined(__CYGWIN__) || defined(__MINGW32__)
#ifndef LIBEMBEDDEDSQL5
#define embeddedclient_export extern __declspec(dllimport)
#else
#define embeddedclient_export extern __declspec(dllexport)
#endif
#else
#define embeddedclient_export extern
#endif

#include <stdio.h>
#include <stream.h>
#include <monet_options.h>

int main(int argc, char** argv);

#endif
