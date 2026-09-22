#ifndef ZIR_DIAGNOSTIC_H
#define ZIR_DIAGNOSTIC_H

#include "zir.h"
#include <stdarg.h>

int ZirSetDiagnosticFormat(const char *format);
void ZirDiagnostic(ZirSourceSpan span, const char *code, const char *format, ...);
void ZirDiagnosticV(ZirSourceSpan span, const char *code, const char *format, va_list args);

#endif
